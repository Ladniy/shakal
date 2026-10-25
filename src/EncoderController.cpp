#include "EncoderController.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace {
constexpr int kLogTailLines = 12;
constexpr int kCancelGraceMs = 5000;
constexpr int kKillGraceMs = 3000;
constexpr int kStdinFlushMs = 200;

// "00:00:12.345000" -> microseconds, for ffmpeg builds without out_time_us.
qint64 parseClock(const QByteArray &value)
{
    const QList<QByteArray> parts = value.split(':');
    if (parts.size() != 3) {
        return -1;
    }

    bool okHours = false;
    bool okMinutes = false;
    bool okSeconds = false;
    const qint64 hours = parts.at(0).toLongLong(&okHours);
    const qint64 minutes = parts.at(1).toLongLong(&okMinutes);
    const double seconds = parts.at(2).toDouble(&okSeconds);

    if (!okHours || !okMinutes || !okSeconds) {
        return -1;
    }
    return qint64((hours * 3600 + minutes * 60 + seconds) * 1e6);
}
} // namespace

EncoderController::EncoderController(QObject *parent)
    : QObject(parent),
      m_probe(new QProcess(this)),
      m_ffmpeg(new QProcess(this)),
      m_killWatchdog(new QTimer(this))
{
    m_killWatchdog->setSingleShot(true);

    connect(m_probe, &QProcess::finished, this, &EncoderController::onProbeFinished);
    connect(m_probe, &QProcess::errorOccurred, this, &EncoderController::onProbeError);

    m_ffmpeg->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_ffmpeg, &QProcess::readyReadStandardOutput, this, &EncoderController::pumpProgress);
    connect(m_ffmpeg, &QProcess::readyReadStandardError, this, &EncoderController::pumpLog);
    connect(m_ffmpeg, &QProcess::finished, this, &EncoderController::onProcessFinished);
    connect(m_ffmpeg, &QProcess::errorOccurred, this, &EncoderController::onProcessError);

    // Graceful 'q' may be ignored when stdin is a pipe, so always escalate.
    connect(m_killWatchdog, &QTimer::timeout, this, [this] {
        if (m_ffmpeg->state() == QProcess::NotRunning) {
            return;
        }
        m_ffmpeg->terminate();
        QTimer::singleShot(kKillGraceMs, this, [this] {
            if (m_ffmpeg->state() != QProcess::NotRunning) {
                m_ffmpeg->kill();
            }
        });
    });
}

QString EncoderController::suffixFor(Container container)
{
    switch (container) {
    case Container::Mkv:
        return QStringLiteral("mkv");
    case Container::Webm:
        return QStringLiteral("webm");
    case Container::Mp4:
        break;
    }
    return QStringLiteral("mp4");
}

QString EncoderController::applyContainer(const QString &fileName, Container container)
{
    const QString suffix = QStringLiteral(".") + suffixFor(container);
    if (fileName.endsWith(suffix, Qt::CaseInsensitive)) {
        return fileName;
    }
    return fileName + suffix;
}

QStringList EncoderController::buildCommand(const QString &input, const QStringList &presetArgs,
                                            const QString &output, bool overwrite)
{
    QStringList cmd;
    cmd << QStringLiteral("ffmpeg") << QStringLiteral("-hide_banner")
        << QStringLiteral("-progress") << QStringLiteral("pipe:1")
        << QStringLiteral("-i") << input << presetArgs;

    // -n as well as -y: an unprompted refusal would otherwise let ffmpeg wait on a
    // stdin that is a pipe, and it exits 0 when it declines to overwrite.
    cmd << (overwrite ? QStringLiteral("-y") : QStringLiteral("-n")) << output;

    return cmd;
}

bool EncoderController::isRunning() const
{
    return m_ffmpeg->state() != QProcess::NotRunning;
}

void EncoderController::start(const QString &input, const QStringList &presetArgs,
                              const QString &output, bool overwrite)
{
    if (isRunning()) {
        return;
    }

    m_input = input;
    m_presetArgs = presetArgs;
    m_output = output;
    m_overwrite = overwrite;
    m_outputPreExisted = QFileInfo::exists(output);

    m_totalUs = 0;
    m_lastOutTimeUs = 0;
    m_sawEnd = false;
    m_cancelRequested = false;
    m_probeHandled = false;
    m_reported = false;
    m_progressBuffer.clear();
    m_logBuffer.clear();
    m_logTail.clear();

    m_probe->start(QStringLiteral("ffprobe"),
                   {QStringLiteral("-v"), QStringLiteral("error"),
                    QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                    QStringLiteral("-of"), QStringLiteral("json"), input});
}

void EncoderController::onProbeFinished(int exitCode, QProcess::ExitStatus)
{
    if (m_probeHandled) {
        return;
    }
    m_probeHandled = true;

    double seconds = -1.0;
    if (exitCode == 0) {
        const QJsonDocument doc = QJsonDocument::fromJson(m_probe->readAllStandardOutput());
        if (doc.isObject()) {
            seconds = doc.object().value(QStringLiteral("format")).toObject()
                              .value(QStringLiteral("duration"))
                              .toDouble(-1.0);
        }
    }

    m_totalUs = seconds > 0.0 ? qint64(seconds * 1e6) : 0;
    emit durationProbed(seconds);
    launch();
}

void EncoderController::onProbeError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart || m_probeHandled) {
        return;
    }
    m_probeHandled = true;

    // Duration only feeds the percentage, so an unknown total degrades rather than fails.
    emit durationProbed(-1.0);
    launch();
}

