// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STUDIO_DOCUMENT_H
#define STUDIO_DOCUMENT_H

#include <QJsonObject>
#include <QString>
#include <QVector>

struct StudioElement
{
    QString name = "HUD element";
    int screen = 1;
    int x = 0, y = 0, width = 64, height = 32;
    bool enabled = true;
};

// Editor metadata only. This model never changes emulation or screen rendering.
class StudioDocument
{
public:
    QString gameId;
    QString gameLabel;
    int activeState = 0;
    QVector<StudioElement> elements[3];
    bool dirty = false;

    static QString stateName(int state);
    QJsonObject toJson() const;
    bool load(const QString& path, QString& error);
    bool save(const QString& path, QString& error);
};
#endif
