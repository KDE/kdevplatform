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
#include "mainwindow_p.h"

#include <QApplication>
#include <QBoxLayout>
#include <QLabel>

#include <KMenuBar>
#include <kmenu.h>
#include <kdebug.h>
#include <klocale.h>
#include <kstatusbar.h>
#include <kxmlguiclient.h>
#include <kxmlguifactory.h>

#include <kparts/part.h>
#include <ktexteditor/view.h>
#include <ktexteditor/document.h>
#include <ktexteditor/editor.h>

#include <kstandardaction.h>
#include <kselectaction.h>
#include <ktogglefullscreenaction.h>
#include <kactioncollection.h>
#include <ktoolbarpopupaction.h>
#include <knotifyconfigwidget.h>
#include <kxmlguiclient.h>

#include <sublime/area.h>
#include <sublime/view.h>
#include <sublime/document.h>
#include <sublime/tooldocument.h>

#include <util/pushvalue.h>

#include <interfaces/iplugin.h>

#include "core.h"
#include "partdocument.h"
#include "partcontroller.h"
#include "uicontroller.h"
#include "statusbar.h"
#include "mainwindow.h"
#include "workingsetcontroller.h"
#include "textdocument.h"
#include "sessioncontroller.h"

#include <language/duchain/duchainlock.h>
#include <language/duchain/duchainutils.h>
#include <language/duchain/topducontext.h>
#include <sublime/container.h>
#include <sublime/holdupdates.h>

