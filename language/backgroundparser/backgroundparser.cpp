/*
 * This file is part of KDevelop
 *
 * Copyright 2006 Adam Treat <treat@kde.org>
 * Copyright 2007 Kris Wong <kris.p.wong@gmail.com>
 * Copyright 2007-2008 David Nolden <david.nolden.kdevelop@art-master.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
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

#include "backgroundparser.h"

#include <QCoreApplication>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QTimer>
#include <QThread>

#include <KConfigGroup>
#include <KSharedConfig>
#include <KLocalizedString>

#include <ktexteditor/document.h>

#include <ThreadWeaver/State>
#include <ThreadWeaver/ThreadWeaver>
#include <ThreadWeaver/DebuggingAids>

#include <interfaces/icore.h>
#include <interfaces/idocumentcontroller.h>
#include <interfaces/ilanguagecontroller.h>
#include <interfaces/ilanguagesupport.h>
#include <interfaces/isession.h>
#include <interfaces/iproject.h>
#include <interfaces/iprojectcontroller.h>

#include "util/debug.h"

#include "parsejob.h"
#include <editor/modificationrevisionset.h>

using namespace KDevelop;

namespace {

const bool separateThreadForHighPriority = true;

/**
 * Elides string in @p path, e.g. "VEEERY/LONG/PATH" -> ".../LONG/PATH"
 * - probably much faster than QFontMetrics::elidedText()
 * - we dont need a widget context
 * - takes path separators into account
 *
 * @p width Maximum number of characters
 *
 * TODO: Move to kdevutil?
 */
QString elidedPathLeft(const QString& path, int width)
{
    static const QChar separator = QDir::separator();
    static const QString placeholder = QStringLiteral("...");

    if (path.size() <= width) {
        return path;
    }

    int start = (path.size() - width) + placeholder.size();
    int pos = path.indexOf(separator, start);
    if (pos == -1) {
        pos = start; // no separator => just cut off the path at the beginning
    }
    Q_ASSERT(path.size() - pos >= 0 && path.size() - pos <= width);

    QStringRef elidedText = path.rightRef(path.size() - pos);
    QString result = placeholder;
    result.append(elidedText);
    return result;
}

/**
 * @return true if @p url is non-empty, valid and has a clean path, false otherwise.
 */
inline bool isValidURL(const IndexedString& url)
{
    if (url.isEmpty()) {
        return false;
    }
    QUrl original = url.toUrl();
    if (!original.isValid() || original.isRelative() || (original.fileName().isEmpty() && original.isLocalFile())) {
        qCWarning(LANGUAGE) << "INVALID URL ENCOUNTERED:" << url << original;
        return false;
    }
    QUrl cleaned = original.adjusted(QUrl::NormalizePathSegments);
    return original == cleaned;
}

}

struct DocumentParseTarget
{
    QPointer<QObject> notifyWhenReady;
    int priority;
    TopDUContext::Features features;
    ParseJob::SequentialProcessingFlags sequentialProcessingFlags;

    bool operator==(const DocumentParseTarget& rhs) const
    {
        return notifyWhenReady == rhs.notifyWhenReady
            && priority == rhs.priority
            && features == rhs.features;
    }
};

inline uint qHash(const DocumentParseTarget& target)
{
    return target.features * 7 + target.priority * 13 + target.sequentialProcessingFlags * 17
                               + reinterpret_cast<size_t>(target.notifyWhenReady.data());
};

struct DocumentParsePlan
{
    QSet<DocumentParseTarget> targets;

    ParseJob::SequentialProcessingFlags sequentialProcessingFlags() const
    {
        //Pick the strictest possible flags
        ParseJob::SequentialProcessingFlags ret = ParseJob::IgnoresSequentialProcessing;
        foreach(const DocumentParseTarget &target, targets) {
            ret |= target.sequentialProcessingFlags;
        }
        return ret;
    }

