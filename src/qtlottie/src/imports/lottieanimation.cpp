/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the lottie-qt module of the Qt Toolkit.
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

#include "lottieanimation.h"

#include <QQuickPaintedItem>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QFile>
#include <QPointF>
#include <QPainter>
#include <QImage>
#include <QTimer>
#include <QMetaObject>
#include <QLoggingCategory>
#include <QThread>
#include <math.h>

#include <QtBodymovin/private/bmbase_p.h>
#include <QtBodymovin/private/bmlayer_p.h>

#include "rasterrenderer/batchrenderer.h"
#include "rasterrenderer/lottierasterrenderer.h"

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(lcLottieQtBodymovinRender, "qt.lottieqt.bodymovin.render");
Q_LOGGING_CATEGORY(lcLottieQtBodymovinParser, "qt.lottieqt.bodymovin.parser");

/*!
    \qmltype LottieAnimation
    \inqmlmodule Qt.labs.lottieqt
    \since 5.13
    \inherits Item
    \brief A Bodymovin player for Qt.

    The LottieAnimation type shows Bodymovin format files.

    LottieAnimation is used to load and render Bodymovin files exported
    from Adobe After Effects. Currently, only subset of the full Bodymovin
    specification is supported. Most notable deviations are:

    \list
    \li Only Shape layer supported
    \li Only integer frame-mode of a timeline supported
        (real frame numbers and time are rounded to the nearest integer)
    \li Expressions are not supported
    \endlist

    For the full list of devations, please refer to the file
    \c unsupported_features.txt in the source code.


    \section1 Example Usage

    The following example shows a simple usage of the LottieAnimation type

    \qml
    LottieAnimation {
        loops: 2
        quality: LottieAnimation.MediumQuality
        source: ":/animation.json"
        autoPlay: false
        onStatusChanged: {
            if (status === LottieAnimation.Ready) {
                // any acvities needed before
                // playing starts go here
                gotoAndPlay(startFrame);
            }
        }
        onFinished: {
            console.log("Finished playing")
        }
    }
    \endqml

    \note Changing width or height of the element does not change the size
    of the animation within. Also, it is not possible to align the the content
    inside of a \c LottieAnimation element. To achieve this, position the
    animation inside e.g. an \c Item.

    \section1 Rendering Performance

    Internally, the rendered frame data is cached to improve performance. You
    can control the memory usage by setting the QLOTTIE_RENDER_CACHE_SIZE
    environment variable (default value is 2).

    You can monitor the rendering performance by turning on two logging categories:

    \list
    \li \c qt.lottieqt.bodymovin.render - Provides information how the animation
        is rendered
    \li \c qt.lottieqt.bodymovin.render.thread - Provides information how the
        rendering process proceeds.
    \endlist

    Specifically, you can monitor does the frame cache gets constantly full, or
    does the rendering process have to wait for frames to become ready. The
    first case implies that the animation is too complex, and the rendering
    cannot keep up the pace. Try making the animation simpler, or optimize
    the QML scene.
*/

/*!
    \qmlproperty enumeration LottieAnimation::status

    This property holds the current status of the LottieAnimation element.

    \value LottieAnimation.Null
           An initial value that is used when the status is not defined
           (Default)

    \value LottieAnimation.Loading
           The player is loading a Bodymovin file

    \value LottieAnimation.Ready
           Loading has finished successfully and the player is ready to play
           the animation

    \value LottieAnimation.Error
           An error occurred while loading the animation

    For example, you could implement \c onStatusChanged signal
    handler to monitor progress of loading an animation as follows:

    \qml
    LottieAnimation {
        source: ":/animation.json"
        autoPlay: false
        onStatusChanged: {
            if (status === LottieAnimation.Ready)
                start();
        }
    \endqml
*/

/*!
    \qmlproperty bool LottieAnimation::autoPlay

    Defines whether the player will start playing animation automatically after
    the animation file has been loaded.

    The default value is \c true.
*/

/*!
    \qmlproperty int LottieAnimation::loops

    This property holds the number of loops the player will repeat.
    The value \c LottieAnimation.Infinite means that the the player repeats
    the animation continuously.

    The default value is \c 1.
*/

/*!
    \qmlsignal LottieAnimation::finished()

    This signal is emitted when the player has finished playing. In case of
    looping, the signal is emitted when the last loop has been finished.
*/