namespace KDevelop {

bool MainWindowPrivate::s_quitRequested = false;

MainWindowPrivate::MainWindowPrivate(MainWindow *mainWindow)
: m_mainWindow(mainWindow), m_statusBar(0), lastXMLGUIClientView(0), m_changingActiveView(false)
{
}

void MainWindowPrivate::setupGui()
{
    m_statusBar = new KDevelop::StatusBar(m_mainWindow);
    setupStatusBar();
}

void MainWindowPrivate::setupStatusBar()
{
    QWidget *location = m_mainWindow->statusBarLocation();
    if (m_statusBar)
        location->layout()->addWidget(m_statusBar);
}

void MainWindowPrivate::addPlugin( IPlugin *plugin )
{
    kDebug() << "add plugin" << plugin << plugin->componentData().componentName();
    Q_ASSERT( plugin );

    //The direct plugin client can only be added to the first mainwindow
    if(m_mainWindow == Core::self()->uiControllerInternal()->mainWindows()[0])
        m_mainWindow->guiFactory()->addClient( plugin );
    
    Q_ASSERT(!m_pluginCustomClients.contains(plugin));
    
    KXMLGUIClient* ownClient = plugin->createGUIForMainWindow(m_mainWindow);
    if(ownClient) {
        m_pluginCustomClients[plugin] = ownClient;
        connect(plugin, SIGNAL(destroyed(QObject*)), SLOT(pluginDestroyed(QObject*)));
        m_mainWindow->guiFactory()->addClient(ownClient);
    }
}

void MainWindowPrivate::pluginDestroyed(QObject* pluginObj)
{
    IPlugin* plugin = static_cast<IPlugin*>(pluginObj);
    Q_ASSERT(m_pluginCustomClients.contains(plugin));
    m_mainWindow->guiFactory()->removeClient( m_pluginCustomClients[plugin] );
    delete m_pluginCustomClients[plugin];
    m_pluginCustomClients.remove(plugin);
}

MainWindowPrivate::~MainWindowPrivate()
{
    foreach(KXMLGUIClient* client, m_pluginCustomClients.values())
        delete client;
}

void MainWindowPrivate::removePlugin( IPlugin *plugin )
{
    Q_ASSERT( plugin );

    if(m_pluginCustomClients.contains(plugin)) {
        m_mainWindow->guiFactory()->removeClient( m_pluginCustomClients[plugin] );
        delete m_pluginCustomClients[plugin];
        m_pluginCustomClients.remove(plugin);
        disconnect(plugin, SIGNAL(destroyed(QObject*)), this, SLOT(pluginDestroyed(QObject*)));
    }
    
    m_mainWindow->guiFactory()->removeClient( plugin );
}

void MainWindowPrivate::activePartChanged(KParts::Part *part)
{
    if ( Core::self()->uiController()->activeMainWindow() == m_mainWindow)
        m_mainWindow->createGUI(part);
}

void MainWindowPrivate::changeActiveView(Sublime::View *view)
{
    //disable updates on a window to avoid toolbar flickering on xmlgui client change
    Sublime::HoldUpdates s(m_mainWindow);
    mergeView(view);

    if(!view)
        return;

    IDocument *doc = dynamic_cast<KDevelop::IDocument*>(view->document());
    if (doc)
    {
        doc->activate(view, m_mainWindow);
    }
    else
    {
        //activated view is not a part document so we need to remove active part gui
        ///@todo adymo: only this window needs to remove GUI
//         KParts::Part *activePart = Core::self()->partController()->activePart();
//         if (activePart)
//             guiFactory()->removeClient(activePart);
    }
}

void MainWindowPrivate::mergeView(Sublime::View* view)
{
    PushPositiveValue<bool> block(m_changingActiveView, true);

    // If the previous view was KXMLGUIClient, remove its actions
    // In the case that that view was removed, lastActiveView
    // will auto-reset, and xmlguifactory will disconnect that
    // client, I think.
    if (lastXMLGUIClientView)
    {
        kDebug() << "clearing last XML GUI client" << lastXMLGUIClientView;
        
        m_mainWindow->guiFactory()->removeClient(dynamic_cast<KXMLGUIClient*>(lastXMLGUIClientView));
        
        disconnect (lastXMLGUIClientView, SIGNAL(destroyed(QObject*)), this, 0);

        lastXMLGUIClientView = NULL;
    }

    if (!view)
        return;

    if( view->isInitialized() ) {
        QWidget* viewWidget = view->widget();
        Q_ASSERT(viewWidget);

        kDebug() << "changing active view to" << view << "doc" << view->document() << "mw" << m_mainWindow;

        // If the new view is KXMLGUIClient, add it.
        if (KXMLGUIClient* c = dynamic_cast<KXMLGUIClient*>(viewWidget))
        {
            kDebug() << "setting new XMLGUI client" << viewWidget;
            lastXMLGUIClientView = viewWidget;
            m_mainWindow->guiFactory()->addClient(c);
            connect(viewWidget, SIGNAL(destroyed(QObject*)),
                    this, SLOT(xmlguiclientDestroyed(QObject*)));
        }
    } else {
        // Delay initialization
        connect( view, SIGNAL(initialized(Sublime::View*)), this, SLOT(viewInitialized(Sublime::View*)) );
    }
}

void MainWindowPrivate::viewInitialized(Sublime::View* view)
{
    mergeView(view);
}

void MainWindowPrivate::xmlguiclientDestroyed(QObject* obj)
{
    /* We're informed the QWidget for the active view that is also
       KXMLGUIclient is dying.  KXMLGUIFactory will not like deleted
       clients, really.  Unfortunately, there's nothing we can do
       at this point. For example, KateView derives from QWidget and
       KXMLGUIClient.  The destroyed() signal is emitted by ~QWidget.
       At this point, event attempt to cross-cast to KXMLGUIClient
       is undefined behaviour.  We hope to catch view deletion a bit
       later, but if we fail, we better report it now, rather than
       get a weird crash a bit later.  */
       Q_ASSERT(obj == lastXMLGUIClientView);
       Q_ASSERT(false && "xmlgui clients management is messed up");
       Q_UNUSED(obj);
}

void MainWindowPrivate::setupActions()
{
    connect(Core::self()->sessionController(), SIGNAL(quitSession()), SLOT(quitAll()));

    KAction *action;

    QString app = qApp->applicationName();
    QString text = i18nc( "%1 = application name", "Configure %1", app );
    action = KStandardAction::preferences( this, SLOT(settingsDialog()),
                                      actionCollection());
    action->setToolTip( text );
    action->setWhatsThis( QString( "<b>%1</b><p>%2</p>" ).arg( text ).arg(
                              i18n( "Lets you customize %1.", app ) ) );

    action = actionCollection()->addAction( "show_editorconfig", this, SLOT(showEditorConfig()) );
    action->setIcon( KIcon("preferences-other") );
    action->setText( i18n("Configure Editor..."));
    action->setWhatsThis( i18nc("@info:whatsthis", "Configure various aspects of this editor.") );

    action =  KStandardAction::configureNotifications(this, SLOT(configureNotifications()), actionCollection());
    action->setText( i18n("Configure Notifications...") );
    action->setToolTip( i18nc("@info:tooltip", "Configure Notifications") );
    action->setWhatsThis( i18nc( "@info:whatsthis", "<b>Configure Notifications</b><p>Shows a dialog that lets you configure notifications.</p>" ) );

    action = actionCollection()->addAction( "about_platform", this, SLOT(showAboutPlatform()) );
    action->setText( i18n("About KDevelop Platform") );
    action->setStatusTip( i18n("Show Information about KDevelop Platform") );
    action->setWhatsThis( i18nc( "@info:whatsthis", "<b>About KDevelop Platform</b><p>Shows a dialog with information about KDevelop Platform.</p>" ) );

    action = actionCollection()->addAction( "loaded_plugins", this, SLOT(showLoadedPlugins()) );
    action->setText( i18n("Loaded Plugins") );
    action->setStatusTip( i18n("Show a list of all loaded plugins") );
    action->setWhatsThis( i18nc( "@info:whatsthis", "<b>Loaded Plugins</b><p>Shows a dialog with information about all loaded plugins.</p>" ) );
    
//     KToolBarPopupAction *popupAction;
//     popupAction = new KToolBarPopupAction( KIcon( "process-stop" ),
//                                            i18n( "&Stop" ),
//                                            actionCollection()
//                                            );
//     actionCollection()->addAction( "stop_processes",popupAction );

//     popupAction->setShortcut( Qt::Key_Escape );
//     popupAction->setToolTip( i18n( "Stop" ) );
//     popupAction->setWhatsThis( i18n( "<b>Stop</b><p>Stops all running processes.</p>" ) );
//     popupAction->setEnabled( false );
//     connect( popupAction, SIGNAL(triggered()),
//              this, SLOT(stopButtonPressed()) );
//     connect( popupAction->menu(), SIGNAL(aboutToShow()),
//              this, SLOT(stopMenuAboutToShow()) );
//     connect( popupAction->menu(), SIGNAL(triggered(Action*)),
//              this, SLOT(stopPopupActivated(QAction*)) );

     action = actionCollection()->addAction( "view_next_window" );
     action->setText( i18n( "&Next Window" ) );
     connect( action, SIGNAL(triggered(bool)), SLOT(gotoNextWindow()) );
     action->setShortcut( Qt::ALT + Qt::SHIFT + Qt::Key_Right );
     action->setToolTip( i18nc( "@info:tooltip", "Next window" ) );
     action->setWhatsThis( i18nc( "@info:whatsthis", "<b>Next window</b><p>Switches to the next window.</p>" ) );
     action->setIcon(KIcon("go-next"));

     action = actionCollection()->addAction( "view_previous_window" );
     action->setText( i18n( "&Previous Window" ) );
     connect( action, SIGNAL(triggered(bool)), SLOT(gotoPreviousWindow()) );
     action->setShortcut( Qt::ALT + Qt::SHIFT + Qt::Key_Left );
     action->setToolTip( i18nc( "@info:tooltip", "Previous window" ) );
     action->setWhatsThis( i18nc( "@info:whatsthis", "<b>Previous window</b><p>Switches to the previous window.</p>" ) );
     action->setIcon(KIcon("go-previous"));

    /*action = actionCollection()->addAction( "new_window" );
    action->setIcon(KIcon( "window-new" ));
    action->setText( i18n( "&New Window" ) );
    action->setShortcut( Qt::CTRL + Qt::SHIFT + Qt::Key_N );
    connect( action, SIGNAL(triggered(bool)), SLOT(newWindow()) );
    action->setToolTip( i18n( "New Window" ) );
    action->setWhatsThis( i18n( "<b>New Window</b><p>Creates a new window with a duplicate of the current area.</p>" ) );*/

    action = actionCollection()->addAction( "split_horizontal" );
    action->setIcon(KIcon( "view-split-top-bottom" ));
    action->setText( i18n( "Split View &Top/Bottom" ) );
    action->setShortcut( Qt::CTRL + Qt::SHIFT + Qt::Key_T );
    connect( action, SIGNAL(triggered(bool)), SLOT(splitHorizontal()) );
    action->setToolTip( i18nc( "@info:tooltip", "Split Horizontal" ) );
    action->setWhatsThis( i18nc( "@info:whatsthis", "<b>Split Horizontal</b><p>Splits the current view horizontally.</p>" ) );

    action = actionCollection()->addAction( "split_vertical" );
    action->setIcon(KIcon( "view-split-left-right" ));
    action->setText( i18n( "Split View &Left/Right" ) );
    action->setShortcut( Qt::CTRL + Qt::SHIFT + Qt::Key_L );
    connect( action, SIGNAL(triggered(bool)), SLOT(splitVertical()) );
    action->setToolTip( i18nc( "@info:tooltip", "Split Vertical" ) );
    action->setWhatsThis( i18nc( "@info:whatsthis", "<b>Split Vertical</b><p>Splits the current view vertically.</p>" ) );

    action = KStandardAction::fullScreen( this, SLOT(toggleFullScreen(bool)), m_mainWindow, actionCollection() );

    action = actionCollection()->addAction( "file_new" );
    action->setIcon(KIcon("document-new"));
    action->setShortcut( Qt::CTRL + Qt::Key_N );
    action->setText( i18n( "&New" ) );
    action->setIconText( i18nc( "Shorter Text for 'New File' shown in the toolbar", "New") );
    connect( action, SIGNAL(triggered(bool)), SLOT(fileNew()) );
    action->setToolTip( i18nc( "@info:tooltip", "New File" ) );
    action->setWhatsThis( i18nc( "@info:whatsthis", "<b>New File</b><p>Creates an empty file.</p>" ) );

    action = actionCollection()->addAction( "add_toolview" );
    action->setIcon(KIcon("window-new"));
    action->setShortcut( Qt::CTRL + Qt::SHIFT + Qt::Key_V );
    action->setText( i18n( "&Add Tool View..." ) );
    connect( action, SIGNAL(triggered(bool)),  SLOT(viewAddNewToolView()) );
    action->setToolTip( i18nc( "@info:tooltip", "Add Tool View" ) );
    action->setWhatsThis( i18nc( "@info:whatsthis", "<b>Add Tool View</b><p>Adds a new tool view to this window.</p>" ) );
}

void MainWindowPrivate::toggleArea(bool b)
{
    if (!b) return;
    KAction *action = qobject_cast<KAction*>(sender());
    if (!action) return;
    m_mainWindow->controller()->showArea(action->data().toString(), m_mainWindow);
}

KActionCollection * MainWindowPrivate::actionCollection()
{
    return m_mainWindow->actionCollection();
}

/* Make sure that the main toolbar has only items from our actionCollection
   and not from anywhere else.  Presently, when new XMLGUI client is added,
   it's actions are always merged, and there's no clear way to stop this.
   So, we execute the below code whenever a new client is added.  */
/*
 * Only re-enable if you manage to also remove the relevant actions from the 
 * configure toolbar dialog. If not, we stay with the actions, as anything else
 * confuses users.
 * void MainWindowPrivate::fixToolbar()
{
    QWidget* w = m_mainWindow->guiFactory()->container(
        "mainToolBar", m_mainWindow);
    if (w)
        foreach (QAction *a, w->actions())
        {
            if ( !a->isSeparator() && (a != m_mainWindow->actionCollection()->action(a->objectName())) )
                w->removeAction(a);
        }
}*/

bool MainWindowPrivate::applicationQuitRequested() const
{
    return s_quitRequested;
}

void MainWindowPrivate::registerStatus(QObject* status)
{
    m_statusBar->registerStatus(status);
}


void MainWindowPrivate::showErrorMessage(QString message, int timeout)
{
    m_statusBar->showErrorMessage(message, timeout);
}

void MainWindowPrivate::tabContextMenuRequested(Sublime::View* view, KMenu* menu)
{
    m_tabView = view;

    QAction* action;

    action = menu->addAction(KIcon("view-split-top-bottom"), i18n("Split View Top/Bottom"));
    connect(action, SIGNAL(triggered(bool)), this, SLOT(contextMenuSplitHorizontal()));

    action = menu->addAction(KIcon("view-split-left-right"), i18n("Split View Left/Right"));
    connect(action, SIGNAL(triggered(bool)), this, SLOT(contextMenuSplitVertical()));
    menu->addSeparator();

    action = menu->addAction(KIcon("document-new"), i18n("New File"));

    connect(action, SIGNAL(triggered(bool)), this, SLOT(contextMenuFileNew()));

    if ( TextDocument* doc = dynamic_cast<TextDocument*>(view->document()) ) {
        action = menu->addAction(KIcon("view-refresh"), i18n("Reload"));
        connect(action, SIGNAL(triggered(bool)), doc, SLOT(reload()));

        action = menu->addAction(KIcon("view-refresh"), i18n("Reload All"));
        connect(action, SIGNAL(triggered(bool)), this, SLOT(reloadAll()));
    }
}

void MainWindowPrivate::tabToolTipRequested(Sublime::View* view, Sublime::Container* container, int tab)
{
    if (m_tabTooltip.second) {
        if (m_tabTooltip.first == view) {
            // tooltip already shown, don't do anything. prevents flicker when moving mouse over same tab
            return;
        } else {
            m_tabTooltip.second.data()->close();
        }
    }

    DUChainReadLocker lock;

    Sublime::UrlDocument* urlDoc = dynamic_cast<Sublime::UrlDocument*>(view->document());

    if (urlDoc) {
        TopDUContext* top = DUChainUtils::standardContextForUrl(urlDoc->url());

        if (top) {
            if ( QWidget* navigationWidget = top->createNavigationWidget() ) {
                NavigationToolTip* tooltip = new KDevelop::NavigationToolTip(m_mainWindow, QCursor::pos() + QPoint(20, 20), navigationWidget);
                tooltip->resize(navigationWidget->sizeHint() + QSize(10, 10));
                tooltip->addExtendRect(container->tabRect(tab));

                m_tabTooltip.first = view;
                m_tabTooltip.second = tooltip;
                ActiveToolTip::showToolTip(m_tabTooltip.second.data());
            }
        }
    }
}

void MainWindowPrivate::dockBarContextMenuRequested(Qt::DockWidgetArea area, const QPoint& position)
{
    KMenu menu;
    menu.addTitle(KIcon("window-new"), i18n("Add Tool View"));
    QMap<IToolViewFactory*, Sublime::ToolDocument*> factories =
        Core::self()->uiControllerInternal()->factoryDocuments();
    QMap<QAction*, IToolViewFactory*> actionToFactory;
    if ( !factories.isEmpty() ) {
        // sorted actions
        QMap<QString, QAction*> actionMap;
        for (QMap<IToolViewFactory*, Sublime::ToolDocument*>::const_iterator it = factories.constBegin();
                it != factories.constEnd(); ++it)
        {
            QAction* action = new QAction(it.value()->statusIcon(), it.value()->title(), &menu);
            action->setIcon(it.value()->statusIcon());
            if (!it.key()->allowMultiple() && Core::self()->uiControllerInternal()->toolViewPresent(it.value(), m_mainWindow->area())) {
                action->setDisabled(true);
            }
            actionToFactory.insert(action, it.key());
            actionMap[action->text()] = action;
        }
        menu.addActions(actionMap.values());
    }

    QAction* triggered = menu.exec(position);
    if ( !triggered ) {
        return;
    }
    Core::self()->uiControllerInternal()->addToolViewToDockArea(
        actionToFactory[triggered],
        area
    );
}

bool MainWindowPrivate::changingActiveView() const
{
    return m_changingActiveView;
}

}

#include "mainwindow_actions.cpp"
#include "mainwindow_p.moc"

