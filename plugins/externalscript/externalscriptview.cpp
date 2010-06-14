/*
    This plugin is part of KDevelop.

    Copyright (C) 2010 Milian Wolff <mail@milianw.de>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "externalscriptview.h"

#include "externalscriptplugin.h"
#include "externalscriptitem.h"

#include <KLocalizedString>
#include <KAction>

#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QMouseEvent>

ExternalScriptView::ExternalScriptView( ExternalScriptPlugin* plugin, QWidget* parent )
    : QWidget( parent ), m_plugin( plugin )
{
  Ui::ExternalScriptViewBase::setupUi( this );

  setWindowTitle( i18n( "External Scripts" ) );

  m_model = new QSortFilterProxyModel( this );
  m_model->setSourceModel( m_plugin->model() );
  connect( filterText, SIGNAL( userTextChanged( QString ) ),
           m_model, SLOT( setFilterWildcard( QString ) ) );

  scriptTree->setModel( m_model );
  scriptTree->setContextMenuPolicy( Qt::CustomContextMenu );
  scriptTree->viewport()->installEventFilter( this );
  connect(scriptTree, SIGNAL(customContextMenuRequested(const QPoint&)),
          this, SLOT(contextMenu(const QPoint&)));

  m_addScriptAction = new KAction(KIcon("document-new"), i18n("Add External Script"), this);
  connect(m_addScriptAction, SIGNAL(triggered()), this, SLOT(addScript()));
  addAction(m_addScriptAction);
  m_editScriptAction = new KAction(KIcon("document-edit"), i18n("Edit External Script"), this);
  connect(m_editScriptAction, SIGNAL(triggered()), this, SLOT(editScript()));
  addAction(m_editScriptAction);
  m_removeScriptAction = new KAction(KIcon("document-close"), i18n("Remove External Script"), this);
  connect(m_removeScriptAction, SIGNAL(triggered()), this, SLOT(removeScript()));
  addAction(m_removeScriptAction);

  connect(scriptTree->selectionModel(), SIGNAL(selectionChanged(QItemSelection, QItemSelection)),
          this, SLOT(validateActions()));

  validateActions();
}

ExternalScriptView::~ExternalScriptView()
{

}

ExternalScriptItem* ExternalScriptView::currentItem() const
{
    QModelIndex index = scriptTree->currentIndex();
    index = m_model->mapToSource( index );
    return static_cast<ExternalScriptItem*>( m_plugin->model()->itemFromIndex( index ) );
}

void ExternalScriptView::validateActions()
{
  bool itemSelected = currentItem();

  m_removeScriptAction->setEnabled(itemSelected);
  m_editScriptAction->setEnabled(itemSelected);
}

void ExternalScriptView::contextMenu( const QPoint& pos )
{

}

bool ExternalScriptView::eventFilter(QObject* obj, QEvent* e)
{
    // no, listening to activated() is not enough since that would also trigger the edit mode which we _dont_ want here
    // users may still rename stuff via select + F2 though
    if (obj == scriptTree->viewport()) {
        const bool singleClick = KGlobalSettings::singleClick();
        if ( (!singleClick && e->type() == QEvent::MouseButtonDblClick) || (singleClick && e->type() == QEvent::MouseButtonRelease) ) {
            QMouseEvent* mouseEvent = dynamic_cast<QMouseEvent*>(e);
            Q_ASSERT(mouseEvent);
            QModelIndex clickedIndex = scriptTree->indexAt(mouseEvent->pos());
            if (clickedIndex.isValid()) {
                ///TODO: execute script
                e->accept();
                return true;
            }
        }
    }
    return QObject::eventFilter(obj, e);
}

void ExternalScriptView::addScript()
{

}

void ExternalScriptView::removeScript()
{
}

void ExternalScriptView::editScript()
{

}

#include "externalscriptview.moc"

// kate: indent-mode cstyle; space-indent on; indent-width 2; replace-tabs on;
