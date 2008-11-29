/***************************************************************************
 *   This file is part of KDevelop                                         *
 *   Copyright 2007 Andreas Pakulat <apaku@gmx.de>                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#include "projectbuildsetwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeView>
#include <QToolButton>
#include <QItemSelectionModel>
#include <QStringListModel>
#include <QModelIndex>
#include <QStandardItemModel>

#include <kdebug.h>
#include <kicon.h>
#include <kmenu.h>

#include <project/projectmodel.h>
#include <interfaces/iproject.h>

#include "projectmanagerviewplugin.h"
#include "projectmanagerview.h"
#include "projectbuildsetmodel.h"

#include "ui_projectbuildsetwidget.h"
#include <QHeaderView>

ProjectBuildSetWidget::ProjectBuildSetWidget( QWidget* parent )
    : QWidget( parent ), m_view( 0 ),
     m_ui( new Ui::ProjectBuildSetWidget )
{
    m_ui->setupUi( this );
    
    m_ui->addItemButton->setIcon( KIcon( "list-add" ) );
    connect( m_ui->addItemButton, SIGNAL( clicked() ),
             this, SLOT( addItems() ) );

    m_ui->removeItemButton->setIcon( KIcon( "list-remove" ) );
    connect( m_ui->removeItemButton, SIGNAL( clicked() ),
             this, SLOT( removeItems() ) );

    m_ui->buildButton->setIcon( KIcon( "run-build" ) );
    m_ui->installButton->setIcon( KIcon( "run-install" ) );
    m_ui->cleanButton->setIcon( KIcon( "run-clean" ) );

    m_ui->itemView->horizontalHeader()->setStretchLastSection(true);
    m_ui->itemView->setContextMenuPolicy( Qt::CustomContextMenu );
    connect( m_ui->itemView, SIGNAL( customContextMenuRequested( const QPoint& ) ),
             SLOT(showContextMenu(const QPoint&) ) );
    layout()->setMargin(0);
}

void ProjectBuildSetWidget::setProjectView( ProjectManagerView* view )
{
    m_view = view;
    m_ui->itemView->setModel( m_view->plugin()->buildSet() );
    connect( m_ui->itemView->selectionModel(), SIGNAL(selectionChanged(const QItemSelection&, const QItemSelection&) ),
             this, SLOT(selectionChanged()) );

    connect( m_ui->buildButton, SIGNAL( clicked() ),
             m_view->plugin(), SLOT( buildProjectItems() ) );
    connect( m_ui->installButton, SIGNAL( clicked() ),
             m_view->plugin(), SLOT( installProjectItems() ) );
    connect( m_ui->cleanButton, SIGNAL( clicked() ),
             m_view->plugin(), SLOT( cleanProjectItems() ) );
}

void ProjectBuildSetWidget::selectionChanged()
{
    kDebug() << "checking selectionmodel:" << m_ui->itemView->selectionModel()->selectedRows();
    m_ui->removeItemButton->setEnabled( !m_ui->itemView->selectionModel()->selectedRows().isEmpty() );
    m_ui->addItemButton->setEnabled( !m_view->selectedItems().isEmpty() );
}

ProjectBuildSetWidget::~ProjectBuildSetWidget()
{
    delete m_ui;
}

void ProjectBuildSetWidget::showContextMenu( const QPoint& p )
{
    KMenu m;
    m.setTitle( i18n("Buildset") );
    m.addAction( i18n( "Remove from buildset" ), this, SLOT( removeItems() ) );
    m.exec( m_ui->itemView->mapToGlobal( p ) );
}

void ProjectBuildSetWidget::addItems()
{
    foreach( KDevelop::ProjectBaseItem* item, m_view->selectedItems() )
    {
        m_view->plugin()->buildSet()->addProjectItem( item );
    }
}

void ProjectBuildSetWidget::removeItems()
{
    QList<int> rows;
    foreach( const QItemSelectionRange &range, m_ui->itemView->selectionModel()->selection() )
    {
        m_view->plugin()->buildSet()->removeRows( range.top(), range.height() );
    }
}
