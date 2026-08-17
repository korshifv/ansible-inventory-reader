import QtQuick
import QtQuick.Controls

Item {
    id: root

    property var nodes: []
    property var edges: []
    property real graphWidth: 1000
    property real graphHeight: 700
    property string searchText: ""
    property string selectedType: ""
    property string selectedName: ""

    signal nodeSelected(string type, string name)

    property real zoom: 1.0

    function matches(node) {
        if (searchText.trim().length === 0)
            return true
        const q = searchText.toLowerCase()
        return node.name.toLowerCase().includes(q)
               || (node.subtitle || "").toLowerCase().includes(q)
    }

    function fit() {
        if (graphWidth <= 0 || graphHeight <= 0)
            return
        const zx = Math.max(0.15, (width - 40) / graphWidth)
        const zy = Math.max(0.15, (height - 40) / graphHeight)
        zoom = Math.min(1.0, zx, zy)
        flick.contentX = 0
        flick.contentY = 0
    }

    Rectangle {
        anchors.fill: parent
        color: palette.base
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(width, root.graphWidth * root.zoom)
        contentHeight: Math.max(height, root.graphHeight * root.zoom)

        Item {
            id: world
            width: root.graphWidth
            height: root.graphHeight
            scale: root.zoom
            transformOrigin: Item.TopLeft

            Canvas {
                id: edgeCanvas
                anchors.fill: parent

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.lineWidth = 1.5 / root.zoom
                    ctx.strokeStyle = palette.mid

                    for (let i = 0; i < root.edges.length; ++i) {
                        const edge = root.edges[i]
                        const dx = Math.max(40, (edge.x2 - edge.x1) * 0.45)
                        ctx.beginPath()
                        ctx.moveTo(edge.x1, edge.y1)
                        ctx.bezierCurveTo(edge.x1 + dx, edge.y1,
                                          edge.x2 - dx, edge.y2,
                                          edge.x2, edge.y2)
                        ctx.stroke()
                    }
                }

                Connections {
                    target: root
                    function onEdgesChanged() { edgeCanvas.requestPaint() }
                    function onZoomChanged() { edgeCanvas.requestPaint() }
                }
            }

            Repeater {
                model: root.nodes

                delegate: Rectangle {
                    required property var modelData

                    x: modelData.x
                    y: modelData.y
                    width: modelData.width
                    height: modelData.height
                    radius: 9
                    opacity: root.matches(modelData) ? 1.0 : 0.22
                    color: modelData.type === "group" ? palette.alternateBase : palette.button
                    border.width: root.selectedType === modelData.type
                                  && root.selectedName === modelData.name ? 2.5 : 1
                    border.color: root.selectedType === modelData.type
                                  && root.selectedName === modelData.name
                                  ? palette.highlight : palette.mid

                    Column {
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 2

                        Text {
                            width: parent.width
                            text: (modelData.type === "group" ? "▣  " : "●  ") + modelData.name
                            color: palette.text
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            visible: (modelData.subtitle || "").length > 0
                            text: modelData.subtitle || ""
                            color: palette.placeholderText
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    TapHandler {
                        onTapped: root.nodeSelected(modelData.type, modelData.name)
                    }
                }
            }
        }

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: function(event) {
                const factor = event.angleDelta.y > 0 ? 1.12 : 0.89
                root.zoom = Math.max(0.18, Math.min(2.6, root.zoom * factor))
                event.accepted = true
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        spacing: 6

        ToolButton {
            text: "−"
            onClicked: root.zoom = Math.max(0.18, root.zoom / 1.15)
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: Math.round(root.zoom * 100) + "%"
        }
        ToolButton {
            text: "+"
            onClicked: root.zoom = Math.min(2.6, root.zoom * 1.15)
        }
        ToolButton {
            text: "Fit"
            onClicked: root.fit()
        }
    }
}
