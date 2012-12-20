/* This file is part of KDevelop
    Copyright 2005 Roberto Raggi <roberto@kdevelop.org>
    Copyright 2007 Andreas Pakulat <apaku@gmx.de>
    Copyright 2007 Aleix Pol <aleixpol@gmail.com>

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

#include "projectmodel.h"

#include <KMimeType>
#include <KDebug>
#include <KLocalizedString>
#include <KMessageBox>

#include <kio/udsentry.h>
#include <kio/netaccess.h>

#include <QApplication>
#include <QPalette>
#include <QBrush>
#include <QColor>
#include <QFileInfo>
#include <QMetaClassInfo>
#include <QThread>
#include <QMutex>

#include <interfaces/iproject.h>
#include <interfaces/iprojectcontroller.h>
#include <interfaces/icore.h>
#include <interfaces/iprojectfilemanager.h>

#include <language/duchain/indexedstring.h>

// Utility function to determine between direct connection and blocking-queued-connection
// for emitting of signals for data changes/row addition/removal
// BlockingQueuedConnection is necessary here as slots connected to the "aboutToBe" signals
// expect the actual model content to not have changed yet. So we need to make sure the
// signal is delivered before we really do something.
static Qt::ConnectionType getConnectionTypeForSignalDelivery( KDevelop::ProjectModel* model )
{
    if( QThread::currentThread() == model->thread() ) {
        return Qt::DirectConnection;
    } else {
        return Qt::BlockingQueuedConnection;
    }
}

namespace KDevelop
{

/**
 * Return true if @p url contains a clean path
 */
inline bool isValidPath(const KUrl& url)
{
    if (!url.isValid()) {
        return false;
    }

    KUrl cleaned = url;
    cleaned.cleanPath();
    return url == cleaned;
}

QStringList removeProjectBasePath( const QStringList& fullpath, KDevelop::ProjectBaseItem* item )
{
    QStringList result = fullpath;
    if( item )
    {
        KDevelop::ProjectModel* model = KDevelop::ICore::self()->projectController()->projectModel();
        QStringList basePath = model->pathFromIndex( model->indexFromItem( item ) );
        if( basePath.count() >= fullpath.count() )
        {
            return QStringList();
        }
        for( int i = 0; i < basePath.count(); i++ )
        {
            result.takeFirst();
        }
    }
    return result;
}

QStringList joinProjectBasePath( const QStringList& partialpath, KDevelop::ProjectBaseItem* item )
{
    QStringList basePath;
    if( item )
    {
        KDevelop::ProjectModel* model = KDevelop::ICore::self()->projectController()->projectModel();
        basePath = model->pathFromIndex( model->indexFromItem( item ) );
    }
    return basePath + partialpath;
}

inline uint indexForUrl(const KUrl& url)
{
    return IndexedString::indexForString(url.pathOrUrl(KUrl::RemoveTrailingSlash));
}

class ProjectModelPrivate
{
public:
    ProjectModelPrivate( ProjectModel* model ): model( model )
    {
    }
    ProjectBaseItem* rootItem;
    ProjectModel* model;
    ProjectBaseItem* itemFromIndex( const QModelIndex& idx ) {
        if( !idx.isValid() ) {
            return rootItem;
        }
        if( idx.model() != model ) {
            return 0;
        }
        return model->itemFromIndex( idx );
    }

    // a hash of IndexedString::indexForString(url) <-> ProjectBaseItem for fast lookup
    QMultiHash<uint, ProjectBaseItem*> urlLookupTable;
};

class ProjectBaseItemPrivate
{
public:
    ProjectBaseItemPrivate() : project(0), parent(0), row(-1), model(0), m_urlIndex(0) {}
    IProject* project;
    ProjectBaseItem* parent;
    int row;
    QList<ProjectBaseItem*> children;
    QString text;
    ProjectBaseItem::ProjectItemType type;
    Qt::ItemFlags flags;
    ProjectModel* model;
    KUrl m_url;
    uint m_urlIndex;
    QString iconName;
};


ProjectBaseItem::ProjectBaseItem( IProject* project, const QString &name, ProjectBaseItem *parent )
        : d_ptr(new ProjectBaseItemPrivate)
{
    Q_ASSERT(!name.isEmpty() || !parent);
    Q_D(ProjectBaseItem);
    d->project = project;
    d->text = name;
    d->flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    if( parent ) {
        parent->appendRow( this );
    }
}

