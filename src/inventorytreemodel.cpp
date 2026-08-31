#include "inventorytreemodel.h"

#include "inventorydocument.h"

#include <algorithm>

InventoryTreeModel::InventoryTreeModel(InventoryDocument *document)
    : QAbstractListModel(document), m_document(document)
{
    m_expanded.insert(QStringLiteral("group:all@"));

    connect(m_document, &InventoryDocument::pingStateChanged, this, [this]() {
        // Avoid rebuilding the entire tree for every host result during a bulk run.
        // Once the run finishes, one final pingStateChanged reorders everything.
        if (m_problemsFirst && !m_document->pingRunning())
            rebuild();
    });
    connect(m_document, &InventoryDocument::exclusionsChanged, this, [this]() {
        rebuild();
    });

    rebuild();
}

int InventoryTreeModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant InventoryTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &row = m_rows.at(index.row());
    switch (role) {
    case NodeTypeRole: return row.type;
    case NameRole: return row.name;
    case DepthRole: return row.depth;
    case HasChildrenRole: return row.hasChildren;
    case ExpandedRole: return row.expanded;
    case SourceGroupRole: return row.sourceGroup;
    case ExcludedRole: return row.excluded;
    case DirectlyExcludedRole: return row.directlyExcluded;
    default: return {};
    }
}

QHash<int, QByteArray> InventoryTreeModel::roleNames() const
{
    return {
        {NodeTypeRole, "nodeType"},
        {NameRole, "name"},
        {DepthRole, "depth"},
        {HasChildrenRole, "hasChildren"},
        {ExpandedRole, "expanded"},
        {SourceGroupRole, "sourceGroup"},
        {ExcludedRole, "excluded"},
        {DirectlyExcludedRole, "directlyExcluded"}
    };
}

void InventoryTreeModel::toggle(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;

    const Row &entry = m_rows.at(row);
    if (!entry.hasChildren)
        return;

    if (m_expanded.contains(entry.expansionKey))
        m_expanded.remove(entry.expansionKey);
    else
        m_expanded.insert(entry.expansionKey);

    rebuild();
}

void InventoryTreeModel::expandAll()
{
    for (auto it = m_document->m_groups.cbegin(); it != m_document->m_groups.cend(); ++it) {
        const auto &group = it.value();
        if (group.name == QStringLiteral("all")) {
            m_expanded.insert(QStringLiteral("group:all@"));
            continue;
        }

        if (group.parents.isEmpty())
            m_expanded.insert(QStringLiteral("group:%1@all").arg(group.name));

        for (const QString &parent : group.parents)
            m_expanded.insert(QStringLiteral("group:%1@%2").arg(group.name, parent));
    }
    rebuild();
}

void InventoryTreeModel::collapseAll()
{
    m_expanded.clear();
    m_expanded.insert(QStringLiteral("group:all@"));
    rebuild();
}

void InventoryTreeModel::setProblemsFirst(bool enabled)
{
    if (m_problemsFirst == enabled)
        return;

    m_problemsFirst = enabled;
    rebuild();
}

void InventoryTreeModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    QSet<QString> path;
    appendGroup(QStringLiteral("all"), QString(), 0, path);
    endResetModel();
}

int InventoryTreeModel::hostProblemRank(const QString &hostName) const
{
    if (m_document->isHostExcluded(hostName))
        return 3;

    const auto it = m_document->m_pingResults.constFind(hostName);
    if (it == m_document->m_pingResults.cend())
        return 2;

    const QString &state = it.value().state;
    if (state == QStringLiteral("unreachable") || state == QStringLiteral("error"))
        return 0;
    if (state == QStringLiteral("failed"))
        return 1;
    return 2;
}

void InventoryTreeModel::appendGroup(const QString &groupName,
                                     const QString &sourceGroup,
                                     int depth,
                                     QSet<QString> &path)
{
    if (!m_document->m_groups.contains(groupName))
        return;

    const auto &group = m_document->m_groups[groupName];

    QStringList childGroups;
    if (groupName == QStringLiteral("all")) {
        for (auto it = m_document->m_groups.cbegin(); it != m_document->m_groups.cend(); ++it) {
            if (it.key() != QStringLiteral("all") && it.value().parents.isEmpty())
                childGroups.append(it.key());
        }
    } else {
        childGroups = group.children.values();
    }
    childGroups.sort(Qt::CaseInsensitive);

    QStringList hosts;
    if (groupName == QStringLiteral("all")) {
        for (auto it = m_document->m_hosts.cbegin(); it != m_document->m_hosts.cend(); ++it) {
            if (it.value().groups.isEmpty())
                hosts.append(it.key());
        }
    } else {
        hosts = group.hosts.values();
    }
    hosts.sort(Qt::CaseInsensitive);

    if (m_problemsFirst) {
        std::stable_sort(hosts.begin(), hosts.end(), [this](const QString &left, const QString &right) {
            return hostProblemRank(left) < hostProblemRank(right);
        });
    }

    const QString expansionKey = QStringLiteral("group:%1@%2").arg(
        groupName,
        sourceGroup.isEmpty() ? QString() : sourceGroup);
    const bool hasChildren = !childGroups.isEmpty() || !hosts.isEmpty();
    const bool expanded = hasChildren && m_expanded.contains(expansionKey);
    const bool excluded = m_document->isGroupExcluded(groupName);
    const bool directlyExcluded = m_document->m_excludedGroups.contains(groupName);

    m_rows.push_back({
        QStringLiteral("group"), groupName, depth, hasChildren, expanded,
        sourceGroup, excluded, directlyExcluded, expansionKey
    });

    if (!expanded)
        return;

    const QString pathKey = sourceGroup + QStringLiteral("->") + groupName;
    if (path.contains(pathKey))
        return;

    path.insert(pathKey);

    for (const QString &child : childGroups) {
        if (path.contains(groupName + QStringLiteral("->") + child))
            continue;
        appendGroup(child, groupName, depth + 1, path);
    }

    for (const QString &host : hosts) {
        m_rows.push_back({
            QStringLiteral("host"), host, depth + 1, false, false,
            groupName,
            m_document->isHostExcluded(host),
            m_document->m_excludedHosts.contains(host),
            QString()
        });
    }

    path.remove(pathKey);
}
