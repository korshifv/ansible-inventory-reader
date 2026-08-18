#include "inventorydocument.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>

namespace {
const QRegularExpression kAnsibleResultLine(
    QStringLiteral(R"(^(.+?)\s+\|\s+([A-Z]+!?)[ \t]*=>[ \t]*(.*)$)"));

bool looksLikeControllerWarning(const QString &text)
{
    const QString lower = text.toLower();
    return lower.contains(QStringLiteral("[warning]"))
        || lower.contains(QStringLiteral("[deprecation warning]"))
        || lower.contains(QStringLiteral("deprecation warnings can be disabled"));
}

QString hostFallbackRaw(const QString &controllerStderr)
{
    const QString raw = controllerStderr.trimmed();
    if (raw.isEmpty())
        return QStringLiteral("Ansible returned no host-specific result.");

    if (looksLikeControllerWarning(raw)) {
        return QStringLiteral(
                   "Ansible returned no host-specific result.\n\n"
                   "The text below came from the local Ansible controller and is not "
                   "a failure reported by this host:\n\n")
            + raw;
    }

    return raw;
}
}

QVariantMap InventoryDocument::pingStates() const
{
    QVariantMap result;
    for (auto it = m_pingResults.cbegin(); it != m_pingResults.cend(); ++it) {
        if (!m_hosts.contains(it.key()))
            continue;

        QVariantMap state;
        state.insert(QStringLiteral("state"), it.value().state);
        state.insert(QStringLiteral("reason"), it.value().reason);
        state.insert(QStringLiteral("raw"), it.value().raw);
        state.insert(QStringLiteral("target"), it.value().target);
        result.insert(it.key(), state);
    }
    return result;
}

bool InventoryDocument::pingRunning() const { return m_pingRunning; }
int InventoryDocument::pingCompleted() const { return m_pingCompleted; }
int InventoryDocument::pingTotal() const { return m_pingTotal; }

bool InventoryDocument::pingAll()
{
    const QStringList hosts = sortedHostNames();
    if (hosts.isEmpty()) {
        setError(QStringLiteral("There are no hosts to ping."));
        return false;
    }
    return startPingRun(QStringLiteral("all"), hosts);
}

bool InventoryDocument::pingHost(const QString &hostName)
{
    if (!m_hosts.contains(hostName)) {
        setError(QStringLiteral("Unknown host '%1'.").arg(hostName));
        return false;
    }
    return startPingRun(hostName, {hostName});
}

void InventoryDocument::cancelPing()
{
    if (!m_pingRunning)
        return;

    for (const QString &host : std::as_const(m_pingExpectedHosts)) {
        if (m_pingCompletedHosts.contains(host))
            continue;
        PingRecord &record = m_pingResults[host];
        record.state = QStringLiteral("cancelled");
        record.reason = QStringLiteral("Cancelled");
        record.raw = QStringLiteral("Ansible ping cancelled by user.");
    }

    m_pingRunning = false;
    if (m_pingProcess) {
        m_pingProcess->kill();
        m_pingProcess->deleteLater();
        m_pingProcess = nullptr;
    }
    if (m_pingSnapshot) {
        delete m_pingSnapshot;
        m_pingSnapshot = nullptr;
    }

    emit pingStateChanged();
}

