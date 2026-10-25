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
    const QVector<Preset> presets = shakalPresets();
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
    check(presetByLabel(QStringLiteral("SHAKAL Light")) != nullptr, "Light exists");
    check(presetByLabel(QStringLiteral("SHAKAL ULTRA")) != nullptr, "ULTRA exists");

    const Preset *light = presetByLabel(QStringLiteral("SHAKAL Light"));
    check(light && light->args.contains("libx265"), "Light uses libx265");
    check(light && light->args.contains("copy"), "Light copies audio");

    const Preset *ultra = presetByLabel(QStringLiteral("SHAKAL ULTRA"));
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
        QStringLiteral("/in.mp4"), presetArgs, QStringLiteral("/out/clip.mp4"), true);
    const QStringList withoutOverwrite = EncoderController::buildCommand(
        QStringLiteral("/in.mp4"), presetArgs, QStringLiteral("/out/clip.mp4"), false);

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

    std::fprintf(stderr, gFailures == 0 ? "\nAll checks passed\n"
                                       : "\n%d check(s) failed\n",
                 gFailures);
    return gFailures == 0 ? 0 : 1;
}
