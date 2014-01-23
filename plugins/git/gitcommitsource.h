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

#ifndef GITCOMMITSOURCE_H
#define GITCOMMITSOURCE_H

#include <vcs/widgets/vcsdiffpatchsources.h>
#include <QStringList>

namespace KTextEditor {
class Range;
class Document;
}

namespace KDevelop {
class DVcsJob;
}

class GitPlugin;

class GitDiffUpdater : public VCSDiffUpdater {
public:
    GitDiffUpdater(const QStringList& args, GitPlugin* vcs, const KUrl& url);

    virtual KDevelop::VcsDiff update() const;
    virtual KUrl url() const { return m_url; }
    virtual KDevelop::IBasicVersionControl* vcs() const;

private:
    GitPlugin* m_git;
    KUrl m_url;
    QStringList m_args;
};

class GitCommitSource : public VCSCommitDiffPatchSource
{
    Q_OBJECT
    public:
        GitCommitSource(const QUrl& url, GitPlugin* p);

        virtual QList<QAction*> actions() override;
        virtual QList< IPatchSource* > relatedPatches();

    private slots:
        virtual void update();
        void toggleStaging();

    private:
        KDevelop::DVcsJob* applyJob();

        QScopedPointer<VCSDiffPatchSource> m_cachedPatch;
        QList<QAction*> m_actions;
        GitPlugin* m_git;
};

#endif
