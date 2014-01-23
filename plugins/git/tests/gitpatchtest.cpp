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

#include <QtTest/QTest>
#include <KTextEditor/Editor>
#include <KTextEditor/EditorChooser>
#include "../gitpatchextractor.h"

class GitPatchTest
    : public QObject
{
Q_OBJECT
private slots:
    void testHunkExtraction();
};

QTEST_MAIN(GitPatchTest)

void GitPatchTest::testHunkExtraction()
{
    KTextEditor::Document* doc = KTextEditor::EditorChooser::editor()->createDocument(this);
    doc->setText(
        "diff --git ...\n"
        "index ....\n"
        "--- ...\n"
        "+++ ...\n"
        "@@ ...\n"
        " this\n"
        "+is\n"
        " a\n"
        "@@ ...\n"
        " test\n"
        "-test2\n"
        " test\n"
    );

    QCOMPARE(GitPatch::extractPatch(doc, doc->documentRange()), doc->text());
    QCOMPARE(GitPatch::extractPatch(doc, KTextEditor::Range(5,0,10,0)), doc->text()); //takes both hunks
    QString secondHunk = doc->text(KTextEditor::Range(0,0, 4,0)) + doc->text(KTextEditor::Range(8,0,12,0));
    QCOMPARE(GitPatch::extractPatch(doc, KTextEditor::Range(9,0,10,0)), secondHunk); //takes second hunk
    QCOMPARE(GitPatch::extractPatch(doc, KTextEditor::Range(10,0,10,0)), secondHunk); //takes second hunk
}

#include "gitpatchtest.moc"