LottieAnimation::LottieAnimation(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    m_frameAdvance = new QTimer(this);
    m_frameAdvance->setSingleShot(false);
    connect (m_frameAdvance, &QTimer::timeout, this, &LottieAnimation::renderNextFrame);

    m_frameRenderThread = BatchRenderer::instance();

    QByteArray cacheStr = qgetenv("QLOTTIE_RENDER_CACHE_SIZE");
    bool ok = false;
    int cacheSize = cacheStr.toInt(&ok);
    if (ok)
       m_frameRenderThread->setCacheSize(cacheSize);

    qRegisterMetaType<LottieAnimation*>();
}

LottieAnimation::~LottieAnimation()
{
    QMetaObject::invokeMethod(m_frameRenderThread, "deregisterAnimator", Q_ARG(LottieAnimation*, this));
}

void LottieAnimation::componentComplete()
{
    QQuickItem::componentComplete();

    m_initialized = true;
    if (m_source.length())
        loadSource(m_source);
}

void LottieAnimation::paint(QPainter *painter)
{
    // TODO: Check does this have effect on output quality (or performance)
    if (m_quality != LowQuality)
        painter->setRenderHints(QPainter::Antialiasing);
    if (m_quality == HighQuality)
        painter->setRenderHints(QPainter::SmoothPixmapTransform);

    LottieRasterRenderer renderer(painter);
    BMBase* bmTree = m_frameRenderThread->getFrame(this, m_currentFrame);

    if (!bmTree) {
        qCDebug(lcLottieQtBodymovinRender) << "LottieAnimation::paint: Got empty element tree."
                                              "Cannot draw (Animator:" << static_cast<void*>(this) << ")";
        return;
    }

    qCDebug(lcLottieQtBodymovinRender) << static_cast<void*>(this) << "Start to paint frame"  << m_currentFrame;

    for (BMBase *elem : bmTree->children()) {
        if (elem->active(m_currentFrame))
            elem->render(renderer);
        else
            qCDebug(lcLottieQtBodymovinRender) << "Element '" << elem->name() << "' inactive. No need to paint";
    }

    m_frameRenderThread->frameRendered(this, m_currentFrame);

    m_currentFrame += m_direction;

    if (m_currentFrame < m_startFrame || m_currentFrame > m_endFrame) {
            m_currentLoop += (m_loops > 0 ? 1 : 0);
    }

    if ((m_loops - m_currentLoop) != 0) {
        m_currentFrame = m_currentFrame < m_startFrame ? m_endFrame :
                         m_currentFrame > m_endFrame ? m_startFrame : m_currentFrame;
    }
}

/*!
    \qmlproperty string LottieAnimation::source

    The path of the Bodymovin asset that LottieAnimation plays.

    Setting the source property starts loading the animation asynchronously.
    To monitor progress of loading, connect to the \l status change signal.
*/
QString LottieAnimation::source() const
{
    return m_source;
}

void LottieAnimation::setSource(const QString &source)
{
    if (m_source != source) {
        m_source = source;
        emit sourceChanged();

        if (m_initialized)
            loadSource(source);
    }
}

/*!
    \qmlproperty int LottieAnimation::startFrame
    \readonly

    Frame number of the start of the animation. The value
    is available after the animation has been loaded and
    ready to play.
*/
int LottieAnimation::startFrame() const
{
    return m_startFrame;
}

/*!
    \qmlproperty int LottieAnimation::endFrame
    \readonly

    Frame number of the end of the animation. The value
    is available after the animation has been loaded and
    ready to play.
*/
int LottieAnimation::endFrame() const
{
    return m_endFrame;
}

int LottieAnimation::currentFrame() const
{
    return m_currentFrame;
}

/*!
    \qmlproperty int LottieAnimation::frameRate

    This property holds the frame rate value of the Bodymovin animation.

    \c frameRate changes after the asset has been loaded. Changing the
    frame rate does not have effect before that, as the value defined in the
    asset overrides the value. To change the frame rate, you can write:

    \qml
    LottieAnimation {
        source: ":/animation.json"
        onStatusChanged: {
            if (status === LottieAnimation.Ready)
                frameRate = 60;
        }
    \endqml
*/
int LottieAnimation::frameRate() const
{
    return m_frameRate;
}

void LottieAnimation::setFrameRate(int frameRate)
{
    m_frameRate = frameRate;
    m_frameAdvance->setInterval(1000 / m_frameRate);
}

/*!
    \qmlproperty enumeration LottieAnimation::quality

    Speficies the rendering quality of the bodymovin player.
    If \c LowQuality is selected the rendering will happen into a frame
    buffer object, whereas with other options, the rendering will be done
    onto \c QImage (which in turn will be rendered on the screen).

    \value LottieAnimation.LowQuality
           Antialiasing or a smooth pixmap transformation algorithm are not
           used

    \value LottieAnimation.MediumQuality
           Antialiasing is used but no smooth pixmap transformation algorithm
           (Default)

    \value LottieAnimation.HighQuality
           Antialiasing and a smooth pixmap tranformation algorithm are both
           used
*/
LottieAnimation::Quality LottieAnimation::quality() const
{
    return m_quality;
}

