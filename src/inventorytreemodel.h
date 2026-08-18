#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QVector>

class InventoryDocument;

class InventoryTreeModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        NodeTypeRole = Qt::UserRole + 1,
        NameRole,
        DepthRole,
        HasChildrenRole,
        ExpandedRole,
        SourceGroupRole
    };
    Q_ENUM(Role)

    explicit InventoryTreeModel(InventoryDocument *document);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void expandAll();
    Q_INVOKABLE void collapseAll();
    Q_INVOKABLE void setProblemsFirst(bool enabled);

    void rebuild();

private:
    struct Row {
        QString type;
        QString name;
        int depth {0};
        bool hasChildren {false};
        bool expanded {false};
        QString sourceGroup;
        QString expansionKey;
    };

    void appendGroup(const QString &group,
                     const QString &sourceGroup,
                     int depth,
                     QSet<QString> &path);
    int hostProblemRank(const QString &hostName) const;

    InventoryDocument *m_document {nullptr};
    QVector<Row> m_rows;
    QSet<QString> m_expanded;
    bool m_problemsFirst {false};
};