    int priority() const
    {
        //Pick the best priority
        int ret = BackgroundParser::WorstPriority;
        foreach(const DocumentParseTarget &target, targets) {
            if(target.priority < ret) {
                ret = target.priority;
            }
        }
        return ret;
    }

    TopDUContext::Features features() const
    {
        //Pick the best features
        TopDUContext::Features ret = (TopDUContext::Features)0;
        foreach(const DocumentParseTarget &target, targets) {
            ret = (TopDUContext::Features) (ret | target.features);
        }
        return ret;
    }

    QList<QPointer<QObject> > notifyWhenReady() const
    {
        QList<QPointer<QObject> > ret;

        foreach(const DocumentParseTarget &target, targets) {
            if(target.notifyWhenReady)
                ret << target.notifyWhenReady;
        }

        return ret;
    }
};

Q_DECLARE_TYPEINFO(DocumentParseTarget, Q_MOVABLE_TYPE);
Q_DECLARE_TYPEINFO(DocumentParsePlan, Q_MOVABLE_TYPE);

class KDevelop::BackgroundParserPrivate
{
public:
    BackgroundParserPrivate(BackgroundParser *parser, ILanguageController *languageController)
        :m_parser(parser), m_languageController(languageController), m_shuttingDown(false), m_mutex(QMutex::Recursive)
    {
        parser->d = this; //Set this so we can safely call back BackgroundParser from within loadSettings()

        m_timer.setSingleShot(true);
        m_progressTimer.setSingleShot(true);
        m_progressTimer.setInterval(500);

        ThreadWeaver::setDebugLevel(true, 1);

        QObject::connect(&m_timer, &QTimer::timeout, m_parser, &BackgroundParser::parseDocuments);
        QObject::connect(&m_progressTimer, &QTimer::timeout, m_parser, &BackgroundParser::updateProgressBar);
    }

    void startTimerThreadSafe(int delay) {
        QMetaObject::invokeMethod(m_parser, "startTimer", Qt::QueuedConnection, Q_ARG(int, delay));
    }

    ~BackgroundParserPrivate()
    {
        m_weaver.resume();
        m_weaver.finish();
    }

    // Non-mutex guarded functions, only call with m_mutex acquired.

    int currentBestRunningPriority() const
    {
        int bestRunningPriority = BackgroundParser::WorstPriority;
        for (const auto* decorator : m_parseJobs) {
            const ParseJob* parseJob = dynamic_cast<const ParseJob*>(decorator->job());
            Q_ASSERT(parseJob);
            if (parseJob->respectsSequentialProcessing() && parseJob->parsePriority() < bestRunningPriority) {
                bestRunningPriority = parseJob->parsePriority();
            }
        }
        return bestRunningPriority;
    }

    IndexedString nextDocumentToParse() const
    {
        // Before starting a new job, first wait for all higher-priority ones to finish.
        // That way, parse job priorities can be used for dependency handling.
        const int bestRunningPriority = currentBestRunningPriority();

        for (auto it1 = m_documentsForPriority.begin();
             it1 != m_documentsForPriority.end(); ++it1 )
        {
            const auto priority = it1.key();
            if(priority > m_neededPriority)
                break; //The priority is not good enough to be processed right now

            if (m_parseJobs.count() >= m_threads && priority > BackgroundParser::NormalPriority && !specialParseJob) {
                break; //The additional parsing thread is reserved for higher priority parsing
            }

            for (const auto& url : it1.value()) {
                // When a document is scheduled for parsing while it is being parsed, it will be parsed
                // again once the job finished, but not now.
                if (m_parseJobs.contains(url)) {
                    continue;
                }

                Q_ASSERT(m_documents.contains(url));
                const auto& parsePlan = m_documents[url];
                // If the current job requires sequential processing, but not all jobs with a better priority have been
                // completed yet, it will not be created now.
                if (    parsePlan.sequentialProcessingFlags() & ParseJob::RequiresSequentialProcessing
                     && parsePlan.priority() > bestRunningPriority )
                {
                    continue;
                }

                return url;
            }
        }
        return {};
    }

