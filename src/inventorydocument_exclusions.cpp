#include "inventorydocument.h"

#include <algorithm>

QStringList InventoryDocument::excludedGroups() const
{
    QStringList names;
    names.reserve(m_excludedGroups.size());
    for (const QString &name : m_excludedGroups) {
        if (name != QStringLiteral("all") && m_groups.contains(name))
            names.append(name);
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool InventoryDocument::setGroupExcluded(const QString &rawGroupName, bool excluded)
{
    const QString groupName = rawGroupName.trimmed();

    if (!excluded) {
        if (!m_excludedGroups.remove(groupName))
            return true;
        emit exclusionsChanged();
        return true;
    }

    if (groupName.isEmpty() || groupName == QStringLiteral("all") || !m_groups.contains(groupName)) {
        setError(QStringLiteral("Choose an existing non-'all' group to exclude."));
        return false;
    }

    // Do not create a redundant exclusion below an already excluded ancestor.
    if (isGroupExcluded(groupName))
        return true;

    m_excludedGroups.insert(groupName);
    setError({});
    emit exclusionsChanged();
    return true;
}

bool InventoryDocument::isGroupExcluded(const QString &groupName) const
{
    return effectiveExcludedGroups().contains(groupName);
}

bool InventoryDocument::isHostExcluded(const QString &hostName) const
{
    return excludedHostSet().contains(hostName);
}

void InventoryDocument::clearExcludedGroups()
{
    if (m_excludedGroups.isEmpty())
        return;
    m_excludedGroups.clear();
    emit exclusionsChanged();
}

QSet<QString> InventoryDocument::effectiveExcludedGroups() const
{
    QSet<QString> result;
    QStringList pending = excludedGroups();

    while (!pending.isEmpty()) {
        const QString groupName = pending.takeLast();
        if (result.contains(groupName) || !m_groups.contains(groupName))
            continue;

        result.insert(groupName);
        const GroupRecord &group = m_groups.constFind(groupName).value();
        for (const QString &child : group.children) {
            if (!result.contains(child))
                pending.append(child);
        }
    }

    return result;
}

QSet<QString> InventoryDocument::excludedHostSet() const
{
    QSet<QString> hosts;
    const QSet<QString> groups = effectiveExcludedGroups();

    for (const QString &groupName : groups) {
        const auto groupIt = m_groups.constFind(groupName);
        if (groupIt == m_groups.cend())
            continue;
        for (const QString &hostName : groupIt->hosts)
            hosts.insert(hostName);
    }

    return hosts;
}

QStringList InventoryDocument::nonExcludedHostNames() const
{
    const QSet<QString> excludedHosts = excludedHostSet();
    QStringList hosts;

    for (const QString &hostName : sortedHostNames()) {
        if (!excludedHosts.contains(hostName))
            hosts.append(hostName);
    }

    return hosts;
}

QString InventoryDocument::excludedPingPattern() const
{
    QString pattern = QStringLiteral("all");
    for (const QString &groupName : excludedGroups())
        pattern += QStringLiteral(":!") + groupName;
    return pattern;
}
