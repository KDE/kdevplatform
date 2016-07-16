/*
 * KDevelop Problem Reporter
 *
 * Copyright 2008 Hamish Rodda <rodda@kde.org>
 * Copyright 2008-2009 David Nolden <david.nolden.kdevelop@art-master.de>
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

#include "problemhighlighter.h"

#include <KTextEditor/Document>
#include <KTextEditor/MarkInterface>
#include <ktexteditor/view.h>
#include <ktexteditor/movinginterface.h>

#include <serialization/indexedstring.h>
#include <language/duchain/navigation/abstractnavigationwidget.h>
#include <language/util/navigationtooltip.h>
#include <interfaces/icore.h>
#include <interfaces/ilanguagecontroller.h>
#include <interfaces/icompletionsettings.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/duchainutils.h>
#include <language/duchain/topducontext.h>
#include <language/duchain/navigation/problemnavigationcontext.h>
#include <util/texteditorhelpers.h>

#include <kcolorscheme.h>

#include <shell/problem.h>

using namespace KTextEditor;
using namespace KDevelop;

namespace
{

QColor colorForSeverity(IProblem::Severity severity)
{
    KColorScheme scheme(QPalette::Active);
    switch (severity) {
    case IProblem::Error:
        return scheme.foreground(KColorScheme::NegativeText).color();
    case IProblem::Warning:
        return scheme.foreground(KColorScheme::NeutralText).color();
    case IProblem::Hint:
    default:
        return scheme.foreground(KColorScheme::PositiveText).color();
    }
}
}

ProblemHighlighter::ProblemHighlighter(KTextEditor::Document* document)
    : m_document(document)
{
    Q_ASSERT(m_document);

    connect(ICore::self()->languageController()->completionSettings(), &ICompletionSettings::settingsChanged, this,
            &ProblemHighlighter::settingsChanged);
    connect(m_document.data(), &Document::aboutToReload, this, &ProblemHighlighter::clearProblems);
    if (qobject_cast<MovingInterface*>(m_document)) {
        // can't use new signal/slot syntax here, MovingInterface is not a QObject
        connect(m_document, SIGNAL(aboutToInvalidateMovingInterfaceContent(KTextEditor::Document*)), this,
                SLOT(clearProblems()));
    }
    connect(m_document, SIGNAL(aboutToRemoveText(KTextEditor::Range)), this,
            SLOT(aboutToRemoveText(KTextEditor::Range)));
}

void ProblemHighlighter::settingsChanged()
{
    // Re-highlight
    setProblems(m_problems);
}

ProblemHighlighter::~ProblemHighlighter()
{
    if (m_topHLRanges.isEmpty() || !m_document)
        return;

    qDeleteAll(m_topHLRanges);
}

void ProblemHighlighter::setProblems(const QVector<IProblem::Ptr>& problems)
{
    if (!m_document)
        return;

    const bool hadProblems = !m_problems.isEmpty();
    m_problems = problems;

    qDeleteAll(m_topHLRanges);
    m_topHLRanges.clear();
    m_problemsForRanges.clear();

    IndexedString url(m_document->url());

    /// TODO: create a better MarkInterface that makes it possible to add the marks to the scrollbar
    ///      but having no background.
    ///      also make it nicer together with other plugins, this would currently fail with
    ///      this method...
    const uint errorMarkType = KTextEditor::MarkInterface::Error;
    const uint warningMarkType = KTextEditor::MarkInterface::Warning;
    KTextEditor::MarkInterface* markIface = dynamic_cast<KTextEditor::MarkInterface*>(m_document.data());
    if (markIface && hadProblems) {
        // clear previously added marks
        foreach (KTextEditor::Mark* mark, markIface->marks()) {
            if (mark->type == errorMarkType || mark->type == warningMarkType) {
                markIface->removeMark(mark->line, mark->type);
            }
        }
    }

    if (problems.isEmpty()) {
        return;
    }

    DUChainReadLocker lock;

    TopDUContext* top = DUChainUtils::standardContextForUrl(m_document->url());

    KTextEditor::MovingInterface* iface = dynamic_cast<KTextEditor::MovingInterface*>(m_document.data());
    Q_ASSERT(iface);

    foreach (const IProblem::Ptr& problem, problems) {
        if (problem->finalLocation().document != url || !problem->finalLocation().isValid())
            continue;

        KTextEditor::Range range;
        if (top)
            range = top->transformFromLocalRevision(RangeInRevision::castFromSimpleRange(problem->finalLocation()));
        else
            range = problem->finalLocation();

        if (range.end().line() >= m_document->lines())
            range.end() = KTextEditor::Cursor(m_document->endOfLine(m_document->lines() - 1));

        if (range.isEmpty()) {
            range.setEnd(range.end() + KTextEditor::Cursor(0, 1));
        }

        KTextEditor::MovingRange* problemRange = iface->newMovingRange(range);

        m_problemsForRanges.insert(problemRange, problem);
        m_topHLRanges.append(problemRange);

        if (problem->source() != IProblem::ToDo
            && (problem->severity() != IProblem::Hint
                || ICore::self()->languageController()->completionSettings()->highlightSemanticProblems())) {
            KTextEditor::Attribute::Ptr attribute(new KTextEditor::Attribute());
            attribute->setUnderlineStyle(QTextCharFormat::WaveUnderline);
            attribute->setUnderlineColor(colorForSeverity(problem->severity()));
            problemRange->setAttribute(attribute);
        }

        if (markIface && ICore::self()->languageController()->completionSettings()->highlightProblematicLines()) {
            uint mark;
            if (problem->severity() == IProblem::Error) {
                mark = errorMarkType;
            } else if (problem->severity() == IProblem::Warning) {
                mark = warningMarkType;
            } else {
                continue;
            }
            markIface->addMark(problem->finalLocation().start().line(), mark);
        }
    }
}

void ProblemHighlighter::aboutToRemoveText(const KTextEditor::Range& range)
{
    if (range.onSingleLine()) { // no need to optimize this
        return;
    }

    QList<MovingRange*>::iterator it = m_topHLRanges.begin();
    while (it != m_topHLRanges.end()) {
        if (range.contains((*it)->toRange())) {
            m_problemsForRanges.remove(*it);
            delete (*it);
            it = m_topHLRanges.erase(it);
        } else {
            ++it;
        }
    }
}

void ProblemHighlighter::clearProblems()
{
    setProblems({});
}
