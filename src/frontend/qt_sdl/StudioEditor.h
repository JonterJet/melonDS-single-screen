// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STUDIO_EDITOR_H
#define STUDIO_EDITOR_H
#include <QObject>
#include <QImage>
#include <QMutex>
#include <atomic>
#include "StudioDocument.h"

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
