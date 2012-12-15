/* This file is part of the KDevelop project
Copyright 2002 Falk Brettschneider <falkbr@kdevelop.org>
Copyright 2003 John Firebaugh <jfirebaugh@kde.org>
Copyright 2006 Adam Treat <treat@kde.org>
Copyright 2006, 2007 Alexander Dymo <adymo@kdevelop.org>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public License
along with this library; see the file COPYING.LIB.  If not, write to
the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.
*/
#ifndef KDEVMAINWINDOW_PRIVATE_H
#define KDEVMAINWINDOW_PRIVATE_H

#include <QObject>
#include <QPointer>
#include <qwidget.h>
#include <kxmlguiclient.h>
#include <language/util/navigationtooltip.h>

class KActionCollection;
class KMenu;

namespace Sublime {
class View;
class Container;
}

namespace KParts {
class Part;
}

namespace KTextEditor {
class View;
}

namespace KDevelop {

class IPlugin;
class MainWindow;
class StatusBar;

class MainWindowPrivate: public QObject {
    Q_OBJECT
public:
    MainWindowPrivate(MainWindow *mainWindow);
    ~MainWindowPrivate();

    QPointer<QWidget> centralPlugin;

    void setupActions();
    void setupGui();
    void setupStatusBar();
    void registerStatus(QObject*);

    void tabContextMenuRequested(Sublime::View *view, KMenu* menu);
    void tabToolTipRequested(Sublime::View* view, Sublime::Container* container, int tab);
    void dockBarContextMenuRequested(Qt::DockWidgetArea area, const QPoint& position);

public Q_SLOTS:
    void addPlugin( KDevelop::IPlugin *plugin );
    void removePlugin( KDevelop::IPlugin *plugin );

    void activePartChanged(KParts::Part *part);
    void mergeView(Sublime::View *view);
    void changeActiveView(Sublime::View *view);
    void xmlguiclientDestroyed(QObject* obj);

    //actions
    void fileNew();

    void gotoNextWindow();
    void gotoPreviousWindow();

    void viewAddNewToolView();

    void newWindow();
    void splitHorizontal();
    void splitVertical();
    void split(Qt::Orientation orientation);
    void toggleFullScreen(bool fullScreen);

    void configureToolbars();
    void newToolbarConfig();

    void settingsDialog();
    void stopPopupActivated( int );
    void stopMenuAboutToShow();
    void stopButtonPressed();

    void quitAll();

//    void fixToolbar();

    ///Returns true if we're currently changing the active view through changeActiveView()
    bool changingActiveView() const ;

    bool applicationQuitRequested() const;

    void configureNotifications();
    void showAboutPlatform();
    void showLoadedPlugins();
    void showEditorConfig();

    void toggleArea(bool b);
    void showErrorMessage(QString message, int timeout);
    void pluginDestroyed(QObject*);

    /// the following slots always activate the m_tabView before calling the normal slot above
    /// @see m_tabView
    /// @see tabContextMenuRequested
    void contextMenuFileNew();
    void contextMenuSplitHorizontal();
    void contextMenuSplitVertical();

    /// reload all open documents
    void reloadAll();
    void viewInitialized(Sublime::View*);

private:
    KActionCollection *actionCollection();

    MainWindow *m_mainWindow;
    StatusBar* m_statusBar;
    QWidget* lastXMLGUIClientView;
    QPointer<QWidget> m_workingSetCornerWidget;
    
    QMap<IPlugin*, KXMLGUIClient*> m_pluginCustomClients;
    
    static bool s_quitRequested;
    bool m_changingActiveView;
    /// the view of the tab that got it's context menu connected
    Sublime::View* m_tabView;
    QPair<Sublime::View*, QWeakPointer<NavigationToolTip> > m_tabTooltip;
};

}

#endif

