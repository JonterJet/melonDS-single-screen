// Synthetic frames only: no Mario Kart DS ROM, game screenshot, or memory addresses.
#include "StudioRecognition.h"
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
        d.marioEnabled = d.automatic = true;
        for (int i = StudioMarioFirst; i < StudioStateCount; ++i)
        {
            QImage f[2]; frames(i,f); QRect roi(16,12,80,32); int screen = i%2;
            d.references[i].append({StudioDocument::stateName(i),screen,roi,f[screen].copy(roi),0.99,true});
        }
        QImage f[2]; frames(StudioRacing,f);
        check(StudioRecognition::similarity(f[1],d.references[StudioRacing][0]) == 1, "Exact match score");
        check(StudioRecognition::similarity(QImage(),d.references[StudioRacing][0]) == 0, "Missing frame score");
        for (int state = StudioMarioFirst; state < StudioStateCount; ++state)
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
        StudioDocument threshold; threshold.confirmationMs = 0; threshold.references[5].append(ref);
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
        auto states = invalid["states"].toArray(); auto state = states[5].toObject(); auto refs = state["references"].toArray();
        auto badRef = refs[0].toObject(); badRef["region"] = QJsonArray{0,0,256,192}; refs[0] = badRef;
        state["references"] = refs; states[5] = state; invalid["states"] = states;
        QFile bad(dir.path()+"/bad.json"); bad.open(QIODevice::WriteOnly); bad.write(QJsonDocument(invalid).toJson()); bad.close();
        check(!loaded.load(bad.fileName(),error) && loaded.toJson() == before,"PNG dimension mismatch must be rejected transactionally");
        StudioDocument wrong; wrong.gameId = "wrong-game"; check(!wrong.load(path,error),"Wrong-game profile rejection");
        QJsonArray legacyStates;
        for (int i = 0; i < 3; ++i) legacyStates.append(QJsonObject{{"name",StudioDocument::stateName(i)},
            {"elements",QJsonArray{QJsonObject{{"name","Legacy HUD"},{"screen",1},{"x",0},{"y",0},{"width",64},{"height",32},{"enabled",true}}}}});
        QJsonObject legacy{{"format","MelonStudio"},{"version",1},{"gameId",d.gameId},{"gameLabel",d.gameLabel},{"activeState",0},{"states",legacyStates}};
        QFile old(dir.path()+"/old.json"); old.open(QIODevice::WriteOnly); old.write(QJsonDocument(legacy).toJson()); old.close();
        check(loaded.load(old.fileName(),error) && !loaded.marioEnabled && !loaded.automatic
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
        std::cout << "Synthetic recognition: five states, thresholds, alternatives, ambiguity, confirmation, unknown/stale/unstable fallback; profile migration and live overlay tests passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
