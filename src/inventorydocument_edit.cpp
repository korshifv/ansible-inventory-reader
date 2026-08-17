#include "inventorydocument.h"

#include <algorithm>
#include <functional>

bool InventoryDocument::addGroup(const QString &rawName, const QString &parentGroup)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty()) {
        setError(QStringLiteral("Group name cannot be empty."));
        return false;
    }
    if (m_groups.contains(name)) {
        setError(QStringLiteral("Group '%1' already exists.").arg(name));
        return false;
    }
    if (m_hosts.contains(name)) {
        setError(QStringLiteral("A host named '%1' already exists.").arg(name));
        return false;
    }

    ensureGroup(name);
    if (!parentGroup.isEmpty() && parentGroup != QStringLiteral("all")) {
        if (!m_groups.contains(parentGroup)) {
            m_groups.remove(name);
            setError(QStringLiteral("Parent group '%1' does not exist.").arg(parentGroup));
            return false;
        }
        m_groups[parentGroup].children.insert(name);
        m_groups[name].parents.insert(parentGroup);
    }

    setError({});
    changed();
    return true;
}

bool InventoryDocument::addHost(const QString &rawName, const QString &group)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty()) {
        setError(QStringLiteral("Host name cannot be empty."));
        return false;
    }
    if (m_hosts.contains(name)) {
        setError(QStringLiteral("Host '%1' already exists.").arg(name));
        return false;
    }
    if (m_groups.contains(name)) {
        setError(QStringLiteral("A group named '%1' already exists.").arg(name));
        return false;
    }

    HostRecord &host = ensureHost(name);
    if (group.isEmpty() || group == QStringLiteral("all")) {
        host.explicitAll = true;
    } else {
        if (!m_groups.contains(group)) {
            m_hosts.remove(name);
            setError(QStringLiteral("Group '%1' does not exist.").arg(group));
            return false;
        }
        host.groups.insert(group);
        m_groups[group].hosts.insert(name);
    }

    setError({});
    changed();
    return true;
}

bool InventoryDocument::deleteNode(const QString &type, const QString &name)
{
    if (type == QStringLiteral("host")) {
        auto it = m_hosts.find(name);
        if (it == m_hosts.end()) {
            setError(QStringLiteral("Unknown host '%1'.").arg(name));
            return false;
        }
        for (const QString &group : it->groups)
            m_groups[group].hosts.remove(name);
        m_hosts.erase(it);
    } else if (type == QStringLiteral("group")) {
        if (name == QStringLiteral("all")) {
            setError(QStringLiteral("The 'all' group cannot be deleted."));
            return false;
        }
        auto it = m_groups.find(name);
        if (it == m_groups.end()) {
            setError(QStringLiteral("Unknown group '%1'.").arg(name));
            return false;
        }

        const GroupRecord group = it.value();
        for (const QString &parent : group.parents)
            m_groups[parent].children.remove(name);
        for (const QString &child : group.children)
            m_groups[child].parents.remove(name);
        for (const QString &hostName : group.hosts) {
            m_hosts[hostName].groups.remove(name);
            if (m_hosts[hostName].varsOwner == name)
                m_hosts[hostName].varsOwner.clear();
            if (m_hosts[hostName].groups.isEmpty())
                m_hosts[hostName].explicitAll = true;
        }
        m_groups.erase(it);
    } else {
        setError(QStringLiteral("Unknown node type '%1'.").arg(type));
        return false;
    }

    setError({});
    changed();
    return true;
}

bool InventoryDocument::renameNode(const QString &type,
                                   const QString &oldName,
                                   const QString &rawNewName)
{
    const QString newName = rawNewName.trimmed();
    if (newName.isEmpty()) {
        setError(QStringLiteral("Name cannot be empty."));
        return false;
    }
    if (oldName == newName)
        return true;
    if (m_groups.contains(newName) || m_hosts.contains(newName)) {
        setError(QStringLiteral("'%1' already exists.").arg(newName));
        return false;
    }

    if (type == QStringLiteral("host")) {
        auto it = m_hosts.find(oldName);
        if (it == m_hosts.end()) {
            setError(QStringLiteral("Unknown host '%1'.").arg(oldName));
            return false;
        }
        HostRecord record = it.value();
        m_hosts.erase(it);
        record.name = newName;
        m_hosts.insert(newName, record);
        for (const QString &group : record.groups) {
            m_groups[group].hosts.remove(oldName);
            m_groups[group].hosts.insert(newName);
        }
    } else if (type == QStringLiteral("group")) {
        if (oldName == QStringLiteral("all")) {
            setError(QStringLiteral("The 'all' group cannot be renamed."));
            return false;
        }
        auto it = m_groups.find(oldName);
        if (it == m_groups.end()) {
            setError(QStringLiteral("Unknown group '%1'.").arg(oldName));
            return false;
        }

        GroupRecord record = it.value();
        m_groups.erase(it);
        record.name = newName;
        m_groups.insert(newName, record);

        for (const QString &parent : record.parents) {
            m_groups[parent].children.remove(oldName);
            m_groups[parent].children.insert(newName);
        }
        for (const QString &child : record.children) {
            m_groups[child].parents.remove(oldName);
            m_groups[child].parents.insert(newName);
        }
        for (const QString &host : record.hosts) {
            m_hosts[host].groups.remove(oldName);
            m_hosts[host].groups.insert(newName);
            if (m_hosts[host].varsOwner == oldName)
                m_hosts[host].varsOwner = newName;
        }
    } else {
        setError(QStringLiteral("Unknown node type '%1'.").arg(type));
        return false;
    }

    setError({});
    changed();
    return true;
}

