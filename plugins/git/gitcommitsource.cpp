/***************************************************************************
 *   Copyright 2014 Aleix Pol Gonzalez <aleixpol@kde.org>                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public License as        *
 *   published by the Free Software Foundation; either version 2 of        *
 *   the License or (at your option) version 3 or any later version        *
 *   accepted by the membership of KDE e.V. (or its successor approved     *
 *   by the membership of KDE e.V.), which shall act as a proxy            *
 *   defined in Section 14 of version 3 of the license.                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "gitcommitsource.h"
#include "gitplugin.h"
#include "gitpatchextractor.h"
#include <vcs/dvcs/dvcsjob.h>
#include <interfaces/icore.h>
#include <interfaces/iplugincontroller.h>
#include <interfaces/idocumentcontroller.h>
#include <interfaces/iruncontroller.h>
#include <KTextEditor/Document>
#include <KTextEditor/View>
#include <QAction>

GitDiffUpdater::GitDiffUpdater(const QStringList& args, GitPlugin* vcs, const KUrl& url)
    : m_git(vcs)
    , m_url(url)
    , m_args(args)
{
}

KDevelop::VcsDiff GitDiffUpdater::update() const
{
    QScopedPointer<KDevelop::DVcsJob> diffJob(m_git->diff(m_url));
    *diffJob << m_args;

    KDevelop::VcsDiff diff;
    bool correctDiff = diffJob->exec();
    if (correctDiff)
        diff = diffJob->fetchResults().value<KDevelop::VcsDiff>();

    return diff;
}

KDevelop::IBasicVersionControl* GitDiffUpdater::vcs() const
{
    return m_git;
}

/////////////////////////////////////

GitCommitSource::GitCommitSource(const QUrl& url, GitPlugin* p)
    : VCSCommitDiffPatchSource(new GitDiffUpdater(QStringList(), p, url))
    , m_cachedPatch(new VCSDiffPatchSource(new GitDiffUpdater(QStringList("--staged"), p, url)))
    , m_git(p)
{
    m_name = i18n("Modifications");
    m_cachedPatch->m_name = i18n("Staged");

    //TODO react to document changes for renaming and dis/enabling
    QAction* stage = new QAction(i18n("Toggle staging"), this);
    connect(stage, SIGNAL(triggered(bool)), SLOT(toggleStaging()));
    m_actions << stage;

    connect(this, SIGNAL(patchChanged()), m_cachedPatch.data(), SIGNAL(patchChanged()));
}

void GitCommitSource::update()
{
    VCSDiffPatchSource::update();
    m_cachedPatch->update();
    emit patchChanged();
}

QList< KDevelop::IPatchSource* > GitCommitSource::relatedPatches()
{
    return QList<KDevelop::IPatchSource*>() << m_cachedPatch.data();
}

QList< QAction* > GitCommitSource::actions()
{
    return m_actions;
}

void GitCommitSource::toggleStaging()
{
    KDevelop::IDocument* doc = KDevelop::ICore::self()->documentController()->activeDocument();
    KTextEditor::Document* textDoc = doc->textDocument();
    QStringList extraArgs;
    if(doc->url() == m_cachedPatch->file()) {
        extraArgs << "-R";
    } else if(doc->url() != file()) {
        return;
    }

    KTextEditor::Range range = textDoc->activeView()->selectionRange();
    KDevelop::DVcsJob* job = m_git->gitJob(baseDir(), KDevelop::OutputJob::Verbose);
    QString str = GitPatch::extractPatch(textDoc, range);
    QTemporaryFile* tfile = new QTemporaryFile(this);
    bool opened = tfile->open();
    Q_ASSERT(opened);
    tfile->write(str.toLocal8Bit());
    tfile->close();
    *job << "apply" << "--index" << "--cache" << extraArgs << tfile->fileName();

    connect(job, SIGNAL(finished(KJob*)), tfile, SLOT(deleteLater()));
    connect(job, SIGNAL(finished(KJob*)), this, SLOT(update()));

    KDevelop::ICore::self()->runController()->registerJob(job);
}