    /**
     * Create a single delayed parse job
     *
     * E.g. jobs for documents which have been changed by the user, but also to
     * handle initial startup where we parse all project files.
     */
    void parseDocumentsInternal()
    {
        if(m_shuttingDown)
            return;

        //Only create parse-jobs for up to thread-count * 2 documents, so we don't fill the memory unnecessarily
        if (m_parseJobs.count() >= m_threads+1
            || (m_parseJobs.count() >= m_threads && !separateThreadForHighPriority))
        {
            return;
        }

        const auto& url = nextDocumentToParse();
        if (!url.isEmpty()) {
            qCDebug(LANGUAGE) << "creating parse-job" << url << "new count of active parse-jobs:" << m_parseJobs.count() + 1;

            const QString elidedPathString = elidedPathLeft(url.str(), 70);
            emit m_parser->showMessage(m_parser, i18n("Parsing: %1", elidedPathString));

            ThreadWeaver::QObjectDecorator* decorator = nullptr;
            {
                // copy shared data before unlocking the mutex
                const auto parsePlanConstIt = m_documents.constFind(url);
                const DocumentParsePlan parsePlan = *parsePlanConstIt;

                // we must not lock the mutex while creating a parse job
                // this could in turn lock e.g. the DUChain and then
                // we have a classic lock order inversion (since, usually,
                // we lock first the duchain and then our background parser
                // mutex)
                // see also: https://bugs.kde.org/show_bug.cgi?id=355100
                m_mutex.unlock();
                decorator = createParseJob(url, parsePlan);
                m_mutex.lock();
            }

            // iterator might get invalid during the time we didn't have the lock
            // search again
            const auto parsePlanIt = m_documents.find(url);
            if (parsePlanIt != m_documents.end()) {
                // Remove all mentions of this document.
                for (const auto& target : parsePlanIt->targets) {
                    m_documentsForPriority[target.priority].remove(url);
                }
                m_documents.erase(parsePlanIt);
            } else {
                qWarning(LANGUAGE) << "Document got removed during parse job creation:" << url;
            }

            if (decorator) {
                if(m_parseJobs.count() == m_threads+1 && !specialParseJob)
                    specialParseJob = decorator; //This parse-job is allocated into the reserved thread

                m_parseJobs.insert(url, decorator);
                m_weaver.enqueue(ThreadWeaver::JobPointer(decorator));
            } else {
                --m_maxParseJobs;
            }

            if (!m_documents.isEmpty()) {
                // Only try creating one parse-job at a time, else we might iterate through thousands of files
                // without finding a language-support, and block the UI for a long time.
                QMetaObject::invokeMethod(m_parser, "parseDocuments", Qt::QueuedConnection);
            } else {
                // make sure we cleaned up properly
                // TODO: also empty m_documentsForPriority when m_documents is empty? or do we want to keep capacity?
                Q_ASSERT(std::none_of(m_documentsForPriority.constBegin(), m_documentsForPriority.constEnd(),
                                        [] (const QSet<IndexedString>& docs) {
                                        return !docs.isEmpty();
                                        }));
            }
        }

        m_parser->updateProgressData();
    }