ProjectBaseItem::~ProjectBaseItem()
{
    Q_D(ProjectBaseItem);

    if (model() && d->m_urlIndex) {
        model()->d->urlLookupTable.remove(d->m_urlIndex, this);
    }

    if( parent() ) {
        parent()->takeRow( d->row );
    } else if( model() ) {
        model()->takeRow( d->row );
    }
    removeRows(0, d->children.size());
    delete d;
}

ProjectBaseItem* ProjectBaseItem::child( int row ) const
{
    Q_D(const ProjectBaseItem);
    if( row < 0 || row >= d->children.length() ) {
        return 0;
    }
    return d->children.at( row );
}

QList< ProjectBaseItem* > ProjectBaseItem::children() const
{
    Q_D(const ProjectBaseItem);
    return d->children;
}

ProjectBaseItem* ProjectBaseItem::takeRow(int row)
{
    Q_D(ProjectBaseItem);
    Q_ASSERT(row >= 0 && row < d->children.size());
    
    if( model() ) {
        QMetaObject::invokeMethod( model(), "rowsAboutToBeRemoved", getConnectionTypeForSignalDelivery( model() ), Q_ARG(QModelIndex, index()), Q_ARG(int, row), Q_ARG(int, row) );
    }
    ProjectBaseItem* olditem = d->children.takeAt( row );
    olditem->d_func()->parent = 0;
    olditem->d_func()->row = -1;
    olditem->setModel( 0 );

    for(int i=row; i<rowCount(); i++) {
        child(i)->d_func()->row--;
        Q_ASSERT(child(i)->d_func()->row==i);
    }
    
    if( model() ) {
        QMetaObject::invokeMethod( model(), "rowsRemoved", getConnectionTypeForSignalDelivery( model() ), Q_ARG(QModelIndex, index()), Q_ARG(int, row), Q_ARG(int, row) );
    }
    return olditem;
}

void ProjectBaseItem::removeRow( int row )
{
    delete takeRow( row );
}

void ProjectBaseItem::removeRows(int row, int count)
{
    if (!count) {
        return;
    }

    Q_D(ProjectBaseItem);
    Q_ASSERT(row >= 0 && row + count <= d->children.size());

    if( model() ) {
        QMetaObject::invokeMethod( model(), "rowsAboutToBeRemoved", getConnectionTypeForSignalDelivery( model() ), Q_ARG(QModelIndex, index()), Q_ARG(int, row), Q_ARG(int, row + count - 1) );
    }

    //NOTE: we unset parent, row and model manually to speed up the deletion
    if (row == 0 && count == d->children.size()) {
        // optimize if we want to delete all
        foreach(ProjectBaseItem* item, d->children) {
            item->d_func()->parent = 0;
            item->d_func()->row = -1;
            item->setModel( 0 );
            delete item;
        }
        d->children.clear();
    } else {
        for (int i = row; i < count; ++i) {
            ProjectBaseItem* item = d->children.at(i);
            item->d_func()->parent = 0;
            item->d_func()->row = -1;
            item->setModel( 0 );
            delete d->children.takeAt( row );
        }
        for(int i = row; i < d->children.size(); ++i) {
            d->children.at(i)->d_func()->row--;
            Q_ASSERT(child(i)->d_func()->row==i);
        }
    }

    if( model() ) {
        QMetaObject::invokeMethod( model(), "rowsRemoved", getConnectionTypeForSignalDelivery( model() ), Q_ARG(QModelIndex, index()), Q_ARG(int, row), Q_ARG(int, row + count - 1) );
    }
}

QModelIndex ProjectBaseItem::index() const
{
    if( model() ) {
        return model()->indexFromItem( this );
    }
    return QModelIndex();
}

int ProjectBaseItem::rowCount() const
{
    Q_D(const ProjectBaseItem);
    return d->children.count();
}

int ProjectBaseItem::type() const
{
    return ProjectBaseItem::BaseItem;
}

ProjectModel* ProjectBaseItem::model() const
{
    Q_D(const ProjectBaseItem);
    return d->model;
}

ProjectBaseItem* ProjectBaseItem::parent() const
{
    Q_D(const ProjectBaseItem);
    if( model() && model()->d->rootItem == d->parent ) {
        return 0;
    }
    return d->parent;
}

