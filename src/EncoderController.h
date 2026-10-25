#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class QTimer;

class EncoderController : public QObject
{
    Q_OBJECT

public:
    enum class Container
    {
        Mp4,
        Mkv,
        Webm
    };
    Q_ENUM(Container)

    explicit EncoderController(QObject *parent = nullptr);

    static QString suffixFor(Container container);
    static QString applyContainer(const QString &fileName, Container container);
    static QStringList buildCommand(const QString &input, const QStringList &presetArgs,
                                    const QString &output, bool overwrite);

    bool isRunning() const;

public slots:
    void start(const QString &input, const QStringList &presetArgs, const QString &output,
               bool overwrite);
    void cancel();

signals:
    void durationProbed(double seconds);
    void progressed(qint64 outTimeUs, qint64 totalUs);
    void logLine(const QString &text);
    void succeeded(const QString &output);
    void failed(const QString &message);
    void runFinished();

private slots:
    void onProbeFinished(int exitCode, QProcess::ExitStatus status);
    void onProbeError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void pumpProgress();
    void pumpLog();

private:
    void launch();
    void handleProgressLine(const QByteArray &line);
    void reportFailure(const QString &message);
    void discardOutputIfOurs();

    QProcess *m_probe;
    QProcess *m_ffmpeg;
    QTimer *m_killWatchdog;

    QString m_input;
    QString m_output;
    QStringList m_presetArgs;
    bool m_overwrite = false;
    bool m_outputPreExisted = false;

    qint64 m_totalUs = 0;
    qint64 m_lastOutTimeUs = 0;
    bool m_sawEnd = false;
    bool m_cancelRequested = false;
    bool m_probeHandled = false;
    bool m_reported = false;

    QByteArray m_progressBuffer;
    QByteArray m_logBuffer;
    QStringList m_logTail;
};
