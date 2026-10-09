// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STUDIO_DOCUMENT_H
#define STUDIO_DOCUMENT_H
#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QPolygonF>
#include <QString>
#include <QVector>
#include <QUuid>

constexpr int StudioLegacyStateCount = 8;
constexpr int StudioLegacyMarioFirst = 3;
constexpr int StudioLegacyRacing = 5;
enum class StudioLayout { Existing = 0, Top, Bottom, Both };

struct StudioElement
{
    QString name = "HUD element";
    int screen = 1;
    int x = 0, y = 0, width = 64, height = 32;
    bool enabled = true;
    QRect destination{176, 120, 72, 54};
    QPolygonF polygon; // Absolute native DS coordinates; empty means legacy rectangle.
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
    QString gameId, gameLabel, profileId;
    QStringList sceneNames, sceneIds;
    int sceneCount() const { return sceneNames.size(); }
    QString sceneName(int i) const { return sceneNames.value(i); }
    int addScene(const QString& name);
    void duplicateScene(int index);
    void removeScene(int index);
    void moveScene(int from, int to);
    int activeState = 0;
    QVector<QVector<StudioElement>> elements;
    QVector<QVector<StudioReference>> references;
    QVector<StudioLayout> layouts;
    bool sceneToolsEnabled = true;
    bool automatic = false;
    StudioLayout fallback = StudioLayout::Both;
    int confirmationMs = 350;
    double ambiguityMargin = 0.03;
    bool dirty = false;
    bool legacyProfile = false;
    static QString legacyStateName(int state);
    int referenceCount() const;
    QJsonObject toJson() const;
    bool load(const QString& path, QString& error);
    bool save(const QString& path, QString& error);
};
#endif
