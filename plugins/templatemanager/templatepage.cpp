/*
 * This file is part of KDevelop
 * Copyright 2012 Miha Čančula <miha@noughmad.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "templatepage.h"
#include "ui_templatepage.h"
#include <interfaces/itemplateprovider.h>
#include <language/codegen/templatesmodel.h>
#include <KFileDialog>
#include <KNS3/DownloadDialog>
#include <KArchive>
#include <KZip>
#include <KTar>
#include <knewstuff3/uploaddialog.h>

TemplatePage::TemplatePage (KDevelop::ITemplateProvider* provider, QWidget* parent) : QWidget (parent),
m_provider(provider)
{
    ui = new Ui::TemplatePage;
    ui->setupUi(this);

    ui->getNewButton->setVisible(!m_provider->knsConfigurationFile().isEmpty());
    connect(ui->getNewButton, SIGNAL(clicked(bool)),
            this, SLOT(getMoreTemplates()));

    ui->shareButton->setVisible(!m_provider->knsConfigurationFile().isEmpty());
    connect(ui->shareButton, SIGNAL(clicked(bool)),
            this, SLOT(shareTemplates()));

    ui->loadButton->setVisible(!m_provider->supportedMimeTypes().isEmpty());
    connect(ui->loadButton, SIGNAL(clicked(bool)),
            this, SLOT(loadFromFile()));

    ui->extractButton->setEnabled(false);
    connect(ui->extractButton, SIGNAL(clicked(bool)),
            this, SLOT(extractTemplate()));

    provider->reload();

    ui->treeView->setModel(provider->templatesModel());
    ui->treeView->expandAll();
    connect(ui->treeView->selectionModel(), SIGNAL(currentChanged(QModelIndex,QModelIndex)), SLOT(currentIndexChanged(QModelIndex)));
}

TemplatePage::~TemplatePage()
{
    delete ui;
}

void TemplatePage::loadFromFile()
{
    QString filename = KFileDialog::getOpenFileName(KUrl("kfiledialog:///kdevtemplates"), m_provider->supportedMimeTypes().join(" "), this);

    if (!filename.isEmpty())
    {
        m_provider->loadTemplate(filename);
    }

    m_provider->reload();
}

void TemplatePage::getMoreTemplates()
{
    KNS3::DownloadDialog dialog(m_provider->knsConfigurationFile(), this);
    dialog.exec();

    if (!dialog.changedEntries().isEmpty())
    {
        m_provider->reload();
    }
}

void TemplatePage::shareTemplates()
{
    KNS3::UploadDialog dialog(m_provider->knsConfigurationFile(), this);
    dialog.exec();
}

void TemplatePage::currentIndexChanged(const QModelIndex& index)
{
    QString archive = ui->treeView->model()->data(index, KDevelop::TemplatesModel::ArchiveFileRole).toString();
    ui->extractButton->setEnabled(QFileInfo(archive).exists());
}

void TemplatePage::extractTemplate()
{
    QModelIndex index = ui->treeView->currentIndex();
    QString archiveName= ui->treeView->model()->data(index, KDevelop::TemplatesModel::ArchiveFileRole).toString();

    QFileInfo info(archiveName);
    if (!info.exists())
    {
        ui->extractButton->setEnabled(false);
        return;
    }

    QScopedPointer<KArchive> archive;
    if (info.suffix() == QLatin1String("zip"))
    {
        archive.reset(new KZip(archiveName));
    }
    else
    {
        archive.reset(new KTar(archiveName));
    }

    archive->open(QIODevice::ReadOnly);

    KUrl destination = KUrl::fromLocalFile(KFileDialog::getExistingDirectory());
    destination.addPath(info.baseName());
    archive->directory()->copyTo(destination.toLocalFile());
}
