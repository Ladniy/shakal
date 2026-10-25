#pragma once

#include <EncoderController.h>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void browseInput();
    void browseOutputDir();
    void startEncode();
    void cancelEncode();
    void handleDuration(double seconds);
    void handleProgress(qint64 outTimeUs, qint64 totalUs);
    void handleSucceeded(const QString &output);
    void handleFailed(const QString &message);
    void setRunning(bool running);

private:
    EncoderController::Container currentContainer() const;
    QString containerFilter() const;
    bool resolveOutput(QString &output, bool &overwrite);
    void applyChosenPath(const QString &path);

    QLineEdit *m_input;
    QLineEdit *m_outputDir;
    QLineEdit *m_fileName;
    QComboBox *m_preset;
    QComboBox *m_container;
    QPushButton *m_encode;
    QPushButton *m_cancel;
    QProgressBar *m_progress;
    QPlainTextEdit *m_log;
    EncoderController *m_encoder;
    bool m_hevcAvailable = true;
};
