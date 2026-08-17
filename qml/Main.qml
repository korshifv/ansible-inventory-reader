import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1500
    height: 900
    minimumWidth: 1050
    minimumHeight: 650
    visible: true
    title: (inventory.modified ? "● " : "")
           + (inventory.filePath.length > 0 ? inventory.filePath : "Untitled inventory")
           + " — Ansible Inventory Studio"

    property string selectedType: ""
    property string selectedName: ""
    property var selectedDetails: ({})
    property string createKind: "host"
    property string createParent: "all"

    function selectNode(type, name) {
        selectedType = type
        selectedName = name
        refreshDetails()
    }

    function refreshDetails() {
        if (selectedType.length === 0 || selectedName.length === 0) {
            selectedDetails = ({})
            return
        }
        selectedDetails = inventory.nodeDetails(selectedType, selectedName)
        nameField.text = selectedName
        varsArea.text = selectedDetails.varsYaml || ""
    }

    function selectedGroupContext() {
        if (selectedType === "group")
            return selectedName
        if (selectedType === "host" && selectedDetails.groups && selectedDetails.groups.length > 0)
            return selectedDetails.groups[0]
        return "all"
    }

    function candidateGroups() {
        const all = inventory.groupNames
        const result = []
        for (let i = 0; i < all.length; ++i) {
            if (all[i] !== "all")
                result.push(all[i])
        }
        return result
    }

    Connections {
        target: inventory
        function onStructureChanged() {
            if (selectedType.length > 0)
                refreshDetails()
        }
    }

    FileDialog {
        id: openDialog
        title: "Open Ansible inventory"
        fileMode: FileDialog.OpenFile
        nameFilters: ["YAML inventory (*.yml *.yaml)", "All files (*)"]
        onAccepted: {
            if (inventory.openFile(selectedFile)) {
                selectedType = ""
                selectedName = ""
                selectedDetails = ({})
            }
        }
    }

    FileDialog {
        id: saveDialog
        title: "Save Ansible inventory"
        fileMode: FileDialog.SaveFile
        nameFilters: ["YAML inventory (*.yml *.yaml)"]
        defaultSuffix: "yml"
        onAccepted: inventory.saveAs(selectedFile)
    }

    Dialog {
        id: createDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)
        title: createKind === "host" ? "Create host" : "Create group"
        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            createName.text = ""
            createName.forceActiveFocus()
        }
        onAccepted: {
            const ok = createKind === "host"
                       ? inventory.addHost(createName.text, createParent)
                       : inventory.addGroup(createName.text, createParent)
            if (ok)
                selectNode(createKind, createName.text.trim())
        }

        ColumnLayout {
            width: 420
            spacing: 10

            Label {
                text: createKind === "host"
                      ? "Host will be created in: " + createParent
                      : "Group will be created under: " + createParent
            }
            TextField {
                id: createName
                Layout.fillWidth: true
                placeholderText: createKind === "host" ? "web-01" : "webservers"
                onAccepted: createDialog.accept()
            }
        }
    }

    menuBar: MenuBar {
        Menu {
            title: "File"
            Action {
                text: "New"
                shortcut: StandardKey.New
                onTriggered: {
                    inventory.newDocument()
                    selectedType = ""
                    selectedName = ""
                    selectedDetails = ({})
                }
            }
            Action { text: "Open…"; shortcut: StandardKey.Open; onTriggered: openDialog.open() }
            MenuSeparator {}
            Action {
                text: "Save"
                shortcut: StandardKey.Save
                onTriggered: inventory.filePath.length > 0 ? inventory.save() : saveDialog.open()
            }
            Action { text: "Save As…"; shortcut: StandardKey.SaveAs; onTriggered: saveDialog.open() }
            MenuSeparator {}
            Action { text: "Quit"; shortcut: StandardKey.Quit; onTriggered: window.close() }
        }

        Menu {
            title: "Tree"
            Action { text: "Expand all"; onTriggered: inventoryTreeModel.expandAll() }
            Action { text: "Collapse all"; onTriggered: inventoryTreeModel.collapseAll() }
        }
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 6

            ToolButton { text: "Open"; onClicked: openDialog.open() }
            ToolButton {
                text: "Save"
                onClicked: inventory.filePath.length > 0 ? inventory.save() : saveDialog.open()
            }

            ToolSeparator {}

            ToolButton {
                text: "+ Host"
                onClicked: {
                    createKind = "host"
                    createParent = selectedGroupContext()
                    createDialog.open()
                }
            }
            ToolButton {
                text: "+ Group"
                onClicked: {
                    createKind = "group"
                    createParent = selectedGroupContext()
                    createDialog.open()
                }
            }
            ToolButton {
                text: "Delete"
                enabled: selectedName.length > 0 && !(selectedType === "group" && selectedName === "all")
                onClicked: {
                    if (inventory.deleteNode(selectedType, selectedName)) {
                        selectedType = ""
                        selectedName = ""
                        selectedDetails = ({})
                    }
                }
            }

            Item { Layout.fillWidth: true }

            TextField {
                id: searchField
                Layout.preferredWidth: 300
                placeholderText: "Search host, group or ansible_host…"
            }
        }
    }

    footer: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10

            Label {
                Layout.fillWidth: true
                text: inventory.errorString.length > 0
                      ? inventory.errorString
                      : (inventory.modified ? "Modified" : "Ready")
                color: inventory.errorString.length > 0 ? palette.brightText : palette.text
                elide: Text.ElideRight
            }

            Label {
                text: inventory.filePath.length > 0 ? inventory.filePath : "No file"
                color: palette.placeholderText
                elide: Text.ElideMiddle
                Layout.maximumWidth: 520
            }
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Pane {
            SplitView.preferredWidth: 310
            SplitView.minimumWidth: 220
            padding: 0

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Label {
                    text: "Inventory"
                    font.weight: Font.DemiBold
                    leftPadding: 12
                    rightPadding: 12
                    topPadding: 10
                    bottomPadding: 8
                }

                ListView {
                    id: tree
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: inventoryTreeModel
                    currentIndex: -1

                    delegate: ItemDelegate {
                        id: treeDelegate
                        required property int index
                        required property string nodeType
                        required property string name
                        required property int depth
                        required property bool hasChildren
                        required property bool expanded
                        required property string sourceGroup

                        width: ListView.view.width
                        height: 36
                        leftPadding: 8 + depth * 18
                        highlighted: window.selectedType === nodeType && window.selectedName === name

                        contentItem: RowLayout {
                            spacing: 5

                            ToolButton {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                visible: treeDelegate.nodeType === "group"
                                enabled: treeDelegate.hasChildren
                                text: treeDelegate.hasChildren
                                      ? (treeDelegate.expanded ? "▾" : "▸")
                                      : "·"
                                onClicked: inventoryTreeModel.toggle(treeDelegate.index)
                            }

                            Label {
                                visible: treeDelegate.nodeType === "host"
                                text: "●"
                                color: palette.placeholderText
                            }

                            Label {
                                Layout.fillWidth: true
                                text: treeDelegate.name
                                elide: Text.ElideRight
                                font.weight: treeDelegate.nodeType === "group" ? Font.DemiBold : Font.Normal
                            }
                        }

                        onClicked: window.selectNode(nodeType, name)
                        onDoubleClicked: {
                            if (nodeType === "group" && hasChildren)
                                inventoryTreeModel.toggle(index)
                        }
                    }
                }
            }
        }

        GraphView {
            id: graph
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            SplitView.minimumWidth: 420
            nodes: inventory.graphNodes
            edges: inventory.graphEdges
            graphWidth: inventory.graphWidth
            graphHeight: inventory.graphHeight
            searchText: searchField.text
            selectedType: window.selectedType
            selectedName: window.selectedName
            onNodeSelected: function(type, name) { window.selectNode(type, name) }
        }

        Pane {
            id: inspector
            SplitView.preferredWidth: 380
            SplitView.minimumWidth: 300

            ScrollView {
                anchors.fill: parent
                clip: true

                ColumnLayout {
                    width: inspector.width - inspector.leftPadding - inspector.rightPadding - 16
                    spacing: 10

                    Label {
                        text: selectedName.length > 0 ? "Properties" : "Nothing selected"
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: selectedName.length > 0
                        spacing: 8

                        Label { text: selectedType === "host" ? "Host" : "Group"; color: palette.placeholderText }

                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: nameField
                                Layout.fillWidth: true
                                enabled: !(selectedType === "group" && selectedName === "all")
                            }
                            Button {
                                text: "Rename"
                                enabled: nameField.enabled && nameField.text.trim().length > 0 && nameField.text.trim() !== selectedName
                                onClicked: {
                                    const newName = nameField.text.trim()
                                    if (inventory.renameNode(selectedType, selectedName, newName)) {
                                        selectedName = newName
                                        refreshDetails()
                                    }
                                }
                            }
                        }

                        Label {
                            visible: selectedType === "host"
                            text: "Groups"
                            font.weight: Font.DemiBold
                        }

                        Repeater {
                            model: selectedType === "host" && selectedDetails.groups ? selectedDetails.groups : []
                            delegate: RowLayout {
                                required property string modelData
                                Layout.fillWidth: true
                                Label { Layout.fillWidth: true; text: modelData }
                                ToolButton {
                                    text: "×"
                                    onClicked: {
                                        inventory.removeHostFromGroup(selectedName, modelData)
                                        refreshDetails()
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: selectedType === "host"
                            ComboBox {
                                id: hostGroupCombo
                                Layout.fillWidth: true
                                model: candidateGroups()
                            }
                            Button {
                                text: "Add"
                                enabled: hostGroupCombo.count > 0
                                onClicked: {
                                    inventory.addHostToGroup(selectedName, hostGroupCombo.currentText)
                                    refreshDetails()
                                }
                            }
                        }

                        Label {
                            visible: selectedType === "host" && selectedDetails.groups && selectedDetails.groups.length === 0
                            text: "Ungrouped (stored under all)"
                            color: palette.placeholderText
                        }

                        Label {
                            visible: selectedType === "group" && selectedName !== "all"
                            text: "Parent groups"
                            font.weight: Font.DemiBold
                        }

                        Repeater {
                            model: selectedType === "group" && selectedDetails.parents ? selectedDetails.parents : []
                            delegate: RowLayout {
                                required property string modelData
                                Layout.fillWidth: true
                                Label { Layout.fillWidth: true; text: modelData }
                                ToolButton {
                                    text: "×"
                                    onClicked: {
                                        inventory.removeGroupFromGroup(selectedName, modelData)
                                        refreshDetails()
                                    }
                                }
                            }
                        }

                        Label {
                            visible: selectedType === "group" && selectedName !== "all"
                                     && selectedDetails.parents && selectedDetails.parents.length === 0
                            text: "Top-level group"
                            color: palette.placeholderText
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: selectedType === "group" && selectedName !== "all"
                            ComboBox {
                                id: parentGroupCombo
                                Layout.fillWidth: true
                                model: inventory.groupNames
                            }
                            Button {
                                text: parentGroupCombo.currentText === "all" ? "Make top-level" : "Add parent"
                                enabled: parentGroupCombo.currentText !== selectedName
                                onClicked: {
                                    inventory.addGroupToGroup(selectedName, parentGroupCombo.currentText)
                                    refreshDetails()
                                }
                            }
                        }

                        Label {
                            text: "Variables (YAML mapping)"
                            font.weight: Font.DemiBold
                            topPadding: 4
                        }

                        TextArea {
                            id: varsArea
                            Layout.fillWidth: true
                            Layout.preferredHeight: 260
                            wrapMode: TextEdit.NoWrap
                            font.family: "monospace"
                            placeholderText: "ansible_host: 10.0.0.10\nansible_user: root"
                            background: Rectangle {
                                color: palette.base
                                border.color: palette.mid
                                radius: 6
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Button {
                                text: "Apply variables"
                                onClicked: {
                                    if (inventory.setNodeVarsYaml(selectedType, selectedName, varsArea.text))
                                        refreshDetails()
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                visible: selectedType === "group"
                                text: (selectedDetails.hosts ? selectedDetails.hosts.length : 0) + " direct hosts"
                                color: palette.placeholderText
                            }
                        }
                    }
                }
            }
        }
    }
}
