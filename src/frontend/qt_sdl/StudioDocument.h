// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STUDIO_DOCUMENT_H
#define STUDIO_DOCUMENT_H
#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QVector>

constexpr int StudioStateCount = 8;
constexpr int StudioMarioFirst = 3;
constexpr int StudioRacing = 5;
enum class StudioLayout { Existing = 0, Top, Bottom, Both };

struct StudioElement
{
    QString name = "HUD element";
    int screen = 1;
    int x = 0, y = 0, width = 64, height = 32;
    bool enabled = true;
    QRect destination{176, 120, 72, 54};
    QRect sourceRect() const { return {x, y, width, height}; }
};
struct StudioReference
{
    QString name;
    int screen = 0;
    QRect region;
    QImage image;
    double threshold = 0.96;
    bool enabled = true;
};
class StudioDocument
{
public:
    StudioDocument();
    QString gameId, gameLabel;
    int activeState = 0;
    QVector<StudioElement> elements[StudioStateCount];
    QVector<StudioReference> references[StudioStateCount];
    StudioLayout layouts[StudioStateCount];
    bool marioEnabled = false;
    bool automatic = false;
    StudioLayout fallback = StudioLayout::Both;
    int confirmationMs = 350;
    double ambiguityMargin = 0.03;
    bool dirty = false;
    bool legacyProfile = false;
    static QString stateName(int state);
    int referenceCount() const;
    QJsonObject toJson() const;
    bool load(const QString& path, QString& error);
    bool save(const QString& path, QString& error);
};
#endif
