/***************************************************************************
 *   This file was partly taken from KDevelop's cvs plugin                 *
 *   Copyright 2007 Robert Gruber <rgruber@users.sourceforge.net>          *
 *                                                                         *
 *   Adapted for Git                                                       *
 *   Copyright 2008 Evgeniy Ivanov <powerfox@kde.ru>                       *
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

#include "gitexecutor.h"

#include <QFileInfo>
#include <QDir>
#include <QString>

#include <KUrl>
#include <KShell>
#include <KDebug>

#include <vcs/dvcs/dvcsjob.h>
#include <interfaces/iplugin.h>

using KDevelop::VcsStatusInfo;

GitExecutor::GitExecutor(KDevelop::IPlugin* parent)
    : QObject(parent), vcsplugin(parent)
{
}

GitExecutor::~GitExecutor()
{
}

QString GitExecutor::name() const
{
    return QLatin1String("Git");
}

//TODO: write tests for this method!
//maybe func()const?
bool GitExecutor::isValidDirectory(const KUrl & dirPath)
{
    DVCSjob* job = gitRevParse(dirPath.path(), QStringList(QString("--is-inside-work-tree")));
    if (job)
    {
        job->exec();
        if (job->status() == KDevelop::VcsJob::JobSucceeded)
        {
            kDebug(9500) << "Dir:" << dirPath << " is is inside work tree of git" ;
            return true;
        }
    }
    kDebug(9500) << "Dir:" << dirPath.path() << " is is not inside work tree of git" ;
    return false;
}

DVCSjob* GitExecutor::init(const KUrl &directory)
{
    DVCSjob* job = new DVCSjob(vcsplugin);
    if (prepareJob(job, directory.toLocalFile(), GitExecutor::Init) ) {
        *job << "git-init";
        return job;
    }
    if (job) delete job;
    return NULL;
}

DVCSjob* GitExecutor::clone(const KUrl &repository, const KUrl directory)
{
    DVCSjob* job = new DVCSjob(vcsplugin);
    if (prepareJob(job, directory.toLocalFile(), GitExecutor::Init) ) {
        *job << "git-clone";
        *job << repository.path();
//         addFileList(job, repository.path(), directory); //TODO it's temp, should work only with local repos
        return job;
    }
    if (job) delete job;
    return NULL;
}

DVCSjob* GitExecutor::add(const QString& repository, const KUrl::List &files)
{
    DVCSjob* job = new DVCSjob(vcsplugin);
    if (prepareJob(job, repository) ) {
        *job << "git";
        *job << "add";
        addFileList(job, files);

        return job;
    }
    if (job) delete job;
    return NULL;
}

//TODO: git doesn't like empty messages, but "KDevelop didn't provide any message, it may be a bug" looks ugly...
//If no files specified then commit already added files
DVCSjob* GitExecutor::commit(const QString& repository,
                             const QString &message, /*= "KDevelop didn't provide any message, it may be a bug"*/
                             const KUrl::List &args /*= QStringList("")*/)
{
    Q_UNUSED(args)
    DVCSjob* job = new DVCSjob(vcsplugin);
    if (prepareJob(job, repository) ) {
        *job << "git-commit";
        ///In args you may find url, not only comd-line args in this case.
/*        foreach(KUrl arg, args)
            *job<<KUrl::relativeUrl(repository + QDir::separator(), arg);*/
        *job << "-m";
        //Note: the message is quoted somewhere else, so if we quote here then we have quotes in the commit log
        *job << message;
        return job;
    }
    if (job) delete job;
    return NULL;
}

DVCSjob* GitExecutor::remove(const QString& repository, const KUrl::List &files)
{
    DVCSjob* job = new DVCSjob(vcsplugin);
    if (prepareJob(job, repository) ) {
        *job << "git-rm";
        addFileList(job, files);
        return job;
    }
    if (job) delete job;
    return NULL;
}

DVCSjob* GitExecutor::status(const QString & repository, const KUrl::List & files, bool recursive, bool taginfo)
{
    Q_UNUSED(files)
    Q_UNUSED(recursive)
    Q_UNUSED(taginfo)
    DVCSjob* job = new DVCSjob(vcsplugin);
    if (prepareJob(job, repository) ) {
        *job << "git";
        *job << "status";

        return job;
    }
    if (job) delete job;
    return NULL;
}