void LottieAnimation::setQuality(LottieAnimation::Quality quality)
{
    if (m_quality != quality) {
        m_quality = quality;
        if (quality == LowQuality)
            setRenderTarget(QQuickPaintedItem::FramebufferObject);
        else
            setRenderTarget(QQuickPaintedItem::Image);
        emit qualityChanged();
    }
}

void LottieAnimation::reset()
{
    m_currentFrame = m_direction > 0 ? m_startFrame : m_endFrame;
    m_currentLoop = 0;
    QMetaObject::invokeMethod(m_frameRenderThread, "gotoFrame",
                              Q_ARG(LottieAnimation*, this),
                              Q_ARG(int, m_currentFrame));
}

/*!
    \qmlmethod void LottieAnimation::start()

    Starts playing the animation from the beginning.
*/
void LottieAnimation::start()
{
    reset();
    m_frameAdvance->start();
}

/*!
    \qmlmethod void LottieAnimation::play()

    Starts or continues playing from the current position.
*/
void LottieAnimation::play()
{
    QMetaObject::invokeMethod(m_frameRenderThread, "gotoFrame",
                              Q_ARG(LottieAnimation*, this),
                              Q_ARG(int, m_currentFrame));
    m_frameAdvance->start();
}

/*!
    \qmlmethod void LottieAnimation::pause()

    Pauses the playback.
*/
void LottieAnimation::pause()
{
    m_frameAdvance->stop();
    QMetaObject::invokeMethod(m_frameRenderThread, "gotoFrame",
                              Q_ARG(LottieAnimation*, this),
                              Q_ARG(int, m_currentFrame));
}

/*!
    \qmlmethod void LottieAnimation::togglePause()

    Toggles the status of player between playing and paused states.
*/
void LottieAnimation::togglePause()
{
    if (m_frameAdvance->isActive()) {
        pause();
    } else {
        play();
    }
}

/*!
    \qmlmethod void LottieAnimation::stop()

    Stops the playback and returns to startFrame.
*/
void LottieAnimation::stop()
{
    m_frameAdvance->stop();
    reset();
    renderNextFrame();
}

/*!
    \qmlmethod void LottieAnimation::gotoAndPlay(int frame)

    Plays the asset from the given \a frame.
*/
void LottieAnimation::gotoAndPlay(int frame)
{
    gotoFrame(frame);
    m_currentLoop = 0;
    m_frameAdvance->start();
}

/*!
    \qmlmethod bool LottieAnimation::gotoAndPlay(string frameMarker)

    Plays the asset from the frame that has a marker with the given \a frameMarker.
    Returns \c true if the frameMarker was found, \c false otherwise.
*/
bool LottieAnimation::gotoAndPlay(const QString &frameMarker)
{
    if (m_markers.contains(frameMarker)) {
        gotoAndPlay(m_markers.value(frameMarker));
        return true;
    } else
        return false;
}

/*!
    \qmlmethod void LottieAnimation::gotoAndStop(int frame)

    Moves the playhead to the given \a frame and stops.
*/
void LottieAnimation::gotoAndStop(int frame)
{
    gotoFrame(frame);
    renderNextFrame();
}

/*!
    \qmlmethod bool LottieAnimation::gotoAndStop(string frameMarker)

    Moves the playhead to the given marker and stops.
    Returns \c true if \a frameMarker was found, \c false otherwise.
*/
bool LottieAnimation::gotoAndStop(const QString &frameMarker)
{
    if (m_markers.contains(frameMarker)) {
        gotoAndStop(m_markers.value(frameMarker));
        return true;
    } else
        return false;
}

void LottieAnimation::gotoFrame(int frame)
{
    m_currentFrame = qMax(m_startFrame, qMin(frame, m_endFrame));
    QMetaObject::invokeMethod(m_frameRenderThread, "gotoFrame",
                              Q_ARG(LottieAnimation*, this),
                              Q_ARG(int, m_currentFrame));
}

/*!
    \qmlmethod double LottieAnimation::getDuration(bool inFrames)

    Returns the duration of the currently playing asset.

    If a given \a inFrames is \c true, the return value is the duration in
    number of frames. Otherwise, returns the duration in seconds.
*/
double LottieAnimation::getDuration(bool inFrames)
{
    return (m_endFrame - m_startFrame) /
            static_cast<double>(inFrames ? 1 : m_frameRate);
}

