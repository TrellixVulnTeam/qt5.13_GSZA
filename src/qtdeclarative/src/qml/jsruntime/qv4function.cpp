/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtQml module of the Qt Toolkit.
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

#include "qv4function_p.h"
#include "qv4functionobject_p.h"
#include "qv4managed_p.h"
#include "qv4string_p.h"
#include "qv4value_p.h"
#include "qv4engine_p.h"
#include "qv4lookup_p.h"
#include <private/qv4mm_p.h>
#include <private/qv4identifiertable_p.h>
#include <private/qv4functiontable_p.h>
#include <assembler/MacroAssemblerCodeRef.h>
#include <private/qv4vme_moth_p.h>
#include <private/qqmlglobal_p.h>

QT_BEGIN_NAMESPACE

using namespace QV4;

ReturnedValue Function::call(const Value *thisObject, const Value *argv, int argc, const ExecutionContext *context) {
    ExecutionEngine *engine = context->engine();
    CppStackFrame frame;
    frame.init(engine, this, argv, argc);
    frame.setupJSFrame(engine->jsStackTop, Value::undefinedValue(), context->d(),
                       thisObject ? *thisObject : Value::undefinedValue(),
                       Value::undefinedValue());

    frame.push();
    engine->jsStackTop += frame.requiredJSStackFrameSize();

    ReturnedValue result = Moth::VME::exec(&frame, engine);

    frame.pop();

    return result;
}

Function *Function::create(ExecutionEngine *engine, CompiledData::CompilationUnit *unit, const CompiledData::Function *function)
{
    quint16 traceSlotCount = 0;
#if QT_CONFIG(qml_tracing)
        traceSlotCount = function->nTraceInfos == CompiledData::Function::NoTracing()
                ? 1
                : function->nTraceInfos;
#endif
    quint8 *storage = new quint8[sizeof(Function) + traceSlotCount];
    return new(storage) Function(engine, unit, function);
}

void Function::destroy()
{
    delete[] reinterpret_cast<quint8 *>(this);
}

Function::Function(ExecutionEngine *engine, CompiledData::CompilationUnit *unit, const CompiledData::Function *function)
    : FunctionData(unit)
    , compiledFunction(function)
    , codeData(function->code())
    , jittedCode(nullptr)
    , codeRef(nullptr)
    , hasQmlDependencies(function->hasQmlDependencies())
{
    Scope scope(engine);
    Scoped<InternalClass> ic(scope, engine->internalClasses(EngineBase::Class_CallContext));

    // first locals
    const quint32_le *localsIndices = compiledFunction->localsTable();
    for (quint32 i = 0; i < compiledFunction->nLocals; ++i)
        ic = ic->addMember(engine->identifierTable->asPropertyKey(compilationUnit->runtimeStrings[localsIndices[i]]), Attr_NotConfigurable);

    const quint32_le *formalsIndices = compiledFunction->formalsTable();
    for (quint32 i = 0; i < compiledFunction->nFormals; ++i)
        ic = ic->addMember(engine->identifierTable->asPropertyKey(compilationUnit->runtimeStrings[formalsIndices[i]]), Attr_NotConfigurable);
    internalClass = ic->d();

    nFormals = compiledFunction->nFormals;

#if QT_CONFIG(qml_tracing)
    if (tracingEnabled()) {
        for (uint i = 0; i < function->nTraceInfos; ++i)
            *traceInfo(i) = 0;
    }
#endif
}

Function::~Function()
{
    if (codeRef) {
        destroyFunctionTable(this, codeRef);
        delete codeRef;
    }
}

void Function::updateInternalClass(ExecutionEngine *engine, const QList<QByteArray> &parameters)
{
    QStringList parameterNames;

    // Resolve duplicate parameter names:
    for (int i = 0, ei = parameters.count(); i != ei; ++i) {
        const QByteArray &param = parameters.at(i);
        int duplicate = -1;

        for (int j = i - 1; j >= 0; --j) {
            const QByteArray &prevParam = parameters.at(j);
            if (param == prevParam) {
                duplicate = j;
                break;
            }
        }

        if (duplicate == -1) {
            parameterNames.append(QString::fromUtf8(param));
        } else {
            const QString &dup = parameterNames[duplicate];
            parameterNames.append(dup);
            parameterNames[duplicate] =
                    QString(0xfffe) + QString::number(duplicate) + dup;
        }

    }

    internalClass = engine->internalClasses(EngineBase::Class_CallContext);

    // first locals
    const quint32_le *localsIndices = compiledFunction->localsTable();
    for (quint32 i = 0; i < compiledFunction->nLocals; ++i)
        internalClass = internalClass->addMember(engine->identifierTable->asPropertyKey(compilationUnit->runtimeStrings[localsIndices[i]]), Attr_NotConfigurable);

    Scope scope(engine);
    ScopedString arg(scope);
    for (const QString &parameterName : parameterNames) {
        arg = engine->newIdentifier(parameterName);
        internalClass = internalClass->addMember(arg->propertyKey(), Attr_NotConfigurable);
    }
    nFormals = parameters.size();
}

QString Function::prettyName(const Function *function, const void *code)
{
    QString prettyName = function ? function->name()->toQString() : QString();
    if (prettyName.isEmpty()) {
        prettyName = QString::number(reinterpret_cast<quintptr>(code), 16);
        prettyName.prepend(QLatin1String("QV4::Function(0x"));
        prettyName.append(QLatin1Char(')'));
    }
    return prettyName;
}

QQmlSourceLocation Function::sourceLocation() const
{
    return QQmlSourceLocation(sourceFile(), compiledFunction->location.line, compiledFunction->location.column);
}

QString Function::traceInfoToString()
{
    QString info = QLatin1String("=== Trace information for ") + name()->toQString() + QLatin1Char(':');
    if (!tracingEnabled())
        return info + QStringLiteral(" disabled. Interpreter call count: %1\n").arg(interpreterCallCount);
    if (compiledFunction->nTraceInfos == 0)
        return info + QLatin1String(" none.\n");

    info += QLatin1Char('\n');
    for (uint i = 0, ei = compiledFunction->nTraceInfos; i < ei; ++i) {
        auto bits = QString::number(*traceInfo(i), 2);
        if (bits.size() < 8)
            bits.prepend(QString(8 - bits.size(), '0'));
        info += QStringLiteral("    %1: %2\n").arg(QString::number(i), bits);
    }
    return info;
}

QT_END_NAMESPACE
