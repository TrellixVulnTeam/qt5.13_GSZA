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

#include "qv4compileddata_p.h"
#include <private/qv4value_p.h>
#ifndef V4_BOOTSTRAP
#include <private/qv4engine_p.h>
#include <private/qv4function_p.h>
#include <private/qv4objectproto_p.h>
#include <private/qv4lookup_p.h>
#include <private/qv4regexpobject_p.h>
#include <private/qv4regexp_p.h>
#include <private/qqmlpropertycache_p.h>
#include <private/qqmltypeloader_p.h>
#include <private/qqmlengine_p.h>
#include <private/qv4vme_moth_p.h>
#include <private/qv4module_p.h>
#include "qv4compilationunitmapper_p.h"
#include <QQmlPropertyMap>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QStandardPaths>
#include <QDir>
#include <private/qv4identifiertable_p.h>
#endif
#include <private/qqmlirbuilder_p.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QScopeGuard>

// generated by qmake:
#include "qml_compile_hash_p.h"

#include <algorithm>

QT_BEGIN_NAMESPACE

namespace QV4 {

namespace CompiledData {

#if defined(QML_COMPILE_HASH)
#  ifdef Q_OS_LINUX
// Place on a separate section on Linux so it's easier to check from outside
// what the hash version is.
__attribute__((section(".qml_compile_hash")))
#  endif
const char qml_compile_hash[48 + 1] = QML_COMPILE_HASH;
static_assert(sizeof(Unit::libraryVersionHash) >= QML_COMPILE_HASH_LENGTH + 1, "Compile hash length exceeds reserved size in data structure. Please adjust and bump the format version");
#else
#  error "QML_COMPILE_HASH must be defined for the build of QtDeclarative to ensure version checking for cache files"
#endif


CompilationUnit::CompilationUnit(const Unit *unitData, const QString &fileName, const QString &finalUrlString)
{
    setUnitData(unitData, nullptr, fileName, finalUrlString);
}

#ifndef V4_BOOTSTRAP
CompilationUnit::~CompilationUnit()
{
    unlink();

    if (data) {
        if (data->qmlUnit() != qmlData)
            free(const_cast<QmlUnit *>(qmlData));
        qmlData = nullptr;

        if (!(data->flags & QV4::CompiledData::Unit::StaticData))
            free(const_cast<Unit *>(data));
    }
    data = nullptr;
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    delete [] constants;
    constants = nullptr;
#endif

    delete [] imports;
    imports = nullptr;
}

QString CompilationUnit::localCacheFilePath(const QUrl &url)
{
    const QString localSourcePath = QQmlFile::urlToLocalFileOrQrc(url);
    const QString cacheFileSuffix = QFileInfo(localSourcePath + QLatin1Char('c')).completeSuffix();
    QCryptographicHash fileNameHash(QCryptographicHash::Sha1);
    fileNameHash.addData(localSourcePath.toUtf8());
    QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QLatin1String("/qmlcache/");
    QDir::root().mkpath(directory);
    return directory + QString::fromUtf8(fileNameHash.result().toHex()) + QLatin1Char('.') + cacheFileSuffix;
}

QV4::Function *CompilationUnit::linkToEngine(ExecutionEngine *engine)
{
    this->engine = engine;
    engine->compilationUnits.insert(this);

    Q_ASSERT(!runtimeStrings);
    Q_ASSERT(data);
    const quint32 stringCount = totalStringCount();
    runtimeStrings = (QV4::Heap::String **)malloc(stringCount * sizeof(QV4::Heap::String*));
    // memset the strings to 0 in case a GC run happens while we're within the loop below
    memset(runtimeStrings, 0, stringCount * sizeof(QV4::Heap::String*));
    for (uint i = 0; i < stringCount; ++i)
        runtimeStrings[i] = engine->newString(stringAt(i));

    runtimeRegularExpressions = new QV4::Value[data->regexpTableSize];
    // memset the regexps to 0 in case a GC run happens while we're within the loop below
    memset(runtimeRegularExpressions, 0, data->regexpTableSize * sizeof(QV4::Value));
    for (uint i = 0; i < data->regexpTableSize; ++i) {
        const CompiledData::RegExp *re = data->regexpAt(i);
        uint f = re->flags;
        const CompiledData::RegExp::Flags flags = static_cast<CompiledData::RegExp::Flags>(f);
        runtimeRegularExpressions[i] = QV4::RegExp::create(engine, stringAt(re->stringIndex), flags);
    }

    if (data->lookupTableSize) {
        runtimeLookups = new QV4::Lookup[data->lookupTableSize];
        memset(runtimeLookups, 0, data->lookupTableSize * sizeof(QV4::Lookup));
        const CompiledData::Lookup *compiledLookups = data->lookupTable();
        for (uint i = 0; i < data->lookupTableSize; ++i) {
            QV4::Lookup *l = runtimeLookups + i;

            Lookup::Type type = Lookup::Type(uint(compiledLookups[i].type_and_flags));
            if (type == CompiledData::Lookup::Type_Getter)
                l->getter = QV4::Lookup::getterGeneric;
            else if (type == CompiledData::Lookup::Type_Setter)
                l->setter = QV4::Lookup::setterGeneric;
            else if (type == CompiledData::Lookup::Type_GlobalGetter)
                l->globalGetter = QV4::Lookup::globalGetterGeneric;
            l->nameIndex = compiledLookups[i].nameIndex;
        }
    }

    if (data->jsClassTableSize) {
        runtimeClasses = (QV4::Heap::InternalClass **)malloc(data->jsClassTableSize * sizeof(QV4::Heap::InternalClass *));
        // memset the regexps to 0 in case a GC run happens while we're within the loop below
        memset(runtimeClasses, 0, data->jsClassTableSize * sizeof(QV4::Heap::InternalClass *));
        for (uint i = 0; i < data->jsClassTableSize; ++i) {
            int memberCount = 0;
            const CompiledData::JSClassMember *member = data->jsClassAt(i, &memberCount);
            runtimeClasses[i] = engine->internalClasses(QV4::ExecutionEngine::Class_Object);
            for (int j = 0; j < memberCount; ++j, ++member)
                runtimeClasses[i] = runtimeClasses[i]->addMember(engine->identifierTable->asPropertyKey(runtimeStrings[member->nameOffset]), member->isAccessor ? QV4::Attr_Accessor : QV4::Attr_Data);
        }
    }

    runtimeFunctions.resize(data->functionTableSize);
    for (int i = 0 ;i < runtimeFunctions.size(); ++i) {
        const QV4::CompiledData::Function *compiledFunction = data->functionAt(i);
        runtimeFunctions[i] = QV4::Function::create(engine, this, compiledFunction);
    }

    Scope scope(engine);
    Scoped<InternalClass> ic(scope);

    runtimeBlocks.resize(data->blockTableSize);
    for (int i = 0 ;i < runtimeBlocks.size(); ++i) {
        const QV4::CompiledData::Block *compiledBlock = data->blockAt(i);
        ic = engine->internalClasses(EngineBase::Class_CallContext);

        // first locals
        const quint32_le *localsIndices = compiledBlock->localsTable();
        for (quint32 j = 0; j < compiledBlock->nLocals; ++j)
            ic = ic->addMember(engine->identifierTable->asPropertyKey(runtimeStrings[localsIndices[j]]), Attr_NotConfigurable);
        runtimeBlocks[i] = ic->d();
    }

    static const bool showCode = qEnvironmentVariableIsSet("QV4_SHOW_BYTECODE");
    if (showCode) {
        qDebug() << "=== Constant table";
        Moth::dumpConstantTable(constants, data->constantTableSize);
        qDebug() << "=== String table";
        for (uint i = 0, end = totalStringCount(); i < end; ++i)
            qDebug() << "    " << i << ":" << runtimeStrings[i]->toQString();
        qDebug() << "=== Closure table";
        for (uint i = 0; i < data->functionTableSize; ++i)
            qDebug() << "    " << i << ":" << runtimeFunctions[i]->name()->toQString();
        qDebug() << "root function at index " << (data->indexOfRootFunction != -1 ? data->indexOfRootFunction : 0);
    }

    if (data->indexOfRootFunction != -1)
        return runtimeFunctions[data->indexOfRootFunction];
    else
        return nullptr;
}

Heap::Object *CompilationUnit::templateObjectAt(int index) const
{
    Q_ASSERT(index < int(data->templateObjectTableSize));
    if (!templateObjects.size())
        templateObjects.resize(data->templateObjectTableSize);
    Heap::Object *o = templateObjects.at(index);
    if (o)
        return o;

    // create the template object
    Scope scope(engine);
    const CompiledData::TemplateObject *t = data->templateObjectAt(index);
    Scoped<ArrayObject> a(scope, engine->newArrayObject(t->size));
    Scoped<ArrayObject> raw(scope, engine->newArrayObject(t->size));
    ScopedValue s(scope);
    for (uint i = 0; i < t->size; ++i) {
        s = runtimeStrings[t->stringIndexAt(i)];
        a->arraySet(i, s);
        s = runtimeStrings[t->rawStringIndexAt(i)];
        raw->arraySet(i, s);
    }

    ObjectPrototype::method_freeze(engine->functionCtor(), nullptr, raw, 1);
    a->defineReadonlyProperty(QStringLiteral("raw"), raw);
    ObjectPrototype::method_freeze(engine->functionCtor(), nullptr, a, 1);

    templateObjects[index] = a->objectValue()->d();
    return templateObjects.at(index);
}

void CompilationUnit::unlink()
{
    if (engine)
        nextCompilationUnit.remove();

    if (isRegisteredWithEngine) {
        Q_ASSERT(data && propertyCaches.count() > 0 && propertyCaches.at(/*root object*/0));
        if (qmlEngine)
            qmlEngine->unregisterInternalCompositeType(this);
        QQmlMetaType::unregisterInternalCompositeType(this);
        isRegisteredWithEngine = false;
    }

    propertyCaches.clear();

    dependentScripts.clear();

    typeNameCache = nullptr;

    qDeleteAll(resolvedTypes);
    resolvedTypes.clear();

    engine = nullptr;
    qmlEngine = nullptr;
    free(runtimeStrings);
    runtimeStrings = nullptr;
    delete [] runtimeLookups;
    runtimeLookups = nullptr;
    delete [] runtimeRegularExpressions;
    runtimeRegularExpressions = nullptr;
    free(runtimeClasses);
    runtimeClasses = nullptr;
    for (QV4::Function *f : qAsConst(runtimeFunctions))
        f->destroy();
    runtimeFunctions.clear();
}

void CompilationUnit::markObjects(QV4::MarkStack *markStack)
{
    if (runtimeStrings) {
        for (uint i = 0, end = totalStringCount(); i < end; ++i)
            if (runtimeStrings[i])
                runtimeStrings[i]->mark(markStack);
    }
    if (runtimeRegularExpressions) {
        for (uint i = 0; i < data->regexpTableSize; ++i)
            runtimeRegularExpressions[i].mark(markStack);
    }
    if (runtimeClasses) {
        for (uint i = 0; i < data->jsClassTableSize; ++i)
            if (runtimeClasses[i])
                runtimeClasses[i]->mark(markStack);
    }
    for (QV4::Function *f : qAsConst(runtimeFunctions))
        if (f && f->internalClass)
            f->internalClass->mark(markStack);
    for (QV4::Heap::InternalClass *c : qAsConst(runtimeBlocks))
        if (c)
            c->mark(markStack);

    for (QV4::Heap::Object *o : qAsConst(templateObjects))
        if (o)
            o->mark(markStack);

    if (runtimeLookups) {
        for (uint i = 0; i < data->lookupTableSize; ++i)
            runtimeLookups[i].markObjects(markStack);
    }

    if (m_module)
        m_module->mark(markStack);
}

IdentifierHash CompilationUnit::createNamedObjectsPerComponent(int componentObjectIndex)
{
    IdentifierHash namedObjectCache(engine);
    const CompiledData::Object *component = objectAt(componentObjectIndex);
    const quint32_le *namedObjectIndexPtr = component->namedObjectsInComponentTable();
    for (quint32 i = 0; i < component->nNamedObjectsInComponent; ++i, ++namedObjectIndexPtr) {
        const CompiledData::Object *namedObject = objectAt(*namedObjectIndexPtr);
        namedObjectCache.add(runtimeStrings[namedObject->idNameIndex], namedObject->id);
    }
    return *namedObjectsPerComponentCache.insert(componentObjectIndex, namedObjectCache);
}

void CompilationUnit::finalizeCompositeType(QQmlEnginePrivate *qmlEngine)
{
    this->qmlEngine = qmlEngine;

    // Add to type registry of composites
    if (propertyCaches.needsVMEMetaObject(/*root object*/0)) {
        QQmlMetaType::registerInternalCompositeType(this);
        qmlEngine->registerInternalCompositeType(this);
    } else {
        const QV4::CompiledData::Object *obj = objectAt(/*root object*/0);
        auto *typeRef = resolvedTypes.value(obj->inheritedTypeNameIndex);
        Q_ASSERT(typeRef);
        if (typeRef->compilationUnit) {
            metaTypeId = typeRef->compilationUnit->metaTypeId;
            listMetaTypeId = typeRef->compilationUnit->listMetaTypeId;
        } else {
            metaTypeId = typeRef->type.typeId();
            listMetaTypeId = typeRef->type.qListTypeId();
        }
    }

    // Collect some data for instantiation later.
    int bindingCount = 0;
    int parserStatusCount = 0;
    int objectCount = 0;
    for (quint32 i = 0, count = this->objectCount(); i < count; ++i) {
        const QV4::CompiledData::Object *obj = objectAt(i);
        bindingCount += obj->nBindings;
        if (auto *typeRef = resolvedTypes.value(obj->inheritedTypeNameIndex)) {
            if (typeRef->type.isValid()) {
                if (typeRef->type.parserStatusCast() != -1)
                    ++parserStatusCount;
            }
            ++objectCount;
            if (typeRef->compilationUnit) {
                bindingCount += typeRef->compilationUnit->totalBindingsCount;
                parserStatusCount += typeRef->compilationUnit->totalParserStatusCount;
                objectCount += typeRef->compilationUnit->totalObjectCount;
            }
        }
    }

    totalBindingsCount = bindingCount;
    totalParserStatusCount = parserStatusCount;
    totalObjectCount = objectCount;
}

bool CompilationUnit::verifyChecksum(const DependentTypesHasher &dependencyHasher) const
{
    if (!dependencyHasher) {
        for (size_t i = 0; i < sizeof(data->dependencyMD5Checksum); ++i) {
            if (data->dependencyMD5Checksum[i] != 0)
                return false;
        }
        return true;
    }
    QCryptographicHash hash(QCryptographicHash::Md5);
    if (!dependencyHasher(&hash))
        return false;
    QByteArray checksum = hash.result();
    Q_ASSERT(checksum.size() == sizeof(data->dependencyMD5Checksum));
    return memcmp(data->dependencyMD5Checksum, checksum.constData(),
                  sizeof(data->dependencyMD5Checksum)) == 0;
}

QStringList CompilationUnit::moduleRequests() const
{
    QStringList requests;
    requests.reserve(data->moduleRequestTableSize);
    for (uint i = 0; i < data->moduleRequestTableSize; ++i)
        requests << stringAt(data->moduleRequestTable()[i]);
    return requests;
}

Heap::Module *CompilationUnit::instantiate(ExecutionEngine *engine)
{
    if (isESModule() && m_module)
        return m_module;

    if (data->indexOfRootFunction < 0)
        return nullptr;

    if (!this->engine)
        linkToEngine(engine);

    Scope scope(engine);
    Scoped<Module> module(scope, engine->memoryManager->allocate<Module>(engine, this));

    if (isESModule())
        m_module = module->d();

    for (const QString &request: moduleRequests()) {
        auto dependentModuleUnit = engine->loadModule(QUrl(request), this);
        if (engine->hasException)
            return nullptr;
        dependentModuleUnit->instantiate(engine);
    }

    ScopedString importName(scope);

    const uint importCount = data->importEntryTableSize;
    imports = new const Value *[importCount];
    memset(imports, 0, importCount * sizeof(Value *));
    for (uint i = 0; i < importCount; ++i) {
        const CompiledData::ImportEntry &entry = data->importEntryTable()[i];
        auto dependentModuleUnit = engine->loadModule(QUrl(stringAt(entry.moduleRequest)), this);
        importName = runtimeStrings[entry.importName];
        const Value *valuePtr = dependentModuleUnit->resolveExport(importName);
        if (!valuePtr) {
            QString referenceErrorMessage = QStringLiteral("Unable to resolve import reference ");
            referenceErrorMessage += importName->toQString();
            engine->throwReferenceError(referenceErrorMessage, fileName(), entry.location.line, entry.location.column);
            return nullptr;
        }
        imports[i] = valuePtr;
    }

    for (uint i = 0; i < data->indirectExportEntryTableSize; ++i) {
        const CompiledData::ExportEntry &entry = data->indirectExportEntryTable()[i];
        auto dependentModuleUnit = engine->loadModule(QUrl(stringAt(entry.moduleRequest)), this);
        if (!dependentModuleUnit)
            return nullptr;

        ScopedString importName(scope, runtimeStrings[entry.importName]);
        if (!dependentModuleUnit->resolveExport(importName)) {
            QString referenceErrorMessage = QStringLiteral("Unable to resolve re-export reference ");
            referenceErrorMessage += importName->toQString();
            engine->throwReferenceError(referenceErrorMessage, fileName(), entry.location.line, entry.location.column);
            return nullptr;
        }
    }

    return module->d();
}

const Value *CompilationUnit::resolveExport(QV4::String *exportName)
{
    QVector<ResolveSetEntry> resolveSet;
    return resolveExportRecursively(exportName, &resolveSet);
}

QStringList CompilationUnit::exportedNames() const
{
    QStringList names;
    QVector<const CompiledData::CompilationUnit*> exportNameSet;
    getExportedNamesRecursively(&names, &exportNameSet);
    names.sort();
    auto last = std::unique(names.begin(), names.end());
    names.erase(last, names.end());
    return names;
}

const Value *CompilationUnit::resolveExportRecursively(QV4::String *exportName, QVector<ResolveSetEntry> *resolveSet)
{
    if (!m_module)
        return nullptr;

    for (const auto &entry: *resolveSet)
        if (entry.module == this && entry.exportName->isEqualTo(exportName))
            return nullptr;

    (*resolveSet) << ResolveSetEntry(this, exportName);

    if (exportName->toQString() == QLatin1String("*"))
        return &m_module->self;

    Scope scope(engine);

    if (auto localExport = lookupNameInExportTable(data->localExportEntryTable(), data->localExportEntryTableSize, exportName)) {
        ScopedString localName(scope, runtimeStrings[localExport->localName]);
        uint index = m_module->scope->internalClass->indexOfValueOrGetter(localName->toPropertyKey());
        if (index == UINT_MAX)
            return nullptr;
        if (index >= m_module->scope->locals.size)
            return imports[index - m_module->scope->locals.size];
        return &m_module->scope->locals[index];
    }

    if (auto indirectExport = lookupNameInExportTable(data->indirectExportEntryTable(), data->indirectExportEntryTableSize, exportName)) {
        auto dependentModuleUnit = engine->loadModule(QUrl(stringAt(indirectExport->moduleRequest)), this);
        if (!dependentModuleUnit)
            return nullptr;
        ScopedString importName(scope, runtimeStrings[indirectExport->importName]);
        return dependentModuleUnit->resolveExportRecursively(importName, resolveSet);
    }


    if (exportName->toQString() == QLatin1String("default"))
        return nullptr;

    const Value *starResolution = nullptr;

    for (uint i = 0; i < data->starExportEntryTableSize; ++i) {
        const CompiledData::ExportEntry &entry = data->starExportEntryTable()[i];
        auto dependentModuleUnit = engine->loadModule(QUrl(stringAt(entry.moduleRequest)), this);
        if (!dependentModuleUnit)
            return nullptr;

        const Value *resolution = dependentModuleUnit->resolveExportRecursively(exportName, resolveSet);
        // ### handle ambiguous
        if (resolution) {
            if (!starResolution) {
                starResolution = resolution;
                continue;
            }
            if (resolution != starResolution)
                return nullptr;
        }
    }

    return starResolution;
}

const ExportEntry *CompilationUnit::lookupNameInExportTable(const ExportEntry *firstExportEntry, int tableSize, QV4::String *name) const
{
    const CompiledData::ExportEntry *lastExportEntry = firstExportEntry + tableSize;
    auto matchingExport = std::lower_bound(firstExportEntry, lastExportEntry, name, [this](const CompiledData::ExportEntry &lhs, QV4::String *name) {
        return stringAt(lhs.exportName) < name->toQString();
    });
    if (matchingExport == lastExportEntry || stringAt(matchingExport->exportName) != name->toQString())
        return nullptr;
    return matchingExport;
}

void CompilationUnit::getExportedNamesRecursively(QStringList *names, QVector<const CompilationUnit*> *exportNameSet, bool includeDefaultExport) const
{
    if (exportNameSet->contains(this))
        return;
    exportNameSet->append(this);

    const auto append = [names, includeDefaultExport](const QString &name) {
        if (!includeDefaultExport && name == QLatin1String("default"))
            return;
        names->append(name);
    };

    for (uint i = 0; i < data->localExportEntryTableSize; ++i) {
        const CompiledData::ExportEntry &entry = data->localExportEntryTable()[i];
        append(stringAt(entry.exportName));
    }

    for (uint i = 0; i < data->indirectExportEntryTableSize; ++i) {
        const CompiledData::ExportEntry &entry = data->indirectExportEntryTable()[i];
        append(stringAt(entry.exportName));
    }

    for (uint i = 0; i < data->starExportEntryTableSize; ++i) {
        const CompiledData::ExportEntry &entry = data->starExportEntryTable()[i];
        auto dependentModuleUnit = engine->loadModule(QUrl(stringAt(entry.moduleRequest)), this);
        if (!dependentModuleUnit)
            return;
        dependentModuleUnit->getExportedNamesRecursively(names, exportNameSet, /*includeDefaultExport*/false);
    }
}

void CompilationUnit::evaluate()
{
    QV4::Scope scope(engine);
    QV4::Scoped<Module> module(scope, m_module);
    module->evaluate();
}

void CompilationUnit::evaluateModuleRequests()
{
    for (const QString &request: moduleRequests()) {
        auto dependentModuleUnit = engine->loadModule(QUrl(request), this);
        if (engine->hasException)
            return;
        dependentModuleUnit->evaluate();
        if (engine->hasException)
            return;
    }
}

bool CompilationUnit::loadFromDisk(const QUrl &url, const QDateTime &sourceTimeStamp, QString *errorString)
{
    if (!QQmlFile::isLocalFile(url)) {
        *errorString = QStringLiteral("File has to be a local file.");
        return false;
    }

    const QString sourcePath = QQmlFile::urlToLocalFileOrQrc(url);
    QScopedPointer<CompilationUnitMapper> cacheFile(new CompilationUnitMapper());

    const QStringList cachePaths = { sourcePath + QLatin1Char('c'), localCacheFilePath(url) };
    for (const QString &cachePath : cachePaths) {
        CompiledData::Unit *mappedUnit = cacheFile->open(cachePath, sourceTimeStamp, errorString);
        if (!mappedUnit)
            continue;

        const Unit * const oldDataPtr = (data && !(data->flags & QV4::CompiledData::Unit::StaticData)) ? data : nullptr;
        const Unit *oldData = data;
        auto dataPtrRevert = qScopeGuard([this, oldData](){
            setUnitData(oldData);
        });
        setUnitData(mappedUnit);

        if (data->sourceFileIndex != 0 && sourcePath != QQmlFile::urlToLocalFileOrQrc(stringAt(data->sourceFileIndex))) {
            *errorString = QStringLiteral("QML source file has moved to a different location.");
            continue;
        }

        dataPtrRevert.dismiss();
        free(const_cast<Unit*>(oldDataPtr));
        backingFile.reset(cacheFile.take());
        return true;
    }

    return false;
}

#endif // V4_BOOTSTRAP

#if defined(V4_BOOTSTRAP)
bool CompilationUnit::saveToDisk(const QString &outputFileName, QString *errorString)
#else
bool CompilationUnit::saveToDisk(const QUrl &unitUrl, QString *errorString)
#endif
{
    errorString->clear();

#if !defined(V4_BOOTSTRAP)
    if (data->sourceTimeStamp == 0) {
        *errorString = QStringLiteral("Missing time stamp for source file");
        return false;
    }

    if (!QQmlFile::isLocalFile(unitUrl)) {
        *errorString = QStringLiteral("File has to be a local file.");
        return false;
    }
    const QString outputFileName = localCacheFilePath(unitUrl);
#endif

#if QT_CONFIG(temporaryfile)
    // Foo.qml -> Foo.qmlc
    QSaveFile cacheFile(outputFileName);
    if (!cacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *errorString = cacheFile.errorString();
        return false;
    }

    QByteArray modifiedUnit;
    modifiedUnit.resize(data->unitSize);
    memcpy(modifiedUnit.data(), data, data->unitSize);
    const char *dataPtr = modifiedUnit.data();
    Unit *unitPtr;
    memcpy(&unitPtr, &dataPtr, sizeof(unitPtr));
    unitPtr->flags |= Unit::StaticData;

    qint64 headerWritten = cacheFile.write(modifiedUnit);
    if (headerWritten != modifiedUnit.size()) {
        *errorString = cacheFile.errorString();
        return false;
    }

    if (!cacheFile.commit()) {
        *errorString = cacheFile.errorString();
        return false;
    }

    return true;
#else
    Q_UNUSED(outputFileName)
    *errorString = QStringLiteral("features.temporaryfile is disabled.");
    return false;
#endif // QT_CONFIG(temporaryfile)
}

void CompilationUnit::setUnitData(const Unit *unitData, const QmlUnit *qmlUnit,
                                  const QString &fileName, const QString &finalUrlString)
{
    data = unitData;
    qmlData = nullptr;
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    delete [] constants;
#endif
    constants = nullptr;
    m_fileName.clear();
    m_finalUrlString.clear();
    if (!data)
        return;

    qmlData = qmlUnit ? qmlUnit : data->qmlUnit();

#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    Value *bigEndianConstants = new Value[data->constantTableSize];
    const quint64_le *littleEndianConstants = data->constants();
    for (uint i = 0; i < data->constantTableSize; ++i)
        bigEndianConstants[i] = Value::fromReturnedValue(littleEndianConstants[i]);
    constants = bigEndianConstants;
#else
    constants = reinterpret_cast<const Value*>(data->constants());
#endif

    m_fileName = !fileName.isEmpty() ? fileName : stringAt(data->sourceFileIndex);
    m_finalUrlString = !finalUrlString.isEmpty() ? finalUrlString : stringAt(data->finalUrlIndex);
}

#ifndef V4_BOOTSTRAP
QString Binding::valueAsString(const CompilationUnit *unit) const
{
    switch (type) {
    case Type_Script:
    case Type_String:
        return unit->stringAt(stringIndex);
    case Type_Null:
        return QStringLiteral("null");
    case Type_Boolean:
        return value.b ? QStringLiteral("true") : QStringLiteral("false");
    case Type_Number:
        return QString::number(valueAsNumber(unit->constants));
    case Type_Invalid:
        return QString();
#if !QT_CONFIG(translation)
    case Type_TranslationById:
    case Type_Translation:
        return unit->stringAt(unit->unitData()->translations()[value.translationDataIndex].stringIndex);
#else
    case Type_TranslationById: {
        const TranslationData &translation = unit->unitData()->translations()[value.translationDataIndex];
        QByteArray id = unit->stringAt(translation.stringIndex).toUtf8();
        return qtTrId(id.constData(), translation.number);
    }
    case Type_Translation: {
        const TranslationData &translation = unit->unitData()->translations()[value.translationDataIndex];
        // This code must match that in the qsTr() implementation
        const QString &path = unit->fileName();
        int lastSlash = path.lastIndexOf(QLatin1Char('/'));
        QStringRef context = (lastSlash > -1) ? path.midRef(lastSlash + 1, path.length() - lastSlash - 5)
                                              : QStringRef();
        QByteArray contextUtf8 = context.toUtf8();
        QByteArray comment = unit->stringAt(translation.commentIndex).toUtf8();
        QByteArray text = unit->stringAt(translation.stringIndex).toUtf8();
        return QCoreApplication::translate(contextUtf8.constData(), text.constData(),
                                           comment.constData(), translation.number);
    }
#endif
    default:
        break;
    }
    return QString();
}

//reverse of Lexer::singleEscape()
QString Binding::escapedString(const QString &string)
{
    QString tmp = QLatin1String("\"");
    for (int i = 0; i < string.length(); ++i) {
        const QChar &c = string.at(i);
        switch (c.unicode()) {
        case 0x08:
            tmp += QLatin1String("\\b");
            break;
        case 0x09:
            tmp += QLatin1String("\\t");
            break;
        case 0x0A:
            tmp += QLatin1String("\\n");
            break;
        case 0x0B:
            tmp += QLatin1String("\\v");
            break;
        case 0x0C:
            tmp += QLatin1String("\\f");
            break;
        case 0x0D:
            tmp += QLatin1String("\\r");
            break;
        case 0x22:
            tmp += QLatin1String("\\\"");
            break;
        case 0x27:
            tmp += QLatin1String("\\\'");
            break;
        case 0x5C:
            tmp += QLatin1String("\\\\");
            break;
        default:
            tmp += c;
            break;
        }
    }
    tmp += QLatin1Char('\"');
    return tmp;
}

QString Binding::valueAsScriptString(const CompilationUnit *unit) const
{
    if (type == Type_String)
        return escapedString(unit->stringAt(stringIndex));
    else
        return valueAsString(unit);
}

/*!
Returns the property cache, if one alread exists.  The cache is not referenced.
*/
QQmlRefPointer<QQmlPropertyCache> ResolvedTypeReference::propertyCache() const
{
    if (type.isValid())
        return typePropertyCache;
    else
        return compilationUnit->rootPropertyCache();
}

/*!
Returns the property cache, creating one if it doesn't already exist.  The cache is not referenced.
*/
QQmlRefPointer<QQmlPropertyCache> ResolvedTypeReference::createPropertyCache(QQmlEngine *engine)
{
    if (typePropertyCache) {
        return typePropertyCache;
    } else if (type.isValid()) {
        typePropertyCache = QQmlEnginePrivate::get(engine)->cache(type.metaObject(), minorVersion);
        return typePropertyCache;
    } else {
        return compilationUnit->rootPropertyCache();
    }
}

bool ResolvedTypeReference::addToHash(QCryptographicHash *hash, QQmlEngine *engine)
{
    if (type.isValid()) {
        bool ok = false;
        hash->addData(createPropertyCache(engine)->checksum(&ok));
        return ok;
    }
    hash->addData(compilationUnit->unitData()->md5Checksum, sizeof(compilationUnit->unitData()->md5Checksum));
    return true;
}

template <typename T>
bool qtTypeInherits(const QMetaObject *mo) {
    while (mo) {
        if (mo == &T::staticMetaObject)
            return true;
        mo = mo->superClass();
    }
    return false;
}

void ResolvedTypeReference::doDynamicTypeCheck()
{
    const QMetaObject *mo = nullptr;
    if (typePropertyCache)
        mo = typePropertyCache->firstCppMetaObject();
    else if (type.isValid())
        mo = type.metaObject();
    else if (compilationUnit)
        mo = compilationUnit->rootPropertyCache()->firstCppMetaObject();
    isFullyDynamicType = qtTypeInherits<QQmlPropertyMap>(mo);
}

bool ResolvedTypeReferenceMap::addToHash(QCryptographicHash *hash, QQmlEngine *engine) const
{
    for (auto it = constBegin(), end = constEnd(); it != end; ++it) {
        if (!it.value()->addToHash(hash, engine))
            return false;
    }

    return true;
}

#endif

void CompilationUnit::destroy()
{
#if !defined(V4_BOOTSTRAP)
    if (qmlEngine)
        QQmlEnginePrivate::deleteInEngineThread(qmlEngine, this);
    else
#endif
        delete this;
}


void Unit::generateChecksum()
{
#ifndef V4_BOOTSTRAP
    QCryptographicHash hash(QCryptographicHash::Md5);

    const int checksummableDataOffset = offsetof(QV4::CompiledData::Unit, md5Checksum) + sizeof(md5Checksum);

    const char *dataPtr = reinterpret_cast<const char *>(this) + checksummableDataOffset;
    hash.addData(dataPtr, unitSize - checksummableDataOffset);

    QByteArray checksum = hash.result();
    Q_ASSERT(checksum.size() == sizeof(md5Checksum));
    memcpy(md5Checksum, checksum.constData(), sizeof(md5Checksum));
#else
    memset(md5Checksum, 0, sizeof(md5Checksum));
#endif
}

bool Unit::verifyHeader(QDateTime expectedSourceTimeStamp, QString *errorString) const
{
#ifndef V4_BOOTSTRAP
    if (strncmp(magic, CompiledData::magic_str, sizeof(magic))) {
        *errorString = QStringLiteral("Magic bytes in the header do not match");
        return false;
    }

    if (version != quint32(QV4_DATA_STRUCTURE_VERSION)) {
        *errorString = QString::fromUtf8("V4 data structure version mismatch. Found %1 expected %2").arg(version, 0, 16).arg(QV4_DATA_STRUCTURE_VERSION, 0, 16);
        return false;
    }

    if (qtVersion != quint32(QT_VERSION)) {
        *errorString = QString::fromUtf8("Qt version mismatch. Found %1 expected %2").arg(qtVersion, 0, 16).arg(QT_VERSION, 0, 16);
        return false;
    }

    if (sourceTimeStamp) {
        // Files from the resource system do not have any time stamps, so fall back to the application
        // executable.
        if (!expectedSourceTimeStamp.isValid())
            expectedSourceTimeStamp = QFileInfo(QCoreApplication::applicationFilePath()).lastModified();

        if (expectedSourceTimeStamp.isValid() && expectedSourceTimeStamp.toMSecsSinceEpoch() != sourceTimeStamp) {
            *errorString = QStringLiteral("QML source file has a different time stamp than cached file.");
            return false;
        }
    }

#if defined(QML_COMPILE_HASH)
    if (qstrcmp(CompiledData::qml_compile_hash, libraryVersionHash) != 0) {
        *errorString = QStringLiteral("QML library version mismatch. Expected compile hash does not match");
        return false;
    }
#else
#error "QML_COMPILE_HASH must be defined for the build of QtDeclarative to ensure version checking for cache files"
#endif

    return true;
#else
    Q_UNUSED(expectedSourceTimeStamp)
    Q_UNUSED(errorString)
    return false;
#endif
}

Location &Location::operator=(const QQmlJS::AST::SourceLocation &astLocation)
{
    line = astLocation.startLine;
    column = astLocation.startColumn;
    return *this;
}

}

}

QT_END_NAMESPACE
