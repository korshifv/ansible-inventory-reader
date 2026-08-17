#include "inventorydocument.h"
#include "inventorytreemodel.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Korshi"));
    QGuiApplication::setApplicationName(QStringLiteral("Ansible Inventory Studio"));

    InventoryDocument inventory;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("inventory"), &inventory);
    engine.rootContext()->setContextProperty(QStringLiteral("inventoryTreeModel"), inventory.treeModel());

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/AnsibleInventoryStudio/Main.qml")));

    if (argc > 1)
        inventory.openFile(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));

    return app.exec();
}