void EncoderController::launch()
{
    const QStringList cmd = buildCommand(m_input, m_presetArgs, m_output, m_overwrite);
    emit logLine(QStringLiteral("Running: %1").arg(cmd.join(QLatin1Char(' '))));
    m_ffmpeg->start(cmd.first(), cmd.mid(1));
}

void EncoderController::pumpProgress()
{
    m_progressBuffer += m_ffmpeg->readAllStandardOutput();

    for (int newline = m_progressBuffer.indexOf('\n'); newline >= 0;
         newline = m_progressBuffer.indexOf('\n')) {
        handleProgressLine(m_progressBuffer.left(newline));
        m_progressBuffer.remove(0, newline + 1);
    }
}

void EncoderController::handleProgressLine(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    const int equals = trimmed.indexOf('=');
    if (equals <= 0) {
        return;
    }

    const QByteArray key = trimmed.left(equals);
    const QByteArray value = trimmed.mid(equals + 1);

    qint64 us = -1;
    if (key == "out_time_us") {
        bool ok = false;
        us = value.toLongLong(&ok);
        if (!ok) {
            return;
        }
    } else if (key == "out_time") {
        us = parseClock(value);
    } else if (key == "progress") {
        if (value == "end") {
            m_sawEnd = true;
            emit progressed(m_lastOutTimeUs, m_totalUs);
        }
        return;
    } else {
        return;
    }

    if (us > m_lastOutTimeUs) {
        m_lastOutTimeUs = us;
        emit progressed(us, m_totalUs);
    }
}

void EncoderController::pumpLog()
{
    QByteArray chunk = m_ffmpeg->readAllStandardError();
    chunk.replace('\r', '\n');
    m_logBuffer += chunk;

    for (int newline = m_logBuffer.indexOf('\n'); newline >= 0;
         newline = m_logBuffer.indexOf('\n')) {
        const QByteArray line = m_logBuffer.left(newline).trimmed();
        m_logBuffer.remove(0, newline + 1);
        if (line.isEmpty()) {
            continue;
        }
        m_logTail.append(QString::fromUtf8(line));
        while (m_logTail.size() > kLogTailLines) {
            m_logTail.removeFirst();
        }
        emit logLine(QString::fromUtf8(line));
    }
}

void EncoderController::cancel()
{
    if (!isRunning()) {
        return;
    }

    m_cancelRequested = true;
    m_ffmpeg->write("q\n");
    m_ffmpeg->waitForBytesWritten(kStdinFlushMs);
    m_killWatchdog->start(kCancelGraceMs);
    emit logLine(QStringLiteral("Cancel requested."));
}

void EncoderController::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    pumpProgress();
    pumpLog();
    if (!m_progressBuffer.isEmpty()) {
        handleProgressLine(m_progressBuffer);
        m_progressBuffer.clear();
    }
    if (!m_logBuffer.trimmed().isEmpty()) {
        m_logTail.append(QString::fromUtf8(m_logBuffer.trimmed()));
    }

    // Must stay ahead of the success check below. A graceful 'q' cancel still exits 0,
    // still emits progress=end, and still finalizes a valid playable file (verified
    // 2026-10-26: 10s input stopped at 3.17s), so the invariant would otherwise
    // report a cancelled encode as a successful one.
    if (m_cancelRequested) {
        // A stopped-mid-encode file is not a deliverable, so it must not look like one.
        discardOutputIfOurs();
        reportFailure(QStringLiteral("Cancelled."));
        return;
    }

    const qint64 size = QFileInfo::exists(m_output) ? QFileInfo(m_output).size() : -1;

    // returnCode 0 alone is not success: this ffmpeg exits 0 after declining to
    // overwrite, having written nothing.
    if (status == QProcess::NormalExit && exitCode == 0 && m_sawEnd && size > 0) {
        m_reported = true;
        emit succeeded(m_output);
        emit runFinished();
        return;
    }

    const QString detail = m_logTail.isEmpty() ? QStringLiteral("(ffmpeg produced no output)")
                                               : m_logTail.join(QLatin1Char('\n'));
    discardOutputIfOurs();
    reportFailure(QStringLiteral("No valid encode (exit %1, status %2, output size %3).\n%4")
                      .arg(exitCode)
                      .arg(status == QProcess::CrashExit ? QStringLiteral("crash")
                                                         : QStringLiteral("normal"))
                      .arg(size)
                      .arg(detail));
}

void EncoderController::onProcessError(QProcess::ProcessError error)
{
    // errorOccurred arrives before finished. A cancel escalated to terminate()/kill()
    // shows up here as Crashed, so the outcome must stay with onProcessFinished, which
    // reports it as a cancel and discards the partial file.
    if (m_cancelRequested) {
        return;
    }

    if (error == QProcess::FailedToStart) {
        reportFailure(QStringLiteral("Could not start ffmpeg. Is it installed and on PATH?"));
    } else if (error == QProcess::Crashed) {
        reportFailure(QStringLiteral("ffmpeg crashed."));
    }
}

void EncoderController::reportFailure(const QString &message)
{
    if (m_reported) {
        return;
    }
    m_reported = true;
    emit failed(message);
    emit runFinished();
}

void EncoderController::discardOutputIfOurs()
{
    if (!QFileInfo::exists(m_output)) {
        return;
    }

    // With -n, ffmpeg declines to touch an existing file, so that file is still the
    // user's original encode and our cleanup must leave it alone.
    if (m_outputPreExisted && !m_overwrite) {
        emit logLine(QStringLiteral("Left pre-existing %1 untouched").arg(m_output));
        return;
    }

    const bool removed = QFile::remove(m_output);
    emit logLine(removed ? QStringLiteral("Removed partial output %1").arg(m_output)
                         : QStringLiteral("Could not remove partial output %1").arg(m_output));
}
