#include "inventorydocument.h"

#include "inventorytreemodel.h"

#include <QMap>
#include <QPointF>

#include <algorithm>

void InventoryDocument::setModified(bool value)
{
    if (m_modified == value)
        return;
    m_modified = value;
    emit modifiedChanged();
}

void InventoryDocument::setError(const QString &message)
{
    if (m_errorString == message)
        return;
    m_errorString = message;
    emit errorStringChanged();
}

void InventoryDocument::changed(bool markModified)
{
    if (markModified)
        setModified(true);
    if (m_treeModel)
        m_treeModel->rebuild();
    rebuildGraph();
    emit structureChanged();
}

void InventoryDocument::rebuildGraph()
{
    m_graphNodes.clear();
    m_graphEdges.clear();

    QHash<QString, int> groupDepth;
    groupDepth.insert(QStringLiteral("all"), 0);

    for (const QString &groupName : sortedGroupNames(false)) {
        if (m_groups[groupName].parents.isEmpty())
            groupDepth.insert(groupName, 1);
    }

    const int maxPasses = std::max(1, static_cast<int>(m_groups.size()) + 1);
    for (int pass = 0; pass < maxPasses; ++pass) {
        bool anyChange = false;
        for (const QString &groupName : sortedGroupNames(false)) {
            int depth = groupDepth.value(groupName, 1);
            for (const QString &parent : m_groups[groupName].parents)
                depth = std::max(depth, groupDepth.value(parent, 1) + 1);
            if (!groupDepth.contains(groupName) || groupDepth[groupName] != depth) {
                groupDepth[groupName] = depth;
                anyChange = true;
            }
        }
        if (!anyChange)
            break;
    }

    QHash<QString, int> hostDepth;
    for (const QString &hostName : sortedHostNames()) {
        const auto hostIt = m_hosts.constFind(hostName);
        const HostRecord &host = hostIt.value();
        int depth = 1;
        for (const QString &group : host.groups)
            depth = std::max(depth, groupDepth.value(group, 1) + 1);
        hostDepth.insert(hostName, depth);
    }

    QMap<int, QStringList> layers;
    layers[0].append(QStringLiteral("group:all"));
    for (const QString &groupName : sortedGroupNames(false))
        layers[groupDepth.value(groupName, 1)].append(QStringLiteral("group:") + groupName);
    for (const QString &hostName : sortedHostNames())
        layers[hostDepth.value(hostName, 1)].append(QStringLiteral("host:") + hostName);

    constexpr qreal left = 48.0;
    constexpr qreal top = 48.0;
    constexpr qreal xGap = 270.0;
    constexpr qreal yGap = 82.0;
    constexpr qreal groupWidth = 190.0;
    constexpr qreal hostWidth = 210.0;
    constexpr qreal nodeHeight = 54.0;

    QHash<QString, QPointF> positions;
    int maxDepth = 0;
    int maxLayerSize = 1;

    for (auto it = layers.cbegin(); it != layers.cend(); ++it) {
        maxDepth = std::max(maxDepth, it.key());
        maxLayerSize = std::max(maxLayerSize, static_cast<int>(it.value().size()));
        const QStringList entries = it.value();
        for (int row = 0; row < entries.size(); ++row) {
            const QString key = entries.at(row);
            const bool isGroup = key.startsWith(QStringLiteral("group:"));
            const QString name = key.mid(key.indexOf(QLatin1Char(':')) + 1);
            const qreal x = left + it.key() * xGap;
            const qreal y = top + row * yGap;
            const qreal width = isGroup ? groupWidth : hostWidth;

            QVariantMap node;
            node.insert(QStringLiteral("id"), key);
            node.insert(QStringLiteral("type"), isGroup ? QStringLiteral("group") : QStringLiteral("host"));
            node.insert(QStringLiteral("name"), name);
            node.insert(QStringLiteral("x"), x);
            node.insert(QStringLiteral("y"), y);
            node.insert(QStringLiteral("width"), width);
            node.insert(QStringLiteral("height"), nodeHeight);

            if (!isGroup) {
                const HostRecord &host = m_hosts[name];
                QString subtitle;
                if (host.vars["ansible_host"])
                    subtitle = QString::fromStdString(host.vars["ansible_host"].as<std::string>());
                node.insert(QStringLiteral("subtitle"), subtitle);
            } else {
                const GroupRecord &group = m_groups[name];
                node.insert(QStringLiteral("subtitle"),
                            QStringLiteral("%1 host%2")
                                .arg(group.hosts.size())
                                .arg(group.hosts.size() == 1 ? QString() : QStringLiteral("s")));
            }

            m_graphNodes.append(node);
            positions.insert(key, QPointF(x, y + nodeHeight / 2.0));
        }
    }

    auto addEdge = [this, &positions](const QString &from, const QString &to, const QString &kind) {
        if (!positions.contains(from) || !positions.contains(to))
            return;
        const QPointF a = positions[from];
        const QPointF b = positions[to];
        QVariantMap edge;
        edge.insert(QStringLiteral("from"), from);
        edge.insert(QStringLiteral("to"), to);
        edge.insert(QStringLiteral("kind"), kind);
        edge.insert(QStringLiteral("x1"), a.x() + 190.0);
        edge.insert(QStringLiteral("y1"), a.y());
        edge.insert(QStringLiteral("x2"), b.x());
        edge.insert(QStringLiteral("y2"), b.y());
        m_graphEdges.append(edge);
    };

    for (const QString &groupName : sortedGroupNames(false)) {
        const auto groupIt = m_groups.constFind(groupName);
        const GroupRecord &group = groupIt.value();
        if (group.parents.isEmpty())
            addEdge(QStringLiteral("group:all"), QStringLiteral("group:") + groupName, QStringLiteral("group"));
        for (const QString &parent : group.parents)
            addEdge(QStringLiteral("group:") + parent, QStringLiteral("group:") + groupName, QStringLiteral("group"));
    }

    for (const QString &hostName : sortedHostNames()) {
        const auto hostIt = m_hosts.constFind(hostName);
        const HostRecord &host = hostIt.value();
        if (host.groups.isEmpty()) {
            addEdge(QStringLiteral("group:all"), QStringLiteral("host:") + hostName, QStringLiteral("host"));
        } else {
            QStringList memberships = host.groups.values();
            memberships.sort(Qt::CaseInsensitive);
            for (const QString &group : memberships)
                addEdge(QStringLiteral("group:") + group, QStringLiteral("host:") + hostName, QStringLiteral("host"));
        }
    }

    m_graphWidth = left * 2 + (maxDepth + 1) * xGap + hostWidth;
    m_graphHeight = top * 2 + maxLayerSize * yGap + nodeHeight;
    emit graphChanged();
}

QStringList InventoryDocument::sortedGroupNames(bool includeAll) const
{
    QStringList names;
    names.reserve(m_groups.size());
    for (auto it = m_groups.cbegin(); it != m_groups.cend(); ++it) {
        if (includeAll || it.key() != QStringLiteral("all"))
            names.append(it.key());
    }
    names.sort(Qt::CaseInsensitive);
    if (includeAll) {
        names.removeAll(QStringLiteral("all"));
        names.prepend(QStringLiteral("all"));
    }
    return names;
}

QStringList InventoryDocument::sortedHostNames() const
{
    QStringList names = m_hosts.keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}