int ProjectBaseItem::row() const
{
    Q_D(const ProjectBaseItem);
    return d->row;
}

QString ProjectBaseItem::text() const
{
    Q_D(const ProjectBaseItem);
    if( project() && !parent() ) {
        return project()->name();
    } else {
        return d->text;
    }
}

void ProjectBaseItem::setModel( ProjectModel* model )
{
    Q_D(ProjectBaseItem);

    if (model == d->model) {
        return;
    }

    if (d->model && d->m_urlIndex) {
        d->model->d->urlLookupTable.remove(d->m_urlIndex, this);
    }

    d->model = model;

    if (model && d->m_urlIndex) {
        model->d->urlLookupTable.insert(d->m_urlIndex, this);
    }

    foreach( ProjectBaseItem* item, d->children ) {
        item->setModel( model );
    }
}

void ProjectBaseItem::setRow( int row )
{
    Q_D(ProjectBaseItem);
    d->row = row;
}

void ProjectBaseItem::setText( const QString& text )
{
    Q_ASSERT(!text.isEmpty() || !parent());
    Q_D(ProjectBaseItem);
    d->text = text;
    if( model() ) {
        QMetaObject::invokeMethod( model(), "dataChanged", getConnectionTypeForSignalDelivery( model() ), Q_ARG(QModelIndex, index()), Q_ARG(QModelIndex, index()) );
    }
}

ProjectBaseItem::RenameStatus ProjectBaseItem::rename(const QString& newname)
{
    if (parent()) {
        foreach(ProjectBaseItem* sibling, parent()->children()) {
            if (sibling->text() == newname) {
                return ExistingItemSameName;
            }
        }
    }
    setText( newname );
    return RenameOk;
}

KDevelop::ProjectBaseItem::ProjectItemType baseType( int type )
{
    if( type == KDevelop::ProjectBaseItem::Folder || type == KDevelop::ProjectBaseItem::BuildFolder )
        return KDevelop::ProjectBaseItem::Folder;
    if( type == KDevelop::ProjectBaseItem::Target || type == KDevelop::ProjectBaseItem::ExecutableTarget
        || type == KDevelop::ProjectBaseItem::LibraryTarget)
        return KDevelop::ProjectBaseItem::Target;

    return static_cast<KDevelop::ProjectBaseItem::ProjectItemType>( type );
}

bool ProjectBaseItem::lessThan( const KDevelop::ProjectBaseItem* item ) const
{
    if(item->type() >= KDevelop::ProjectBaseItem::CustomProjectItemType ) {
        // For custom types we want to make sure that if they override lessThan, then we
        // prefer their lessThan implementation
        return !item->lessThan( this );
    }
    KDevelop::ProjectBaseItem::ProjectItemType leftType=baseType(type()), rightType=baseType(item->type());
    if(leftType==rightType)
    {
        if(leftType==KDevelop::ProjectBaseItem::File)
        {
            return file()->fileName().compare(item->file()->fileName(), Qt::CaseInsensitive) < 0;
        }
        return this->text()<item->text();
    }
    else
    {
        return leftType<rightType;
    }

    return false;
}

bool ProjectBaseItem::urlLessThan(ProjectBaseItem* item1, ProjectBaseItem* item2)
{
    return item1->url().path() < item2->url().path();
}

IProject* ProjectBaseItem::project() const
{
    Q_D(const ProjectBaseItem);
    return d->project;
}

void ProjectBaseItem::appendRow( ProjectBaseItem* item )
{
    Q_D(ProjectBaseItem);
    if( !item ) {
        return;
    }
    if( item->parent() ) {
        // Proper way is to first removeRow() on the original parent, then appendRow on this one
        kWarning() << "Ignoring double insertion of item" << item;
        return;
    }
    // this is too slow... O(n) and thankfully not a problem anyways
//     Q_ASSERT(!d->children.contains(item));
    int startrow,endrow;
    if( model() ) {
        startrow = endrow = d->children.count();
        QMetaObject::invokeMethod( model(), "rowsAboutToBeInserted", getConnectionTypeForSignalDelivery( model() ), Q_ARG(QModelIndex, index()), Q_ARG(int, startrow), Q_ARG(int, endrow) );
    }
    d->children.append( item );
    item->setRow( d->children.count() - 1 );
    item->d_func()->parent = this;
    item->setModel( model() );
    if( model() ) {
        QMetaObject::invokeMethod( model(), "rowsInserted", getConnectionTypeForSignalDelivery( model() ), Q_ARG( QModelIndex, index() ), Q_ARG( int, startrow ), Q_ARG( int, endrow ) );
    }
}

