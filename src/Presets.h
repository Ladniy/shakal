#pragma once

#include <QStringList>
#include <QVector>

struct Preset
{
    QString label;
    QStringList args;
};

// There is one real preset, "default". "ULTRA" is the easter egg behind the "Shakal ULTRA"
// checkbox: its hardcoded 320:180 -> 1920:1080 chain aspect-distorts non-16:9 sources on
// purpose, and that is the joke rather than a bug. Args are ported from the pre-Qt TUI,
// see commit afd0361.
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
