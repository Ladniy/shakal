#include "MainWindow.h"

#include "EncoderController.h"
#include "Presets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDragMoveEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kProbeTimeoutMs = 4000;
constexpr int kLogMaxBlocks = 2000;

bool hevcAvailable()
{
    QProcess probe;
    probe.start(QStringLiteral("ffmpeg"),
                {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});

    if (!probe.waitForStarted(kProbeTimeoutMs)) {
        return true; // unknown: never block the UI on a failed guess
    }
    if (!probe.waitForFinished(kProbeTimeoutMs)) {
        probe.kill();
        probe.waitForFinished(1000);
        return true;
    }

    return probe.readAllStandardOutput().contains("libx265");
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent),
      m_input(new QLineEdit(this)),
      m_outputDir(new QLineEdit(this)),
      m_fileName(new QLineEdit(this)),
      m_container(new QComboBox(this)),
      m_ultra(new QCheckBox(tr("Shakal ULTRA"), this)),
      m_encode(new QPushButton(tr("Encode"), this)),
      m_cancel(new QPushButton(tr("Cancel"), this)),
      m_progress(new QProgressBar(this)),
      m_log(new QPlainTextEdit(this)),
      m_encoder(new EncoderController(this))
{
    setWindowTitle(tr("shakal"));
    resize(720, 480);

    m_container->addItem(QStringLiteral("mp4"), int(EncoderController::Container::Mp4));
    m_container->addItem(QStringLiteral("mkv"), int(EncoderController::Container::Mkv));
    m_container->addItem(QStringLiteral("webm"), int(EncoderController::Container::Webm));

    m_input->setPlaceholderText(tr("Video to encode"));
    // Scoped to this field rather than the whole window, so a drop meant for another widget
    // is never swallowed.
    m_input->setAcceptDrops(true);
    m_input->installEventFilter(this);
    m_fileName->setPlaceholderText(tr("Name without extension"));
    m_progress->setRange(0, 100);
    // The percentage text reserves space and insets the groove, which is why the bar
    // rendered narrower than the log pane. Fill the row and drop the text.
    m_progress->setTextVisible(false);
    m_progress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(kLogMaxBlocks);
    m_cancel->setEnabled(false);

    auto *inputRow = new QHBoxLayout;
    inputRow->addWidget(m_input);
    auto *browseInput = new QPushButton(tr("Browse..."), this);
    inputRow->addWidget(browseInput);

    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(m_outputDir);
    auto *browseDir = new QPushButton(tr("Browse..."), this);
    dirRow->addWidget(browseDir);

    auto *containerRow = new QHBoxLayout;
    containerRow->addWidget(m_container);
    containerRow->addWidget(m_ultra);
    containerRow->addStretch();

    auto *inputs = new QFormLayout;
    inputs->addRow(tr("Input file"), inputRow);
    inputs->addRow(tr("Output directory"), dirRow);
    inputs->addRow(tr("Output name"), m_fileName);
    inputs->addRow(tr("Container"), containerRow);

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_encode);
    actions->addWidget(m_cancel);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(inputs);
    layout->addLayout(actions);
    layout->addWidget(m_progress);
    layout->addWidget(m_log, 1);

    connect(m_encoder, &EncoderController::durationProbed, this, &MainWindow::handleDuration);
    connect(m_encoder, &EncoderController::progressed, this, &MainWindow::handleProgress);
    connect(m_encoder, &EncoderController::logLine, m_log,
            &QPlainTextEdit::appendPlainText);
    connect(m_encoder, &EncoderController::succeeded, this, &MainWindow::handleSucceeded);
    connect(m_encoder, &EncoderController::failed, this, &MainWindow::handleFailed);
    connect(m_encoder, &EncoderController::runFinished, this, [this] { setRunning(false); });

    connect(m_encode, &QPushButton::clicked, this, &MainWindow::startEncode);
    connect(m_cancel, &QPushButton::clicked, this, &MainWindow::cancelEncode);
    connect(browseInput, &QPushButton::clicked, this, &MainWindow::browseInput);
    connect(browseDir, &QPushButton::clicked, this, &MainWindow::browseOutputDir);

    m_hevcAvailable = hevcAvailable();
    if (!m_hevcAvailable) {
        m_encode->setEnabled(false);
        m_log->appendPlainText(
            tr("This ffmpeg build has no libx265, so every preset would fail. Encode disabled."));
    }
}