KUrl ProjectBaseItem::url( ) const
{
    Q_D(const ProjectBaseItem);
    return d->m_url;
}

QString ProjectBaseItem::baseName() const
{
    return text();
}

void ProjectBaseItem::setUrl( const KUrl& url )
{
    Q_D(ProjectBaseItem);

    if (model() && d->m_urlIndex) {
        model()->d->urlLookupTable.remove(d->m_urlIndex, this);
    }

    d->m_url = url;
    d->m_urlIndex = indexForUrl(url);
    const QString baseName = url.fileName();
    setText( baseName );

    if (model() && d->m_urlIndex) {
        model()->d->urlLookupTable.insert(d->m_urlIndex, this);
    }
}

Qt::ItemFlags ProjectBaseItem::flags()
{
    Q_D(ProjectBaseItem);
    return d->flags;
}

Qt::DropActions ProjectModel::supportedDropActions() const
{
    return (Qt::DropActions)(Qt::MoveAction);
}

void ProjectBaseItem::setFlags(Qt::ItemFlags flags)
{
    Q_D(ProjectBaseItem);
    d->flags = flags;
    if(d->model)
        d->model->dataChanged(index(), index());
}

QString ProjectBaseItem::iconName() const
{
    return "";
}

ProjectFolderItem *ProjectBaseItem::folder() const
{
    return 0;
}

ProjectTargetItem *ProjectBaseItem::target() const
{
    return 0;
}

ProjectExecutableTargetItem *ProjectBaseItem::executable() const
{
    return 0;
}

ProjectFileItem *ProjectBaseItem::file() const
{
    return 0;
}

QList<ProjectFolderItem*> ProjectBaseItem::folderList() const
{
    QList<ProjectFolderItem*> lst;
    for ( int i = 0; i < rowCount(); ++i )
    {
        ProjectBaseItem* item = child( i );
        if ( item->type() == Folder || item->type() == BuildFolder )
        {
            ProjectFolderItem *kdevitem = dynamic_cast<ProjectFolderItem*>( item );
            if ( kdevitem )
                lst.append( kdevitem );
        }
    }

    return lst;
}

QList<ProjectTargetItem*> ProjectBaseItem::targetList() const
{
    QList<ProjectTargetItem*> lst;
    for ( int i = 0; i < rowCount(); ++i )
    {
        ProjectBaseItem* item = child( i );
        
        if ( item->type() == Target || item->type() == LibraryTarget || item->type() == ExecutableTarget )
        {
            ProjectTargetItem *kdevitem = dynamic_cast<ProjectTargetItem*>( item );
            if ( kdevitem )
                lst.append( kdevitem );
        }
    }

    return lst;
}

QList<ProjectFileItem*> ProjectBaseItem::fileList() const
{
    QList<ProjectFileItem*> lst;
    for ( int i = 0; i < rowCount(); ++i )
    {
        ProjectBaseItem* item = child( i );
        Q_ASSERT(item);
        if ( item && item->type() == File )
        {
            ProjectFileItem *kdevitem = dynamic_cast<ProjectFileItem*>( item );
            if ( kdevitem )
                lst.append( kdevitem );
        }

    }
    return lst;
}

void ProjectModel::resetModel()
{
    reset();
}

void ProjectModel::clear()
{
    d->rootItem->removeRows(0, d->rootItem->rowCount());
}


ProjectFolderItem::ProjectFolderItem( IProject* project, const KUrl & dir, ProjectBaseItem * parent )
        : ProjectBaseItem( project, dir.fileName(), parent )
{
    setFlags(flags() | Qt::ItemIsDropEnabled);
    if (project && project->folder() != dir)
        setFlags(flags() | Qt::ItemIsDragEnabled);
    setUrl( dir );
}

ProjectFolderItem::~ProjectFolderItem()
{
}

void ProjectFolderItem::setUrl( const KUrl& url )
{
    KUrl copy(url);
    copy.adjustPath(KUrl::AddTrailingSlash);
    ProjectBaseItem::setUrl(copy);
    
    propagateRename(url);
}

