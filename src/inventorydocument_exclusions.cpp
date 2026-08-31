#include "inventorydocument.h"

#include <algorithm>

namespace {
bool rejectExclusionChangeWhilePingRuns(InventoryDocument *document)
{
    if (!document->pingRunning())
        return false;

    return true;
}
}

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

QStringList InventoryDocument::excludedHosts() const
{
    QStringList names;
    names.reserve(m_excludedHosts.size());
    for (const QString &name : m_excludedHosts) {
        if (m_hosts.contains(name))
            names.append(name);
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool InventoryDocument::setGroupExcluded(const QString &rawGroupName, bool excluded)
{
    if (rejectExclusionChangeWhilePingRuns(this)) {
        setError(QStringLiteral("Cannot change exclusions while Ansible ping is running."));
        return false;
    }

    const QString groupName = rawGroupName.trimmed();

    if (!excluded) {
        if (!m_excludedGroups.remove(groupName))
            return true;
        setError({});
        rebuildGraph();
        emit exclusionsChanged();
        return true;
    }

    if (groupName.isEmpty() || groupName == QStringLiteral("all") || !m_groups.contains(groupName)) {
        setError(QStringLiteral("Choose an existing non-'all' group to exclude."));
        return false;
    }

    // Do not create a redundant direct exclusion below an already excluded ancestor.
    if (isGroupExcluded(groupName))
        return true;

    m_excludedGroups.insert(groupName);
    setError({});
    rebuildGraph();
    emit exclusionsChanged();
    return true;
}

bool InventoryDocument::setHostExcluded(const QString &rawHostName, bool excluded)
{
    if (rejectExclusionChangeWhilePingRuns(this)) {
        setError(QStringLiteral("Cannot change exclusions while Ansible ping is running."));
        return false;
    }

    const QString hostName = rawHostName.trimmed();

    if (!excluded) {
        if (!m_excludedHosts.remove(hostName))
            return true;
        setError({});
        rebuildGraph();
        emit exclusionsChanged();
        return true;
    }

    if (hostName.isEmpty() || !m_hosts.contains(hostName)) {
        setError(QStringLiteral("Choose an existing host to exclude."));
        return false;
    }

    if (isHostExcluded(hostName))
        return true;

    m_excludedHosts.insert(hostName);
    m_pingResults.remove(hostName);
    setError({});
    rebuildGraph();
    emit pingStateChanged();
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
    if (m_pingRunning) {
        setError(QStringLiteral("Cannot change exclusions while Ansible ping is running."));
        return;
    }
    if (m_excludedGroups.isEmpty())
        return;
    m_excludedGroups.clear();
    setError({});
    rebuildGraph();
    emit exclusionsChanged();
}

void InventoryDocument::clearExcludedHosts()
{
    if (m_pingRunning) {
        setError(QStringLiteral("Cannot change exclusions while Ansible ping is running."));
        return;
    }
    if (m_excludedHosts.isEmpty())
        return;
    m_excludedHosts.clear();
    setError({});
    rebuildGraph();
    emit exclusionsChanged();
}

void InventoryDocument::clearExclusions()
{
    if (m_pingRunning) {
        setError(QStringLiteral("Cannot change exclusions while Ansible ping is running."));
        return;
    }
    if (m_excludedGroups.isEmpty() && m_excludedHosts.isEmpty())
        return;
    m_excludedGroups.clear();
    m_excludedHosts.clear();
    setError({});
    rebuildGraph();
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

    for (const QString &hostName : m_excludedHosts) {
        if (m_hosts.contains(hostName))
            hosts.insert(hostName);
    }

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
    QStringList hosts = excludedHostSet().values();
    hosts.sort(Qt::CaseInsensitive);

    // Exclude the concrete host aliases as well as keeping a reduced expected-host
    // set. This is important: otherwise Ansible would still connect to an excluded
    // host and the application would merely ignore its result.
    for (const QString &hostName : hosts)
        pattern += QStringLiteral(":!") + hostName;

    return pattern;
}