bool InventoryDocument::startPingRun(const QString &pattern, const QStringList &hosts)
{
    if (m_pingRunning) {
        setError(QStringLiteral("An Ansible ping is already running."));
        return false;
    }

    const QString inventoryPath = pingInventoryPath();
    if (inventoryPath.isEmpty())
        return false;

    if (m_pingProcess) {
        m_pingProcess->deleteLater();
        m_pingProcess = nullptr;
    }

    m_pingExpectedHosts.clear();
    m_pingCompletedHosts.clear();
    m_pingStdoutBuffer.clear();
    m_pingStderrBuffer.clear();
    m_pingCompleted = 0;
    m_pingTotal = hosts.size();

    if (pattern == QStringLiteral("all"))
        m_pingResults.clear();

    for (const QString &hostName : hosts) {
        m_pingExpectedHosts.insert(hostName);
        PingRecord &record = m_pingResults[hostName];
        record.state = QStringLiteral("checking");
        record.reason = QStringLiteral("Checking…");
        record.raw.clear();
        record.target = pingTargetForHost(m_hosts.constFind(hostName).value());
    }

    m_pingProcess = new QProcess(this);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("ANSIBLE_NOCOLOR"), QStringLiteral("1"));
    environment.insert(QStringLiteral("ANSIBLE_FORCE_COLOR"), QStringLiteral("0"));

    // The ansible command already uses the supported `minimal` callback by default.
    // Keep its result JSON compact so every host result stays on one parseable line.
    // This replaces the deprecated -o/oneline callback entirely.
    environment.insert(QStringLiteral("ANSIBLE_CALLBACK_RESULT_FORMAT"), QStringLiteral("json"));
    environment.insert(QStringLiteral("ANSIBLE_CALLBACK_FORMAT_PRETTY"), QStringLiteral("false"));
    m_pingProcess->setProcessEnvironment(environment);

    const QFileInfo inventoryInfo(inventoryPath);
    m_pingProcess->setWorkingDirectory(
        m_filePath.isEmpty() ? QDir::currentPath() : QFileInfo(m_filePath).absolutePath());

    connect(m_pingProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        consumePingStdout();
    });
    connect(m_pingProcess, &QProcess::readyReadStandardError, this, [this]() {
        if (m_pingProcess)
            m_pingStderrBuffer += QString::fromUtf8(m_pingProcess->readAllStandardError());
    });
    connect(m_pingProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_pingRunning)
            return;
        if (error == QProcess::FailedToStart) {
            const QString raw = m_pingProcess ? m_pingProcess->errorString() : QString();
            finishPingRun(QStringLiteral("Could not start Ansible"), raw);
        }
    });
    connect(m_pingProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (!m_pingRunning)
                    return;

                consumePingStdout();
                if (!m_pingStdoutBuffer.trimmed().isEmpty()) {
                    processPingLine(m_pingStdoutBuffer.trimmed());
                    m_pingStdoutBuffer.clear();
                }
                if (m_pingProcess)
                    m_pingStderrBuffer += QString::fromUtf8(m_pingProcess->readAllStandardError());

                QString fallbackReason;
                if (exitStatus == QProcess::CrashExit)
                    fallbackReason = QStringLiteral("Ansible process crashed");
                else if (exitCode != 0)
                    fallbackReason = QStringLiteral("No host result from Ansible");
                else
                    fallbackReason = QStringLiteral("No ping result returned");

                finishPingRun(fallbackReason, hostFallbackRaw(m_pingStderrBuffer));
            });

    m_pingRunning = true;
    setError({});
    emit pingStateChanged();

    const QStringList arguments {
        pattern,
        QStringLiteral("-i"), inventoryInfo.absoluteFilePath(),
        QStringLiteral("-m"), QStringLiteral("ping")
    };
    m_pingProcess->start(QStringLiteral("ansible"), arguments);
    return true;
}

QString InventoryDocument::pingInventoryPath()
{
    if (!m_filePath.isEmpty() && !m_modified)
        return m_filePath;

    if (m_pingSnapshot) {
        delete m_pingSnapshot;
        m_pingSnapshot = nullptr;
    }

    const QString directory = m_filePath.isEmpty()
        ? QDir::tempPath()
        : QFileInfo(m_filePath).absolutePath();
    const QString templateName =
        QDir(directory).filePath(QStringLiteral(".ansible-inventory-studio-XXXXXX.yml"));

    m_pingSnapshot = new QTemporaryFile(templateName, this);
    m_pingSnapshot->setAutoRemove(true);
    if (!m_pingSnapshot->open()) {
        setError(QStringLiteral("Cannot create temporary inventory for Ansible ping: %1")
                     .arg(m_pingSnapshot->errorString()));
        delete m_pingSnapshot;
        m_pingSnapshot = nullptr;
        return {};
    }

    try {
        YAML::Emitter emitter;
        emitter.SetIndent(2);
        emitter << serializeYaml();
        if (!emitter.good()) {
            setError(QStringLiteral("Cannot serialize temporary inventory: %1")
                         .arg(QString::fromStdString(emitter.GetLastError())));
            delete m_pingSnapshot;
            m_pingSnapshot = nullptr;
            return {};
        }

        const QByteArray bytes(emitter.c_str(), static_cast<qsizetype>(emitter.size()));
        if (m_pingSnapshot->write(bytes) != bytes.size()
            || m_pingSnapshot->write("\n", 1) != 1
            || !m_pingSnapshot->flush()) {
            setError(QStringLiteral("Cannot write temporary inventory: %1")
                         .arg(m_pingSnapshot->errorString()));
            delete m_pingSnapshot;
            m_pingSnapshot = nullptr;
            return {};
        }
        m_pingSnapshot->close();
        return m_pingSnapshot->fileName();
    } catch (const std::exception &exception) {
        setError(QStringLiteral("Cannot prepare temporary inventory: %1")
                     .arg(QString::fromUtf8(exception.what())));
        delete m_pingSnapshot;
        m_pingSnapshot = nullptr;
        return {};
    }
}

QString InventoryDocument::pingTargetForHost(const HostRecord &host) const
{
    try {
        if (host.vars["ansible_host"])
            return QString::fromStdString(host.vars["ansible_host"].as<std::string>());
    } catch (const std::exception &) {
    }
    return host.name;
}

