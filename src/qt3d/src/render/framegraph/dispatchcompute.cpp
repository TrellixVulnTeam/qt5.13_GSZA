/****************************************************************************
**
** Copyright (C) 2015 Klaralvdalens Datakonsult AB (KDAB).
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

#include "dispatchcompute_p.h"
#include <Qt3DRender/private/qdispatchcompute_p.h>
#include <Qt3DCore/qpropertyupdatedchange.h>

QT_BEGIN_NAMESPACE

namespace Qt3DRender {

namespace Render {

DispatchCompute::DispatchCompute()
    : FrameGraphNode(FrameGraphNode::ComputeDispatch)
{
    m_workGroups[0] = 1;
    m_workGroups[1] = 1;
    m_workGroups[2] = 1;
}

DispatchCompute::~DispatchCompute()
{
}

void DispatchCompute::cleanup()
{
    m_workGroups[0] = 1;
    m_workGroups[1] = 1;
    m_workGroups[2] = 1;
}

void DispatchCompute::initializeFromPeer(const Qt3DCore::QNodeCreatedChangeBasePtr &change)
{
    FrameGraphNode::initializeFromPeer(change);
    const auto typedChange = qSharedPointerCast<Qt3DCore::QNodeCreatedChange<QDispatchComputeData>>(change);
    const auto &data = typedChange->data;
    m_workGroups[0] = data.workGroupX;
    m_workGroups[1] = data.workGroupY;
    m_workGroups[2] = data.workGroupZ;
}

void DispatchCompute::sceneChangeEvent(const Qt3DCore::QSceneChangePtr &e)
{
    if (e->type() == Qt3DCore::PropertyUpdated) {
        Qt3DCore::QPropertyUpdatedChangePtr propertyChange = qSharedPointerCast<Qt3DCore::QPropertyUpdatedChange>(e);
        if (propertyChange->propertyName() == QByteArrayLiteral("workGroupX")) {
            m_workGroups[0] = propertyChange->value().toInt();
            markDirty(AbstractRenderer::FrameGraphDirty|AbstractRenderer::ComputeDirty);
        } else if (propertyChange->propertyName() == QByteArrayLiteral("workGroupY")) {
            m_workGroups[1] = propertyChange->value().toInt();
            markDirty(AbstractRenderer::FrameGraphDirty|AbstractRenderer::ComputeDirty);
        } else if (propertyChange->propertyName() == QByteArrayLiteral("workGroupZ")) {
            m_workGroups[2] = propertyChange->value().toInt();
            markDirty(AbstractRenderer::FrameGraphDirty|AbstractRenderer::ComputeDirty);
        }
    }
    FrameGraphNode::sceneChangeEvent(e);
}

} // Render

} // Qt3DRender

QT_END_NAMESPACE