ProjectFolderItem *ProjectFolderItem::folder() const
{
    return const_cast<ProjectFolderItem*>(this);
}

int ProjectFolderItem::type() const
{
    return ProjectBaseItem::Folder;
}

QString ProjectFolderItem::folderName() const
{
    return baseName();
}

void ProjectFolderItem::propagateRename(const KUrl& newBase) const
{
    KUrl url = newBase;
    url.addPath("dummy");
    foreach( KDevelop::ProjectBaseItem* child, children() )
    {
        url.setFileName( child->text() );
        child->setUrl( url );
        
        const ProjectFolderItem* folder = child->folder();
        if ( folder ) {
            folder->propagateRename( url );
        }
    }
}

ProjectBaseItem::RenameStatus ProjectFolderItem::rename(const QString& newname)
{
    if (newname.contains('/'))
        return ProjectBaseItem::InvalidNewName;

    //TODO: Same as ProjectFileItem, so should be shared somehow
    KUrl dest = url().upUrl();
    dest.addPath(newname);

    // url() may contain a trailing slash, try to ignore that
    if (url().equals(dest, KUrl::CompareWithoutTrailingSlash)) {
        kDebug() << "Source and destination folder equal, no rename needed";
        return ProjectBaseItem::RenameOk;
    }

    KIO::UDSEntry entry;
    //There exists a file with that name?
    if( !KIO::NetAccess::stat(dest, entry, 0) )
    {
        if( !project() || !project()->projectFileManager() )
        {
            return ProjectBaseItem::rename(newname);
        }
        else if( project()->projectFileManager()->renameFolder(this, dest) )
        {
            return ProjectBaseItem::RenameOk;
        }
        else
        {
            return ProjectBaseItem::ProjectManagerRenameFailed;
        }
    } else
    {
        return ProjectBaseItem::ExistingItemSameName;
    }
}

bool ProjectFolderItem::hasFileOrFolder(const QString& name) const
{
    foreach ( ProjectBaseItem* item, children() )
    {
        if ( (item->type() == Folder || item->type() == File || item->type() == BuildFolder)
             && name == item->baseName() )
        {
            return true;
        }
    }
    return false;
}

bool ProjectBaseItem::isProjectRoot() const
{
    return parent()==0;
}

ProjectBuildFolderItem::ProjectBuildFolderItem( IProject* project, const KUrl &dir, ProjectBaseItem *parent)
    : ProjectFolderItem( project, dir, parent )
{
}

QString ProjectFolderItem::iconName() const
{
    return "folder";
}

int ProjectBuildFolderItem::type() const
{
    return ProjectBaseItem::BuildFolder;
}

QString ProjectBuildFolderItem::iconName() const
{
    return "folder-development";
}

ProjectFileItem::ProjectFileItem( IProject* project, const KUrl & file, ProjectBaseItem * parent )
        : ProjectBaseItem( project, file.fileName(), parent )
{
    Q_ASSERT(isValidPath(file));

    setFlags(flags() | Qt::ItemIsDragEnabled);
    setUrl( file );
}

ProjectFileItem::~ProjectFileItem()
{
    if( project() && d_ptr->m_urlIndex ) {
        project()->removeFromFileSet( indexedUrl() );
    }
}

IndexedString ProjectFileItem::indexedUrl() const
{
    return IndexedString::fromIndex( d_ptr->m_urlIndex );
}

ProjectBaseItem::RenameStatus ProjectFileItem::rename(const QString& newname)
{
    if (newname.contains('/'))
        return ProjectBaseItem::InvalidNewName;

    KUrl dest = url().upUrl();
    dest.addPath(newname);

    if (url() == dest) {
        kDebug() << "Source and destination file equal, no rename needed:" << dest;
        // it's fine to just say the rename succeeded here, std::rename has the same behavior
        return ProjectBaseItem::RenameOk;
    }

    KIO::UDSEntry entry;
    //There exists a file with that name?
    if( !KIO::NetAccess::stat(dest, entry, 0) )
    {
        if( !project() || !project()->projectFileManager() )
        {
            return ProjectBaseItem::rename(newname);
        }
        else if( project()->projectFileManager()->renameFile(this, dest) )
        {
            return ProjectBaseItem::RenameOk;
        }
        else
        {
            return ProjectBaseItem::ProjectManagerRenameFailed;
        }
    } else
    {
        return ProjectBaseItem::ExistingItemSameName;
    }
}

