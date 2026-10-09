// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioDocument.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>
#include <QUuid>
#include <QSet>

StudioDocument::StudioDocument()
{
    profileId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    addScene("Gameplay"); addScene("Menus"); addScene("Cutscenes");
}
int StudioDocument::addScene(const QString& name)
{
    sceneNames.append(name.trimmed().isEmpty() ? "New scene" : name.trimmed().left(128));
    sceneIds.append(QUuid::createUuid().toString(QUuid::WithoutBraces));
    elements.append(QVector<StudioElement>{}); references.append(QVector<StudioReference>{});
    layouts.append(StudioLayout::Existing); dirty = true; return sceneCount()-1;
}
void StudioDocument::duplicateScene(int i)
{
    if(i<0 || i>=sceneCount()) return;
    int row=addScene(sceneNames[i]+" copy"); elements[row]=elements[i]; for(auto& e : elements[row]) e.id=QUuid::createUuid().toString(QUuid::WithoutBraces); references[row]=references[i]; layouts[row]=layouts[i];
    moveScene(row,i+1); activeState=i+1;
}
void StudioDocument::removeScene(int i)
{
    if(i<0 || i>=sceneCount()) return;
    sceneNames.removeAt(i); sceneIds.removeAt(i); elements.removeAt(i); references.removeAt(i); layouts.removeAt(i);
    if(sceneCount()==0) addScene("New scene");
    activeState=qBound(0,activeState>i ? activeState-1 : activeState,sceneCount()-1); dirty=true;
}
void StudioDocument::moveScene(int from,int to)
{
    if(from<0 || to<0 || from>=sceneCount() || to>=sceneCount()) return;
    QString active=sceneIds[activeState];
    sceneNames.move(from,to); sceneIds.move(from,to); elements.move(from,to); references.move(from,to); layouts.move(from,to);
    activeState=sceneIds.indexOf(active); dirty=true;
}
QString StudioDocument::legacyStateName(int state)
{
    return QStringList{"Gameplay", "Menus", "Cutscenes", "Main menus", "Character / kart selection",
        "Racing", "Pause menus", "Race results"}.value(state);
}
int StudioDocument::referenceCount() const
{
    int count = 0;
    for (const auto& refs : references) count += refs.size();
    return count;
}
namespace
{
QJsonArray rectJson(const QRect& r) { return {r.x(), r.y(), r.width(), r.height()}; }
bool integer(const QJsonValue& v, int min, int max)
{
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() == std::floor(v.toDouble())
        && v.toDouble() >= min && v.toDouble() <= max;
}
bool number(const QJsonValue& v, double min, double max)
{
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= min && v.toDouble() <= max;
}
bool readRect(const QJsonValue& v, QRect& r)
{
    if (!v.isArray()) return false;
    auto a = v.toArray();
    if (a.size() != 4 || !integer(a[0], 0, 255) || !integer(a[1], 0, 191)
        || !integer(a[2], 1, 256) || !integer(a[3], 1, 192)
        || a[0].toInt() + a[2].toInt() > 256 || a[1].toInt() + a[3].toInt() > 192) return false;
    r = QRect(a[0].toInt(), a[1].toInt(), a[2].toInt(), a[3].toInt());
    return true;
}
bool text(const QJsonValue& v)
{
    return v.isString() && !v.toString().trimmed().isEmpty() && v.toString().size() <= 128;
}
}
QJsonObject StudioDocument::toJson() const
{
    QJsonArray states;
    for (int i = 0; i < sceneCount(); ++i)
    {
        QJsonArray items, refs;
        for (const auto& e : elements[i])
        {
            QJsonArray polygon; for(const auto& p : e.polygon) polygon.append(QJsonArray{p.x(),p.y()});
            items.append(QJsonObject{{"id", e.id}, {"name", e.name}, {"screen", e.screen}, {"x", e.x}, {"y", e.y},
                {"width", e.width}, {"height", e.height}, {"enabled", e.enabled}, {"destination", rectJson(e.destination)}, {"polygon",polygon}});
        }
        for (const auto& r : references[i])
        {
            QByteArray png;
            QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
            r.image.save(&buffer, "PNG");
            refs.append(QJsonObject{{"name", r.name}, {"screen", r.screen}, {"region", rectJson(r.region)},
                {"png", QString::fromLatin1(png.toBase64())}, {"threshold", r.threshold}, {"enabled", r.enabled}});
        }
        states.append(QJsonObject{{"name", sceneName(i)}, {"id",sceneIds[i]}, {"layout", int(layouts[i])}, {"elements", items}, {"references", refs}});
    }
    return {{"format", "MelonStudio"}, {"version", 3}, {"gameId", gameId}, {"gameLabel", gameLabel},
        {"activeState", activeState}, {"scenes", states}, {"profileId",profileId}, {"sceneToolsEnabled", sceneToolsEnabled}, {"automatic", automatic},
        {"fallback", int(fallback)}, {"confirmationMs", confirmationMs}, {"ambiguityMargin", ambiguityMargin}};
}
bool StudioDocument::save(const QString& path, QString& error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) { error = "Cannot create the configuration folder."; return false; }
    if (legacyProfile && QFile::exists(path) && !QFile::exists(path + ".v1.bak") && !QFile::copy(path, path + ".v1.bak"))
    { error = "Cannot back up the original version 1 profile."; return false; }
    QSaveFile file(path);
    const auto json = toJson();
    for (const auto& state : json["scenes"].toArray())
        for (const auto& reference : state.toObject()["references"].toArray())
            if (reference.toObject()["png"].toString().isEmpty())
            { error = "Cannot encode a visual reference as PNG."; return false; }
    const auto data = QJsonDocument(json).toJson();
    if (data.size() > 32 * 1024 * 1024) { error = "Profile exceeds the 32 MiB limit."; return false; }
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    { error = file.errorString(); return false; }
    dirty = false; legacyProfile = false;
    return true;
}
bool StudioDocument::load(const QString& path, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > 32 * 1024 * 1024) { error = "Profile exceeds the 32 MiB limit."; return false; }
    QJsonParseError parseError;
    auto json = QJsonDocument::fromJson(file.readAll(), &parseError);
    auto root = json.object();
    auto invalid = [&]() { error = "Invalid, wrong-game, or incompatible MelonStudio profile."; return false; };
    if (parseError.error != QJsonParseError::NoError || !json.isObject() || root["format"] != "MelonStudio"
        || !integer(root["version"], 1, 3) || (!gameId.isEmpty() && root["gameId"] != gameId) || !root["gameLabel"].isString()
        || !root[root["version"].toInt()==3 ? "scenes" : "states"].isArray()) return invalid();
    bool legacy = root["version"].toInt() == 1;
    bool modern=root["version"].toInt()==3;
    auto states=root[modern ? "scenes" : "states"].toArray();
    int count=modern ? states.size() : legacy ? 3 : StudioLegacyStateCount;
    if(count<1 || states.size()!=count || !integer(root["activeState"],0,count-1)) return invalid();
    StudioDocument candidate; candidate.legacyProfile = !modern; candidate.sceneToolsEnabled=false;
    candidate.sceneNames.clear(); candidate.sceneIds.clear(); candidate.elements.clear(); candidate.references.clear(); candidate.layouts.clear();
    candidate.gameId=root["gameId"].toString(); candidate.gameLabel=root["gameLabel"].toString();
    if(modern) {
        if(!root["profileId"].isString() || QUuid(root["profileId"].toString()).isNull()) return invalid();
        candidate.profileId=root["profileId"].toString();
    }
    candidate.activeState = root["activeState"].toInt();
    if (!legacy)
    {
        // Early v3 editor builds used the v2 flag name. Preserve their settings
        // while writing the current spelling on the next successful save.
        QString enabledKey=modern && root.contains("sceneToolsEnabled") ? "sceneToolsEnabled" : "marioEnabled";
        if (!root[enabledKey].isBool() || !root["automatic"].isBool() || !integer(root["fallback"], 0, 3)
            || !integer(root["confirmationMs"], 0, 5000) || !number(root["ambiguityMargin"], 0, 1)) return invalid();
        candidate.sceneToolsEnabled = root[enabledKey].toBool();
        candidate.automatic = root["automatic"].toBool();
        candidate.fallback = StudioLayout(root["fallback"].toInt());
        candidate.confirmationMs = root["confirmationMs"].toInt();
        candidate.ambiguityMargin = root["ambiguityMargin"].toDouble();
    }
    for (int i = 0; i < count; ++i)
    {
        auto s = states[i].toObject();
        if ((!modern && s["name"] != legacyStateName(i)) || (modern && !text(s["name"])) || !s["elements"].isArray()) return invalid();
        candidate.addScene(s["name"].toString());
        if(modern) {
            if(!s["id"].isString() || QUuid(s["id"].toString()).isNull()) return invalid();
            candidate.sceneIds[i]=s["id"].toString();
        }
        if (!legacy)
        {
            if (!integer(s["layout"], 0, 3) || !s["references"].isArray()) return invalid();
            candidate.layouts[i] = StudioLayout(s["layout"].toInt());
        }
        for (auto item : s["elements"].toArray())
        {
            auto o = item.toObject();
            QRect src, dest{176, 120, 72, 54};
            if (!text(o["name"]) || !o["enabled"].isBool() || !integer(o["screen"], 0, 1)
                || !readRect(QJsonArray{o["x"], o["y"], o["width"], o["height"]}, src)
                || (!legacy && !readRect(o["destination"], dest))) return invalid();
            StudioElement e{o["name"].toString(), o["screen"].toInt(), src.x(), src.y(), src.width(), src.height(), o["enabled"].toBool()};
            e.destination = dest;
            if(modern) {
                if(!o["polygon"].isArray() || o["polygon"].toArray().size()>4096) return invalid();
                for(auto v : o["polygon"].toArray()) {
                    auto point=v.toArray();
                    if(point.size()!=2 || !number(point[0],0,256) || !number(point[1],0,192)) return invalid();
                    e.polygon.append(QPointF(point[0].toDouble(),point[1].toDouble()));
                }
                if(!e.polygon.isEmpty() && (e.polygon.size()<3 || !QRectF(src).contains(e.polygon.boundingRect()))) return invalid();
            }
            if(o.contains("id")) {
                if(!o["id"].isString() || QUuid(o["id"].toString()).isNull()) return invalid();
                e.id=o["id"].toString();
            }
            for(const auto& existing : candidate.elements[i]) if(existing.id==e.id) return invalid();
            candidate.elements[i].append(e);
        }
        if (!legacy) for (auto value : s["references"].toArray())
        {
            auto r = value.toObject(); QRect region;
            if (!text(r["name"]) || !integer(r["screen"], 0, 1)
                || !readRect(r["region"], region) || !number(r["threshold"], 0, 1)
                || !r["enabled"].isBool() || !r["png"].isString() || r["png"].toString().size() > 400000) return invalid();
            auto decoded = QByteArray::fromBase64Encoding(r["png"].toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded) return invalid();
            QBuffer png(&decoded.decoded); png.open(QIODevice::ReadOnly);
            QImageReader reader(&png, "PNG");
            if (reader.size() != region.size()) return invalid(); // Check dimensions before allocating image pixels.
            auto image = reader.read(); if (image.isNull()) return invalid();
            candidate.references[i].append({r["name"].toString(), r["screen"].toInt(), region,
                image.convertToFormat(QImage::Format_RGB32), r["threshold"].toDouble(), r["enabled"].toBool()});
        }
    }
    if(QSet<QString>(candidate.sceneIds.begin(),candidate.sceneIds.end()).size()!=count) return invalid();
    candidate.dirty=false;
    *this = candidate; // Transactional load; rejected profiles never replace the current document.
    return true;
}
