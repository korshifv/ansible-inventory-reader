#pragma once

#include <QObject>
#include <QHash>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QTemporaryFile>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <yaml-cpp/yaml.h>

#include <memory>
#include <utility>

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
    Q_PROPERTY(QStringList excludedGroups READ excludedGroups NOTIFY exclusionsChanged)
    Q_PROPERTY(QStringList excludedHosts READ excludedHosts NOTIFY exclusionsChanged)
    Q_PROPERTY(QVariantMap pingStates READ pingStates NOTIFY pingStateChanged)
    Q_PROPERTY(bool pingRunning READ pingRunning NOTIFY pingStateChanged)
    Q_PROPERTY(int pingCompleted READ pingCompleted NOTIFY pingStateChanged)
    Q_PROPERTY(int pingTotal READ pingTotal NOTIFY pingStateChanged)

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
    QStringList excludedGroups() const;
    QStringList excludedHosts() const;
    QVariantMap pingStates() const;
    bool pingRunning() const;
    int pingCompleted() const;
    int pingTotal() const;

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

    Q_INVOKABLE bool setGroupExcluded(const QString &groupName, bool excluded = true);
    Q_INVOKABLE bool setHostExcluded(const QString &hostName, bool excluded = true);
    Q_INVOKABLE bool isGroupExcluded(const QString &groupName) const;
    Q_INVOKABLE bool isHostExcluded(const QString &hostName) const;
    Q_INVOKABLE void clearExcludedGroups();
    Q_INVOKABLE void clearExcludedHosts();
    Q_INVOKABLE void clearExclusions();

    Q_INVOKABLE bool pingAll();
    Q_INVOKABLE bool pingHost(const QString &hostName);
    Q_INVOKABLE void cancelPing();

signals:
    void filePathChanged();
    void modifiedChanged();
    void errorStringChanged();
    void structureChanged();
    void graphChanged();
    void exclusionsChanged();
    void pingStateChanged();

private:
    struct HostRecord {
        QString name;
        YAML::Node vars {YAML::NodeType::Map};
        QSet<QString> groups;
        bool explicitAll {false};
        QString varsOwner;
        QString comment;
    };

    struct GroupRecord {
        QString name;
        YAML::Node vars {YAML::NodeType::Map};
        QSet<QString> children;
        QSet<QString> parents;
        QSet<QString> hosts;
    };

    struct PingRecord {
        QString state {QStringLiteral("unknown")};
        QString reason;
        QString raw;
        QString target;
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
    QString serializeYamlText() const;
    QString injectHostComments(const QString &yamlText) const;
    YAML::Node hostVarsForLocation(const HostRecord &host, const QString &location) const;
    QString canonicalHostOwner(const HostRecord &host) const;
    QString sourceCommentAfterLine(int sourceLine) const;
    static QString extractLeadingYamlComment(const QString &yamlText);
    static QString formatHostEditorYaml(const HostRecord &host);
    static QString emitYamlScalar(const QString &value);
    static YAML::Node emptyMap();
    static QString emitYaml(const YAML::Node &node);

    bool wouldCreateGroupCycle(const QString &child, const QString &parent) const;
    bool groupReachable(const QString &from, const QString &target, QSet<QString> &seen) const;
    bool validateGroupGraph();

    QSet<QString> effectiveExcludedGroups() const;
    QSet<QString> excludedHostSet() const;
    QStringList nonExcludedHostNames() const;
    QString excludedPingPattern() const;
    bool pingAllUnfiltered();
    bool pingHostUnfiltered(const QString &hostName);

    bool startPingRun(const QString &pattern, const QStringList &hosts);
    QString pingInventoryPath();
    QString pingTargetForHost(const HostRecord &host) const;
    void consumePingStdout();
    void processPingLine(const QString &line);
    void finishPingRun(const QString &fallbackReason = QString(),
                       const QString &fallbackRaw = QString());
    void setPingResult(const QString &hostName,
                       const QString &state,
                       const QString &reason,
                       const QString &raw);
    static QString classifyPingReason(const QString &text);

    void setModified(bool value);
    void setError(const QString &message);
    void changed(bool markModified = true);
    void rebuildGraph();

    QStringList sortedGroupNames(bool includeAll = false) const;
    QStringList sortedHostNames() const;

    QHash<QString, GroupRecord> m_groups;
    QHash<QString, HostRecord> m_hosts;
    QSet<QString> m_excludedGroups;
    QSet<QString> m_excludedHosts;
    std::unique_ptr<InventoryTreeModel> m_treeModel;

    QString m_filePath;
    bool m_modified {false};
    QString m_errorString;
    QStringList m_sourceLines;

    QVariantList m_graphNodes;
    QVariantList m_graphEdges;
    qreal m_graphWidth {960.0};
    qreal m_graphHeight {640.0};

    QHash<QString, PingRecord> m_pingResults;
    QSet<QString> m_pingExpectedHosts;
    QSet<QString> m_pingCompletedHosts;
    QString m_pingStdoutBuffer;
    QString m_pingStderrBuffer;
    QProcess *m_pingProcess {nullptr};
    QTemporaryFile *m_pingSnapshot {nullptr};
    bool m_pingRunning {false};
    int m_pingCompleted {0};
    int m_pingTotal {0};
};