bool InventoryDocument::addHostToGroup(const QString &hostName, const QString &groupName)
{
    if (!m_hosts.contains(hostName) || !m_groups.contains(groupName) || groupName == QStringLiteral("all")) {
        setError(QStringLiteral("Choose an existing host and a non-'all' group."));
        return false;
    }

    HostRecord &host = m_hosts[hostName];
    host.groups.insert(groupName);
    host.explicitAll = false;
    m_groups[groupName].hosts.insert(hostName);
    setError({});
    changed();
    return true;
}

bool InventoryDocument::removeHostFromGroup(const QString &hostName, const QString &groupName)
{
    if (!m_hosts.contains(hostName) || !m_groups.contains(groupName)) {
        setError(QStringLiteral("Unknown host or group."));
        return false;
    }

    HostRecord &host = m_hosts[hostName];
    host.groups.remove(groupName);
    m_groups[groupName].hosts.remove(hostName);
    if (host.varsOwner == groupName)
        host.varsOwner.clear();
    if (host.groups.isEmpty())
        host.explicitAll = true;
    setError({});
    changed();
    return true;
}

bool InventoryDocument::addGroupToGroup(const QString &child, const QString &parent)
{
    if (!m_groups.contains(child)) {
        setError(QStringLiteral("Unknown child group '%1'.").arg(child));
        return false;
    }
    if (child == QStringLiteral("all")) {
        setError(QStringLiteral("The 'all' group cannot be nested."));
        return false;
    }

    if (parent == QStringLiteral("all")) {
        const auto parents = m_groups[child].parents.values();
        for (const QString &oldParent : parents)
            m_groups[oldParent].children.remove(child);
        m_groups[child].parents.clear();
        setError({});
        changed();
        return true;
    }

    if (!m_groups.contains(parent)) {
        setError(QStringLiteral("Unknown parent group '%1'.").arg(parent));
        return false;
    }
    if (wouldCreateGroupCycle(child, parent)) {
        setError(QStringLiteral("That relationship would create a group cycle."));
        return false;
    }

    m_groups[parent].children.insert(child);
    m_groups[child].parents.insert(parent);
    setError({});
    changed();
    return true;
}

bool InventoryDocument::removeGroupFromGroup(const QString &child, const QString &parent)
{
    if (!m_groups.contains(child) || !m_groups.contains(parent)) {
        setError(QStringLiteral("Unknown child or parent group."));
        return false;
    }

    m_groups[parent].children.remove(child);
    m_groups[child].parents.remove(parent);
    setError({});
    changed();
    return true;
}

bool InventoryDocument::moveTreeEntry(const QString &type,
                                      const QString &name,
                                      const QString &sourceGroup,
                                      const QString &targetGroup)
{
    if (targetGroup.isEmpty() || !m_groups.contains(targetGroup)) {
        setError(QStringLiteral("Unknown target group."));
        return false;
    }

    if (type == QStringLiteral("host")) {
        if (!addHostToGroup(name, targetGroup))
            return false;
        if (!sourceGroup.isEmpty() && sourceGroup != QStringLiteral("all") && sourceGroup != targetGroup)
            removeHostFromGroup(name, sourceGroup);
        return true;
    }

    if (type == QStringLiteral("group")) {
        if (!addGroupToGroup(name, targetGroup))
            return false;
        if (!sourceGroup.isEmpty() && sourceGroup != QStringLiteral("all") && sourceGroup != targetGroup)
            removeGroupFromGroup(name, sourceGroup);
        return true;
    }

    setError(QStringLiteral("Unknown node type '%1'.").arg(type));
    return false;
}

