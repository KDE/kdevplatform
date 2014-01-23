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

#ifndef GITPATCHEXTRACTOR_H
#define GITPATCHEXTRACTOR_H

#include <KTextEditor/Document>
#include <KTextEditor/Range>

namespace GitPatch
{

QString extractPatch(KTextEditor::Document* doc, const KTextEditor::Range &range)
{
    QString ret;
    KTextEditor::Range nr(doc->documentRange());
    bool foundStart = false;
    for(int i = range.start().line()-1; i>=0; --i) {
        const QString line = doc->line(i);
        if(line.size()>0 && line[0].isLetter() && !line.startsWith("index")) {
            ret += doc->text(KTextEditor::Range(i, 0, i+4, 0));
            break;
        } else if(!foundStart && line.startsWith("@@ ")) {
            foundStart = true;
            nr.start() = KTextEditor::Cursor(i, 0);
        }
    }

    for(int i = range.end().line(), e = doc->lines(); i<e; ++i) {
        const QString line = doc->line(i);
        if(line.size()>0 && (line[0].isLetter() || line.startsWith("@@ ")) && !line.startsWith("index")) {
            nr.end() = KTextEditor::Cursor(i-1, 0);
            break;
        }
    }
    ret += doc->text(nr);
    return ret;
}

}

#endif
