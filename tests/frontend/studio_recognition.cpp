// Synthetic frames only: no Mario Kart DS ROM, game screenshot, or memory addresses.
#include "StudioRecognition.h"
#include "StudioProfiles.h"
#include "StudioRendering.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static QImage synthetic(int scene)
{
    QImage image(256,192,QImage::Format_RGB32);
    for (int y = 0; y < 192; ++y) for (int x = 0; x < 256; ++x)
        image.setPixel(x,y,qRgb((scene*41+x*3)%256,(scene*67+y*5)%256,(scene*97+x+y)%256));
    return image;
}
static void frames(int state, QImage (&out)[2]) { out[0] = synthetic(state); out[1] = synthetic(state+10); }
int main(int argc, char** argv)
{
    QCoreApplication app(argc,argv);
    try
    {
        StudioDocument d; d.gameId = "synthetic-mkds"; d.gameLabel = "Synthetic tests";
        while(d.sceneCount()<8) d.addScene(StudioDocument::legacyStateName(d.sceneCount()));
        d.sceneToolsEnabled = d.automatic = true;
        for (int i = StudioLegacyMarioFirst; i < StudioLegacyStateCount; ++i)
        {
            QImage f[2]; frames(i,f); QRect roi(16,12,80,32); int screen = i%2;
            d.references[i].append({StudioDocument::legacyStateName(i),screen,roi,f[screen].copy(roi),0.99,true});
        }
        QImage f[2]; frames(StudioLegacyRacing,f);
        check(StudioRecognition::similarity(f[1],d.references[StudioLegacyRacing][0]) == 1, "Exact match score");
        check(StudioRecognition::similarity(QImage(),d.references[StudioLegacyRacing][0]) == 0, "Missing frame score");
        for (int state = StudioLegacyMarioFirst; state < StudioLegacyStateCount; ++state)
        {
            StudioRecognition r; frames(state,f);
            check(r.update(f,d,0).state == -1, "Must start on fallback");
            for (int t = 100; t <= 300; t += 100) check(r.update(f,d,t).state == -1, "Must honor confirmation period");
            auto result = r.update(f,d,400);
            check(result.state == state && result.confidence == 1, "Five synthetic states must confirm independently");
        }
        StudioRecognition r; frames(3,f); r.update(f,d,0); r.update(f,d,400);
        frames(5,f); check(r.update(f,d,500).state == 3,"One frame must not switch");
        frames(3,f); check(r.update(f,d,600).state == 3,"Flicker must retain confirmed state");
        f[0] = synthetic(99); f[1] = synthetic(100);
        check(r.update(f,d,700).state == 3,"Unknown must have a short confirmation grace period");
        check(r.update(f,d,1100).state == -1,"Sustained unknown must fall back");
        r.reset(); frames(3,f); r.update(f,d,0); r.update(f,d,400);
        for (int t = 500; t <= 900; t += 100) { frames(t%200 ? 4 : 5,f); r.update(f,d,t); }
        frames(4,f); check(r.update(f,d,1000).state == -1,"Unstable competing states must not retain stale known scene forever");
        r.reset(); frames(3,f); r.update(f,d,0); r.update(f,d,400);
        check(r.update(f,d,1001).state == -1,"Stale sample gaps must reset confirmation");
        StudioDocument ambiguous = d; ambiguous.references[4] = ambiguous.references[3];
        r.reset(); frames(3,f); auto match = r.update(f,ambiguous,0);
        check(match.ambiguous && match.candidate == -1, "Equal competing states must be rejected");
        StudioDocument alternate = d; frames(42,f);
        auto ref = d.references[5][0]; ref.image = f[ref.screen].copy(ref.region);
        alternate.references[5].append(ref); alternate.confirmationMs = 0;
        r.reset(); check(r.update(f,alternate,0).state == 5,"Multiple references are alternatives");
        alternate.references[5].last().enabled = false;
        r.reset(); check(r.update(f,alternate,0).state == -1,"Disabled references must not match");
        StudioDocument threshold; while(threshold.sceneCount()<8) threshold.addScene("Custom scene"); threshold.confirmationMs = 0; threshold.references[5].append(ref);
        auto changed = f[ref.screen].copy();
        for (int y = ref.region.y(); y < ref.region.y()+ref.region.height(); ++y)
            for (int x = ref.region.x(); x < ref.region.x()+ref.region.width(); ++x)
            { auto c = changed.pixelColor(x,y); c.setRed(qMin(255,c.red()+24)); changed.setPixelColor(x,y,c); }
        f[ref.screen] = changed; threshold.references[5][0].threshold = 0.999;
        r.reset(); check(r.update(f,threshold,0).state == -1,"Strict threshold must reject altered pixels");
        threshold.references[5][0].threshold = 0.9;
        r.reset(); check(r.update(f,threshold,0).state == 5,"Configurable threshold must allow altered pixels");
        f[0] = QImage(); f[1] = QImage(); threshold.references[5][0].threshold = 0;
        r.reset(); check(r.update(f,threshold,0).state == -1,"Missing frames must never match even at threshold zero");

        QTemporaryDir dir; QString error, path = dir.path()+"/game.json";
        d.activeState = 5; d.elements[5].append(StudioElement{});
        check(d.save(path,error),"Version 2 save"); StudioDocument loaded; loaded.gameId = d.gameId; loaded.gameLabel = d.gameLabel;
        check(loaded.load(path,error) && loaded.toJson() == d.toJson(),"Detection settings, PNG references, overlays and layouts must round-trip");
        auto before = loaded.toJson(); auto invalid = before;
        auto states = invalid["scenes"].toArray(); auto state = states[5].toObject(); auto refs = state["references"].toArray();
        auto badRef = refs[0].toObject(); badRef["region"] = QJsonArray{0,0,256,192}; refs[0] = badRef;
        state["references"] = refs; states[5] = state; invalid["scenes"] = states;
        QFile bad(dir.path()+"/bad.json"); bad.open(QIODevice::WriteOnly); bad.write(QJsonDocument(invalid).toJson()); bad.close();
        check(!loaded.load(bad.fileName(),error) && loaded.toJson() == before,"PNG dimension mismatch must be rejected transactionally");
        StudioDocument wrong; wrong.gameId = "wrong-game"; check(!wrong.load(path,error),"Wrong-game profile rejection");
        QJsonArray legacyStates;
        for (int i = 0; i < 3; ++i) legacyStates.append(QJsonObject{{"name",StudioDocument::legacyStateName(i)},
            {"elements",QJsonArray{QJsonObject{{"name","Legacy HUD"},{"screen",1},{"x",0},{"y",0},{"width",64},{"height",32},{"enabled",true}}}}});
        QJsonObject legacy{{"format","MelonStudio"},{"version",1},{"gameId",d.gameId},{"gameLabel",d.gameLabel},{"activeState",0},{"states",legacyStates}};
        QFile old(dir.path()+"/old.json"); old.open(QIODevice::WriteOnly); old.write(QJsonDocument(legacy).toJson()); old.close();
        check(loaded.load(old.fileName(),error) && !loaded.sceneToolsEnabled && !loaded.automatic
            && loaded.elements[0][0].name == "Legacy HUD", "Legacy profiles must migrate without changing behavior");
        check(loaded.save(old.fileName(),error) && QFile::exists(old.fileName()+".v1.bak"),"Migration must back up the original profile");

        QImage live[2]; live[0] = QImage(256,192,QImage::Format_RGB32); live[0].fill(Qt::red);
        live[1] = QImage(256,192,QImage::Format_RGB32); live[1].fill(Qt::blue);
        StudioElement overlay; overlay.x = overlay.y = 0; overlay.width = 256; overlay.height = 192;
        overlay.destination = QRect(160,120,80,60); QVector<StudioElement> overlays{overlay};
        auto render = [&] { QImage out = live[0].copy(); QPainter p(&out); paintStudioOverlays(p,live,overlays); p.end(); return out; };
        auto out = render(); check(out.pixelColor(180,140) == QColor(Qt::blue) && out.pixelColor(20,20) == QColor(Qt::red),"Live overlay placement");
        live[1].fill(Qt::green); check(render().pixelColor(180,140) == QColor(Qt::green),"Overlay must follow live source changes, not a stored screenshot");
        overlays[0].enabled = false; check(render().pixelColor(180,140) == QColor(Qt::red),"Disabled per-state overlay must disappear");
        auto vertices = studioOverlayVertices(overlay);
        check(vertices[0] == 160 && vertices[1] == 120 && vertices[4] == 1
            && vertices[12] == 1 && vertices[13] == 1, "OpenGL overlay geometry and live texture layer");
        // True polygon masks and native compositing must leave the exterior transparent.
        overlay.polygon=QPolygonF{QPointF(0,0),QPointF(256,0),QPointF(128,192)};
        auto mask=studioPolygonMask(overlay);
        check(mask.constScanLine(180)[2]==0 && mask.constScanLine(50)[128]==255,"Polygon alpha exterior/interior");
        overlays[0]=overlay; overlays[0].enabled=true;
        check(render().pixelColor(162,176)==QColor(Qt::red) && render().pixelColor(200,140)==QColor(Qt::green),"Native polygon transparency after scaling");
        d.elements[5][0]=overlay; check(d.save(path,error),"Polygon save");
        check(loaded.load(path,error) && loaded.elements[5][0].polygon==overlay.polygon,"Polygon vertices round trip");
        // Dynamic scenes have no fixed five-state restriction.
        int custom=d.addScene("User scene: credits"); d.references[custom]=d.references[5]; d.references[5].clear();
        d.confirmationMs=0; r.reset(); frames(5,f);
        check(r.update(f,d,0).state==custom,"Recognition must find a custom scene");
        d.sceneNames[custom]="Renamed credits"; QString sceneId=d.sceneIds[custom];
        d.activeState=custom; d.moveScene(custom,0);
        check(d.sceneName(0)=="Renamed credits" && d.sceneIds[0]==sceneId && d.activeState==0,"Scene reorder must preserve identity");
        d.duplicateScene(0); check(d.references[1].size()==d.references[0].size() && d.sceneIds[1]!=d.sceneIds[0],"Scene duplication");
        d.removeScene(0); check(d.activeState==0 && d.sceneName(0).contains("copy"),"Scene deletion");
        for(int i=0;i<1000;++i) d.addScene(QString("Custom %1").arg(i));
        check(d.sceneCount()>1000,"No fixed scene cap");
        StudioProfiles store(dir.path()+"/profiles");
        d.gameLabel="My game profile"; check(store.save(d,error) && store.prefer(d,error),"Profile creation and association");
        auto originalId=d.profileId; StudioDocument duplicate;
        check(store.duplicate(originalId,"Another profile",duplicate,error) && duplicate.profileId!=originalId,"Profile duplication");
        duplicate.gameLabel="Renamed profile"; check(store.save(duplicate,error) && store.prefer(duplicate,error),"Profile rename/default");
        StudioDocument automatic; check(store.forRom(d.gameId,"ROM label",QString{},automatic,error) && automatic.profileId==duplicate.profileId,"Associated profile auto load");
        check(store.setOrder({duplicate.profileId,originalId},error) && store.list()[0].id==duplicate.profileId,"Profile order persistence");
        check(duplicate.sceneIds!=d.sceneIds,"Copied profile scene identities must be independent");
        check(store.remove(duplicate.profileId,error),"Profile archive/delete");
        check(store.forRom(d.gameId,"ROM label",QString{},automatic,error) && automatic.profileId==originalId,"Deleted profile fallback");
        check(automatic.sceneCount()==d.sceneCount() && automatic.elements[5].size()==d.elements[5].size(),"Dynamic profile persistence");
        StudioDocument optional; optional.gameId="optional-uuid"; optional.gameLabel="Existing v3";
        StudioElement original; original.polygon=overlay.polygon; original.x=original.y=0; original.width=256; original.height=192;
        optional.elements[0].append(original);
        auto oldV3=optional.toJson(); oldV3["marioEnabled"]=oldV3.take("sceneToolsEnabled"); auto oldScenes=oldV3["scenes"].toArray(); auto oldScene=oldScenes[0].toObject();
        auto oldWidgets=oldScene["elements"].toArray(); auto oldWidget=oldWidgets[0].toObject(); oldWidget.remove("id"); oldWidgets[0]=oldWidget;
        oldScene["elements"]=oldWidgets; oldScenes[0]=oldScene; oldV3["scenes"]=oldScenes;
        QString optionalPath=dir.path()+"/optional.json"; QFile optionalFile(optionalPath); check(optionalFile.open(QIODevice::WriteOnly),"Write old v3 fixture");
        optionalFile.write(QJsonDocument(oldV3).toJson()); optionalFile.close();
        StudioDocument updated;
        check(updated.load(optionalPath,error) && updated.sceneToolsEnabled && updated.elements[0][0].polygon==original.polygon && updated.elements[0][0].destination==original.destination,"Existing v3 widgets must preserve mask and transform when adding UUID");
        auto generatedId=updated.elements[0][0].id;
        check(!QUuid(generatedId).isNull() && updated.save(optionalPath,error) && updated.load(optionalPath,error) && updated.elements[0][0].id==generatedId,"Generated widget identity must persist");
        auto duplicateIds=updated.toJson(); auto badScenes=duplicateIds["scenes"].toArray(); auto badScene=badScenes[0].toObject(); auto badWidgets=badScene["elements"].toArray(); badWidgets.append(badWidgets[0]);
        badScene["elements"]=badWidgets; badScenes[0]=badScene; duplicateIds["scenes"]=badScenes;
        QFile duplicateFile(dir.path()+"/duplicate-ids.json"); duplicateFile.open(QIODevice::WriteOnly); duplicateFile.write(QJsonDocument(duplicateIds).toJson()); duplicateFile.close();
        auto preserved=updated.toJson(); check(!updated.load(duplicateFile.fileName(),error) && updated.toJson()==preserved,"Duplicate widget IDs must be rejected transactionally");
        // A version 2 file retains all eight scenes and reference images during import.
        StudioDocument v2; while(v2.sceneCount()<8) v2.addScene(StudioDocument::legacyStateName(v2.sceneCount()));
        v2.gameId="legacy-v2"; v2.gameLabel="Legacy Mario"; v2.references[5].append(ref); v2.layouts[5]=StudioLayout::Top;
        auto v2json=v2.toJson(); v2json["version"]=2; v2json["marioEnabled"]=v2json.take("sceneToolsEnabled"); v2json["states"]=v2json.take("scenes");
        QString v2path=dir.path()+"/v2.json"; QFile v2file(v2path); v2file.open(QIODevice::WriteOnly); v2file.write(QJsonDocument(v2json).toJson()); v2file.close();
        StudioDocument imported; check(store.forRom("legacy-v2","Legacy Mario",v2path,imported,error),"Version 2 import");
        check(imported.sceneCount()==8 && imported.references[5][0].image==ref.image && imported.layouts[5]==StudioLayout::Top && QFile::exists(v2path+".pre-v3.bak"),"Version 2 references/layout/backups preserved");
        std::cout << "Synthetic recognition: five states, thresholds, alternatives, ambiguity, confirmation, unknown/stale/unstable fallback; profile migration and live overlay tests passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
