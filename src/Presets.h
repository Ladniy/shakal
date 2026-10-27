#pragma once

#include <QStringList>
#include <QVector>

struct Preset
{
    QString label;
    QStringList args;
};

// Ported from the pre-Qt TUI; see commit afd0361. ULTRA's hardcoded
// 320:180 -> 1920:1080 aspect-distorts non-16:9 sources on purpose, it is not a bug.
// "default" is the former "Strong" and is what runs unless the ULTRA checkbox is ticked.
inline QVector<Preset> shakalPresets()
{
    return {
        {QStringLiteral("default"),
         {QStringLiteral("-vcodec"), QStringLiteral("libx265"), QStringLiteral("-crf"),
          QStringLiteral("32"), QStringLiteral("-preset"), QStringLiteral("fast"),
          QStringLiteral("-acodec"), QStringLiteral("aac"), QStringLiteral("-b:a"),
          QStringLiteral("96k")}},
        {QStringLiteral("ULTRA"),
         {QStringLiteral("-vf"),
          QStringLiteral("scale=320:180,fps=12,scale=1920:1080:flags=neighbor"),
          QStringLiteral("-vcodec"), QStringLiteral("libx265"), QStringLiteral("-crf"),
          QStringLiteral("42"), QStringLiteral("-preset"), QStringLiteral("ultrafast"),
          QStringLiteral("-acodec"), QStringLiteral("aac"), QStringLiteral("-b:a"),
          QStringLiteral("48k")}},
    };
}