DVCSjob* GitExecutor::log(const KUrl& url)
{
    QFileInfo info(url.toLocalFile());

    DVCSjob* job = new DVCSjob(vcsplugin);
    if (prepareJob(job, info.absolutePath()) ) {
        *job << "git";
        *job << "log";
        *job << info.fileName();
        return job;
    }
    if (job) delete job;
    return NULL;
}

DVCSjob* GitExecutor::var(const QString & repository)
{
    DVCSjob* job = new DVCSjob(vcsplugin);
    if (prepareJob(job, repository) ) {
        *job << "git-var";
        *job << "-l";
        return job;
    }
    if (job) delete job;
    return NULL;
}

DVCSjob* GitExecutor::empty_cmd() const
{
    ///TODO: maybe just "" command?
    DVCSjob* job = new DVCSjob(vcsplugin);
    *job << "echo";
    *job << "-n";
    return job;
}

DVCSjob* GitExecutor::checkout(const QString &repository, const QString &branch)
{
    ///TODO Check if the branch exists. or send only existed branch names here!
    DVCSjob* job = new DVCSjob(vcsplugin);
    if (prepareJob(job, repository) ) {
        *job << "git-checkout";
        *job << branch;
        return job;
    }
    if (job) delete job;
    return NULL;
}

DVCSjob* GitExecutor::branch(const QString &repository, const QString &basebranch, const QString &branch,
                             const QStringList &args)
{
    DVCSjob* job = new DVCSjob(vcsplugin);
    if (prepareJob(job, repository) ) {
        *job << "git-branch";
        //Empty branch has 'something' so it breaks the command
        if (!args.isEmpty())
            *job << args;
        if (!branch.isEmpty())
            *job << branch;
        if (!basebranch.isEmpty())
            *job << basebranch;
        return job;
    }
    if (job) delete job;
    return NULL;
}

DVCSjob* GitExecutor::reset(const QString &repository, const QStringList &args, const QStringList files)
{
    DVCSjob* job = new DVCSjob(vcsplugin);
    if (prepareJob(job, repository) ) {
        *job << "git-reset";
        //Empty branch has 'something' so it breaks the command
        if (!args.isEmpty())
            *job << args;
        addFileList(job, files);
        return job;
    }
    if (job) delete job;
    return NULL;
}

DVCSjob* GitExecutor::lsFiles(const QString &repository, const QStringList &args)
{
    DVCSjob* job = new DVCSjob(vcsplugin);
    if (prepareJob(job, repository) ) {
        *job << "git-ls-files";
        //Empty branch has 'something' so it breaks the command
        if (!args.isEmpty())
            *job << args;
        return job;
    }
    if (job) delete job;
    return NULL;
}

QString GitExecutor::curBranch(const QString &repository)
{
    DVCSjob* job = branch(repository);
    if (job)
    {
        kDebug() << "Getting branch list";
        job->exec();
    }
    QString branch;
    if (job->status() == KDevelop::VcsJob::JobSucceeded)
        branch = job->output();

    branch = branch.prepend('\n').section("\n*", 1);
    branch = branch.section('\n', 0, 0).trimmed();
    kDebug() << "Current branch is: " << branch;
    return branch;
}

QStringList GitExecutor::branches(const QString &repository)
{
    DVCSjob* job = branch(repository);
    if (job)
    {
        kDebug() << "Getting branch list";
        job->exec();
    }
    QStringList branchListDirty;
    if (job->status() == KDevelop::VcsJob::JobSucceeded)
        branchListDirty = job->output().split('\n');
    else
        return QStringList();

    QStringList branchList;
    foreach(QString branch, branchListDirty)
    {
        if (branch.contains("*"))
        {
            branch = branch.prepend('\n').section("\n*", 1);
            branch = branch.trimmed();
        }
        else
        {
            branch = branch.prepend('\n').section("\n", 1);
            branch = branch.trimmed();
        }
        branchList<<branch;
    }
    return branchList;
}

QStringList GitExecutor::getOtherFiles(const QString &directory)
{
    return getLsFiles(directory, QStringList(QString("--others")) );
}

QList<VcsStatusInfo> GitExecutor::getModifiedFiles(const QString &directory)
{
    DVCSjob* job = new DVCSjob(vcsplugin);
    if (prepareJob(job, directory) )
        *job << "git-diff-files";
    if (job)
        job->exec();
    QStringList output;
    if (job->status() == KDevelop::VcsJob::JobSucceeded)
        output = job->output().split('\n');
    else
        return QList<VcsStatusInfo>();

    QList<VcsStatusInfo> modifiedFiles;
    foreach(QString line, output)
    {
        QChar stCh = line[97];

        KUrl file(line.section('\t', 1).trimmed());

        VcsStatusInfo status;
        status.setUrl(file);
        status.setState(charToState(stCh.toAscii() ) );

        kDebug() << line[97] << " " << file.path();

        modifiedFiles.append(status);
    }

    return modifiedFiles;
}