    // NOTE: you must not access any of the data structures that are protected by any of the
    //       background parser internal mutexes in this method
    //       see also: https://bugs.kde.org/show_bug.cgi?id=355100
    ThreadWeaver::QObjectDecorator* createParseJob(const IndexedString& url, const DocumentParsePlan& parsePlan)
    {
        ///FIXME: use IndexedString in the other APIs as well! Esp. for createParseJob!
        QUrl qUrl = url.toUrl();
        const auto languages = m_languageController->languagesForUrl(qUrl);
        const auto& notifyWhenReady = parsePlan.notifyWhenReady();
        for (const auto language : languages) {
            if (!language) {
                qCWarning(LANGUAGE) << "got zero language for" << qUrl;
                continue;
            }

            ParseJob* job = language->createParseJob(url);
            if (!job) {
                continue; // Language part did not produce a valid ParseJob.
            }

            job->setParsePriority(parsePlan.priority());
            job->setMinimumFeatures(parsePlan.features());
            job->setNotifyWhenReady(notifyWhenReady);
            job->setSequentialProcessingFlags(parsePlan.sequentialProcessingFlags());

            ThreadWeaver::QObjectDecorator* decorator = new ThreadWeaver::QObjectDecorator(job);

            QObject::connect(decorator, &ThreadWeaver::QObjectDecorator::done,
                             m_parser, &BackgroundParser::parseComplete);
            QObject::connect(decorator, &ThreadWeaver::QObjectDecorator::failed,
                             m_parser, &BackgroundParser::parseComplete);
            QObject::connect(job, &ParseJob::progress,
                             m_parser, &BackgroundParser::parseProgress, Qt::QueuedConnection);

            // TODO more thinking required here to support multiple parse jobs per url (where multiple language plugins want to parse)
            return decorator;
        }

        if (languages.isEmpty())
            qCDebug(LANGUAGE) << "found no languages for url" << qUrl;
        else
            qCDebug(LANGUAGE) << "could not create parse-job for url" << qUrl;

        //Notify that we failed
        for (const auto& n : notifyWhenReady) {
            if (!n) {
                continue;
            }

            QMetaObject::invokeMethod(n.data(), "updateReady", Qt::QueuedConnection,
                                      Q_ARG(KDevelop::IndexedString, url),
                                      Q_ARG(KDevelop::ReferencedTopDUContext, ReferencedTopDUContext()));
        }

        return nullptr;
    }


    void loadSettings()
    {
        ///@todo re-load settings when they have been changed!
        Q_ASSERT(ICore::self()->activeSession());
        KConfigGroup config(ICore::self()->activeSession()->config(), "Background Parser");

        // stay backwards compatible
        KConfigGroup oldConfig(KSharedConfig::openConfig(), "Background Parser");
#define BACKWARDS_COMPATIBLE_ENTRY(entry, default) \
config.readEntry(entry, oldConfig.readEntry(entry, default))

        m_delay = BACKWARDS_COMPATIBLE_ENTRY("Delay", 500);
        m_timer.setInterval(m_delay);
        m_threads = 0;

        if (qEnvironmentVariableIsSet("KDEV_BACKGROUNDPARSER_MAXTHREADS")) {
            m_parser->setThreadCount(qgetenv("KDEV_BACKGROUNDPARSER_MAXTHREADS").toInt());
        } else {
            m_parser->setThreadCount(BACKWARDS_COMPATIBLE_ENTRY("Number of Threads", QThread::idealThreadCount()));
        }

        resume();

        if (BACKWARDS_COMPATIBLE_ENTRY("Enabled", true)) {
            m_parser->enableProcessing();
        } else {
            m_parser->disableProcessing();
        }
    }

    void suspend()
    {
        qCDebug(LANGUAGE) << "Suspending background parser";

        bool s = m_weaver.state()->stateId() == ThreadWeaver::Suspended ||
                 m_weaver.state()->stateId() == ThreadWeaver::Suspending;

        if (s) { // Already suspending
            qCWarning(LANGUAGE) << "Already suspended or suspending";
            return;
        }

        m_timer.stop();
        m_weaver.suspend();
    }

    void resume()
    {
        bool s = m_weaver.state()->stateId() == ThreadWeaver::Suspended ||
                 m_weaver.state()->stateId() == ThreadWeaver::Suspending;

        if (m_timer.isActive() && !s) { // Not suspending
            return;
        }

        m_timer.start(m_delay);
        m_weaver.resume();
    }