QString ProjectFileItem::fileName() const
{
    return baseName();
}

// Maximum length of a string to still consider it as a file extension which we cache
// This has to be a slow value, so that we don't fill our file extension cache with crap
static const int maximumCacheExtensionLength = 3;

bool isNumeric(const QStringRef& str)
{
    int len = str.length();
    if(len == 0)
        return false;
    for(int a = 0; a < len; ++a)
        if(!str.at(a).isNumber())
            return false;
    return true;
}

class IconNameCache
{
public:
    QString iconNameForUrl(const KUrl& url, const QString& fileName)
    {
        // find icon name based on file extension, if possible
        QString extension;
        int extensionStart = fileName.lastIndexOf(QLatin1Char('.'));
        if( extensionStart != -1 && fileName.length() - extensionStart - 1 <= maximumCacheExtensionLength ) {
            QStringRef extRef = fileName.midRef(extensionStart + 1);
            if( isNumeric(extRef) ) {
                // don't cache numeric extensions
                extRef.clear();
            }
            if( !extRef.isEmpty() ) {
                extension = extRef.toString();
                QMutexLocker lock(&mutex);
                QHash< QString, QString >::const_iterator it = fileExtensionToIcon.constFind( extension );
                if( it != fileExtensionToIcon.constEnd() ) {
                    return *it;
                }
            }
        }

        KMimeType::Ptr mime = KMimeType::findByUrl( url, 0, false, true );
        QMutexLocker lock(&mutex);
        QHash< QString, QString >::const_iterator it = mimeToIcon.constFind( mime->name() );
        QString iconName;
        if ( it == mimeToIcon.constEnd() ) {
            iconName = mime->iconName();
            mimeToIcon.insert(mime->name(), iconName);
        } else {
            iconName = *it;
        }
        if ( !extension.isEmpty() ) {
            fileExtensionToIcon.insert(extension, iconName);
        }
        return iconName;
    }
    QMutex mutex;
    QHash<QString, QString> mimeToIcon;
    QHash<QString, QString> fileExtensionToIcon;
};

K_GLOBAL_STATIC(IconNameCache, s_cache);

QString ProjectFileItem::iconName() const
{
    // think of d_ptr->iconName as mutable, possible since d_ptr is not const
    if (d_ptr->iconName.isEmpty()) {
        // lazy load implementation of icon lookup
        d_ptr->iconName = s_cache->iconNameForUrl( d_ptr->m_url, d_ptr->text );
        // we should always get *some* icon name back
        Q_ASSERT(!d_ptr->iconName.isEmpty());
    }
    return d_ptr->iconName;
}

void ProjectFileItem::setUrl( const KUrl& url )
{
    if (url == d_ptr->m_url) {
        return;
    }

    if( project() && d_ptr->m_urlIndex ) {
        // remove from fileset if we are in there
        project()->removeFromFileSet( indexedUrl() );
    }

    ProjectBaseItem::setUrl( url );

    if( project() && d_ptr->m_urlIndex ) {
        // add to fileset with new url index
        project()->addToFileSet( indexedUrl() );
    }

    // invalidate icon name for future lazy-loaded updated
    d_ptr->iconName.clear();
}

int ProjectFileItem::type() const
{
    return ProjectBaseItem::File;
}

ProjectFileItem *ProjectFileItem::file() const
{
    return const_cast<ProjectFileItem*>( this );
}

ProjectTargetItem::ProjectTargetItem( IProject* project, const QString &name, ProjectBaseItem *parent )
    : ProjectBaseItem( project, name, parent )
{
    setFlags(flags() | Qt::ItemIsDropEnabled);
}

QString ProjectTargetItem::iconName() const
{
    return "system-run";
}

void ProjectTargetItem::setUrl(const KUrl& url)
{
    d_ptr->m_url=url;
}

int ProjectTargetItem::type() const
{
    return ProjectBaseItem::Target;
}

ProjectTargetItem *ProjectTargetItem::target() const
{
    return const_cast<ProjectTargetItem*>( this );
}

ProjectExecutableTargetItem::ProjectExecutableTargetItem( IProject* project, const QString &name, ProjectBaseItem *parent )
    : ProjectTargetItem(project, name, parent)
{
}

ProjectExecutableTargetItem *ProjectExecutableTargetItem::executable() const
{
    return const_cast<ProjectExecutableTargetItem*>( this );
}

