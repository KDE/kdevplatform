/*
    Copyright 2009 David Nolden <david.nolden.kdevelop@art-master.de>
    
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

/**
 * This is an internal header
*/

#ifndef KDEVPLATFORM_VCSDIFFPATCHSOURCES_H
#define KDEVPLATFORM_VCSDIFFPATCHSOURCES_H

#include <ktemporaryfile.h>
#include <qtextstream.h>
#include <QSharedPointer>
#include <kdebug.h>
#include <interfaces/ipatchsource.h>
#include "vcs/vcsstatusinfo.h"

#include "../vcsexport.h"

class KTextEdit;
class KComboBox;
namespace KDevelop {
class VcsCommitDialog;
class IBasicVersionControl;
class VcsDiff;
}

class QWidget;

class KDEVPLATFORMVCS_EXPORT VCSDiffUpdater {
public:
    virtual ~VCSDiffUpdater();
    virtual KDevelop::VcsDiff update() const = 0;
    virtual KDevelop::IBasicVersionControl* vcs() const = 0;
    virtual KUrl url() const = 0;
};

class KDEVPLATFORMVCS_EXPORT VCSStandardDiffUpdater : public VCSDiffUpdater {
public:
    VCSStandardDiffUpdater(KDevelop::IBasicVersionControl* vcs, KUrl url);
    virtual ~VCSStandardDiffUpdater();
    virtual KDevelop::VcsDiff update() const;
    virtual KDevelop::IBasicVersionControl* vcs() const { return m_vcs; }
    virtual KUrl url() const { return m_url; }
private:
    KDevelop::IBasicVersionControl* m_vcs;
    KUrl m_url;
};

class KDEVPLATFORMVCS_EXPORT VCSDiffPatchSource : public KDevelop::IPatchSource {
public:
    /// The ownership of the updater is taken
    VCSDiffPatchSource(VCSDiffUpdater* updater);
    VCSDiffPatchSource(const KDevelop::VcsDiff& diff);
    virtual ~VCSDiffPatchSource();
        
    virtual KUrl baseDir() const ;
    
    virtual KUrl file() const ;
    
    virtual QString name() const ;
    
    virtual void update() ;
    
    virtual bool isAlreadyApplied() const { return true; }
    
    QMap<KUrl, KDevelop::VcsStatusInfo::State> additionalSelectableFiles() const ;
    
    KUrl m_base, m_file;
    QString m_name;
    VCSDiffUpdater* m_updater;
    QList<KDevelop::VcsStatusInfo> m_infos;
    QMap<KUrl, KDevelop::VcsStatusInfo::State> m_selectable;
private:
    void updateFromDiff(KDevelop::VcsDiff diff);
};

class KDEVPLATFORMVCS_EXPORT VCSCommitDiffPatchSource : public VCSDiffPatchSource {
    Q_OBJECT
    public:
    /// The ownership of the updater is taken
    VCSCommitDiffPatchSource(VCSDiffUpdater* updater);
    ~VCSCommitDiffPatchSource() ;
    
    QStringList oldMessages() const;
    
    virtual bool canSelectFiles() const ;
    
    virtual QWidget* customWidget() const ;
    
    virtual QString finishReviewCustomText() const ;
    
    virtual bool canCancel() const;
    
    virtual void cancelReview();
    
    virtual bool finishReview(QList< KUrl > selection) ;
    QList<KDevelop::VcsStatusInfo> infos() const { return m_infos; }
Q_SIGNALS:
    void reviewFinished(QString message, QList<KUrl> selection);
    void reviewCancelled(QString message);
public:
    QWeakPointer<QWidget> m_commitMessageWidget;
    QWeakPointer<KTextEdit> m_commitMessageEdit;
    KDevelop::IBasicVersionControl* m_vcs;
    KComboBox* m_oldMessages;
public slots:
    void addMessageToHistory(const QString& message);
    void oldMessageChanged(QString);
};

///Sends the diff to the patch-review plugin.
///Returns whether the diff was shown successfully.
bool KDEVPLATFORMVCS_EXPORT showVcsDiff(KDevelop::IPatchSource* vcsDiff);

#endif // KDEVPLATFORM_VCSDIFFPATCHSOURCES_H