    BackgroundParser *m_parser;
    ILanguageController* m_languageController;

    //Current parse-job that is executed in the additional thread
    QPointer<QObject> specialParseJob;

    QTimer m_timer;
    int m_delay = 500;
    int m_threads = 1;

    bool m_shuttingDown;

    // A list of documents that are planned to be parsed, and their priority
    QHash<IndexedString, DocumentParsePlan > m_documents;
    // The documents ordered by priority
    QMap<int, QSet<IndexedString> > m_documentsForPriority;
    // Currently running parse jobs
    QHash<IndexedString, ThreadWeaver::QObjectDecorator*> m_parseJobs;
    // The url for each managed document. Those may temporarily differ from the real url.
    QHash<KTextEditor::Document*, IndexedString> m_managedTextDocumentUrls;
    // Projects currently in progress of loading
    QSet<IProject*> m_loadingProjects;

    ThreadWeaver::Queue m_weaver;

    // generic high-level mutex
    QMutex m_mutex;

    // local mutex only protecting m_managed
    QMutex m_managedMutex;
    // A change tracker for each managed document
    QHash<IndexedString, DocumentChangeTracker*> m_managed;

    int m_maxParseJobs = 0;
    int m_doneParseJobs = 0;
    QHash<KDevelop::ParseJob*, float> m_jobProgress;
    /// The minimum priority needed for processed jobs
    int m_neededPriority = BackgroundParser::WorstPriority;
    int m_progressMax = 0;
    int m_progressDone = 0;
    QTimer m_progressTimer;
};

BackgroundParser::BackgroundParser(ILanguageController *languageController)
    : QObject(languageController), d(new BackgroundParserPrivate(this, languageController))
{
    Q_ASSERT(ICore::self()->documentController());
    connect(ICore::self()->documentController(), &IDocumentController::documentLoaded, this, &BackgroundParser::documentLoaded);
    connect(ICore::self()->documentController(), &IDocumentController::documentUrlChanged, this, &BackgroundParser::documentUrlChanged);
    connect(ICore::self()->documentController(), &IDocumentController::documentClosed, this, &BackgroundParser::documentClosed);
    connect(ICore::self(), &ICore::aboutToShutdown, this, &BackgroundParser::aboutToQuit);

    bool connected = QObject::connect(ICore::self()->projectController(),
                                      &IProjectController::projectAboutToBeOpened,
                                      this, &BackgroundParser::projectAboutToBeOpened);
    Q_ASSERT(connected);
    connected = QObject::connect(ICore::self()->projectController(),
                                 &IProjectController::projectOpened,
                                 this, &BackgroundParser::projectOpened);
    Q_ASSERT(connected);
    connected = QObject::connect(ICore::self()->projectController(),
                                 &IProjectController::projectOpeningAborted,
                                 this, &BackgroundParser::projectOpeningAborted);
    Q_ASSERT(connected);
    Q_UNUSED(connected);
}

void BackgroundParser::aboutToQuit()
{
    d->m_shuttingDown = true;
}

BackgroundParser::~BackgroundParser()
{
    delete d;
}

QString BackgroundParser::statusName() const
{
    return i18n("Background Parser");
}

void BackgroundParser::loadSettings()
{
    d->loadSettings();
}

void BackgroundParser::parseProgress(KDevelop::ParseJob* job, float value, QString text)
{
    Q_UNUSED(text)
    d->m_jobProgress[job] = value;
    updateProgressData();
}

void BackgroundParser::revertAllRequests(QObject* notifyWhenReady)
{
    QMutexLocker lock(&d->m_mutex);
    for (auto it = d->m_documents.begin(); it != d->m_documents.end(); ) {

        d->m_documentsForPriority[it.value().priority()].remove(it.key());

        foreach ( const DocumentParseTarget& target, (*it).targets ) {
            if ( notifyWhenReady && target.notifyWhenReady.data() == notifyWhenReady ) {
                (*it).targets.remove(target);
            }
        }

        if((*it).targets.isEmpty()) {
            it = d->m_documents.erase(it);
            --d->m_maxParseJobs;

            continue;
        }

        d->m_documentsForPriority[it.value().priority()].insert(it.key());
        ++it;
    }
}

