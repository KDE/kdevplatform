/***************************************************************************
 *   Copyright 2007 Dukju Ahn <dukjuahn@gmail.com>                         *
 *   Copyright 2008 Evgeniy Ivanov <powerfox@kde.ru>                       *
 *   Copyright 2011 Andrey Batyiev <batyiev@gmail.com>                     *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "vcscommitdialog.h"
#include "vcsdiffpatchsources.h"

#include <QtCore/QHash>
#include <QtGui/QBrush>
#include <QtCore/QRegExp>

#include <KDE/KTextEdit>
#include <KDE/KComboBox>
#include <KDE/KLocale>
#include <KDE/KDebug>
#include <KDE/KColorScheme>
#include <KDE/KMessageBox>
#include <kparts/mainwindow.h>

#include <interfaces/icore.h>
#include <interfaces/iprojectcontroller.h>
#include <interfaces/iproject.h>
#include <interfaces/iplugin.h>
#include <interfaces/iuicontroller.h>
#include <interfaces/ipatchsource.h>

#include "../vcsjob.h"
#include "../interfaces/ibasicversioncontrol.h"
#include "../interfaces/idistributedversioncontrol.h"
#include "../interfaces/icentralizedversioncontrol.h"
#include "../vcsstatusinfo.h"
#include "../models/vcsfilechangesmodel.h"

#include "ui_vcscommitdialog.h"
#include <KComponentData>
#include <vcspluginhelper.h>

namespace KDevelop
{

class VcsCommitDialogPrivate
{
public:
    Ui::VcsCommitDialog ui;
    IPatchSource* m_patchSource;
    VcsFileChangesModel* m_model;
};

VcsCommitDialog::VcsCommitDialog( IPatchSource *patchSource, QWidget *parent )
    : KDialog( parent ), d(new VcsCommitDialogPrivate())
{
    d->ui.setupUi( mainWidget() );
    QWidget *customWidget = patchSource->customWidget();
    if( customWidget )
    {
        d->ui.gridLayout->addWidget( customWidget, 0, 0, 1, 2 );
    }

    setButtons( KDialog::Ok | KDialog::Cancel );

    d->m_patchSource = patchSource;
    d->m_model = new VcsFileChangesModel( this, true );
    d->ui.files->setModel( d->m_model );
    connect(this, SIGNAL(okClicked()), SLOT(ok()) );
    connect(this, SIGNAL(cancelClicked()), SLOT(cancel()) );

    if(VCSCommitDiffPatchSource* commitPatch = qobject_cast<VCSCommitDiffPatchSource*>(patchSource)) {
        setCommitCandidates(commitPatch->infos());
    }
}

VcsCommitDialog::~VcsCommitDialog()
{
    delete d;
}

void VcsCommitDialog::setRecursive( bool recursive )
{
    d->ui.recursiveChk->setChecked( recursive );
}

void VcsCommitDialog::setCommitCandidates( const QList<KDevelop::VcsStatusInfo>& statuses )
{
    foreach( const VcsStatusInfo& info, statuses )
    {
        d->m_model->updateState( info );
    }
}

bool VcsCommitDialog::recursive() const
{
    return d->ui.recursiveChk->isChecked();
}

void VcsCommitDialog::ok()
{
    if( d->m_patchSource->finishReview( d->m_model->checkedUrls() ) )
    {
        deleteLater();
    }
}

void VcsCommitDialog::cancel()
{
    d->m_patchSource->cancelReview();
}

}
