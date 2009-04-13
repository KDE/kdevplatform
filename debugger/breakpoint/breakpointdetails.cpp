/*
 * This file is part of KDevelop
 *
 * Copyright 2008 Vladimir Prus <ghost@cs.msu.su>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
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

#include "breakpointdetails.h"

#include <QLineEdit>
#include <QVBoxLayout>
#include <QLabel>
#include <QGridLayout>

#include <KLocalizedString>

#include "../interfaces/ibreakpoint.h"
#include <QWhatsThis>

using namespace KDevelop;

class SmallLineEdit : public QLineEdit
{
public:
    SmallLineEdit(QWidget *parent)
    : QLineEdit(parent)
    {}

    QSize sizeHint() const
    {
        QSize s = QLineEdit::sizeHint();
        int width = QFontMetrics(font()).width("99");
        return QSize(width, s.height());
    }
};


BreakpointDetails::BreakpointDetails(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(11, 0, 0, 11);

    status_ = new QLabel(this);
    status_->setText(i18n("Breakpoint is active"));
    status_->hide();
    layout->addWidget(status_);

    QGridLayout* hitsLayout = new QGridLayout();
    layout->addLayout(hitsLayout);

    hitsLayout->setContentsMargins(0, 0, 0, 0);

    hits_ = new QLabel(i18n("Not hit yet"), this);
    hitsLayout->addWidget(hits_, 0, 0, 1, 3);

    QFrame* frame = new QFrame(this);
    frame->setFrameShape(QFrame::HLine);
    hitsLayout->addWidget(frame, 1, 0, 1, 3);

    QLabel *l2 = new QLabel(i18n("Ignore"), this);
    hitsLayout->addWidget(l2, 2, 0);

    ignore_ = new SmallLineEdit(this);
    hitsLayout->addWidget(ignore_, 2, 1);

    QLabel *l3 = new QLabel(i18n("next hits"), this);
    hitsLayout->addWidget(l3, 2, 2);


    layout->addStretch();
}


void BreakpointDetails::setItem(IBreakpoint *b)
{
    if (!b)
    {
        status_->hide();
        hits_->setEnabled(false);
        ignore_->setEnabled(false);
        return;
    }

    status_->show();
    hits_->setEnabled(true);
    ignore_->setEnabled(true);

    if (b->pending())
        status_->setText(i18n("Breakpoint is %1",QString("<a href=\"pending\">pending</a>")));
    else if (b->dirty())
        status_->setText(i18n("Breakpoint is %1",QString("<a href=\"dirty\">dirty</a>")));
    else
        status_->setText("Breakpoint is active");

    if (b->hitCount())
        hits_->setText(i18np("Hit %1 time", "Hit %1 times", b->hitCount()));
    else
        hits_->setText(i18n("Not hit yet"));

    connect(status_, SIGNAL(linkActivated(const QString&)),
            this, SLOT(showExplanation(const QString&)));
}

void BreakpointDetails::showExplanation(const QString& link)
{
    QPoint pos = status_->mapToGlobal(status_->geometry().topLeft());
    if (link == "pending")
    {
        QWhatsThis::showText(pos,
                                i18n("<b>Breakpoint is pending</b>"
                                "<p>Pending breakpoints are those that are "
                                "communucated to GDB, but which are not yet "
                                "installed in the target, because GDB cannot "
                                "find the function or the file the breakpoint "
                                "refers to. The most common case is a breakpoint "
                                "in a shared library: GDB will insert this "
                                "breakpoint only when the library is loaded."),
                                status_);
    }
    else if (link == "dirty")
    {
        QWhatsThis::showText(pos,
                                i18n("<b>Breakpoint is dirty</b>"
                                "<p>The breakpoint is not yet communicated "
                                "to GDB."),
                                status_);
    }
}

#include "breakpointdetails.moc"
