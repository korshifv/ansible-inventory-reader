#include "inventorydocument.h"

#include "inventorytreemodel.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QPointF>
#include <QSaveFile>

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace {
QString localPathFromUrl(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

QString yamlExceptionMessage(const std::exception &exception)
{
    return QString::fromUtf8(exception.what());
}
}

InventoryDocument::InventoryDocument(QObject *parent)
    : QObject(parent)
{
    clearData();
    m_treeModel = std::make_unique<InventoryTreeModel>(this);
    rebuildGraph();
}

InventoryDocument::~InventoryDocument() = default;

QString InventoryDocument::filePath() const { return m_filePath; }
bool InventoryDocument::modified() const { return m_modified; }
QString InventoryDocument::errorString() const { return m_errorString; }
QVariantList InventoryDocument::graphNodes() const { return m_graphNodes; }
QVariantList InventoryDocument::graphEdges() const { return m_graphEdges; }
qreal InventoryDocument::graphWidth() const { return m_graphWidth; }
qreal InventoryDocument::graphHeight() const { return m_graphHeight; }
InventoryTreeModel *InventoryDocument::treeModel() const { return m_treeModel.get(); }

QStringList InventoryDocument::groupNames() const
{
    return sortedGroupNames(true);
}

void InventoryDocument::newDocument()
{
    clearData();
    m_filePath.clear();
    setError({});
    setModified(false);
    emit filePathChanged();
    changed(false);
}

bool InventoryDocument::openFile(const QUrl &url)
{
    const QString path = localPathFromUrl(url);
    if (path.isEmpty()) {
        setError(QStringLiteral("No inventory file selected."));
        return false;
    }

    try {
        const YAML::Node root = YAML::LoadFile(path.toStdString());
        clearData();
        if (!parseYaml(root)) {
            clearData();
            changed(false);
            return false;
        }
    } catch (const std::exception &exception) {
        clearData();
        changed(false);
        setError(QStringLiteral("Failed to parse %1: %2")
                     .arg(QDir::toNativeSeparators(path), yamlExceptionMessage(exception)));
        return false;
    }

    m_filePath = QFileInfo(path).absoluteFilePath();
    setError({});
    setModified(false);
    emit filePathChanged();
    changed(false);
    return true;
}

bool InventoryDocument::save()
{
    if (m_filePath.isEmpty()) {
        setError(QStringLiteral("Choose a file name first."));
        return false;
    }
    return saveAs(QUrl::fromLocalFile(m_filePath));
}

bool InventoryDocument::saveAs(const QUrl &url)
{
    const QString path = localPathFromUrl(url);
    if (path.isEmpty()) {
        setError(QStringLiteral("No destination file selected."));
        return false;
    }

    try {
        YAML::Emitter emitter;
        emitter.SetIndent(2);
        emitter << serializeYaml();
        if (!emitter.good()) {
            setError(QStringLiteral("YAML emitter failed: %1")
                         .arg(QString::fromStdString(emitter.GetLastError())));
            return false;
        }

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            setError(QStringLiteral("Cannot write %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString()));
            return false;
        }

        const QByteArray bytes(emitter.c_str(), static_cast<qsizetype>(emitter.size()));
        if (file.write(bytes) != bytes.size() || file.write("\n", 1) != 1) {
            setError(QStringLiteral("Failed while writing %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString()));
            return false;
        }

        if (!file.commit()) {
            setError(QStringLiteral("Failed to commit %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString()));
            return false;
        }
    } catch (const std::exception &exception) {
        setError(QStringLiteral("Failed to serialize inventory: %1")
                     .arg(yamlExceptionMessage(exception)));
        return false;
    }

    if (m_filePath != QFileInfo(path).absoluteFilePath()) {
        m_filePath = QFileInfo(path).absoluteFilePath();
        emit filePathChanged();
    }
    setError({});
    setModified(false);
    return true;
}

void InventoryDocument::clearData()
{
    m_groups.clear();
    m_hosts.clear();
    ensureGroup(QStringLiteral("all"));
}

InventoryDocument::GroupRecord &InventoryDocument::ensureGroup(const QString &name)
{
    if (!m_groups.contains(name)) {
        GroupRecord record;
        record.name = name;
        m_groups.insert(name, record);
    }
    return m_groups[name];
}

InventoryDocument::HostRecord &InventoryDocument::ensureHost(const QString &name)
{
    if (!m_hosts.contains(name)) {
        HostRecord record;
        record.name = name;
        m_hosts.insert(name, record);
    }
    return m_hosts[name];
}

bool InventoryDocument::parseYaml(const YAML::Node &root)
{
    if (!root || !root.IsMap()) {
        setError(QStringLiteral("Inventory root must be a YAML mapping."));
        return false;
    }

    QSet<QString> stack;

    if (root["all"])
        parseGroupDefinition(QStringLiteral("all"), root["all"], QString(), stack);

    for (auto it = root.begin(); it != root.end(); ++it) {
        const QString groupName = QString::fromStdString(it->first.as<std::string>());
        if (groupName == QStringLiteral("all"))
            continue;
        parseGroupDefinition(groupName, it->second, QString(), stack);
    }

    if (!validateGroupGraph())
        return false;

    setError({});
    return true;
}

void InventoryDocument::parseGroupDefinition(const QString &name,
                                             const YAML::Node &node,
                                             const QString &parent,
                                             QSet<QString> &stack)
{
    ensureGroup(name);

    if (!parent.isEmpty() && parent != QStringLiteral("all")) {
        ensureGroup(parent);
        m_groups[parent].children.insert(name);
        m_groups[name].parents.insert(parent);
    }

    GroupRecord &group = m_groups[name];

    if (!node || node.IsNull())
        return;
    if (!node.IsMap())
        throw std::runtime_error(QStringLiteral("Group '%1' must be a mapping.").arg(name).toStdString());

    if (node["vars"]) {
        if (!node["vars"].IsMap())
            throw std::runtime_error(QStringLiteral("Group '%1'.vars must be a mapping.").arg(name).toStdString());
        mergeYamlMap(group.vars, node["vars"]);
    }

    if (node["hosts"]) {
        if (!node["hosts"].IsMap())
            throw std::runtime_error(QStringLiteral("Group '%1'.hosts must be a mapping.").arg(name).toStdString());

        for (auto it = node["hosts"].begin(); it != node["hosts"].end(); ++it) {
            const QString hostName = QString::fromStdString(it->first.as<std::string>());
            HostRecord &host = ensureHost(hostName);
            if (it->second && !it->second.IsNull()) {
                if (!it->second.IsMap())
                    throw std::runtime_error(QStringLiteral("Host '%1' variables must be a mapping.").arg(hostName).toStdString());
                mergeYamlMap(host.vars, it->second);
                if (it->second.size() > 0 && host.varsOwner.isEmpty())
                    host.varsOwner = name;
            }

            if (name == QStringLiteral("all")) {
                host.explicitAll = true;
            } else {
                host.groups.insert(name);
                group.hosts.insert(hostName);
            }
        }
    }

    if (!node["children"])
        return;
    if (!node["children"].IsMap())
        throw std::runtime_error(QStringLiteral("Group '%1'.children must be a mapping.").arg(name).toStdString());

    if (stack.contains(name))
        throw std::runtime_error(QStringLiteral("Circular nested group definition at '%1'.").arg(name).toStdString());

    stack.insert(name);
    for (auto it = node["children"].begin(); it != node["children"].end(); ++it) {
        const QString childName = QString::fromStdString(it->first.as<std::string>());
        parseGroupDefinition(childName, it->second, name, stack);
    }
    stack.remove(name);
}

void InventoryDocument::mergeYamlMap(YAML::Node &destination, const YAML::Node &source)
{
    if (!destination || !destination.IsMap())
        destination = YAML::Node(YAML::NodeType::Map);
    for (auto it = source.begin(); it != source.end(); ++it)
        destination[YAML::Clone(it->first)] = YAML::Clone(it->second);
}

YAML::Node InventoryDocument::serializeYaml() const
{
    YAML::Node root(YAML::NodeType::Map);
    YAML::Node allNode(YAML::NodeType::Map);

    const auto allIt = m_groups.constFind(QStringLiteral("all"));
    const GroupRecord &all = allIt.value();
    if (all.vars && all.vars.IsMap() && all.vars.size() > 0)
        allNode["vars"] = YAML::Clone(all.vars);

    YAML::Node allHosts(YAML::NodeType::Map);
    bool haveAllHosts = false;
    for (const QString &hostName : sortedHostNames()) {
        const auto hostIt = m_hosts.constFind(hostName);
        const HostRecord &host = hostIt.value();
        if (host.groups.isEmpty() || host.explicitAll) {
            allHosts[hostName.toStdString()] = hostVarsForLocation(host, QStringLiteral("all"));
            haveAllHosts = true;
        }
    }
    if (haveAllHosts)
        allNode["hosts"] = allHosts;

    YAML::Node topChildren(YAML::NodeType::Map);
    bool haveTopChildren = false;
    for (const QString &groupName : sortedGroupNames(false)) {
        if (m_groups.constFind(groupName).value().parents.isEmpty()) {
            topChildren[groupName.toStdString()] = emptyMap();
            haveTopChildren = true;
        }
    }
    if (haveTopChildren)
        allNode["children"] = topChildren;

    root["all"] = allNode;

    for (const QString &groupName : sortedGroupNames(false)) {
        const auto groupIt = m_groups.constFind(groupName);
        const GroupRecord &group = groupIt.value();
        YAML::Node groupNode(YAML::NodeType::Map);

        if (group.vars && group.vars.IsMap() && group.vars.size() > 0)
            groupNode["vars"] = YAML::Clone(group.vars);

        if (!group.hosts.isEmpty()) {
            YAML::Node hosts(YAML::NodeType::Map);
            QStringList hostNames = group.hosts.values();
            hostNames.sort(Qt::CaseInsensitive);
            for (const QString &hostName : hostNames)
                hosts[hostName.toStdString()] = hostVarsForLocation(m_hosts.constFind(hostName).value(), groupName);
            groupNode["hosts"] = hosts;
        }

        if (!group.children.isEmpty()) {
            YAML::Node children(YAML::NodeType::Map);
            QStringList childNames = group.children.values();
            childNames.sort(Qt::CaseInsensitive);
            for (const QString &childName : childNames)
                children[childName.toStdString()] = emptyMap();
            groupNode["children"] = children;
        }

        root[groupName.toStdString()] = groupNode;
    }

    return root;
}

YAML::Node InventoryDocument::hostVarsForLocation(const HostRecord &host, const QString &location) const
{
    if (canonicalHostOwner(host) == location && host.vars && host.vars.IsMap() && host.vars.size() > 0)
        return YAML::Clone(host.vars);
    return emptyMap();
}

QString InventoryDocument::canonicalHostOwner(const HostRecord &host) const
{
    if (!host.varsOwner.isEmpty()) {
        if (host.varsOwner == QStringLiteral("all") && (host.explicitAll || host.groups.isEmpty()))
            return QStringLiteral("all");
        if (host.groups.contains(host.varsOwner))
            return host.varsOwner;
    }

    if (host.explicitAll || host.groups.isEmpty())
        return QStringLiteral("all");

    QStringList groups = host.groups.values();
    groups.sort(Qt::CaseInsensitive);
    return groups.constFirst();
}

YAML::Node InventoryDocument::emptyMap()
{
    return YAML::Node(YAML::NodeType::Map);
}

QString InventoryDocument::emitYaml(const YAML::Node &node)
{
    if (!node || !node.IsMap() || node.size() == 0)
        return {};

    YAML::Emitter emitter;
    emitter.SetIndent(2);
    emitter << node;
    return emitter.good() ? QString::fromUtf8(emitter.c_str()) : QString();
}
