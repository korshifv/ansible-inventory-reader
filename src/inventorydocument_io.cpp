#include "inventorydocument.h"

#include "inventorytreemodel.h"

#include <QDir>
#include <QFile>
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

int leadingSpaces(const QString &line)
{
    int count = 0;
    while (count < line.size() && line.at(count) == QLatin1Char(' '))
        ++count;
    return count;
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
    m_sourceLines.clear();
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

    QFile input(path);
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(QStringLiteral("Cannot read %1: %2")
                     .arg(QDir::toNativeSeparators(path), input.errorString()));
        return false;
    }

    const QByteArray sourceBytes = input.readAll();
    const QString sourceText = QString::fromUtf8(sourceBytes);

    try {
        const YAML::Node root = YAML::Load(std::string(sourceBytes.constData(),
                                                       static_cast<std::size_t>(sourceBytes.size())));
        clearData();
        m_sourceLines = sourceText.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        if (!parseYaml(root)) {
            m_sourceLines.clear();
            clearData();
            changed(false);
            return false;
        }
        m_sourceLines.clear();
    } catch (const std::exception &exception) {
        m_sourceLines.clear();
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
        const QString yamlText = serializeYamlText();
        if (yamlText.isEmpty()) {
            setError(QStringLiteral("YAML emitter failed."));
            return false;
        }

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            setError(QStringLiteral("Cannot write %1: %2")
                         .arg(QDir::toNativeSeparators(path), file.errorString()));
            return false;
        }

        const QByteArray bytes = yamlText.toUtf8();
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

            if (host.comment.isEmpty()) {
                const YAML::Mark mark = it->first.Mark();
                if (!mark.is_null())
                    host.comment = sourceCommentAfterLine(mark.line);
            }

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

QString InventoryDocument::serializeYamlText() const
{
    YAML::Emitter emitter;
    emitter.SetIndent(2);
    emitter << serializeYaml();
    if (!emitter.good())
        throw std::runtime_error(emitter.GetLastError());
    return injectHostComments(QString::fromUtf8(emitter.c_str()));
}

QString InventoryDocument::injectHostComments(const QString &yamlText) const
{
    QStringList lines = yamlText.split(QLatin1Char('\n'), Qt::KeepEmptyParts);

    QHash<QString, QString> groupKeys;
    for (const QString &groupName : sortedGroupNames(true))
        groupKeys.insert(emitYamlScalar(groupName), groupName);

    QHash<QString, QString> hostKeys;
    for (const QString &hostName : sortedHostNames())
        hostKeys.insert(emitYamlScalar(hostName), hostName);

    QString currentGroup;
    bool inHosts = false;

    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.isEmpty())
            continue;

        const int indent = leadingSpaces(lines.at(i));

        if (indent == 0) {
            currentGroup.clear();
            inHosts = false;
            for (auto it = groupKeys.cbegin(); it != groupKeys.cend(); ++it) {
                if (trimmed == it.key() + QLatin1Char(':')) {
                    currentGroup = it.value();
                    break;
                }
            }
            continue;
        }

        if (currentGroup.isEmpty())
            continue;

        if (indent == 2) {
            inHosts = trimmed == QStringLiteral("hosts:");
            continue;
        }

        if (!inHosts)
            continue;
        if (indent <= 2) {
            inHosts = false;
            continue;
        }
        if (indent != 4)
            continue;

        QString hostName;
        QString hostKey;
        for (auto it = hostKeys.cbegin(); it != hostKeys.cend(); ++it) {
            const QString prefix = it.key() + QLatin1Char(':');
            if (trimmed == prefix || trimmed.startsWith(prefix + QLatin1Char(' '))) {
                hostName = it.value();
                hostKey = it.key();
                break;
            }
        }

        if (hostName.isEmpty())
            continue;

        const auto hostIt = m_hosts.constFind(hostName);
        if (hostIt == m_hosts.cend() || hostIt->comment.trimmed().isEmpty())
            continue;
        if (canonicalHostOwner(hostIt.value()) != currentGroup)
            continue;

        const bool inlineEmptyMap = trimmed == hostKey + QStringLiteral(": {}");
        const QString childIndent(indent + 2, QLatin1Char(' '));

        if (inlineEmptyMap)
            lines[i] = QString(indent, QLatin1Char(' ')) + hostKey + QLatin1Char(':');

        QStringList commentLines = hostIt->comment.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        int insertAt = i + 1;
        for (const QString &commentLine : commentLines) {
            const QString text = commentLine.trimmed();
            lines.insert(insertAt++, childIndent + (text.isEmpty()
                                                    ? QStringLiteral("#")
                                                    : QStringLiteral("# ") + text));
        }

        if (inlineEmptyMap)
            lines.insert(insertAt++, childIndent + QStringLiteral("{}"));

        i = insertAt - 1;
    }

    return lines.join(QLatin1Char('\n'));
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

QString InventoryDocument::sourceCommentAfterLine(int sourceLine) const
{
    if (sourceLine < 0 || sourceLine >= m_sourceLines.size())
        return {};

    const int baseIndent = leadingSpaces(m_sourceLines.at(sourceLine));
    QStringList comments;
    bool foundComment = false;

    for (int i = sourceLine + 1; i < m_sourceLines.size(); ++i) {
        QString line = m_sourceLines.at(i);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            if (foundComment)
                comments.append(QString());
            continue;
        }

        const int indent = leadingSpaces(line);
        if (indent <= baseIndent)
            break;
        if (!trimmed.startsWith(QLatin1Char('#')))
            break;

        foundComment = true;
        QString text = trimmed.mid(1);
        if (text.startsWith(QLatin1Char(' ')))
            text.remove(0, 1);
        comments.append(text);
    }

    while (!comments.isEmpty() && comments.constLast().isEmpty())
        comments.removeLast();
    return comments.join(QLatin1Char('\n')).trimmed();
}

QString InventoryDocument::extractLeadingYamlComment(const QString &yamlText)
{
    const QStringList lines = yamlText.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    QStringList comments;
    bool foundComment = false;

    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QString trimmed = line.trimmed();

        if (trimmed.isEmpty()) {
            if (foundComment)
                comments.append(QString());
            continue;
        }

        if (!trimmed.startsWith(QLatin1Char('#')))
            break;

        foundComment = true;
        QString text = trimmed.mid(1);
        if (text.startsWith(QLatin1Char(' ')))
            text.remove(0, 1);
        comments.append(text);
    }

    while (!comments.isEmpty() && comments.constLast().isEmpty())
        comments.removeLast();
    return comments.join(QLatin1Char('\n')).trimmed();
}

QString InventoryDocument::formatHostEditorYaml(const HostRecord &host)
{
    QStringList result;
    if (!host.comment.trimmed().isEmpty()) {
        const QStringList comments = host.comment.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        for (const QString &comment : comments) {
            const QString text = comment.trimmed();
            result.append(text.isEmpty() ? QStringLiteral("#") : QStringLiteral("# ") + text);
        }
    }

    const QString vars = emitYaml(host.vars);
    if (!vars.isEmpty())
        result.append(vars);

    return result.join(QLatin1Char('\n'));
}

QString InventoryDocument::emitYamlScalar(const QString &value)
{
    YAML::Emitter emitter;
    emitter << value.toStdString();
    if (!emitter.good())
        return value;
    return QString::fromUtf8(emitter.c_str());
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