void BackgroundParser::addDocument(const IndexedString& url, TopDUContext::Features features, int priority,
                                   QObject* notifyWhenReady, ParseJob::SequentialProcessingFlags flags, int delay)
{
//     qCDebug(LANGUAGE) << "BackgroundParser::addDocument" << url.toUrl();
    Q_ASSERT(isValidURL(url));
    QMutexLocker lock(&d->m_mutex);
    {
        DocumentParseTarget target;
        target.priority = priority;
        target.features = features;
        target.sequentialProcessingFlags = flags;
        target.notifyWhenReady = QPointer<QObject>(notifyWhenReady);

        auto it = d->m_documents.find(url);

        if (it != d->m_documents.end()) {
            //Update the stored plan

            d->m_documentsForPriority[it.value().priority()].remove(url);
            it.value().targets << target;
            d->m_documentsForPriority[it.value().priority()].insert(url);
        }else{
//             qCDebug(LANGUAGE) << "BackgroundParser::addDocument: queuing" << cleanedUrl;
            d->m_documents[url].targets << target;
            d->m_documentsForPriority[d->m_documents[url].priority()].insert(url);
            ++d->m_maxParseJobs; //So the progress-bar waits for this document
        }

        if ( delay == ILanguageSupport::DefaultDelay ) {
            delay = d->m_delay;
        }
        d->startTimerThreadSafe(delay);
    }
}

void BackgroundParser::removeDocument(const IndexedString& url, QObject* notifyWhenReady)
{
    Q_ASSERT(isValidURL(url));

    QMutexLocker lock(&d->m_mutex);

    if(d->m_documents.contains(url)) {

        d->m_documentsForPriority[d->m_documents[url].priority()].remove(url);

        foreach(const DocumentParseTarget& target, d->m_documents[url].targets) {
            if(target.notifyWhenReady.data() == notifyWhenReady) {
                d->m_documents[url].targets.remove(target);
            }
        }

        if(d->m_documents[url].targets.isEmpty()) {
            d->m_documents.remove(url);
            --d->m_maxParseJobs;
        }else{
            //Insert with an eventually different priority
            d->m_documentsForPriority[d->m_documents[url].priority()].insert(url);
        }
    }
}

void BackgroundParser::parseDocuments()
{
    if (!d->m_loadingProjects.empty()) {
        startTimer(d->m_delay);
        return;
    }
    QMutexLocker lock(&d->m_mutex);

    d->parseDocumentsInternal();
}

void BackgroundParser::parseComplete(const ThreadWeaver::JobPointer& job)
{
    auto decorator = dynamic_cast<ThreadWeaver::QObjectDecorator*>(job.data());
    Q_ASSERT(decorator);
    ParseJob* parseJob = dynamic_cast<ParseJob*>(decorator->job());
    Q_ASSERT(parseJob);
    emit parseJobFinished(parseJob);

    {
        QMutexLocker lock(&d->m_mutex);

        d->m_parseJobs.remove(parseJob->document());

        d->m_jobProgress.remove(parseJob);

        ++d->m_doneParseJobs;
        updateProgressData();
    }

    //Continue creating more parse-jobs
    QMetaObject::invokeMethod(this, "parseDocuments", Qt::QueuedConnection);
}

void BackgroundParser::disableProcessing()
{
    setNeededPriority(BestPriority);
}

void BackgroundParser::enableProcessing()
{
    setNeededPriority(WorstPriority);
}

int BackgroundParser::priorityForDocument(const IndexedString& url) const
{
    Q_ASSERT(isValidURL(url));
    QMutexLocker lock(&d->m_mutex);
    return d->m_documents[url].priority();
}

