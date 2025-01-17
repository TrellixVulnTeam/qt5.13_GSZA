/****************************************************************************
**
** Copyright (C) 2017 Klaralvdalens Datakonsult AB (KDAB).
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt3D module of the Qt Toolkit.
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

#include "shaderbuilder_p.h"

#include <Qt3DRender/private/qshaderprogrambuilder_p.h>
#include <Qt3DRender/qshaderprogram.h>
#include <Qt3DRender/private/qurlhelper_p.h>

#include <QtGui/private/qshaderformat_p.h>
#include <QtGui/private/qshadergraphloader_p.h>
#include <QtGui/private/qshadergenerator_p.h>
#include <QtGui/private/qshadernodesloader_p.h>

#include <QFile>
#include <QFileInfo>
#include <QUrl>

static void initResources()
{
#ifdef QT_STATIC
    Q_INIT_RESOURCE(materialsystem);
#endif
}

QT_BEGIN_NAMESPACE

class GlobalShaderPrototypes
{
public:
    GlobalShaderPrototypes()
    {
        initResources();
        setPrototypesFile(QStringLiteral(":/prototypes/default.json"));
    }

    QString prototypesFile() const
    {
        return m_fileName;
    }

    void setPrototypesFile(const QString &fileName)
    {
        m_fileName = fileName;
        load();
    }

    QHash<QString, QShaderNode> prototypes() const
    {
        return m_prototypes;
    }

private:
    void load()
    {
        QFile file(m_fileName);
        if (!file.open(QFile::ReadOnly)) {
            qWarning() << "Couldn't open file:" << m_fileName;
            return;
        }

        QShaderNodesLoader loader;
        loader.setDevice(&file);
        loader.load();
        m_prototypes = loader.nodes();
    }

    QString m_fileName;
    QHash<QString, QShaderNode> m_prototypes;
};

Q_GLOBAL_STATIC(GlobalShaderPrototypes, qt3dGlobalShaderPrototypes)

using namespace Qt3DCore;

namespace Qt3DRender {
namespace Render {


namespace {

QShaderProgram::ShaderType toQShaderProgramType(ShaderBuilder::ShaderType type)
{
    switch (type) {
    case ShaderBuilder::ShaderType::Vertex:
        return QShaderProgram::Vertex;
    case ShaderBuilder::ShaderType::TessellationControl:
        return QShaderProgram::TessellationControl;
    case ShaderBuilder::ShaderType::TessellationEvaluation:
        return QShaderProgram::TessellationEvaluation;
    case ShaderBuilder::ShaderType::Geometry:
        return QShaderProgram::Geometry;
    case ShaderBuilder::ShaderType::Fragment:
        return QShaderProgram::Fragment;
    case ShaderBuilder::ShaderType::Compute:
        return QShaderProgram::Compute;
    default:
        Q_UNREACHABLE();
    }
}

} // anonymous

QString ShaderBuilder::getPrototypesFile()
{
    return qt3dGlobalShaderPrototypes->prototypesFile();
}

void ShaderBuilder::setPrototypesFile(const QString &file)
{
    qt3dGlobalShaderPrototypes->setPrototypesFile(file);
}

QStringList ShaderBuilder::getPrototypeNames()
{
    return qt3dGlobalShaderPrototypes->prototypes().keys();
}

ShaderBuilder::ShaderBuilder()
    : BackendNode(ReadWrite)
{
}

ShaderBuilder::~ShaderBuilder()
{
}

void ShaderBuilder::cleanup()
{
    m_shaderProgramId = Qt3DCore::QNodeId();
    m_enabledLayers.clear();
    m_graphs.clear();
    m_dirtyTypes.clear();
    QBackendNode::setEnabled(false);
}

Qt3DCore::QNodeId ShaderBuilder::shaderProgramId() const
{
    return m_shaderProgramId;
}

QStringList ShaderBuilder::enabledLayers() const
{
    return m_enabledLayers;
}


void ShaderBuilder::setEnabledLayers(const QStringList &layers)
{
    if (m_enabledLayers == layers)
        return;

    m_enabledLayers = layers;

    for (QHash<ShaderType, QUrl>::const_iterator it = m_graphs.cbegin(); it != m_graphs.cend(); ++it) {
        if (!it.value().isEmpty())
            m_dirtyTypes.insert(it.key());
    }
}

GraphicsApiFilterData ShaderBuilder::graphicsApi() const
{
    return m_graphicsApi;
}

void ShaderBuilder::setGraphicsApi(const GraphicsApiFilterData &graphicsApi)
{
    if (m_graphicsApi == graphicsApi)
        return;

    m_graphicsApi = graphicsApi;
    for (QHash<ShaderType, QUrl>::const_iterator it = m_graphs.cbegin(); it != m_graphs.cend(); ++it) {
        if (!it.value().isEmpty())
            m_dirtyTypes.insert(it.key());
    }
}

QUrl ShaderBuilder::shaderGraph(ShaderBuilder::ShaderType type) const
{
    return m_graphs.value(type);
}

void ShaderBuilder::setShaderGraph(ShaderBuilder::ShaderType type, const QUrl &url)
{
    if (url != m_graphs.value(type)) {
        m_graphs.insert(type, url);
        m_dirtyTypes.insert(type);
    }
}

QByteArray ShaderBuilder::shaderCode(ShaderBuilder::ShaderType type) const
{
    return m_codes.value(type);
}

bool ShaderBuilder::isShaderCodeDirty(ShaderBuilder::ShaderType type) const
{
    return m_dirtyTypes.contains(type);
}

static QByteArray deincludify(const QByteArray &contents, const QString &filePath);

static QByteArray deincludify(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Could not read shader source file:" << f.fileName();
        return QByteArray();
    }

    QByteArray contents = f.readAll();
    return deincludify(contents, filePath);
}

static QByteArray deincludify(const QByteArray &contents, const QString &filePath)
{
    QByteArrayList lines = contents.split('\n');
    const QByteArray includeDirective = QByteArrayLiteral("#pragma include");
    for (int i = 0; i < lines.count(); ++i) {
        const auto line = lines[i].simplified();
        if (line.startsWith(includeDirective)) {
            const QString includePartialPath = QString::fromUtf8(line.mid(includeDirective.count() + 1));

            QString includePath = QFileInfo(includePartialPath).isAbsolute() ? includePartialPath
                                : QFileInfo(filePath).absolutePath() + QLatin1Char('/') + includePartialPath;
            if (qEnvironmentVariableIsSet("QT3D_GLSL100_WORKAROUND")) {
                QString candidate = includePath + QLatin1String("100");
                if (QFile::exists(candidate))
                    includePath = candidate;
            }
            lines.removeAt(i);
            QByteArray includedContents = deincludify(includePath);
            lines.insert(i, includedContents);
            QString lineDirective = QString(QStringLiteral("#line %1")).arg(i + 2);
            lines.insert(i + 1, lineDirective.toUtf8());
        }
    }

    return lines.join('\n');
}

void ShaderBuilder::generateCode(ShaderBuilder::ShaderType type)
{
    const auto graphPath = QUrlHelper::urlToLocalFileOrQrc(shaderGraph(type));
    QFile file(graphPath);
    if (!file.open(QFile::ReadOnly)) {
        qWarning() << "Couldn't open file:" << graphPath;
        return;
    }

    auto graphLoader = QShaderGraphLoader();
    graphLoader.setPrototypes(qt3dGlobalShaderPrototypes->prototypes());
    graphLoader.setDevice(&file);
    graphLoader.load();

    if (graphLoader.status() == QShaderGraphLoader::Error)
        return;

    const auto graph = graphLoader.graph();

    auto format = QShaderFormat();
    format.setApi(m_graphicsApi.m_api == QGraphicsApiFilter::OpenGLES ? QShaderFormat::OpenGLES
                : m_graphicsApi.m_profile == QGraphicsApiFilter::CoreProfile ? QShaderFormat::OpenGLCoreProfile
                : m_graphicsApi.m_profile == QGraphicsApiFilter::CompatibilityProfile ? QShaderFormat::OpenGLCompatibilityProfile
                : QShaderFormat::OpenGLNoProfile);
    format.setVersion(QVersionNumber(m_graphicsApi.m_major, m_graphicsApi.m_minor));
    format.setExtensions(m_graphicsApi.m_extensions);
    format.setVendor(m_graphicsApi.m_vendor);

    auto generator = QShaderGenerator();
    generator.format = format;
    generator.graph = graph;

    const auto code = generator.createShaderCode(m_enabledLayers);
    m_codes.insert(type, deincludify(code, graphPath + QStringLiteral(".glsl")));
    m_dirtyTypes.remove(type);

    // Send notification to the frontend
    Qt3DCore::QPropertyUpdatedChangePtr propertyChange = Qt3DCore::QPropertyUpdatedChangePtr::create(peerId());
    propertyChange->setDeliveryFlags(Qt3DCore::QSceneChange::DeliverToAll);
    propertyChange->setPropertyName("generatedShaderCode");
    propertyChange->setValue(QVariant::fromValue(qMakePair(int(toQShaderProgramType(type)), m_codes.value(type))));
    notifyObservers(propertyChange);
}

void ShaderBuilder::sceneChangeEvent(const Qt3DCore::QSceneChangePtr &e)
{
    if (e->type() == PropertyUpdated) {
        QPropertyUpdatedChangePtr propertyChange = e.staticCast<QPropertyUpdatedChange>();
        QVariant propertyValue = propertyChange->value();

        if (propertyChange->propertyName() == QByteArrayLiteral("shaderProgram"))
            m_shaderProgramId = propertyValue.value<Qt3DCore::QNodeId>();
        else if (propertyChange->propertyName() == QByteArrayLiteral("enabledLayers"))
            setEnabledLayers(propertyValue.toStringList());
        else if (propertyChange->propertyName() == QByteArrayLiteral("vertexShaderGraph"))
            setShaderGraph(Vertex, propertyValue.toUrl());
        else if (propertyChange->propertyName() == QByteArrayLiteral("tessellationControlShaderGraph"))
            setShaderGraph(TessellationControl, propertyValue.toUrl());
        else if (propertyChange->propertyName() == QByteArrayLiteral("tessellationEvaluationShaderGraph"))
            setShaderGraph(TessellationEvaluation, propertyValue.toUrl());
        else if (propertyChange->propertyName() == QByteArrayLiteral("geometryShaderGraph"))
            setShaderGraph(Geometry, propertyValue.toUrl());
        else if (propertyChange->propertyName() == QByteArrayLiteral("fragmentShaderGraph"))
            setShaderGraph(Fragment, propertyValue.toUrl());
        else if (propertyChange->propertyName() == QByteArrayLiteral("computeShaderGraph"))
            setShaderGraph(Compute, propertyValue.toUrl());

        markDirty(AbstractRenderer::ShadersDirty);
    }
    BackendNode::sceneChangeEvent(e);
}

void ShaderBuilder::initializeFromPeer(const Qt3DCore::QNodeCreatedChangeBasePtr &change)
{
    const auto typedChange = qSharedPointerCast<Qt3DCore::QNodeCreatedChange<QShaderProgramBuilderData>>(change);
    const auto &data = typedChange->data;

    m_shaderProgramId = data.shaderProgramId;
    m_enabledLayers = data.enabledLayers;
    setShaderGraph(Vertex, data.vertexShaderGraph);
    setShaderGraph(TessellationControl, data.tessellationControlShaderGraph);
    setShaderGraph(TessellationEvaluation, data.tessellationEvaluationShaderGraph);
    setShaderGraph(Geometry, data.geometryShaderGraph);
    setShaderGraph(Fragment, data.fragmentShaderGraph);
    setShaderGraph(Compute, data.computeShaderGraph);
}

} // namespace Render
} // namespace Qt3DRender

QT_END_NAMESPACE
