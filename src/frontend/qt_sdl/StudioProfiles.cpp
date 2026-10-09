// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioProfiles.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>
#include <QJsonArray>
#include <algorithm>
QString StudioProfiles::path(const QString& id) const
{
    if(QUuid(id).isNull()) return {};
    return root+"/"+QUuid(id).toString(QUuid::WithoutBraces)+".json";
}
QVector<StudioProfileInfo> StudioProfiles::list() const
{
    QVector<StudioProfileInfo> result;
    for(const auto& name : QDir(root).entryList({"*.json"},QDir::Files,QDir::Name)) {
        if(QUuid(name.chopped(5)).isNull()) continue;
        QFile file(root+"/"+name); if(!file.open(QIODevice::ReadOnly) || file.size()>32*1024*1024) continue;
        auto json=QJsonDocument::fromJson(file.readAll()).object();
        if(json["format"]!="MelonStudio" || json["version"].toInt()!=3 || json["profileId"].toString()!=name.chopped(5)) continue;
        result.append({json["profileId"].toString(),json["gameLabel"].toString(),json["gameId"].toString()});
    }
    QFile orderFile(root+"/order.json");
    QJsonArray order;
    if(orderFile.open(QIODevice::ReadOnly)) order=QJsonDocument::fromJson(orderFile.readAll()).array();
    QStringList ids; for(const auto& id : order) ids.append(id.toString());
    std::stable_sort(result.begin(),result.end(),[&](const auto& a,const auto& b) {
        int ai=ids.indexOf(a.id), bi=ids.indexOf(b.id);
        return (ai<0 ? ids.size() : ai)<(bi<0 ? ids.size() : bi);
    });
    return result;
}
bool StudioProfiles::load(const QString& id,StudioDocument& doc,QString& error) const
{
    StudioDocument candidate;
    if(path(id).isEmpty() || !candidate.load(path(id),error)) return false;
    if(candidate.profileId!=id) { error="Profile identifier does not match its file."; return false; }
    doc=std::move(candidate); return true;
}
bool StudioProfiles::prefer(const StudioDocument& doc,QString& error)
{
    QFile existing(root+"/associations.json"); QJsonObject choices;
    if(existing.open(QIODevice::ReadOnly)) choices=QJsonDocument::fromJson(existing.readAll()).object();
    existing.close(); // Windows cannot atomically replace an open registry file.
    choices[doc.gameId]=doc.profileId;
    QSaveFile file(root+"/associations.json"); auto bytes=QJsonDocument(choices).toJson();
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) { error=file.errorString(); return false; }
    return true;
}
bool StudioProfiles::save(StudioDocument& doc,QString& error)
{
    if(path(doc.profileId).isEmpty()) { error="Invalid profile identifier."; return false; }
    return doc.save(path(doc.profileId),error);
}
bool StudioProfiles::forRom(const QString& rom,const QString& label,const QString& legacyPath,StudioDocument& doc,QString& error)
{
    QFile choices(root+"/associations.json"); QString preferred;
    if(choices.open(QIODevice::ReadOnly)) preferred=QJsonDocument::fromJson(choices.readAll()).object()[rom].toString();
    choices.close(); // Release the read handle before prefer() can rewrite it.
    auto catalog=list();
    for(const auto& p : catalog) if(p.rom==rom && p.id==preferred) return load(p.id,doc,error);
    for(const auto& p : catalog) if(p.rom==rom) return load(p.id,doc,error) && prefer(doc,error);
    StudioDocument candidate; candidate.gameId=rom; candidate.gameLabel=label;
    if(QFile::exists(legacyPath)) {
        if(!candidate.load(legacyPath,error)) return false;
        if(!QFile::exists(legacyPath+".pre-v3.bak") && !QFile::copy(legacyPath,legacyPath+".pre-v3.bak")) {
            error="Cannot back up the original profile."; return false;
        }
    }
    if(!save(candidate,error) || !prefer(candidate,error)) return false;
    doc=std::move(candidate); return true;
}
bool StudioProfiles::duplicate(const QString& id,const QString& name,StudioDocument& doc,QString& error)
{
    StudioDocument candidate; if(!load(id,candidate,error)) return false;
    candidate.profileId=QUuid::createUuid().toString(QUuid::WithoutBraces); candidate.gameLabel=name.left(128);
    for(auto& id : candidate.sceneIds) id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    for(auto& scene : candidate.elements) for(auto& e : scene) e.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    candidate.dirty=true; if(!save(candidate,error)) return false;
    doc=std::move(candidate); return true;
}
bool StudioProfiles::remove(const QString& id,QString& error)
{
    if(path(id).isEmpty()) { error="Invalid profile identifier."; return false; }
    // Keep a recoverable copy outside the active catalog.
    QString archive=root+"/deleted/"; if(!QDir().mkpath(archive)) { error="Cannot create profile archive."; return false; }
    QString backup=archive+id+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".json";
    if(!QFile::rename(path(id),backup)) { error="Cannot archive the selected profile."; return false; }
    return true;
}

bool StudioProfiles::setOrder(const QStringList& ids,QString& error)
{
    if(!QDir().mkpath(root)) { error="Cannot create profiles directory."; return false; }
    QJsonArray order; for(const auto& id : ids) { if(path(id).isEmpty()) { error="Invalid profile identifier."; return false; } order.append(id); }
    auto bytes=QJsonDocument(order).toJson(); QSaveFile file(root+"/order.json");
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) { error=file.errorString(); return false; }
    return true;
}