int ProjectExecutableTargetItem::type() const
{
    return ProjectBaseItem::ExecutableTarget;
}

ProjectLibraryTargetItem::ProjectLibraryTargetItem( IProject* project, const QString &name, ProjectBaseItem *parent )
    : ProjectTargetItem(project, name, parent)
{}

int ProjectLibraryTargetItem::type() const
{
    return ProjectBaseItem::LibraryTarget;
}

QModelIndex ProjectModel::pathToIndex(const QStringList& tofetch_) const
{
    if(tofetch_.isEmpty())
        return QModelIndex();
    QStringList tofetch(tofetch_);
    if(tofetch.last().isEmpty())
        tofetch.takeLast();

    QModelIndex current=index(0,0, QModelIndex());

    QModelIndex ret;
    for(int a = 0; a < tofetch.size(); ++a)
    {
        const QString& currentName = tofetch[a];

        bool matched = false;
        QModelIndexList l = match(current, Qt::DisplayRole, currentName, -1, Qt::MatchExactly);
        foreach(const QModelIndex& idx, l) {
            //If this is not the last item, only match folders, as there may be targets and folders with the same name
            if(a == tofetch.size()-1 || itemFromIndex(idx)->folder()) {
                ret = idx;
                current = index(0,0, ret);
                matched = true;
                break;
            }
        }
        if(!matched) {
            ret = QModelIndex();
            break;
        }
    }
    Q_ASSERT(!ret.isValid() || data(ret).toString()==tofetch.last());
    return ret;
}

QStringList ProjectModel::pathFromIndex(const QModelIndex& index) const
{
    if (!index.isValid())
        return QStringList();

    QModelIndex idx = index;
    QStringList list;
    do {
        QString t = data(idx, Qt::DisplayRole).toString();
        list.prepend(t);
        QModelIndex parent = idx.parent();
        idx = parent.sibling(parent.row(), index.column());
    } while (idx.isValid());

    return list;
}

int ProjectModel::columnCount( const QModelIndex& ) const
{
    return 1;
}

int ProjectModel::rowCount( const QModelIndex& parent ) const
{
    ProjectBaseItem* item = d->itemFromIndex( parent );
    return item ? item->rowCount() : 0;
}

QModelIndex ProjectModel::parent( const QModelIndex& child ) const
{
    if( child.isValid() ) {
        ProjectBaseItem* item = static_cast<ProjectBaseItem*>( child.internalPointer() );
        return indexFromItem( item );
    }
    return QModelIndex();
}

QModelIndex ProjectModel::indexFromItem( const ProjectBaseItem* item ) const
{
    if( item && item->d_func()->parent ) {
        return createIndex( item->row(), 0, item->d_func()->parent );
    }
    return QModelIndex();
}

ProjectBaseItem* ProjectModel::itemFromIndex( const QModelIndex& index ) const
{
    if( index.row() >= 0 && index.column() == 0  && index.model() == this ) {
        ProjectBaseItem* parent = static_cast<ProjectBaseItem*>( index.internalPointer() );
        if( parent ) {
            return parent->child( index.row() );
        }
    }
    return 0;
}


QVariant ProjectModel::data( const QModelIndex& index, int role ) const
{
    if( ( role == Qt::DisplayRole || role == Qt::ToolTipRole || role == Qt::DecorationRole ) && index.isValid() ) {
        ProjectBaseItem* item = itemFromIndex( index );
        
        if( item ) {
            switch(role) {
                case Qt::DecorationRole:
                    return item->iconName();
                case Qt::ToolTipRole:
                    return item->url().prettyUrl();
                default:
                    return item->text();
            }
        }
    }
    return QVariant();
}

ProjectModel::ProjectModel( QObject *parent )
        : QAbstractItemModel( parent ), d( new ProjectModelPrivate( this ) )
{
    d->rootItem = new ProjectBaseItem( 0, "", 0 );
    d->rootItem->setModel( this );
}

ProjectModel::~ProjectModel()
{
}


ProjectVisitor::ProjectVisitor()
{
}

QModelIndex ProjectModel::index( int row, int column, const QModelIndex& parent ) const
{
    ProjectBaseItem* parentItem = d->itemFromIndex( parent );
    if( parentItem && row >= 0 && row < parentItem->rowCount() && column == 0 ) {
        return createIndex( row, column, parentItem );
    }
    return QModelIndex();
}

