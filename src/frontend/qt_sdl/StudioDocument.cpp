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

StudioDocument::StudioDocument()
{
    for (int i = 0; i < StudioStateCount; ++i)
        layouts[i] = i < StudioMarioFirst ? StudioLayout::Existing : StudioLayout::Both;
    layouts[StudioRacing] = StudioLayout::Top;
}
QString StudioDocument::stateName(int state)
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
    for (int i = 0; i < StudioStateCount; ++i)
    {
        QJsonArray items, refs;
        for (const auto& e : elements[i])
            items.append(QJsonObject{{"name", e.name}, {"screen", e.screen}, {"x", e.x}, {"y", e.y},
                {"width", e.width}, {"height", e.height}, {"enabled", e.enabled}, {"destination", rectJson(e.destination)}});
        for (const auto& r : references[i])
        {
            QByteArray png;
            QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
            r.image.save(&buffer, "PNG");
            refs.append(QJsonObject{{"name", r.name}, {"screen", r.screen}, {"region", rectJson(r.region)},
                {"png", QString::fromLatin1(png.toBase64())}, {"threshold", r.threshold}, {"enabled", r.enabled}});
        }
        states.append(QJsonObject{{"name", stateName(i)}, {"layout", int(layouts[i])}, {"elements", items}, {"references", refs}});
    }
    return {{"format", "MelonStudio"}, {"version", 2}, {"gameId", gameId}, {"gameLabel", gameLabel},
        {"activeState", activeState}, {"states", states}, {"marioEnabled", marioEnabled}, {"automatic", automatic},
        {"fallback", int(fallback)}, {"confirmationMs", confirmationMs}, {"ambiguityMargin", ambiguityMargin}};
}
bool StudioDocument::save(const QString& path, QString& error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) { error = "Cannot create the configuration folder."; return false; }
    if (legacyProfile && QFile::exists(path) && !QFile::exists(path + ".v1.bak") && !QFile::copy(path, path + ".v1.bak"))
    { error = "Cannot back up the original version 1 profile."; return false; }
    QSaveFile file(path);
    const auto json = toJson();
    for (const auto& state : json["states"].toArray())
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
        || !integer(root["version"], 1, 2) || root["gameId"] != gameId || !root["gameLabel"].isString()
        || !root["states"].isArray()) return invalid();
    bool legacy = root["version"].toInt() == 1;
    int count = legacy ? 3 : StudioStateCount;
    if (!integer(root["activeState"], 0, count - 1) || root["states"].toArray().size() != count) return invalid();
    StudioDocument candidate; candidate.legacyProfile = legacy;
    candidate.gameId = gameId; candidate.gameLabel = gameLabel;
    candidate.activeState = root["activeState"].toInt();
    if (!legacy)
    {
        if (!root["marioEnabled"].isBool() || !root["automatic"].isBool() || !integer(root["fallback"], 0, 3)
            || !integer(root["confirmationMs"], 0, 5000) || !number(root["ambiguityMargin"], 0, 1)) return invalid();
        candidate.marioEnabled = root["marioEnabled"].toBool();
        candidate.automatic = root["automatic"].toBool();
        candidate.fallback = StudioLayout(root["fallback"].toInt());
        candidate.confirmationMs = root["confirmationMs"].toInt();
        candidate.ambiguityMargin = root["ambiguityMargin"].toDouble();
    }
    auto states = root["states"].toArray();
    for (int i = 0; i < count; ++i)
    {
        auto s = states[i].toObject();
        if (s["name"] != stateName(i) || !s["elements"].isArray() || s["elements"].toArray().size() > 512) return invalid();
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
            e.destination = dest; candidate.elements[i].append(e);
        }
        if (!legacy) for (auto value : s["references"].toArray())
        {
            auto r = value.toObject(); QRect region;
            if (candidate.referenceCount() >= 64 || !text(r["name"]) || !integer(r["screen"], 0, 1)
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
    *this = candidate; // Transactional load; rejected profiles never replace the current document.
    return true;
}
