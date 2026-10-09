// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioDocument.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>

QString StudioDocument::stateName(int state)
{
    return QStringList{"Gameplay", "Menus", "Cutscenes"}.value(state);
}

QJsonObject StudioDocument::toJson() const
{
    QJsonArray states;
    for (int i = 0; i < 3; ++i)
    {
        QJsonArray items;
        for (const auto& e : elements[i])
            items.append(QJsonObject{{"name", e.name}, {"screen", e.screen},
                {"x", e.x}, {"y", e.y}, {"width", e.width}, {"height", e.height}, {"enabled", e.enabled}});
        states.append(QJsonObject{{"name", stateName(i)}, {"elements", items}});
    }
    return {{"format", "MelonStudio"}, {"version", 1}, {"gameId", gameId},
            {"gameLabel", gameLabel}, {"activeState", activeState}, {"states", states}};
}

bool StudioDocument::save(const QString& path, QString& error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        error = "Cannot create the configuration folder.";
        return false;
    }
    QSaveFile file(path);
    const QByteArray data = QJsonDocument(toJson()).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        error = file.errorString();
        return false;
    }
    dirty = false;
    return true;
}

bool StudioDocument::load(const QString& path, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > 1024 * 1024) { error = "Configuration exceeds the 1 MiB limit."; return false; }
    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson(file.readAll(), &parseError);
    const auto root = json.object();
    auto invalid = [&]() { error = "Invalid or incompatible MelonStudio configuration."; return false; };
    auto integer = [](const QJsonValue& v, int min, int max) {
        return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() == std::floor(v.toDouble())
            && v.toDouble() >= min && v.toDouble() <= max;
    };
    if (parseError.error != QJsonParseError::NoError || !json.isObject()
        || root["format"] != "MelonStudio" || root["version"] != 1
        || root["gameId"] != gameId || !root["gameLabel"].isString()
        || !integer(root["activeState"], 0, 2) || !root["states"].isArray()) return invalid();
    const auto states = root["states"].toArray();
    if (states.size() != 3) return invalid();
    StudioDocument candidate;
    candidate.gameId = gameId;
    candidate.gameLabel = gameLabel;
    candidate.activeState = root["activeState"].toInt();
    for (int i = 0; i < 3; ++i)
    {
        const auto state = states[i].toObject();
        if (state["name"] != stateName(i) || !state["elements"].isArray()) return invalid();
        const auto items = state["elements"].toArray();
        if (items.size() > 512) return invalid();
        for (const auto& item : items)
        {
            const auto o = item.toObject();
            if (!item.isObject() || !o["name"].isString() || o["name"].toString().trimmed().isEmpty()
                || o["name"].toString().size() > 128 || !o["enabled"].isBool()
                || !integer(o["screen"], 0, 1) || !integer(o["x"], 0, 255)
                || !integer(o["y"], 0, 191) || !integer(o["width"], 1, 256)
                || !integer(o["height"], 1, 192)
                || o["x"].toInt() + o["width"].toInt() > 256
                || o["y"].toInt() + o["height"].toInt() > 192) return invalid();
            candidate.elements[i].append({o["name"].toString(), o["screen"].toInt(),
                o["x"].toInt(), o["y"].toInt(), o["width"].toInt(), o["height"].toInt(), o["enabled"].toBool()});
        }
    }
    *this = candidate; // Commit only after the entire document validates.
    return true;
}
