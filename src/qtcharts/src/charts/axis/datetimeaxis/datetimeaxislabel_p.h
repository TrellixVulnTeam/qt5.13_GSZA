/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Charts module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL$
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
** General Public License version 3 or (at your option) any later version
** approved by the KDE Free Qt Foundation. The licenses are as published by
** the Free Software Foundation and appearing in the file LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

//  W A R N I N G
//  -------------
//
// This file is not part of the Qt Chart API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.

#ifndef DATETIMEAXISLABEL_H
#define DATETIMEAXISLABEL_H

#include <private/editableaxislabel_p.h>

#include <QtCore/qdatetime.h>

QT_CHARTS_BEGIN_NAMESPACE

class QT_CHARTS_PRIVATE_EXPORT DateTimeAxisLabel : public EditableAxisLabel
{
    Q_OBJECT
public:
    DateTimeAxisLabel(QGraphicsItem *parent = nullptr);

    void keyPressEvent(QKeyEvent *event);

    QDateTime value() const;
    void setValue(const QDateTime &value);
    void setFormat(const QString &format);

private:
    QDateTime m_dateTime;
    QDateTime m_dateTimeBeforeEdit;
    QString m_format;

    void setInitialEditValue() override;
    void finishEditing() override;
    void resetBeforeEditValue() override;

Q_SIGNALS:
    void dateTimeChanged(const QDateTime &oldDateTime, const QDateTime &newDateTime);
};

QT_CHARTS_END_NAMESPACE

#endif // VALUEAXISLABEL_H
