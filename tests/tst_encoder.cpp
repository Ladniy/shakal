#include "EncoderController.h"
#include "Presets.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

namespace {
using Container = EncoderController::Container;

constexpr int kEncodeTimeoutMs = 180000;
constexpr int kCancelWaitMs = 15000;

bool runTool(const QString &program, const QStringList &args, int timeoutMs)
{
    QProcess proc;
    proc.start(program, args);
    if (!proc.waitForStarted(timeoutMs)) {
        return false;
    }
    return proc.waitForFinished(timeoutMs) && proc.exitCode() == 0;
}

const Preset &preset(const QString &label)
{
    static const QVector<Preset> presets = shakalPresets();
    for (const Preset &p : presets) {
        if (p.label == label) {
            return p;
        }
    }
    qFatal("unknown preset: %s", qPrintable(label));
}
} // namespace

class TstEncoder : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void cleanEncodeSucceeds();
    void webmEncodeUsesWebmCodecs();
    void refusedOverwriteKeepsExistingFile();
    void muxFailureRemovesJunkFile();
    void cancelIsNotReportedAsSuccess();

private:
    QString makeInput(const QString &name, const QString &size, const QString &seconds,
                      const QString &audioCodec, const QString &container);
    QStringList probeCodecNames(const QString &path);
    void finishAndVerify(EncoderController &encoder, QSignalSpy &succeeded, QSignalSpy &failed);

    QTemporaryDir m_dir;
};

QString TstEncoder::makeInput(const QString &name, const QString &size, const QString &seconds,
                              const QString &audioCodec, const QString &container)
{
    const QString path = m_dir.filePath(name + QLatin1Char('.') + container);

    const bool ok = runTool(
        QStringLiteral("ffmpeg"),
        {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
         QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("testsrc=size=%1:rate=25").arg(size), QStringLiteral("-t"), seconds,
         QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("sine=frequency=440:duration=%1").arg(seconds), QStringLiteral("-c:v"),
         QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("ultrafast"),
         QStringLiteral("-c:a"), audioCodec, QStringLiteral("-shortest"), path},
        kEncodeTimeoutMs);

    // An unusable fixture must fail its own test case, not abort the whole binary.
    return ok && QFileInfo::exists(path) ? path : QString();
}

void TstEncoder::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QVERIFY2(QFile::exists(QStringLiteral("/usr/bin/ffmpeg")), "ffmpeg not installed");
    QVERIFY2(QFile::exists(QStringLiteral("/usr/bin/ffprobe")), "ffprobe not installed");
}

QStringList TstEncoder::probeCodecNames(const QString &path)
{
    QProcess probe;
    probe.start(QStringLiteral("ffprobe"),
                {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                 QStringLiteral("stream=codec_name"), QStringLiteral("-of"),
                 QStringLiteral("csv=p=0"), path});

    if (!probe.waitForStarted(kEncodeTimeoutMs) || !probe.waitForFinished(kEncodeTimeoutMs)) {
        return {};
    }
    return QString::fromUtf8(probe.readAllStandardOutput())
        .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

void TstEncoder::finishAndVerify(EncoderController &encoder, QSignalSpy &succeeded,
                                 QSignalSpy &failed)
{
    QSignalSpy done(&encoder, &EncoderController::runFinished);
    QVERIFY(done.wait(kEncodeTimeoutMs));
    QCOMPARE(succeeded.count(), 0);
    QCOMPARE(failed.count(), 1);
}

void TstEncoder::cleanEncodeSucceeds()
{
    // lavfi sizes use WIDTHxHEIGHT; the colon form is scale= filter syntax only.
    const QString input = makeInput(QStringLiteral("src_small"), QStringLiteral("640x360"),
                                    QStringLiteral("2"), QStringLiteral("aac"),
                                    QStringLiteral("mp4"));
    QVERIFY(!input.isEmpty());
    const QString output = m_dir.filePath(QStringLiteral("clean.mp4"));

    EncoderController encoder;
    QSignalSpy succeeded(&encoder, &EncoderController::succeeded);
    QSignalSpy failed(&encoder, &EncoderController::failed);
    QSignalSpy done(&encoder, &EncoderController::runFinished);

    encoder.start(input, preset(QStringLiteral("ULTRA")).args, output, false, Container::Mp4);
    QVERIFY(done.wait(kEncodeTimeoutMs));

    QVERIFY2(succeeded.count() == 1,
             qPrintable(failed.isEmpty() ? QStringLiteral("no signals at all")
                                         : failed.at(0).at(0).toString()));
    QCOMPARE(failed.count(), 0);
    QVERIFY(QFileInfo(output).size() > 0);
}

void TstEncoder::webmEncodeUsesWebmCodecs()
{
    // The bug: both table entries name libx265, and the WebM muxer refuses HEVC outright.
    const QString input = makeInput(QStringLiteral("src_webm"), QStringLiteral("320x240"),
                                    QStringLiteral("1"), QStringLiteral("aac"),
                                    QStringLiteral("mp4"));
    QVERIFY(!input.isEmpty());
    const QString output = m_dir.filePath(QStringLiteral("encoded.webm"));

    EncoderController encoder;
    QSignalSpy succeeded(&encoder, &EncoderController::succeeded);
    QSignalSpy failed(&encoder, &EncoderController::failed);
    QSignalSpy done(&encoder, &EncoderController::runFinished);

    encoder.start(input, preset(QStringLiteral("default")).args, output, false, Container::Webm);
    QVERIFY(done.wait(kEncodeTimeoutMs));

    QVERIFY2(succeeded.count() == 1,
             qPrintable(failed.isEmpty() ? QStringLiteral("no signals at all")
                                         : failed.at(0).at(0).toString()));
    QCOMPARE(failed.count(), 0);

    // A file that merely exists proves nothing: assert what the container actually got.
    const QStringList codecs = probeCodecNames(output);
    QVERIFY2(codecs.contains(QStringLiteral("vp9")),
             qPrintable(QStringLiteral("no VP9 stream, got: ") + codecs.join(QLatin1Char(' '))));
    QVERIFY2(codecs.contains(QStringLiteral("opus")),
             qPrintable(QStringLiteral("no Opus stream, got: ") + codecs.join(QLatin1Char(' '))));
}

void TstEncoder::refusedOverwriteKeepsExistingFile()
{
    const QString input = makeInput(QStringLiteral("src_small2"), QStringLiteral("640x360"),
                                    QStringLiteral("2"), QStringLiteral("aac"),
                                    QStringLiteral("mp4"));
    QVERIFY(!input.isEmpty());
    const QString output = m_dir.filePath(QStringLiteral("precious.mp4"));

    // Stand in for a user's earlier encode: distinctive bytes we can check for later.
    const QByteArray sentinel = QByteArrayLiteral("NOT AN ENCODE - USER DATA");
    {
        QFile file(output);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(sentinel);
    }

    EncoderController encoder;
    QSignalSpy succeeded(&encoder, &EncoderController::succeeded);
    QSignalSpy failed(&encoder, &EncoderController::failed);
    QSignalSpy done(&encoder, &EncoderController::runFinished);

    // overwrite == false => -n, and the target already exists: ffmpeg exits 0 here.
    encoder.start(input, preset(QStringLiteral("ULTRA")).args, output, false, Container::Mp4);
    QVERIFY(done.wait(kEncodeTimeoutMs));

    // Exit 0 with no progress=end must not be a success...
    QCOMPARE(succeeded.count(), 0);
    QCOMPARE(failed.count(), 1);
    // ...and cleanup must not have destroyed the pre-existing file.
    QFile file(output);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), sentinel);
}