void MainWindow::applyInputPath(const QString &path)
{
    m_input->setText(path);

    const QFileInfo info(path);
    m_outputDir->setText(info.absolutePath());
    if (m_fileName->text().isEmpty()) {
        m_fileName->setText(info.completeBaseName());
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (watched == m_input
        && (type == QEvent::DragEnter || type == QEvent::DragMove || type == QEvent::Drop)) {
        // QDragEnterEvent and QDragMoveEvent both derive from QDropEvent, so one cast covers
        // all three; anything that is not a drop is left to QWidget::eventFilter below.
        auto *drop = static_cast<QDropEvent *>(event);

        if (type == QEvent::Drop) {
            const QList<QUrl> urls = drop->mimeData()->urls();
            if (!urls.isEmpty() && urls.first().isLocalFile()) {
                applyInputPath(urls.first().toLocalFile());
                drop->acceptProposedAction();
                return true;
            }
        } else if (drop->mimeData()->hasUrls()) {
            drop->acceptProposedAction();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void MainWindow::browseInput()
{
    const QString chosen = QFileDialog::getOpenFileName(this, tr("Choose input file"));
    if (!chosen.isEmpty()) {
        applyInputPath(chosen);
    }
}

void MainWindow::browseOutputDir()
{
    const QString chosen =
        QFileDialog::getExistingDirectory(this, tr("Choose output directory"),
                                          m_outputDir->text().isEmpty() ? QDir::homePath()
                                                                        : m_outputDir->text());
    if (!chosen.isEmpty()) {
        m_outputDir->setText(chosen);
    }
}

EncoderController::Container MainWindow::currentContainer() const
{
    return EncoderController::Container(m_container->currentData().toInt());
}

QString MainWindow::containerFilter() const
{
    return tr("Video") + QStringLiteral(" (*.") + EncoderController::suffixFor(currentContainer())
            + QStringLiteral(")");
}

bool MainWindow::resolveOutput(QString &output, bool &overwrite)
{
    overwrite = false;

    while (true) {
        if (!QFileInfo::exists(output)) {
            return true;
        }

        QMessageBox box(this);
        box.setWindowTitle(tr("Output already exists"));
        box.setText(tr("%1 already exists.").arg(QDir::toNativeSeparators(output)));

        QAbstractButton *doOverwrite =
            box.addButton(tr("Overwrite"), QMessageBox::AcceptRole);
        QAbstractButton *rename = box.addButton(tr("Choose another name"), QMessageBox::ActionRole);
        box.addButton(tr("Cancel"), QMessageBox::RejectRole);
        box.exec();

        QAbstractButton *clicked = box.clickedButton();
        if (clicked == doOverwrite) {
            overwrite = true;
            return true;
        }
        if (clicked != rename) {
            return false;
        }

        const QString chosen =
            QFileDialog::getSaveFileName(this, tr("Choose output file"), output, containerFilter());
        if (chosen.isEmpty()) {
            return false;
        }
        applyChosenPath(chosen);
        output = chosen;
    }
}

void MainWindow::applyChosenPath(const QString &path)
{
    const QFileInfo info(path);
    m_outputDir->setText(info.absolutePath());

    const QString suffix = QStringLiteral(".") + EncoderController::suffixFor(currentContainer());
    QString name = info.fileName();
    if (name.endsWith(suffix, Qt::CaseInsensitive)) {
        name.chop(suffix.size());
    }
    m_fileName->setText(name);
}

void MainWindow::startEncode()
{
    const QString input = m_input->text().trimmed();
    if (input.isEmpty() || !QFileInfo::exists(input)) {
        QMessageBox::warning(this, tr("Missing input"), tr("Choose an existing input file."));
        return;
    }

    const QString dir = m_outputDir->text().trimmed();
    const QString name = m_fileName->text().trimmed();
    if (dir.isEmpty() || name.isEmpty()) {
        QMessageBox::warning(this, tr("Missing output"),
                             tr("Set an output directory and a file name."));
        return;
    }
    if (!QFileInfo(dir).isDir()) {
        QMessageBox::warning(this, tr("Missing output"),
                             tr("%1 is not a directory.").arg(QDir::toNativeSeparators(dir)));
        return;
    }

    QString output = QDir(dir).filePath(EncoderController::applyContainer(name, currentContainer()));

    bool overwrite = false;
    if (!resolveOutput(output, overwrite)) {
        return;
    }

    const QVector<Preset> presets = shakalPresets();
    const QString wanted = m_ultra->isChecked() ? QStringLiteral("ULTRA")
                                                : QStringLiteral("default");
    const Preset *chosen = &presets.first();
    for (const Preset &preset : presets) {
        if (preset.label == wanted) {
            chosen = &preset;
            break;
        }
    }

    m_log->appendPlainText(tr("Preset: %1").arg(chosen->label));
    setRunning(true);
    m_encoder->start(input, chosen->args, output, overwrite, currentContainer());
}

void MainWindow::cancelEncode()
{
    m_encoder->cancel();
}

void MainWindow::setRunning(bool running)
{
    m_encode->setEnabled(!running && m_hevcAvailable);
    m_cancel->setEnabled(running);
    m_input->setEnabled(!running);
    m_outputDir->setEnabled(!running);
    m_fileName->setEnabled(!running);
    m_container->setEnabled(!running);
    m_ultra->setEnabled(!running);

    if (!running) {
        // An indeterminate run (duration probe failed) leaves maximum()==0, which used to
        // make the reset below unreachable -- the bar then animated forever after the encode.
        m_progress->setRange(0, 100);
        if (m_progress->value() < 100) {
            m_progress->setValue(0);
        }
    }
}

void MainWindow::handleDuration(double seconds)
{
    if (seconds > 0.0) {
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
    } else {
        m_progress->setRange(0, 0); // indeterminate rather than a fake percentage
    }
}

void MainWindow::handleProgress(qint64 outTimeUs, qint64 totalUs)
{
    if (totalUs <= 0) {
        return;
    }
    const int percent = int((outTimeUs * 100) / totalUs);
    m_progress->setValue(qMin(100, percent));
}

void MainWindow::handleSucceeded(const QString &output)
{
    m_progress->setValue(100);
    QMessageBox::information(this, tr("Done"),
                             tr("Wrote %1").arg(QDir::toNativeSeparators(output)));
}

void MainWindow::handleFailed(const QString &message)
{
    QMessageBox::critical(this, tr("Failed"), message);
}