bool BackgroundParser::isQueued(const IndexedString& url) const
{
    Q_ASSERT(isValidURL(url));
    QMutexLocker lock(&d->m_mutex);
    return d->m_documents.contains(url);
}

int BackgroundParser::queuedCount() const
{
    QMutexLocker lock(&d->m_mutex);
    return d->m_documents.count();
}

bool BackgroundParser::isIdle() const
{
    QMutexLocker lock(&d->m_mutex);
    return d->m_documents.isEmpty() && d->m_weaver.isIdle();
}

void BackgroundParser::setNeededPriority(int priority)
{
    QMutexLocker lock(&d->m_mutex);
    d->m_neededPriority = priority;
    d->startTimerThreadSafe(d->m_delay);
}

void BackgroundParser::abortAllJobs()
{
    qCDebug(LANGUAGE) << "Aborting all parse jobs";

    d->m_weaver.requestAbort();
}

void BackgroundParser::suspend()
{
    d->suspend();

    emit hideProgress(this);
}

void BackgroundParser::resume()
{
    d->resume();
    updateProgressData();
}

void BackgroundParser::updateProgressData()
{
    if (d->m_doneParseJobs >= d->m_maxParseJobs) {
        if(d->m_doneParseJobs > d->m_maxParseJobs) {
            qCDebug(LANGUAGE) << "m_doneParseJobs larger than m_maxParseJobs:" << d->m_doneParseJobs << d->m_maxParseJobs;
        }
        d->m_doneParseJobs = 0;
        d->m_maxParseJobs = 0;
    } else {
        float additionalProgress = 0;
        for (auto it = d->m_jobProgress.constBegin(); it != d->m_jobProgress.constEnd(); ++it) {
            additionalProgress += *it;
        }

        d->m_progressMax = d->m_maxParseJobs*1000;
        d->m_progressDone = (additionalProgress + d->m_doneParseJobs)*1000;

        if (!d->m_progressTimer.isActive()) {
            d->m_progressTimer.start();
        }
    }

    // Cancel progress updating and hide progress-bar when parsing is done.
    if(d->m_doneParseJobs == d->m_maxParseJobs
        || (d->m_neededPriority == BackgroundParser::BestPriority && d->m_weaver.queueLength() == 0))
    {
        if (d->m_progressTimer.isActive()) {
            d->m_progressTimer.stop();
        }
        emit d->m_parser->hideProgress(d->m_parser);
    }
}

ParseJob* BackgroundParser::parseJobForDocument(const IndexedString& document) const
{
    Q_ASSERT(isValidURL(document));

    QMutexLocker lock(&d->m_mutex);
    auto decorator = d->m_parseJobs.value(document);
    return decorator ? dynamic_cast<ParseJob*>(decorator->job()) : nullptr;
}

void BackgroundParser::setThreadCount(int threadCount)
{
    if (d->m_threads != threadCount) {
        d->m_threads = threadCount;
        d->m_weaver.setMaximumNumberOfThreads(d->m_threads+1); //1 Additional thread for high-priority parsing
    }
}

int BackgroundParser::threadCount() const
{
    return d->m_threads;
}

void BackgroundParser::setDelay(int milliseconds)
{
    if (d->m_delay != milliseconds) {
        d->m_delay = milliseconds;
        d->m_timer.setInterval(d->m_delay);
    }
}

QList< IndexedString > BackgroundParser::managedDocuments()
{
    QMutexLocker l(&d->m_managedMutex);
    return d->m_managed.keys();
}