void InventoryDocument::consumePingStdout()
{
    if (!m_pingProcess)
        return;

    m_pingStdoutBuffer += QString::fromUtf8(m_pingProcess->readAllStandardOutput());
    for (;;) {
        const qsizetype newline = m_pingStdoutBuffer.indexOf(QLatin1Char('\n'));
        if (newline < 0)
            break;
        QString line = m_pingStdoutBuffer.left(newline);
        m_pingStdoutBuffer.remove(0, newline + 1);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (!line.trimmed().isEmpty())
            processPingLine(line);
    }
}

void InventoryDocument::processPingLine(const QString &line)
{
    const QRegularExpressionMatch match = kAnsibleResultLine.match(line);
    if (!match.hasMatch())
        return;

    const QString hostName = match.captured(1).trimmed();
    if (!m_pingExpectedHosts.contains(hostName))
        return;

    const QString status = match.captured(2).trimmed();
    const QString payload = match.captured(3).trimmed();

    QString message = payload;
    QString pingValue;
    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &jsonError);
    if (jsonError.error == QJsonParseError::NoError && document.isObject()) {
        const QJsonObject object = document.object();
        message = object.value(QStringLiteral("msg")).toString(payload);
        pingValue = object.value(QStringLiteral("ping")).toString();
    }

    if (status == QStringLiteral("SUCCESS")) {
        setPingResult(hostName,
                      QStringLiteral("reachable"),
                      pingValue.isEmpty() ? QStringLiteral("pong") : pingValue,
                      line);
        return;
    }

    const QString reason = classifyPingReason(message + QLatin1Char('\n') + line);
    if (status.startsWith(QStringLiteral("UNREACHABLE"))) {
        setPingResult(hostName, QStringLiteral("unreachable"), reason, line);
    } else if (status.startsWith(QStringLiteral("FAILED"))) {
        setPingResult(hostName, QStringLiteral("failed"), reason, line);
    } else {
        setPingResult(hostName,
                      QStringLiteral("failed"),
                      reason == QStringLiteral("Other")
                          ? QStringLiteral("Unexpected Ansible ping response")
                          : reason,
                      line);
    }
}

void InventoryDocument::finishPingRun(const QString &fallbackReason, const QString &fallbackRaw)
{
    if (!m_pingRunning)
        return;

    for (const QString &hostName : std::as_const(m_pingExpectedHosts)) {
        if (m_pingCompletedHosts.contains(hostName))
            continue;
        setPingResult(hostName,
                      QStringLiteral("error"),
                      fallbackReason.isEmpty() ? QStringLiteral("No result from Ansible") : fallbackReason,
                      fallbackRaw);
    }

    m_pingRunning = false;
    if (m_pingProcess) {
        m_pingProcess->deleteLater();
        m_pingProcess = nullptr;
    }
    if (m_pingSnapshot) {
        delete m_pingSnapshot;
        m_pingSnapshot = nullptr;
    }
    emit pingStateChanged();
}

void InventoryDocument::setPingResult(const QString &hostName,
                                      const QString &state,
                                      const QString &reason,
                                      const QString &raw)
{
    if (!m_hosts.contains(hostName))
        return;

    PingRecord &record = m_pingResults[hostName];
    record.state = state;
    record.reason = reason;
    record.raw = raw;
    if (record.target.isEmpty())
        record.target = pingTargetForHost(m_hosts.constFind(hostName).value());

    if (!m_pingCompletedHosts.contains(hostName)) {
        m_pingCompletedHosts.insert(hostName);
        ++m_pingCompleted;
    }
    emit pingStateChanged();
}

QString InventoryDocument::classifyPingReason(const QString &text)
{
    const QString lower = text.toLower();

    if (lower.contains(QStringLiteral("ansible requires python"))
        && lower.contains(QStringLiteral("current version")))
        return QStringLiteral("Python too old");
    if (lower.contains(QStringLiteral("permission denied"))
        || lower.contains(QStringLiteral("authentication failed")))
        return QStringLiteral("Permission denied");
    if (lower.contains(QStringLiteral("no route to host"))
        || lower.contains(QStringLiteral("network is unreachable"))
        || lower.contains(QStringLiteral("network unreachable")))
        return QStringLiteral("No route");
    if (lower.contains(QStringLiteral("host key verification failed")))
        return QStringLiteral("Host key verification failed");
    if (lower.contains(QStringLiteral("could not resolve hostname"))
        || lower.contains(QStringLiteral("name or service not known"))
        || lower.contains(QStringLiteral("temporary failure in name resolution")))
        return QStringLiteral("Name resolution failed");
    if (lower.contains(QStringLiteral("connection refused")))
        return QStringLiteral("Connection refused");
    if (lower.contains(QStringLiteral("timed out"))
        || lower.contains(QStringLiteral("timeout")))
        return QStringLiteral("Timeout");
    if (lower.contains(QStringLiteral("unreachable")))
        return QStringLiteral("Unreachable");
    return QStringLiteral("Other");
}
