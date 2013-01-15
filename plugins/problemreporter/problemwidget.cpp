/*
 * KDevelop Problem Reporter
 *
 * Copyright (c) 2006-2007 Hamish Rodda <rodda@kde.org>
 * Copyright 2006 Adam Treat <treat@kde.org>
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

#include "problemwidget.h"

#include <QMenu>
#include <QCursor>
#include <QContextMenuEvent>
#include <QSignalMapper>
#include <QHeaderView>

#include <kaction.h>
#include <kactionmenu.h>
#include <klocale.h>
#include <kicon.h>

#include <interfaces/icore.h>
#include <interfaces/idocumentcontroller.h>
#include <interfaces/iassistant.h>
#include <language/duchain/duchain.h>
#include <language/duchain/duchainlock.h>

#include "problemreporterplugin.h"
#include "problemmodel.h"

//#include "modeltest.h"

using namespace KDevelop;

ProblemWidget::ProblemWidget(QWidget* parent, ProblemReporterPlugin* plugin)
    : QTreeView(parent)
    , m_plugin(plugin)
{
    setObjectName("Problem Reporter Tree");
    setWindowTitle(i18n("Problems"));
    setWindowIcon( KIcon("dialog-information") ); ///@todo Use a proper icon
    setRootIsDecorated(true);
    setWhatsThis( i18n( "Problems" ) );

    setModel(m_plugin->getModel());
    header()->setStretchLastSection(false);

    KAction* fullUpdateAction = new KAction(this);
    fullUpdateAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    fullUpdateAction->setText(i18n("Force Full Update"));
    fullUpdateAction->setToolTip(i18nc("@info:tooltip", "Re-parse all watched documents"));
    fullUpdateAction->setIcon(KIcon("view-refresh"));
    connect(fullUpdateAction, SIGNAL(triggered(bool)), model(), SLOT(forceFullUpdate()));
    addAction(fullUpdateAction);

    KAction* showImportsAction = new KAction(this);
    addAction(showImportsAction);
    showImportsAction->setCheckable(true);
    showImportsAction->setChecked(false);
    showImportsAction->setText(i18n("Show Imports"));
    showImportsAction->setToolTip(i18nc("@info:tooltip", "Display problems in imported files"));
    this->model()->setShowImports(false);
    connect(showImportsAction, SIGNAL(triggered(bool)), this->model(), SLOT(setShowImports(bool)));

    KActionMenu* scopeMenu = new KActionMenu(this);
    scopeMenu->setDelayed(false);
    scopeMenu->setText(i18n("Scope"));
    scopeMenu->setToolTip(i18nc("@info:tooltip", "Which files to display the problems for"));

    QActionGroup* scopeActions = new QActionGroup(this);

    KAction* currentDocumentAction = new KAction(this);
    currentDocumentAction->setText(i18n("Current Document"));
    currentDocumentAction->setToolTip(i18nc("@info:tooltip", "Display problems in current document"));

    KAction* openDocumentsAction = new KAction(this);
    openDocumentsAction->setText(i18n("Open documents"));
    openDocumentsAction->setToolTip(i18nc("@info:tooltip", "Display problems in all open documents"));

    KAction* currentProjectAction = new KAction(this);
    currentProjectAction->setText(i18n("Current Project"));
    currentProjectAction->setToolTip(i18nc("@info:tooltip", "Display problems in current project"));

    KAction* allProjectAction = new KAction(this);
    allProjectAction->setText(i18n("All Projects"));
    allProjectAction->setToolTip(i18nc("@info:tooltip", "Display problems in all projects"));

    KAction* scopeActionArray[] = {currentDocumentAction, openDocumentsAction, currentProjectAction, allProjectAction};
    for (int i = 0; i < 4; ++i) {
        scopeActionArray[i]->setCheckable(true);
        scopeActions->addAction(scopeActionArray[i]);
        scopeMenu->addAction(scopeActionArray[i]);
    }
    addAction(scopeMenu);

    currentDocumentAction->setChecked(true);
    model()->setScope(ProblemModel::CurrentDocument);
    QSignalMapper * scopeMapper = new QSignalMapper(this);
    scopeMapper->setMapping(currentDocumentAction, ProblemModel::CurrentDocument);
    scopeMapper->setMapping(openDocumentsAction, ProblemModel::OpenDocuments);
    scopeMapper->setMapping(currentProjectAction, ProblemModel::CurrentProject);
    scopeMapper->setMapping(allProjectAction, ProblemModel::AllProjects);
    connect(currentDocumentAction, SIGNAL(triggered()), scopeMapper, SLOT(map()));
    connect(openDocumentsAction, SIGNAL(triggered()), scopeMapper, SLOT(map()));
    connect(currentProjectAction, SIGNAL(triggered()), scopeMapper, SLOT(map()));
    connect(allProjectAction, SIGNAL(triggered()), scopeMapper, SLOT(map()));
    connect(scopeMapper, SIGNAL(mapped(int)), model(), SLOT(setScope(int)));

    KActionMenu* severityMenu = new KActionMenu(i18n("Severity"), this);
    severityMenu->setDelayed(false);
    severityMenu->setToolTip(i18nc("@info:tooltip", "Select the lowest level of problem severity to be displayed"));
    QActionGroup* severityActions = new QActionGroup(this);

    KAction* errorSeverityAction = new KAction(i18n("Error"), this);
    errorSeverityAction->setToolTip(i18nc("@info:tooltip", "Display only errors"));

    KAction* warningSeverityAction = new KAction(i18n("Warning"), this);
    warningSeverityAction->setToolTip(i18nc("@info:tooltip", "Display errors and warnings"));

    KAction* hintSeverityAction = new KAction(i18n("Hint"), this);
    hintSeverityAction->setToolTip(i18nc("@info:tooltip", "Display errors, warnings and hints"));

    KAction* severityActionArray[] = {errorSeverityAction, warningSeverityAction, hintSeverityAction};
    for (int i = 0; i < 3; ++i) {
        severityActionArray[i]->setCheckable(true);
        severityActions->addAction(severityActionArray[i]);
        severityMenu->addAction(severityActionArray[i]);
    }
    addAction(severityMenu);

    hintSeverityAction->setChecked(true);
    model()->setSeverity(ProblemData::Hint);
    QSignalMapper * severityMapper = new QSignalMapper(this);
    severityMapper->setMapping(errorSeverityAction, ProblemData::Error);
    severityMapper->setMapping(warningSeverityAction, ProblemData::Warning);
    severityMapper->setMapping(hintSeverityAction, ProblemData::Hint);
    connect(errorSeverityAction, SIGNAL(triggered()), severityMapper, SLOT(map()));
    connect(warningSeverityAction, SIGNAL(triggered()), severityMapper, SLOT(map()));
    connect(hintSeverityAction, SIGNAL(triggered()), severityMapper, SLOT(map()));
    connect(severityMapper, SIGNAL(mapped(int)), model(), SLOT(setSeverity(int)));

    connect(this, SIGNAL(activated(QModelIndex)), SLOT(itemActivated(QModelIndex)));
}

ProblemWidget::~ProblemWidget()
{
}

void ProblemWidget::itemActivated(const QModelIndex& index)
{
    if (!index.isValid())
        return;

  KTextEditor::Cursor start;
    KUrl url;

    {
      // TODO: is this really necessary?
      DUChainReadLocker lock(DUChain::lock());
      KDevelop::ProblemPointer problem = model()->problemForIndex(index);
      if (!index.internalPointer()) {
        url = problem->finalLocation().document.toUrl();
        start = problem->finalLocation().start.textCursor();
      }else{
        url = problem->locationStack().at(index.row()).document.toUrl();
        start = problem->locationStack().at(index.row()).textCursor();
      }
    }

    m_plugin->core()->documentController()->openDocument(url, start);
}

void ProblemWidget::resizeColumns()
{
    // Do actual resizing only if the widget is visible and there are not too many items
    const int ResizeRowLimit = 15;
    if (isVisible() && model()->rowCount() > 0 && model()->rowCount() < ResizeRowLimit) {
        const int columnCount = model()->columnCount();
        QVector<int> widthArray(columnCount);
        int totalWidth = 0;
        for (int i = 0; i < columnCount; ++i) {
            widthArray[i] = columnWidth(i);
            totalWidth += widthArray[i];
        }
        for (int i = 0; i < columnCount; ++i) {
            int columnWidthHint = qMax(sizeHintForColumn(i), header()->sectionSizeHint(i));
            if (columnWidthHint - widthArray[i] > 0) {
                if (columnWidthHint - widthArray[i] < width() - totalWidth) { // enough space to resize
                    setColumnWidth(i, columnWidthHint);
                    totalWidth += (columnWidthHint - widthArray[i]);
                } else {
                    setColumnWidth(i, widthArray[i] + width() - totalWidth);
                    break;
                }
            }
        }
    }
}

void ProblemWidget::dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    QTreeView::dataChanged(topLeft, bottomRight);
    resizeColumns();
}

void ProblemWidget::reset()
{
    QTreeView::reset();
    resizeColumns();
}

ProblemModel * ProblemWidget::model() const
{
    return static_cast<ProblemModel*>(QTreeView::model());
}

void ProblemWidget::contextMenuEvent(QContextMenuEvent* event) {
    QModelIndex index = indexAt(event->pos());
    if(index.isValid()) {
        KDevelop::ProblemPointer problem = model()->problemForIndex(index);
        if(problem) {
            KSharedPtr<KDevelop::IAssistant> solution = problem->solutionAssistant();
            QList<QAction*> actions;
            if(solution) {
                foreach(KDevelop::IAssistantAction::Ptr action, solution->actions())
                {
                    actions << action->toKAction();
                    if(!solution->title().isEmpty())
                        actions.back()->setText(solution->title() + ' ' + actions.back()->text());
                }
            }
            if(!actions.isEmpty())
                QMenu::exec(actions, event->globalPos());
        }
    }
}

void ProblemWidget::showEvent(QShowEvent * event)
{
    Q_UNUSED(event)

    for (int i = 0; i < model()->columnCount(); ++i)
        resizeColumnToContents(i);
}

#include "problemwidget.moc"
