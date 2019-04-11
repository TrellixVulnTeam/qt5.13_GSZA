/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the tools applications of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

/*
  text.cpp
*/

#include <qregexp.h>
#include "text.h"
#include <stdio.h>

QT_BEGIN_NAMESPACE

Text::Text()
    : first(nullptr), last(nullptr)
{
}

Text::Text(const QString &str)
    : first(nullptr), last(nullptr)
{
    operator<<(str);
}

Text::Text(const Text& text)
    : first(nullptr), last(nullptr)
{
    operator=(text);
}

Text::~Text()
{
    clear();
}

Text& Text::operator=(const Text& text)
{
    if (this != &text) {
        clear();
        operator<<(text);
    }
    return *this;
}

Text& Text::operator<<(Atom::AtomType atomType)
{
    return operator<<(Atom(atomType));
}

Text& Text::operator<<(const QString& string)
{
    return operator<<(Atom(Atom::String, string));
}

Text& Text::operator<<(const Atom& atom)
{
    if (atom.count() < 2) {
        if (first == nullptr) {
            first = new Atom(atom.type(), atom.string());
            last = first;
        }
        else
            last = new Atom(last, atom.type(), atom.string());
    }
    else {
        if (first == nullptr) {
            first = new Atom(atom.type(), atom.string(), atom.string(1));
            last = first;
        }
        else
            last = new Atom(last, atom.type(), atom.string(), atom.string(1));
    }
    return *this;
}

/*!
  Special output operator for LinkAtom. It makes a copy of
  the LinkAtom \a atom and connects the cop;y to the list
  in this Text.
 */
Text& Text::operator<<(const LinkAtom& atom)
{
    if (first == nullptr) {
        first = new LinkAtom(atom);
        last = first;
    }
    else
        last = new LinkAtom(last, atom);
    return *this;
}

Text& Text::operator<<(const Text& text)
{
    const Atom* atom = text.firstAtom();
    while (atom != nullptr) {
        operator<<(*atom);
        atom = atom->next();
    }
    return *this;
}

void Text::stripFirstAtom()
{
    if (first != nullptr) {
        if (first == last)
            last = nullptr;
        Atom* oldFirst = first;
        first = first->next();
        delete oldFirst;
    }
}

void Text::stripLastAtom()
{
    if (last != nullptr) {
        Atom* oldLast = last;
        if (first == last) {
            first = nullptr;
            last = nullptr;
        } else {
            last = first;
            while (last->next() != oldLast)
                last = last->next();
            last->setNext(nullptr);
        }
        delete oldLast;
    }
}

/*!
  This function traverses the atom list of the Text object,
  extracting all the string parts. It concatenates them to
  a result string and returns it.
 */
QString Text::toString() const
{
    QString str;
    const Atom* atom = firstAtom();
    while (atom != nullptr) {
        if (atom->type() == Atom::String ||
            atom->type() == Atom::AutoLink ||
            atom->type() == Atom::C)
            str += atom->string();
        atom = atom->next();
    }
    return str;
}

/*!
  Returns true if this Text contains the substring \a str.
 */
bool Text::contains(const QString &str) const
{
    const Atom* atom = firstAtom();
    while (atom != nullptr) {
        if (atom->type() == Atom::String ||
            atom->type() == Atom::AutoLink ||
            atom->type() == Atom::C)
            if (atom->string().contains(str, Qt::CaseInsensitive))
                return true;
        atom = atom->next();
    }
    return false;
}

Text Text::subText(Atom::AtomType left, Atom::AtomType right, const Atom* from, bool inclusive) const
{
    const Atom* begin = from ? from : firstAtom();
    const Atom* end;

    while (begin != nullptr && begin->type() != left)
        begin = begin->next();
    if (begin != nullptr) {
        if (!inclusive)
            begin = begin->next();
    }

    end = begin;
    while (end != nullptr && end->type() != right)
        end = end->next();
    if (end == nullptr)
        begin = nullptr;
    else if (inclusive)
        end = end->next();
    return subText(begin, end);
}

Text Text::sectionHeading(const Atom* sectionLeft)
{
    if (sectionLeft != nullptr) {
        const Atom* begin = sectionLeft;
        while (begin != nullptr && begin->type() != Atom::SectionHeadingLeft)
            begin = begin->next();
        if (begin != nullptr)
            begin = begin->next();

        const Atom* end = begin;
        while (end != nullptr && end->type() != Atom::SectionHeadingRight)
            end = end->next();

        if (end != nullptr)
            return subText(begin, end);
    }
    return Text();
}

const Atom* Text::sectionHeadingAtom(const Atom* sectionLeft)
{
    if (sectionLeft != nullptr) {
        const Atom* begin = sectionLeft;
        while (begin != nullptr && begin->type() != Atom::SectionHeadingLeft)
            begin = begin->next();
        if (begin != nullptr)
            begin = begin->next();

        return begin;
    }
    return nullptr;
}

void Text::dump() const
{
    const Atom* atom = firstAtom();
    while (atom != nullptr) {
        QString str = atom->string();
        str.replace("\\", "\\\\");
        str.replace("\"", "\\\"");
        str.replace("\n", "\\n");
        str.replace(QRegExp("[^\x20-\x7e]"), "?");
        if (!str.isEmpty())
            str = " \"" + str + QLatin1Char('"');
        fprintf(stderr, "    %-15s%s\n", atom->typeString().toLatin1().data(), str.toLatin1().data());
        atom = atom->next();
    }
}

Text Text::subText(const Atom* begin, const Atom* end)
{
    Text text;
    if (begin != nullptr) {
        while (begin != end) {
            text << *begin;
            begin = begin->next();
        }
    }
    return text;
}

void Text::clear()
{
    while (first != nullptr) {
        Atom* atom = first;
        first = first->next();
        delete atom;
    }
    first = nullptr;
    last = nullptr;
}

int Text::compare(const Text &text1, const Text &text2)
{
    if (text1.isEmpty())
        return text2.isEmpty() ? 0 : -1;
    if (text2.isEmpty())
        return 1;

    const Atom* atom1 = text1.firstAtom();
    const Atom* atom2 = text2.firstAtom();

    for (;;) {
        if (atom1->type() != atom2->type())
            return (int)atom1->type() - (int)atom2->type();
        int cmp = QString::compare(atom1->string(), atom2->string());
        if (cmp != 0)
            return cmp;

        if (atom1 == text1.lastAtom())
            return atom2 == text2.lastAtom() ? 0 : -1;
        if (atom2 == text2.lastAtom())
            return 1;
        atom1 = atom1->next();
        atom2 = atom2->next();
    }
}

QT_END_NAMESPACE