/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
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

#ifndef QDOCCOMMANDLINEPARSER_H
#define QDOCCOMMANDLINEPARSER_H
#include <QtCore/qcommandlineparser.h>
#include "qdocglobals.h"

QT_BEGIN_NAMESPACE

class QDocCommandLineParser : public QCommandLineParser
{
public:
    QDocCommandLineParser();
    void process(const QCoreApplication &app, QDocGlobals &qdocGlobals);

private:
    QCommandLineOption defineOption, dependsOption, highlightingOption;
    QCommandLineOption showInternalOption, redirectDocumentationToDevNullOption;
    QCommandLineOption noExamplesOption, indexDirOption, installDirOption;
    QCommandLineOption obsoleteLinksOption, outputDirOption, outputFormatOption;
    QCommandLineOption noLinkErrorsOption, autoLinkErrorsOption, debugOption;
    QCommandLineOption prepareOption, generateOption, logProgressOption;
    QCommandLineOption singleExecOption, writeQaPagesOption;
    QCommandLineOption includePathOption, includePathSystemOption, frameworkOption;
};

QT_END_NAMESPACE

#endif // QDOCCOMMANDLINEPARSER_H
