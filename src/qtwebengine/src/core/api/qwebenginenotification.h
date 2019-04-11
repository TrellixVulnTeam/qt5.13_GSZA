/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtWebEngine module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QWEBENGINENOTIFICATION_H
#define QWEBENGINENOTIFICATION_H

#include <QtWebEngineCore/qtwebenginecoreglobal.h>

#include <QtCore/QObject>
#include <QtCore/QScopedPointer>
#include <QtCore/QSharedPointer>
#include <QtCore/QUrl>
#include <QtGui/QIcon>

namespace QtWebEngineCore {
class UserNotificationController;
}

QT_BEGIN_NAMESPACE

class QWebEngineNotificationPrivate;

class QWEBENGINECORE_EXPORT QWebEngineNotification : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl origin READ origin CONSTANT FINAL)
    Q_PROPERTY(QIcon icon READ icon CONSTANT FINAL)
    Q_PROPERTY(QString title READ title CONSTANT FINAL)
    Q_PROPERTY(QString message READ message CONSTANT FINAL)
    Q_PROPERTY(QString tag READ tag CONSTANT FINAL)
    Q_PROPERTY(QString language READ language CONSTANT FINAL)
    Q_PROPERTY(Direction direction READ direction CONSTANT FINAL)

public:
    QWebEngineNotification();
    QWebEngineNotification(const QWebEngineNotification &);
    virtual ~QWebEngineNotification();
    const QWebEngineNotification &operator=(const QWebEngineNotification &);

    enum Direction {
        LeftToRight = Qt::LeftToRight,
        RightToLeft = Qt::RightToLeft,
        DirectionAuto = Qt::LayoutDirectionAuto
    };
    Q_ENUM(Direction)

    bool matches(const QWebEngineNotification &) const;

    QUrl origin() const;
    QIcon icon() const;
    QString title() const;
    QString message() const;
    QString tag() const;
    QString language() const;
    Direction direction() const;

    bool isNull() const;

public Q_SLOTS:
    void show() const;
    void click() const;
    void close() const;

Q_SIGNALS:
    void closed();

private:
    QWebEngineNotification(const QSharedPointer<QtWebEngineCore::UserNotificationController> &);
    Q_DECLARE_PRIVATE(QWebEngineNotification)
    QScopedPointer<QWebEngineNotificationPrivate> d_ptr;
    friend class QQuickWebEngineProfilePrivate;
    friend class QWebEngineProfilePrivate;
};

QT_END_NAMESPACE

#endif // QWEBENGINENOTIFICATION_H
