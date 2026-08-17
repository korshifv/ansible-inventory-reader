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
    property var relatedIds: buildRelationSet(selectedType, selectedName, edges)
    property var displayPositions: buildDisplayPositions(nodes, relatedIds, selectedName)

    function nodeId(type, name) {
        return type + ":" + name
    }

    function buildRelationSet(type, name, graphEdges) {
        const result = ({})
        if (type.length === 0 || name.length === 0)
            return result

        const selected = nodeId(type, name)
        result[selected] = true

        const parents = ({})
        const children = ({})

        for (let i = 0; i < graphEdges.length; ++i) {
            const edge = graphEdges[i]

            if (!parents[edge.to])
                parents[edge.to] = []
            parents[edge.to].push(edge.from)

            if (!children[edge.from])
                children[edge.from] = []
            children[edge.from].push(edge.to)
        }

        // Selecting any child keeps its full ancestry visible, all the way to `all`.
        let queue = [selected]
        let cursor = 0
        while (cursor < queue.length) {
            const current = queue[cursor++]
            const currentParents = parents[current] || []
            for (let i = 0; i < currentParents.length; ++i) {
                const parent = currentParents[i]
                if (!result[parent]) {
                    result[parent] = true
                    queue.push(parent)
                }
            }
        }

        // A group behaves like a folder: selecting it lights up everything below it,
        // including nested groups and hosts in those groups.
        if (type === "group") {
            queue = [selected]
            cursor = 0
            while (cursor < queue.length) {
                const current = queue[cursor++]
                const currentChildren = children[current] || []
                for (let i = 0; i < currentChildren.length; ++i) {
                    const child = currentChildren[i]
                    if (!result[child]) {
                        result[child] = true
                        queue.push(child)
                    }
                }
            }
        }

        return result
    }

    function buildDisplayPositions(graphNodes, relationSet, selectionName) {
        const positions = ({})

        // With no selection, preserve the canonical layout produced by C++.
        if (selectionName.length === 0) {
            for (let i = 0; i < graphNodes.length; ++i) {
                const node = graphNodes[i]
                positions[node.id] = {
                    x: node.x,
                    y: node.y,
                    width: node.width,
                    height: node.height
                }
            }
            return positions
        }

        // Nodes are laid out in vertical columns (same x = same depth). Keep those
        // columns intact, but stable-partition every column so highlighted nodes
        // occupy its top slots. Nothing in the actual inventory model is reordered.
        const columns = ({})
        for (let i = 0; i < graphNodes.length; ++i) {
            const node = graphNodes[i]
            const key = String(node.x)
            if (!columns[key])
                columns[key] = []
            columns[key].push(node)
        }

        for (const key in columns) {
            const column = columns[key]
            column.sort(function(a, b) { return a.y - b.y })

            const slots = []
            const highlighted = []
            const rest = []

            for (let i = 0; i < column.length; ++i) {
                const node = column[i]
                slots.push(node.y)
                if (relationSet[node.id] === true)
                    highlighted.push(node)
                else
                    rest.push(node)
            }

            const ordered = highlighted.concat(rest)
            for (let i = 0; i < ordered.length; ++i) {
                const node = ordered[i]
                positions[node.id] = {
                    x: node.x,
                    y: slots[i],
                    width: node.width,
                    height: node.height
                }
            }
        }

        return positions
    }

    function positionFor(node) {
        return displayPositions[node.id] || node
    }

    function isRelated(type, name) {
        if (selectedName.length === 0)
            return false
        return relatedIds[nodeId(type, name)] === true
    }

    function edgeIsRelated(edge) {
        if (selectedName.length === 0)
            return false
        return relatedIds[edge.from] === true && relatedIds[edge.to] === true
    }

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

                    for (let i = 0; i < root.edges.length; ++i) {
                        const edge = root.edges[i]
                        const related = root.edgeIsRelated(edge)
                        const hasSelection = root.selectedName.length > 0
                        const from = root.displayPositions[edge.from]
                        const to = root.displayPositions[edge.to]

                        if (!from || !to)
                            continue

                        const x1 = edge.x1
                        const y1 = from.y + from.height / 2
                        const x2 = edge.x2
                        const y2 = to.y + to.height / 2
                        const dx = Math.max(40, (x2 - x1) * 0.45)

                        ctx.lineWidth = (related ? 3.0 : 1.5) / root.zoom
                        ctx.strokeStyle = related ? palette.highlight : palette.mid
                        ctx.globalAlpha = related ? 1.0 : (hasSelection ? 0.13 : 0.75)
                        ctx.beginPath()
                        ctx.moveTo(x1, y1)
                        ctx.bezierCurveTo(x1 + dx, y1,
                                          x2 - dx, y2,
                                          x2, y2)
                        ctx.stroke()
                    }
                    ctx.globalAlpha = 1.0
                }

                Connections {
                    target: root
                    function onEdgesChanged() { edgeCanvas.requestPaint() }
                    function onZoomChanged() { edgeCanvas.requestPaint() }
                    function onRelatedIdsChanged() { edgeCanvas.requestPaint() }
                    function onDisplayPositionsChanged() { edgeCanvas.requestPaint() }
                    function onSelectedNameChanged() { edgeCanvas.requestPaint() }
                }
            }

            Repeater {
                model: root.nodes

                delegate: Rectangle {
                    id: nodeCard
                    required property var modelData

                    property bool selected: root.selectedType === modelData.type
                                            && root.selectedName === modelData.name
                    property bool related: root.isRelated(modelData.type, modelData.name)
                    property bool hasSelection: root.selectedName.length > 0
                    property var displayPosition: root.positionFor(modelData)

                    x: displayPosition.x
                    y: displayPosition.y
                    width: modelData.width
                    height: modelData.height
                    radius: 9
                    opacity: !root.matches(modelData)
                             ? 0.10
                             : (hasSelection && !related ? 0.18 : 1.0)
                    color: modelData.type === "group" ? palette.alternateBase : palette.button
                    border.width: selected ? 3.0 : (related ? 2.0 : 1.0)
                    border.color: selected || related ? palette.highlight : palette.mid

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: Math.max(0, parent.radius - 2)
                        color: palette.highlight
                        opacity: nodeCard.selected ? 0.22 : (nodeCard.related ? 0.09 : 0.0)
                    }

                    Column {
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 2

                        Text {
                            width: parent.width
                            text: (modelData.type === "group" ? "▣  " : "●  ") + modelData.name
                            color: palette.text
                            font.weight: nodeCard.selected ? Font.Bold : Font.DemiBold
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
