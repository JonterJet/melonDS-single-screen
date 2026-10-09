// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STUDIO_PROFILES_H
#define STUDIO_PROFILES_H
#include "StudioDocument.h"
struct StudioProfileInfo { QString id, name, rom; };
class StudioProfiles
{
public:
    explicit StudioProfiles(QString directory) : root(std::move(directory)) {}
    QVector<StudioProfileInfo> list() const;
    QString path(const QString& id) const;
    bool load(const QString& id, StudioDocument& doc, QString& error) const;
    bool save(StudioDocument& doc, QString& error);
    bool forRom(const QString& rom, const QString& label, const QString& legacyPath, StudioDocument& doc, QString& error);
    bool duplicate(const QString& id, const QString& name, StudioDocument& doc, QString& error);
    bool remove(const QString& id, QString& error);
    bool setOrder(const QStringList& ids, QString& error);
    bool prefer(const StudioDocument& doc, QString& error);
private:
    QString root;
};
#endif