QVariantMap InventoryDocument::nodeDetails(const QString &type, const QString &name) const
{
    QVariantMap result;
    result.insert(QStringLiteral("type"), type);
    result.insert(QStringLiteral("name"), name);

    if (type == QStringLiteral("host")) {
        const auto it = m_hosts.constFind(name);
        if (it == m_hosts.cend())
            return {};
        QStringList groups = it->groups.values();
        groups.sort(Qt::CaseInsensitive);
        result.insert(QStringLiteral("groups"), groups);
        result.insert(QStringLiteral("varsYaml"), emitYaml(it->vars));
        result.insert(QStringLiteral("ansibleHost"),
                      it->vars["ansible_host"]
                          ? QString::fromStdString(it->vars["ansible_host"].as<std::string>())
                          : QString());
    } else if (type == QStringLiteral("group")) {
        const auto it = m_groups.constFind(name);
        if (it == m_groups.cend())
            return {};
        QStringList parents = it->parents.values();
        QStringList children = it->children.values();
        QStringList hosts = it->hosts.values();
        parents.sort(Qt::CaseInsensitive);
        children.sort(Qt::CaseInsensitive);
        hosts.sort(Qt::CaseInsensitive);
        result.insert(QStringLiteral("parents"), parents);
        result.insert(QStringLiteral("children"), children);
        result.insert(QStringLiteral("hosts"), hosts);
        result.insert(QStringLiteral("varsYaml"), emitYaml(it->vars));
    }

    return result;
}

bool InventoryDocument::setNodeVarsYaml(const QString &type,
                                        const QString &name,
                                        const QString &yamlText)
{
    try {
        YAML::Node parsed = yamlText.trimmed().isEmpty()
            ? YAML::Node(YAML::NodeType::Map)
            : YAML::Load(yamlText.toStdString());

        if (!parsed.IsMap()) {
            setError(QStringLiteral("Variables must be a YAML mapping (key: value)."));
            return false;
        }

        if (type == QStringLiteral("host") && m_hosts.contains(name)) {
            m_hosts[name].vars = YAML::Clone(parsed);
            if (m_hosts[name].varsOwner.isEmpty())
                m_hosts[name].varsOwner = canonicalHostOwner(m_hosts[name]);
        } else if (type == QStringLiteral("group") && m_groups.contains(name)) {
            m_groups[name].vars = YAML::Clone(parsed);
        } else {
            setError(QStringLiteral("Unknown node '%1'.").arg(name));
            return false;
        }
    } catch (const std::exception &exception) {
        setError(QStringLiteral("Invalid variables YAML: %1").arg(QString::fromUtf8(exception.what())));
        return false;
    }

    setError({});
    changed();
    return true;
}

QString InventoryDocument::nodeVarsYaml(const QString &type, const QString &name) const
{
    if (type == QStringLiteral("host") && m_hosts.contains(name))
        return emitYaml(m_hosts[name].vars);
    if (type == QStringLiteral("group") && m_groups.contains(name))
        return emitYaml(m_groups[name].vars);
    return {};
}

bool InventoryDocument::validateGroupGraph()
{
    QSet<QString> visiting;
    QSet<QString> visited;

    std::function<bool(const QString &)> visit = [&](const QString &name) -> bool {
        if (visited.contains(name))
            return true;
        if (visiting.contains(name)) {
            setError(QStringLiteral("Circular group relationship involving '%1'.").arg(name));
            return false;
        }

        visiting.insert(name);
        const auto it = m_groups.constFind(name);
        if (it != m_groups.cend()) {
            for (const QString &child : it.value().children) {
                if (!visit(child))
                    return false;
            }
        }
        visiting.remove(name);
        visited.insert(name);
        return true;
    };

    for (const QString &group : sortedGroupNames(false)) {
        if (!visit(group))
            return false;
    }
    return true;
}

bool InventoryDocument::wouldCreateGroupCycle(const QString &child, const QString &parent) const
{
    if (child == parent)
        return true;
    QSet<QString> seen;
    return groupReachable(child, parent, seen);
}

bool InventoryDocument::groupReachable(const QString &from,
                                       const QString &target,
                                       QSet<QString> &seen) const
{
    if (from == target)
        return true;
    if (seen.contains(from) || !m_groups.contains(from))
        return false;
    seen.insert(from);
    const auto groupIt = m_groups.constFind(from);
    if (groupIt == m_groups.cend())
        return false;
    for (const QString &child : groupIt.value().children) {
        if (groupReachable(child, target, seen))
            return true;
    }
    return false;
}
