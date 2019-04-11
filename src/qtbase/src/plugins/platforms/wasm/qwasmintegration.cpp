/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
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

#include "qwasmintegration.h"
#include "qwasmeventtranslator.h"
#include "qwasmeventdispatcher.h"
#include "qwasmcompositor.h"
#include "qwasmopenglcontext.h"
#include "qwasmtheme.h"
#include "qwasmclipboard.h"

#include "qwasmwindow.h"
#ifndef QT_NO_OPENGL
# include "qwasmbackingstore.h"
#endif
#include "qwasmfontdatabase.h"
#if defined(Q_OS_UNIX)
#include <QtEventDispatcherSupport/private/qgenericunixeventdispatcher_p.h>
#endif
#include <qpa/qplatformwindow.h>
#include <QtGui/qscreen.h>
#include <qpa/qwindowsysteminterface.h>
#include <QtCore/qcoreapplication.h>

#include <emscripten/bind.h>

// this is where EGL headers are pulled in, make sure it is last
#include "qwasmscreen.h"

using namespace emscripten;
QT_BEGIN_NAMESPACE

void browserBeforeUnload(emscripten::val)
{
    QWasmIntegration::QWasmBrowserExit();
}

EMSCRIPTEN_BINDINGS(my_module)
{
    function("browserBeforeUnload", &browserBeforeUnload);
}

QWasmIntegration *QWasmIntegration::s_instance;

QWasmIntegration::QWasmIntegration()
    : m_fontDb(nullptr),
      m_eventDispatcher(nullptr),
      m_clipboard(new QWasmClipboard)
{
    s_instance = this;

    // We expect that qtloader.js has populated Module.qtCanvasElements with one or more canvases.
    // Also check Module.canvas, which may be set if the emscripen or a custom loader is used.
    emscripten::val qtCanvaseElements = val::module_property("qtCanvasElements");
    emscripten::val canvas = val::module_property("canvas");

    if (!qtCanvaseElements.isUndefined()) {
        int screenCount = qtCanvaseElements["length"].as<int>();
        for (int i = 0; i < screenCount; ++i) {
            emscripten::val canvas = qtCanvaseElements[i].as<emscripten::val>();
            QString canvasId = QString::fromStdString(canvas["id"].as<std::string>());
            addScreen(canvasId);
        }
    } else if (!canvas.isUndefined()){
        QString canvasId = QString::fromStdString(canvas["id"].as<std::string>());
        addScreen(canvasId);
    }

    emscripten::val::global("window").set("onbeforeunload", val::module_property("browserBeforeUnload"));
}

QWasmIntegration::~QWasmIntegration()
{
    delete m_fontDb;
    qDeleteAll(m_screens);
    s_instance = nullptr;
}

void QWasmIntegration::QWasmBrowserExit()
{
    QCoreApplication *app = QCoreApplication::instance();
    app->quit();
}

bool QWasmIntegration::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap) {
    case ThreadedPixmaps: return true;
    case OpenGL: return true;
    case ThreadedOpenGL: return true;
    case RasterGLSurface: return false; // to enable this you need to fix qopenglwidget and quickwidget for wasm
    case MultipleWindows: return true;
    case WindowManagement: return true;
    default: return QPlatformIntegration::hasCapability(cap);
    }
}

QPlatformWindow *QWasmIntegration::createPlatformWindow(QWindow *window) const
{
    QWasmCompositor *compositor = QWasmScreen::get(window->screen())->compositor();
    return new QWasmWindow(window, compositor, m_backingStores.value(window));
}

QPlatformBackingStore *QWasmIntegration::createPlatformBackingStore(QWindow *window) const
{
#ifndef QT_NO_OPENGL
    QWasmCompositor *compositor = QWasmScreen::get(window->screen())->compositor();
    QWasmBackingStore *backingStore = new QWasmBackingStore(compositor, window);
    m_backingStores.insert(window, backingStore);
    return backingStore;
#else
    return nullptr;
#endif
}

#ifndef QT_NO_OPENGL
QPlatformOpenGLContext *QWasmIntegration::createPlatformOpenGLContext(QOpenGLContext *context) const
{
    return new QWasmOpenGLContext(context->format());
}
#endif

QPlatformFontDatabase *QWasmIntegration::fontDatabase() const
{
    if (m_fontDb == nullptr)
        m_fontDb = new QWasmFontDatabase;

    return m_fontDb;
}

QAbstractEventDispatcher *QWasmIntegration::createEventDispatcher() const
{
    return new QWasmEventDispatcher;
}

QVariant QWasmIntegration::styleHint(QPlatformIntegration::StyleHint hint) const
{
    return QPlatformIntegration::styleHint(hint);
}

QStringList QWasmIntegration::themeNames() const
{
    return QStringList() << QLatin1String("webassembly");
}

QPlatformTheme *QWasmIntegration::createPlatformTheme(const QString &name) const
{
    if (name == QLatin1String("webassembly"))
        return new QWasmTheme;
    return QPlatformIntegration::createPlatformTheme(name);
}

QPlatformClipboard* QWasmIntegration::clipboard() const
{
    return m_clipboard;
}

QVector<QWasmScreen *> QWasmIntegration::screens()
{
    return m_screens;
}

void QWasmIntegration::addScreen(const QString &canvasId)
{
    QWasmScreen *screen = new QWasmScreen(canvasId);
    m_clipboard->installEventHandlers(canvasId);
    m_screens.append(screen);
    screenAdded(screen);
}

QT_END_NAMESPACE