void ProjectModel::appendRow( ProjectBaseItem* item )
{
    d->rootItem->appendRow( item );
}

void ProjectModel::removeRow( int row )
{
    d->rootItem->removeRow( row );
}

ProjectBaseItem* ProjectModel::takeRow( int row )
{
    return d->rootItem->takeRow( row );
}

ProjectBaseItem* ProjectModel::itemAt(int row) const
{
    return d->rootItem->child(row);
}

QList< ProjectBaseItem* > ProjectModel::topItems() const
{
    return d->rootItem->children();
}

Qt::ItemFlags ProjectModel::flags(const QModelIndex& index) const
{
    ProjectBaseItem* item = itemFromIndex( index );
    if(item)
        return item->flags();
    else
        return 0;
}

bool ProjectModel::insertColumns(int, int, const QModelIndex&)
{
    // Not supported
    return false;
}

bool ProjectModel::insertRows(int, int, const QModelIndex&)
{
    // Not supported
    return false;
}

bool ProjectModel::setData(const QModelIndex&, const QVariant&, int)
{
    // Not supported
    return false;
}

QList< ProjectBaseItem* > ProjectModel::itemsForUrl ( const KUrl& url ) const
{
    if (!url.isValid()) {
        return QList<ProjectBaseItem*>();
    }

    return d->urlLookupTable.values(indexForUrl(url));
}

ProjectBaseItem* ProjectModel::itemForUrl(const IndexedString& url) const
{
    return d->urlLookupTable.value(url.index());
}

void ProjectVisitor::visit( ProjectModel* model )
{
    foreach( ProjectBaseItem* item, model->topItems() )
    {
        visit( item->project() );
    }
}

void ProjectVisitor::visit ( IProject* prj )
{
    visit( prj->projectItem() );
}

void ProjectVisitor::visit ( ProjectBuildFolderItem* folder )
{
    foreach( ProjectFileItem* item, folder->fileList() )
    {
        visit( item );
    }
    foreach( ProjectTargetItem* item, folder->targetList() )
    {
        if( item->type() == ProjectBaseItem::LibraryTarget )
        {
            visit( dynamic_cast<ProjectLibraryTargetItem*>( item ) );
        } else if( item->type() == ProjectBaseItem::ExecutableTarget )
        {
            visit( dynamic_cast<ProjectExecutableTargetItem*>( item ) );
        }
    }
    foreach( ProjectFolderItem* item, folder->folderList() )
    {
        if( item->type() == ProjectBaseItem::BuildFolder )
        {
            visit( dynamic_cast<ProjectBuildFolderItem*>( item ) );
        } else if( item->type() == ProjectBaseItem::Folder )
        {
            visit( dynamic_cast<ProjectFolderItem*>( item ) );
        }
    }
}


void ProjectVisitor::visit ( ProjectExecutableTargetItem* exec )
{
    foreach( ProjectFileItem* item, exec->fileList() )
    {
        visit( item );
    }
}


void ProjectVisitor::visit ( ProjectFolderItem* folder )
{
    foreach( ProjectFileItem* item, folder->fileList() )
    {
        visit( item );
    }
    foreach( ProjectTargetItem* item, folder->targetList() )
    {
        if( item->type() == ProjectBaseItem::LibraryTarget )
        {
            visit( dynamic_cast<ProjectLibraryTargetItem*>( item ) );
        } else if( item->type() == ProjectBaseItem::ExecutableTarget )
        {
            visit( dynamic_cast<ProjectExecutableTargetItem*>( item ) );
        }
    }
    foreach( ProjectFolderItem* item, folder->folderList() )
    {
        if( item->type() == ProjectBaseItem::BuildFolder )
        {
            visit( dynamic_cast<ProjectBuildFolderItem*>( item ) );
        } else if( item->type() == ProjectBaseItem::Folder )
        {
            visit( dynamic_cast<ProjectFolderItem*>( item ) );
        }
    }
}


void ProjectVisitor::visit ( ProjectFileItem* )
{
}

void ProjectVisitor::visit ( ProjectLibraryTargetItem* lib )
{
    foreach( ProjectFileItem* item, lib->fileList() )
    {
        visit( item );
    }
}

ProjectVisitor::~ProjectVisitor()
{
}


}
#include "projectmodel.moc"