QList<VcsStatusInfo> GitExecutor::getCachedFiles(const QString &directory)
{
//     gitRevParse(dirPath.path(), QString("--is-inside-work-tree")
    DVCSjob* job = gitRevParse(directory, QStringList(QString("--branches")));
    job->exec();
    QStringList shaArg;
    if (job->output().isEmpty())
    {
        //there is no branches, which means there is no commit yet
        //let's create an empty tree to use with git-diff-index
        DVCSjob* job = new DVCSjob(vcsplugin);
        if (prepareJob(job, directory) )
        {
            *job << "git-mktree";
            job->setStandardInputFile("/dev/null");
        }
        if (job && job->exec() && job->status() == KDevelop::VcsJob::JobSucceeded)
            shaArg<<job->output().split('\n');
    }
    else
        shaArg<<"HEAD";
    job = new DVCSjob(vcsplugin);
    if (prepareJob(job, directory) )
        *job << "git-diff-index" << "--cached" << shaArg;
    if (job)
        job->exec();
    QStringList output;
    if (job->status() == KDevelop::VcsJob::JobSucceeded)
        output = job->output().split('\n');
    else
        return QList<VcsStatusInfo>();

    QList<VcsStatusInfo> cachedFiles;

    foreach(QString line, output)
    {
        QChar stCh = line[97];

        KUrl file(line.section('\t', 1).trimmed());

        VcsStatusInfo status;
        status.setUrl(file);
        status.setState(charToState(stCh.toAscii() ) );

        kDebug() << line[97] << " " << file.path();

        cachedFiles.append(status);
    }

    return cachedFiles;
}

//Actually we can just copy the outpuc without parsing. So it's a kind of draft for future
void GitExecutor::parseOutput(const QString& jobOutput, QList<DVCScommit>& commits) const
{
//     static QRegExp rx_sep( "[-=]+" );
//     static QRegExp rx_date( "date:\\s+([^;]*);\\s+author:\\s+([^;]*).*" );

    static QRegExp rx_com( "commit \\w{1,40}" );

    QStringList lines = jobOutput.split("\n");

    DVCScommit item;

    for (int i=0; i<lines.count(); ++i) {
        QString s = lines[i];
        kDebug(9500) << "line:" << s ;

        if (rx_com.exactMatch(s)) {
            kDebug(9500) << "MATCH COMMIT";
            item.commit = s;
            s = lines[++i];
            item.author = s;
            s = lines[++i];
            item.date = s;
            commits.append(item);
        }
        else 
        {
            item.log += s+'\n';
        }
    }
}

QStringList GitExecutor::getLsFiles(const QString &directory, const QStringList &args)
{
    DVCSjob* job = lsFiles(directory, args);
    if (job)
    {
        job->exec();
        if (job->status() == KDevelop::VcsJob::JobSucceeded)
            return job->output().split('\n');
        else
            return QStringList();
    }
    return QStringList();
}

DVCSjob* GitExecutor::gitRevParse(const QString &repository, const QStringList &args)
{
    //Use prepareJob() here only if you like "dead" recursion and KDevelop crashes
    DVCSjob* job = new DVCSjob(vcsplugin);
    if (job)
    {
        QString workDir = repository;
        QFileInfo fsObject(workDir);
        if (fsObject.isFile())
            workDir = fsObject.path();

        job->clear();
        job->setDirectory(workDir);
        *job << "git-rev-parse";
        foreach(const QString &arg, args)
            *job << arg;
        return job;
    }
    if (job) delete job;
    return NULL;
}

KDevelop::VcsStatusInfo::State GitExecutor::charToState(const char ch)
{
    switch (ch)
    {
        case 'M':
        {
            return VcsStatusInfo::ItemModified;
            break;
        }
        case 'A':
        {
            return VcsStatusInfo::ItemAdded;
            break;
        }
        case 'D':
        {
            return VcsStatusInfo::ItemDeleted;
            break;
        }
        //ToDo: hasConflicts
        default:
        {
            return VcsStatusInfo::ItemUnknown;
            break;
        }
    }
    return VcsStatusInfo::ItemUnknown;
}

// #include "gitexetor.moc"
