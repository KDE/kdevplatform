/* This file is part of KDevelop
Copyright 2005 Adam Treat <treat@kde.org>

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

#include "kdevdocumentview.h"
#include "kdevdocumentviewplugin.h"
#include "kdevdocumentmodel.h"

#include <QHeaderView>
#include <QContextMenuEvent>
#include <QSortFilterProxyModel>

#include <kaction.h>
#include <kurl.h>
#include <kmenu.h>
//#include <klocale.h>
#include <kiconloader.h>
#include <kstandardaction.h>

#include "kdevdocumentselection.h"
#include "kdevdocumentviewdelegate.h"

#include <interfaces/contextmenuextension.h>
#include <interfaces/icore.h>
#include <interfaces/idocumentcontroller.h>
#include <interfaces/iplugincontroller.h>
#include <interfaces/context.h>
#include <interfaces/idocument.h>

KDevDocumentView::KDevDocumentView( KDevDocumentViewPlugin *plugin, QWidget *parent )
    : QTreeView( parent ),
        m_plugin( plugin )
{
    m_documentModel = new KDevDocumentModel();

    m_delegate = new KDevDocumentViewDelegate( this, this );

    m_proxy = new QSortFilterProxyModel( this );
    m_proxy->setSourceModel( m_documentModel );
    m_proxy->setDynamicSortFilter( true );
    m_proxy->setSortCaseSensitivity( Qt::CaseInsensitive );
    m_proxy->sort(0);

    m_selectionModel = new KDevDocumentSelection( m_proxy );

    setModel( m_proxy );
    setSelectionModel( m_selectionModel );
    setItemDelegate( m_delegate );

    setObjectName( i18n( "Documents" ) );

    setWindowIcon( SmallIcon( "document-multiple" ) );
    setWindowTitle( i18n( "Documents" ) );
    setWhatsThis( i18n( "Document View" ) );

    setFocusPolicy( Qt::NoFocus );

    setRootIsDecorated( false );
    header()->hide();

    setSelectionBehavior( QAbstractItemView::SelectRows );
    setSelectionMode( QAbstractItemView::ExtendedSelection );
}

KDevDocumentView::~KDevDocumentView()
{}

KDevDocumentViewPlugin *KDevDocumentView::plugin() const
{
    return m_plugin;
}

void KDevDocumentView::mousePressEvent( QMouseEvent * event )
{
    QModelIndex proxyIndex = indexAt( event->pos() );
    QModelIndex index = m_proxy->mapToSource( proxyIndex );

    if ( event->button() == Qt::LeftButton && proxyIndex.parent().isValid() &&
            event->modifiers() == Qt::NoModifier )
    {
        KDevelop::IDocumentController* dc = m_plugin->core()->documentController();
        KUrl documentUrl = static_cast<KDevDocumentItem*>( m_documentModel->itemFromIndex( index ) )->fileItem()->url();
        if (dc->documentForUrl(documentUrl) != dc->activeDocument())
        {
            dc->openDocument(documentUrl);
            return;
        }
    }

    if ( !proxyIndex.parent().isValid() )
    {
        setExpanded( proxyIndex, !isExpanded( proxyIndex ) );
        return;
    }

    QTreeView::mousePressEvent( event );
}

template<typename F> void KDevDocumentView::visitItems(F f, bool selectedItems)
{
    KDevelop::IDocumentController* dc = m_plugin->core()->documentController();
    QList<KUrl> docs = selectedItems ? m_selectedDocs : m_unselectedDocs;
    
    foreach(const KUrl& url, docs) {
       KDevelop::IDocument* doc = dc->documentForUrl(url);
       if (doc) f(doc);
    }
}

namespace
{
class DocSaver
{
public: void operator()(KDevelop::IDocument* doc) { doc->save(); }
};
class DocCloser
{
public: void operator()(KDevelop::IDocument* doc) { doc->close(); }
};
class DocReloader
{
public: void operator()(KDevelop::IDocument* doc) { doc->reload(); }
};
}

void KDevDocumentView::saveSelected()
{
    visitItems(DocSaver(), true);
}

void KDevDocumentView::closeSelected()
{
    visitItems(DocCloser(), true);
}

void KDevDocumentView::closeUnselected()
{
    visitItems(DocCloser(), false);
}

void KDevDocumentView::reloadSelected()
{
    visitItems(DocReloader(), true);
}

void KDevDocumentView::contextMenuEvent( QContextMenuEvent * event )
{
    updateSelectedDocs();
    
    if (!m_selectedDocs.isEmpty())
    {
        KMenu* ctxMenu = new KMenu(this);
        
        KDevelop::FileContext context(m_selectedDocs);
        QList<KDevelop::ContextMenuExtension> extensions =
            m_plugin->core()->pluginController()->queryPluginsForContextMenuExtensions( &context );

        QList<QAction*> vcsActions;
        QList<QAction*> fileActions;
        QList<QAction*> editActions;
        QList<QAction*> extensionActions;
        foreach( const KDevelop::ContextMenuExtension& ext, extensions )
        {
            fileActions += ext.actions(KDevelop::ContextMenuExtension::FileGroup);
            vcsActions += ext.actions(KDevelop::ContextMenuExtension::VcsGroup);
            editActions += ext.actions(KDevelop::ContextMenuExtension::EditGroup);
            extensionActions += ext.actions(KDevelop::ContextMenuExtension::ExtensionGroup);
        }
        
        appendActions(ctxMenu, fileActions);
        
        KAction* save = KStandardAction::save(this, SLOT(saveSelected()), ctxMenu);
        save->setEnabled(selectedDocHasChanges());
        ctxMenu->addAction(save);
        ctxMenu->addAction(KIcon("view-refresh"), i18n( "Reload" ), this, SLOT(reloadSelected()));
        
        appendActions(ctxMenu, editActions);
        
        ctxMenu->addAction(KStandardAction::close(this, SLOT(closeSelected()), ctxMenu));
        QAction* closeUnselected = ctxMenu->addAction(KIcon("document-close"), i18n( "Close Other Files" ), this, SLOT(closeUnselected()));
        closeUnselected->setEnabled(!m_unselectedDocs.isEmpty());

        appendActions(ctxMenu, vcsActions);
        
        appendActions(ctxMenu, extensionActions);
        
        connect(ctxMenu, SIGNAL(aboutToHide()), ctxMenu, SLOT(deleteLater()));
        ctxMenu->popup( event->globalPos() );
    }
}

void KDevDocumentView::appendActions(QMenu* menu, const QList<QAction*>& actions)
{
    foreach( QAction* act, actions )
    {
        menu->addAction(act);
    }
    menu->addSeparator();
}

bool KDevDocumentView::selectedDocHasChanges()
{
    KDevelop::IDocumentController* dc = m_plugin->core()->documentController();
    foreach(const KUrl& url, m_selectedDocs)
    {
        KDevelop::IDocument* doc = dc->documentForUrl(url);
        if (!doc) continue;
        if (doc->state() != KDevelop::IDocument::Clean)
        {
            return true;
        }
    }
    return false;
}

void KDevDocumentView::updateSelectedDocs()
{
    m_selectedDocs.clear();
    m_unselectedDocs.clear();

    for (int i = 0; i < m_documentModel->rowCount(); i++)
    {
        QList<QStandardItem*> allItems = m_documentModel->findItems("*", Qt::MatchWildcard | Qt::MatchRecursive);
        foreach (QStandardItem* item, allItems)
        {
            if (KDevFileItem * fileItem = dynamic_cast<KDevDocumentItem*>(item)->fileItem())
            {
                if (m_selectionModel->isSelected(m_proxy->mapFromSource(m_documentModel->indexFromItem(fileItem))))
                    m_selectedDocs << fileItem->url();
                else
                    m_unselectedDocs << fileItem->url();
            }
        }
    }
}

void KDevDocumentView::activated( KDevelop::IDocument* document )
{
    setCurrentIndex( m_proxy->mapFromSource( m_documentModel->indexFromItem( m_doc2index[ document ] ) ) );
}

void KDevDocumentView::saved( KDevelop::IDocument* )
{
}

void KDevDocumentView::opened( KDevelop::IDocument* document )
{
    QString mimeType = document->mimeType()->comment();
    KDevMimeTypeItem *mimeItem = m_documentModel->mimeType( mimeType );
    if ( !mimeItem )
    {
        mimeItem = new KDevMimeTypeItem( mimeType );
        m_documentModel->insertRow( m_documentModel->rowCount(), mimeItem );
        setExpanded( m_proxy->mapFromSource( m_documentModel->indexFromItem( mimeItem ) ), false);
    }

    if ( !mimeItem->file( document->url() ) )
    {
        KDevFileItem * fileItem = new KDevFileItem( document->url() );
        mimeItem->setChild( mimeItem->rowCount(), fileItem );
        setCurrentIndex( m_proxy->mapFromSource( m_documentModel->indexFromItem( fileItem ) ) );
        m_doc2index[ document ] = fileItem;
    }
}

void KDevDocumentView::closed( KDevelop::IDocument* document )
{
    KDevFileItem* file = m_doc2index[ document ];
    if ( !file )
        return;

    QStandardItem* mimeItem = file->parent();

    qDeleteAll(mimeItem->takeRow(m_documentModel->indexFromItem(file).row()));

    m_doc2index.remove(document);

    if ( mimeItem->hasChildren() )
        return;

    qDeleteAll(m_documentModel->takeRow(m_documentModel->indexFromItem(mimeItem).row()));

    doItemsLayout();
}

void KDevDocumentView::contentChanged( KDevelop::IDocument* )
{
}

void KDevDocumentView::stateChanged( KDevelop::IDocument* document )
{
    KDevDocumentItem * documentItem = m_doc2index[ document ];

    if ( documentItem && documentItem->documentState() != document->state() )
        documentItem->setDocumentState( document->state() );

    doItemsLayout();
}

void KDevDocumentView::documentUrlChanged( KDevelop::IDocument* document )
{
    closed(document);
    opened(document);
}

#include "kdevdocumentview.moc"
