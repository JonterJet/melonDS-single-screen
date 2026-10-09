// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STUDIO_EDITOR_H
#define STUDIO_EDITOR_H
#include <QObject>
#include <QImage>
#include <QMutex>
#include <atomic>
#include "StudioDocument.h"
#include "StudioRecognition.h"
#include <QElapsedTimer>

class MainWindow;
class QDockWidget;
class QLabel;
class QTreeWidget;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QWidget;
class QAction;
class QToolBar;
class QListWidget;
class QDoubleSpinBox;
class QMenu;
class StudioHudCanvas;

class StudioEditor : public QObject
{
    Q_OBJECT
public:
    explicit StudioEditor(MainWindow* window);
    bool saveOnClose();
    void clearScreens();
    void setFullscreen(bool full);
    void setGame(const QString& id, const QString& label);
    // Called only by the rendering thread with its OpenGL context current.
    void captureScreens(void* top, void* bottom, bool software);
    bool ownsFocus() const;
    bool isPlayMode() const { return playMode; }
    QString configurationPath() const;

private:
    MainWindow* window;
    StudioDocument document;
    QVector<QDockWidget*> docks;
    QVector<bool> dockVisibility;
    QDockWidget* originals;
    QWidget* preview;
    QLabel* profile;
    QTreeWidget* outliner;
    QLineEdit* name;
    QComboBox* source;
    QSpinBox* bounds[4];
    QCheckBox* enabled;
    QSpinBox* destination[4];
    QWidget* inspector;
    QComboBox* states;
    QAction* playAction;
    QToolBar* toolbar;
    QVector<bool> fullscreenVisibility;
    bool fullscreenToolbarVisible = true;
    bool refreshing = false;
    bool playMode = false;
    std::atomic<bool> captureRequested{false};
    QMutex imageMutex;
    QImage images[2];
    quint64 imageSerial = 0, seenSerial = 0, teachAfterSerial = 0;
    QElapsedTimer recognitionClock;
    qint64 lastSampleMs = -1;
    StudioRecognition recognizer;
    StudioMatch lastMatch;
    int runtimeState = -2;
    bool teachingPending = false, selectingOverlay = false;
    QCheckBox *marioMode, *automatic, *refEnabled;
    QComboBox *sceneLayout, *fallbackLayout, *refState;
    QSpinBox* confirmation;
    QDoubleSpinBox *margin, *refThreshold;
    QListWidget* refList;
    QLineEdit* refName;
    QLabel *debug, *selectionHint;
    StudioHudCanvas* hudCanvas;
    QWidget* rules;

    void initializeMarioControls(QWidget* sceneWidget);
    void refreshMarioControls();
    void refreshReferences();
    void editReference();
    void settingsChanged();
    void resetRecognition();
    void tickScreens();
    void applyPresentation();
    void beginSelection(bool overlay);
    void selectedRegion(int screen, const QRect& region);
    void updateDebug();

    QDockWidget* dock(const QString& title, QWidget* content, int area);
    void refresh(int selected = -1);
    void selectElement();
    void editElement();
    void setPlayMode(bool play);
    bool save();
    void load();
    void showError(const QString& error);
};
#endif
