#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <yaml-cpp/yaml.h>

#include <memory>

class InventoryTreeModel;

class InventoryDocument final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(QVariantList graphNodes READ graphNodes NOTIFY graphChanged)
    Q_PROPERTY(QVariantList graphEdges READ graphEdges NOTIFY graphChanged)
    Q_PROPERTY(qreal graphWidth READ graphWidth NOTIFY graphChanged)
    Q_PROPERTY(qreal graphHeight READ graphHeight NOTIFY graphChanged)
    Q_PROPERTY(QStringList groupNames READ groupNames NOTIFY structureChanged)

public:
    explicit InventoryDocument(QObject *parent = nullptr);
    ~InventoryDocument() override;

    QString filePath() const;
    bool modified() const;
    QString errorString() const;
    QVariantList graphNodes() const;
    QVariantList graphEdges() const;
    qreal graphWidth() const;
    qreal graphHeight() const;
    QStringList groupNames() const;

    InventoryTreeModel *treeModel() const;

    Q_INVOKABLE void newDocument();
    Q_INVOKABLE bool openFile(const QUrl &url);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl &url);

    Q_INVOKABLE bool addGroup(const QString &name, const QString &parentGroup = QStringLiteral("all"));
    Q_INVOKABLE bool addHost(const QString &name, const QString &group = QStringLiteral("all"));
    Q_INVOKABLE bool deleteNode(const QString &type, const QString &name);
    Q_INVOKABLE bool renameNode(const QString &type, const QString &oldName, const QString &newName);

    Q_INVOKABLE bool addHostToGroup(const QString &host, const QString &group);
    Q_INVOKABLE bool removeHostFromGroup(const QString &host, const QString &group);
    Q_INVOKABLE bool addGroupToGroup(const QString &child, const QString &parent);
    Q_INVOKABLE bool removeGroupFromGroup(const QString &child, const QString &parent);
    Q_INVOKABLE bool moveTreeEntry(const QString &type,
                                   const QString &name,
                                   const QString &sourceGroup,
                                   const QString &targetGroup);

    Q_INVOKABLE QVariantMap nodeDetails(const QString &type, const QString &name) const;
    Q_INVOKABLE bool setNodeVarsYaml(const QString &type, const QString &name, const QString &yamlText);
    Q_INVOKABLE QString nodeVarsYaml(const QString &type, const QString &name) const;

signals:
    void filePathChanged();
    void modifiedChanged();
    void errorStringChanged();
    void structureChanged();
    void graphChanged();

private:
    struct HostRecord {
        QString name;
        YAML::Node vars {YAML::NodeType::Map};
        QSet<QString> groups;
        bool explicitAll {false};
        QString varsOwner;
    };

    struct GroupRecord {
        QString name;
        YAML::Node vars {YAML::NodeType::Map};
        QSet<QString> children;
        QSet<QString> parents;
        QSet<QString> hosts;
    };

    friend class InventoryTreeModel;

    void clearData();
    GroupRecord &ensureGroup(const QString &name);
    HostRecord &ensureHost(const QString &name);

    bool parseYaml(const YAML::Node &root);
    void parseGroupDefinition(const QString &name,
                              const YAML::Node &node,
                              const QString &parent,
                              QSet<QString> &stack);
    static void mergeYamlMap(YAML::Node &destination, const YAML::Node &source);

    YAML::Node serializeYaml() const;
    YAML::Node hostVarsForLocation(const HostRecord &host, const QString &location) const;
    QString canonicalHostOwner(const HostRecord &host) const;
    static YAML::Node emptyMap();
    static QString emitYaml(const YAML::Node &node);

    bool wouldCreateGroupCycle(const QString &child, const QString &parent) const;
    bool groupReachable(const QString &from, const QString &target, QSet<QString> &seen) const;
    bool validateGroupGraph();

    void setModified(bool value);
    void setError(const QString &message);
    void changed(bool markModified = true);
    void rebuildGraph();

    QStringList sortedGroupNames(bool includeAll = false) const;
    QStringList sortedHostNames() const;

    QHash<QString, GroupRecord> m_groups;
    QHash<QString, HostRecord> m_hosts;
    std::unique_ptr<InventoryTreeModel> m_treeModel;

    QString m_filePath;
    bool m_modified {false};
    QString m_errorString;

    QVariantList m_graphNodes;
    QVariantList m_graphEdges;
    qreal m_graphWidth {960.0};
    qreal m_graphHeight {640.0};
};
