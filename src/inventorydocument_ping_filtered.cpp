#include "inventorydocument.h"

// Reuse the existing ping implementation verbatim, but keep its public entry
// points under private names so the wrappers below can enforce exclusions.
#define pingAll pingAllUnfiltered
#define pingHost pingHostUnfiltered
#include "inventorydocument_ping.cpp"
#undef pingHost
#undef pingAll

bool InventoryDocument::pingAll()
{
    const QStringList hosts = nonExcludedHostNames();
    if (hosts.isEmpty()) {
        setError(m_hosts.isEmpty()
                     ? QStringLiteral("There are no hosts to ping.")
                     : QStringLiteral("All hosts are excluded from ping."));
        return false;
    }

    // A filtered Ansible pattern does not equal the literal "all", so the
    // original startPingRun() would otherwise retain results from a prior run.
    m_pingResults.clear();
    emit pingStateChanged();

    // excludedPingPattern() contains concrete !host aliases. This guarantees
    // that excluded hosts are not contacted by Ansible at all, instead of just
    // dropping their results in the UI.
    return startPingRun(excludedPingPattern(), hosts);
}

bool InventoryDocument::pingHost(const QString &hostName)
{
    if (isHostExcluded(hostName)) {
        setError(QStringLiteral("Host '%1' is excluded from ping.").arg(hostName));
        return false;
    }

    return pingHostUnfiltered(hostName);
}