/*!
    \qmlproperty enumeration LottieAnimation::direction

    This property holds the direction of rendering.

    \value LottieAnimation.Forward
           Forward direction (Default)

    \value LottieAnimation.Reverse
           Reverse direction
*/
LottieAnimation::Direction LottieAnimation::direction() const
{
    if (m_direction < 0)
        return Reverse;
    else if (m_direction > 0)
        return Forward;
    else {
        Q_UNREACHABLE();
        return Forward;
    }
}

void LottieAnimation::setDirection(Direction direction)
{
    if (direction == Forward) {
        m_direction = 1;
        emit directionChanged();
    } else if (direction == Reverse) {
        m_direction = -1;
        emit directionChanged();
    }
}

bool LottieAnimation::loadSource(QString filename)
{
    QFile sourceFile(filename);
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        m_status = Error;
        emit statusChanged();
        return false;
    }

    m_status = Loading;
    emit statusChanged();

    QByteArray json = sourceFile.readAll();
    parse(json);

    setWidth(m_animWidth);
    emit widthChanged();
    setHeight(m_animHeight);
    emit heightChanged();

    sourceFile.close();

    QMetaObject::invokeMethod(m_frameRenderThread, "registerAnimator", Q_ARG(LottieAnimation*, this));

    m_frameAdvance->setInterval(1000 / m_frameRate);

    if (m_autoPlay)
        start();

    m_frameRenderThread->start();

    m_status = Ready;
    emit statusChanged();

    return true;
}

QByteArray LottieAnimation::jsonSource() const
{
    return m_jsonSource;
}

void LottieAnimation::renderNextFrame()
{
    if (m_currentFrame >= m_startFrame && m_currentFrame <= m_endFrame) {
        if (m_frameRenderThread->getFrame(this, m_currentFrame)) {
            update();
        } else if (!m_waitForFrameConn) {
            qCDebug(lcLottieQtBodymovinRender) << static_cast<void*>(this)
                                               << "Frame cache was empty for frame" << m_currentFrame;
            m_waitForFrameConn = connect(m_frameRenderThread, &BatchRenderer::frameReady,
                                         this, [=](LottieAnimation *target, int frameNumber) {
                if (target != this)
                    return;
                qCDebug(lcLottieQtBodymovinRender) << static_cast<void*>(this)
                                                   << "Frame ready" << frameNumber;
                disconnect(m_waitForFrameConn);
                update();
            });
        }
    } else if (m_loops == m_currentLoop) {
        if ( m_loops != Infinite)
            m_frameAdvance->stop();
        emit finished();
    }
}

int LottieAnimation::parse(QByteArray jsonSource)
{
    m_jsonSource = jsonSource;

    QJsonDocument doc = QJsonDocument::fromJson(jsonSource);
    QJsonObject rootObj = doc.object();

    if (rootObj.empty()) {
        m_status = Error;
        return -1;
    }

    m_startFrame = rootObj.value(QLatin1String("ip")).toVariant().toInt();
    m_endFrame = rootObj.value(QLatin1String("op")).toVariant().toInt();
    m_frameRate = rootObj.value(QLatin1String("fr")).toVariant().toInt();
    m_animWidth = rootObj.value(QLatin1String("w")).toVariant().toReal();
    m_animHeight = rootObj.value(QLatin1String("h")).toVariant().toReal();

    setWidth(m_animWidth);
    setHeight(m_animHeight);

    QJsonArray markerArr = rootObj.value(QLatin1String("markers")).toArray();
    QJsonArray::const_iterator markerIt = markerArr.constBegin();
    while (markerIt != markerArr.constEnd()) {
        QString marker = (*markerIt).toObject().value(QLatin1String("cm")).toString();
        int frame = (*markerIt).toObject().value(QLatin1String("tm")).toInt();
        m_markers.insert(marker, frame);

        if ((*markerIt).toObject().value(QLatin1String("dr")).toInt())
            qCWarning(lcLottieQtBodymovinParser)
                    << "property 'dr' not support in a marker";
        ++markerIt;
    }

    if (rootObj.value(QLatin1String("assets")).toArray().count())
        qCWarning(lcLottieQtBodymovinParser) << "assets not supported";

    if (rootObj.value(QLatin1String("chars")).toArray().count())
        qCWarning(lcLottieQtBodymovinParser) << "chars not supported";

    emit frameRateChanged();
    emit startFrameChanged();
    emit endFrameChanged();

    return 0;
}

QT_END_NAMESPACE