void TstEncoder::muxFailureRemovesJunkFile()
{
    // Verified on this host: a codec the WebM muxer refuses fails with exit 234 after ffmpeg
    // has already written a ~262 byte stub, and it still prints progress=end.
    // Neither preset can reach this path any more because the mapping re-expresses libx265 as
    // VP9, so the rejection is forced through a codec the mapping deliberately leaves alone.
    const QString input = makeInput(QStringLiteral("src_ac3"), QStringLiteral("640x360"),
                                    QStringLiteral("2"), QStringLiteral("ac3"),
                                    QStringLiteral("mkv"));
    QVERIFY(!input.isEmpty());
    const QString output = m_dir.filePath(QStringLiteral("junk.webm"));

    EncoderController encoder;
    QSignalSpy succeeded(&encoder, &EncoderController::succeeded);
    QSignalSpy failed(&encoder, &EncoderController::failed);

    encoder.start(input,
                  {QStringLiteral("-vcodec"), QStringLiteral("libx264"), QStringLiteral("-acodec"),
                   QStringLiteral("copy")},
                  output, true, Container::Webm);
    finishAndVerify(encoder, succeeded, failed);

    QVERIFY2(!QFileInfo::exists(output),
             "a broken stub was left behind and would look like an encode");
}

void TstEncoder::cancelIsNotReportedAsSuccess()
{
    // Deliberately slow so the encode is provably still running when q is sent.
    const QString input = makeInput(QStringLiteral("src_slow"), QStringLiteral("1280x720"),
                                    QStringLiteral("6"), QStringLiteral("aac"),
                                    QStringLiteral("mp4"));
    QVERIFY(!input.isEmpty());
    const QString output = m_dir.filePath(QStringLiteral("cancelled.mp4"));

    EncoderController encoder;
    QSignalSpy succeeded(&encoder, &EncoderController::succeeded);
    QSignalSpy failed(&encoder, &EncoderController::failed);
    QSignalSpy progressed(&encoder, &EncoderController::progressed);
    QSignalSpy done(&encoder, &EncoderController::runFinished);

    encoder.start(input,
                  {QStringLiteral("-vcodec"), QStringLiteral("libx265"), QStringLiteral("-crf"),
                   QStringLiteral("22"), QStringLiteral("-preset"), QStringLiteral("veryslow"),
                   QStringLiteral("-threads"), QStringLiteral("1"), QStringLiteral("-acodec"),
                   QStringLiteral("copy")},
                  output, true, Container::Mp4);

    QVERIFY(progressed.wait(kCancelWaitMs));
    encoder.cancel();
    QVERIFY(done.wait(kEncodeTimeoutMs));

    QCOMPARE(succeeded.count(), 0);
    QCOMPARE(failed.count(), 1);
    QVERIFY2(failed.at(0).at(0).toString().contains(QStringLiteral("Cancelled")),
             qPrintable(failed.at(0).at(0).toString()));
    QVERIFY2(!QFileInfo::exists(output), "cancelled partial output was left in place");
}

QTEST_GUILESS_MAIN(TstEncoder)

#include "tst_encoder.moc"
