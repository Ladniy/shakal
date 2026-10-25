#pragma once

#include <QStringList>
#include <QVector>

struct Preset
{
    QString label;
    QStringList args;
};

// Ported verbatim from the pre-Qt TUI; see commit afd0361. ULTRA's hardcoded
// 320:180 -> 1920:1080 aspect-distorts non-16:9 sources on purpose, it is not a bug.
inline QVector<Preset> shakalPresets()
{
    return {
        {QStringLiteral("SHAKAL Light"),
         {QStringLiteral("-vcodec"), QStringLiteral("libx265"), QStringLiteral("-crf"),
          QStringLiteral("22"), QStringLiteral("-preset"), QStringLiteral("slow"),
          QStringLiteral("-acodec"), QStringLiteral("copy")}},
        {QStringLiteral("SHAKAL Medium"),
         {QStringLiteral("-vcodec"), QStringLiteral("libx265"), QStringLiteral("-crf"),
          QStringLiteral("26"), QStringLiteral("-preset"), QStringLiteral("medium"),
          QStringLiteral("-acodec"), QStringLiteral("aac"), QStringLiteral("-b:a"),
          QStringLiteral("128k")}},
        {QStringLiteral("SHAKAL Strong"),
         {QStringLiteral("-vcodec"), QStringLiteral("libx265"), QStringLiteral("-crf"),
          QStringLiteral("32"), QStringLiteral("-preset"), QStringLiteral("fast"),
          QStringLiteral("-acodec"), QStringLiteral("aac"), QStringLiteral("-b:a"),
          QStringLiteral("96k")}},
        {QStringLiteral("SHAKAL ULTRA"),
         {QStringLiteral("-vf"),
          QStringLiteral("scale=320:180,fps=12,scale=1920:1080:flags=neighbor"),
          QStringLiteral("-vcodec"), QStringLiteral("libx265"), QStringLiteral("-crf"),
          QStringLiteral("42"), QStringLiteral("-preset"), QStringLiteral("ultrafast"),
          QStringLiteral("-acodec"), QStringLiteral("aac"), QStringLiteral("-b:a"),
          QStringLiteral("48k")}},
    };
}
