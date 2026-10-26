#include "EncoderController.h"
#include "Presets.h"

#include <cstdio>

namespace {
int gFailures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++gFailures;
        return;
    }
    std::fprintf(stdout, "ok: %s\n", what);
}

const Preset *presetByLabel(const QString &label)
{
    // Must outlive the call: the returned pointer is read throughout main(), and a local
    // QVector here leaves a dangling pointer that only appears to work.
    static const QVector<Preset> presets = shakalPresets();
    for (const Preset &preset : presets) {
        if (preset.label == label) {
            return &preset;
        }
    }
    return nullptr;
}
} // namespace

int main()
{
    using C = EncoderController::Container;

    check(shakalPresets().size() == 4, "four presets ported");
    check(presetByLabel(QStringLiteral("Light")) != nullptr, "Light exists");
    check(presetByLabel(QStringLiteral("ULTRA")) != nullptr, "ULTRA exists");

    const Preset *light = presetByLabel(QStringLiteral("Light"));
    check(light && light->args.contains("libx265"), "Light uses libx265");
    check(light && light->args.contains("copy"), "Light copies audio");

    const Preset *ultra = presetByLabel(QStringLiteral("ULTRA"));
    check(ultra && ultra->args.contains(
              QStringLiteral("scale=320:180,fps=12,scale=1920:1080:flags=neighbor")),
          "ULTRA keeps its verbatim vf chain");

    check(EncoderController::suffixFor(C::Mp4) == QLatin1String("mp4"), "suffix mp4");
    check(EncoderController::suffixFor(C::Mkv) == QLatin1String("mkv"), "suffix mkv");
    check(EncoderController::suffixFor(C::Webm) == QLatin1String("webm"), "suffix webm");

    // Defect 2 from the review: the old TUI handed ffmpeg an extension-less path.
    check(EncoderController::applyContainer(QStringLiteral("clip"), C::Mp4)
              == QLatin1String("clip.mp4"),
          "missing extension is appended");
    check(EncoderController::applyContainer(QStringLiteral("clip.mp4"), C::Mp4)
              == QLatin1String("clip.mp4"),
          "existing extension is not duplicated");
    check(EncoderController::applyContainer(QStringLiteral("clip.MP4"), C::Mp4)
              == QLatin1String("clip.MP4"),
          "extension match is case-insensitive");

    const QStringList presetArgs{QStringLiteral("-crf"), QStringLiteral("22")};
    const QStringList withOverwrite = EncoderController::buildCommand(
        QStringLiteral("/in.mp4"), presetArgs, QStringLiteral("/out/clip.mp4"), true, C::Mp4);
    const QStringList withoutOverwrite = EncoderController::buildCommand(
        QStringLiteral("/in.mp4"), presetArgs, QStringLiteral("/out/clip.mp4"), false, C::Mp4);

    check(withOverwrite.first() == QLatin1String("ffmpeg"), "argv[0] is ffmpeg");
    check(withOverwrite.contains(QStringLiteral("-progress"))
              && withOverwrite.contains(QStringLiteral("pipe:1")),
          "progress is sent to stdout only");
    check(withOverwrite.indexOf(QStringLiteral("-i")) + 1 == withOverwrite.indexOf(
              QStringLiteral("/in.mp4")),
          "input follows -i");
    check(withOverwrite.indexOf(QStringLiteral("/out/clip.mp4")) == withOverwrite.size() - 1,
          "output is the final argument");
    check(withOverwrite.contains(QStringLiteral("-y")), "-y when overwrite confirmed");
    check(withoutOverwrite.contains(QStringLiteral("-n")), "-n when not confirmed");
    check(!withoutOverwrite.contains(QStringLiteral("-y")), "no -y without confirmation");
    check(withOverwrite.indexOf(QStringLiteral("-crf"))
                  < withOverwrite.indexOf(QStringLiteral("-y")),
          "preset args precede the overwrite flag");

    // WebM refuses the codecs every preset is written in, so buildCommand re-expresses them.
    // The CRF number and the vf chain are the degradation itself and must survive untouched.
    const QStringList webmLight = EncoderController::buildCommand(
        QStringLiteral("/in.mp4"), light->args, QStringLiteral("/out/clip.webm"), true, C::Webm);
    check(webmLight.indexOf(QStringLiteral("-vcodec")) + 1
                  == webmLight.indexOf(QStringLiteral("libvpx-vp9")),
          "webm swaps libx265 for VP9");
    check(!webmLight.contains(QStringLiteral("libx265")), "webm argv keeps no HEVC encoder");
    check(webmLight.indexOf(QStringLiteral("-acodec")) + 1
                  == webmLight.indexOf(QStringLiteral("libopus")),
          "webm re-encodes audio as Opus");
    check(!webmLight.contains(QStringLiteral("copy")), "webm does not stream-copy audio");
    check(webmLight.contains(QStringLiteral("22")), "webm keeps Light's CRF");

    const QStringList webmUltra = EncoderController::buildCommand(
        QStringLiteral("/in.mp4"), ultra->args, QStringLiteral("/out/clip.webm"), true, C::Webm);
    check(!webmUltra.contains(QStringLiteral("-preset")),
          "webm drops the -preset libvpx ignores");
    check(webmUltra.indexOf(QStringLiteral("-cpu-used")) + 1 == webmUltra.indexOf(QStringLiteral("4")),
          "webm maps x265 speed to -cpu-used");
    check(webmUltra.contains(QStringLiteral("42")), "webm keeps ULTRA's CRF");
    check(webmUltra.contains(
              QStringLiteral("scale=320:180,fps=12,scale=1920:1080:flags=neighbor")),
          "webm keeps ULTRA's degradation chain");

    // A codec outside the mapping is passed through, so ffmpeg's own muxer error stays visible
    // instead of an encoder being substituted behind the user's back.
    const QStringList unmapped = EncoderController::buildCommand(
        QStringLiteral("/in.mp4"), {QStringLiteral("-vcodec"), QStringLiteral("libx264")},
        QStringLiteral("/out/clip.webm"), true, C::Webm);
    check(unmapped.contains(QStringLiteral("libx264")), "webm leaves an unmapped codec alone");

    // mp4 and mkv accept HEVC, so they must keep receiving exactly the preset's own args.
    const QStringList mkvLight = EncoderController::buildCommand(
        QStringLiteral("/in.mp4"), light->args, QStringLiteral("/out/clip.mkv"), true, C::Mkv);
    check(mkvLight.contains(QStringLiteral("libx265")) && mkvLight.contains(QStringLiteral("copy"))
              && mkvLight.contains(QStringLiteral("-preset")),
          "mkv passes the preset through unchanged");

    std::fprintf(stderr, gFailures == 0 ? "\nAll checks passed\n"
                                       : "\n%d check(s) failed\n",
                 gFailures);
    return gFailures == 0 ? 0 : 1;
}