bool BackgroundParser::waitForIdle() const
{
    QList<IndexedString> runningParseJobsUrls;
    forever {
        {
            QMutexLocker lock(&d->m_mutex);
            if (d->m_parseJobs.isEmpty()) {
                qCDebug(LANGUAGE) << "All parse jobs done" << d->m_parseJobs.keys();
                return true;
            }

            if (d->m_parseJobs.size() != runningParseJobsUrls.size()) {
                runningParseJobsUrls = d->m_parseJobs.keys();
                qCDebug(LANGUAGE) << "Waiting for background parser to get in idle state... -- the following parse jobs are still running:" << runningParseJobsUrls;
            }
        }

        QCoreApplication::processEvents();
        QThread::msleep(100);
    }
    return false;
}

DocumentChangeTracker* BackgroundParser::trackerForUrl(const KDevelop::IndexedString& url) const
{
    if (url.isEmpty()) {
        // this happens e.g. when setting the final location of a problem that is not
        // yet associated with a top ctx.
        return nullptr;
    }
    if ( !isValidURL(url) ) {
        qCWarning(LANGUAGE) << "Tracker requested for invalild URL:" << url.toUrl();
    }
    Q_ASSERT(isValidURL(url));

    QMutexLocker l(&d->m_managedMutex);
    return d->m_managed.value(url, nullptr);
}

void BackgroundParser::documentClosed(IDocument* document)
{
    QMutexLocker l(&d->m_mutex);

    if(document->textDocument())
    {
        KTextEditor::Document* textDocument = document->textDocument();

        if(!d->m_managedTextDocumentUrls.contains(textDocument))
            return; // Probably the document had an invalid url, and thus it wasn't added to the background parser

        Q_ASSERT(d->m_managedTextDocumentUrls.contains(textDocument));

        IndexedString url(d->m_managedTextDocumentUrls[textDocument]);
        
        QMutexLocker l2(&d->m_managedMutex);
        Q_ASSERT(d->m_managed.contains(url));

        qCDebug(LANGUAGE) << "removing" << url.str() << "from background parser";
        delete d->m_managed[url];
        d->m_managedTextDocumentUrls.remove(textDocument);
        d->m_managed.remove(url);
    }
}

void BackgroundParser::documentLoaded( IDocument* document )
{
    QMutexLocker l(&d->m_mutex);
    if(document->textDocument() && document->textDocument()->url().isValid())
    {
        KTextEditor::Document* textDocument = document->textDocument();

        IndexedString url(document->url());
        // Some debugging because we had issues with this

        QMutexLocker l2(&d->m_managedMutex);
        if(d->m_managed.contains(url) && d->m_managed[url]->document() == textDocument)
        {
            qCDebug(LANGUAGE) << "Got redundant documentLoaded from" << document->url() << textDocument;
            return;
        }

        qCDebug(LANGUAGE) << "Creating change tracker for " << document->url();


        Q_ASSERT(!d->m_managed.contains(url));
        Q_ASSERT(!d->m_managedTextDocumentUrls.contains(textDocument));

        d->m_managedTextDocumentUrls[textDocument] = url;
        d->m_managed.insert(url, new DocumentChangeTracker(textDocument));
    }else{
        qCDebug(LANGUAGE) << "NOT creating change tracker for" << document->url();
    }
}

void BackgroundParser::documentUrlChanged(IDocument* document)
{
    documentClosed(document);

    // Only call documentLoaded if the file wasn't renamed to a filename that is already tracked.
    if(document->textDocument() && !trackerForUrl(IndexedString(document->textDocument()->url())))
        documentLoaded(document);
}

void BackgroundParser::startTimer(int delay) {
    d->m_timer.start(delay);
}

void BackgroundParser::projectAboutToBeOpened(IProject* project)
{
    d->m_loadingProjects.insert(project);
}

void BackgroundParser::projectOpened(IProject* project)
{
    d->m_loadingProjects.remove(project);
}

void BackgroundParser::projectOpeningAborted(IProject* project)
{
    d->m_loadingProjects.remove(project);
}

void BackgroundParser::updateProgressBar()
{
    emit showProgress(this, 0, d->m_progressMax, d->m_progressDone);
}
