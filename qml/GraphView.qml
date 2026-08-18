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
    property int moveDuration: 210
    property int fadeDuration: 140
    property int edgeFrameInterval: edges.length > 300 ? 24 : 16
    property var relatedIds: buildRelationSet(selectedType, selectedName, edges)
    property var displayPositions: buildDisplayPositions(nodes, relatedIds, selectedName)
    property var nodeItems: ({})

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

    function registerNode(id, item) {
        nodeItems[id] = item
    }

    function unregisterNode(id, item) {
        if (nodeItems[id] === item)
            delete nodeItems[id]
    }

    function scheduleEdgeFrames() {
        // One repaint per display frame, regardless of how many cards are moving.
        edgeFrameTimer.running = false
        edgeFrameTimer.elapsed = 0
        edgeCanvas.requestPaint()
        edgeFrameTimer.running = true
    }

    function worldToViewportX(x) {
        return x * zoom - flick.contentX
    }

    function worldToViewportY(y) {
        return y * zoom - flick.contentY
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
        edgeCanvas.requestPaint()
    }

    onDisplayPositionsChanged: scheduleEdgeFrames()

    Timer {
        id: edgeFrameTimer
        interval: root.edgeFrameInterval
        repeat: true
        running: false
        property int elapsed: 0

        onTriggered: {
            edgeCanvas.requestPaint()
            elapsed += interval
            if (elapsed >= root.moveDuration + 48) {
                running = false
                // One final settled frame restores all faint background edges.
                edgeCanvas.requestPaint()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: palette.base
        z: -2
    }

    // Important: this canvas is viewport-sized, not graph-sized. A very tall
    // inventory therefore does not allocate and clear a gigantic backing image
    // for every animation frame.
    Canvas {
        id: edgeCanvas
        anchors.fill: parent
        z: -1

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            const hasSelection = root.selectedName.length > 0
            const motionActive = edgeFrameTimer.running
            const highlightColor = palette.highlight
            const normalColor = palette.mid
            const margin = 100

            for (let i = 0; i < root.edges.length; ++i) {
                const edge = root.edges[i]
                const related = root.edgeIsRelated(edge)

                // While a branch is flying into focus, don't waste paint time on
                // unrelated spaghetti that is intentionally almost invisible anyway.
                if (motionActive && hasSelection && !related)
                    continue

                const fromItem = root.nodeItems[edge.from]
                const toItem = root.nodeItems[edge.to]
                const fromFallback = root.displayPositions[edge.from]
                const toFallback = root.displayPositions[edge.to]

                if ((!fromItem && !fromFallback) || (!toItem && !toFallback))
                    continue

                const fromX = fromItem ? fromItem.x : fromFallback.x
                const fromY = fromItem ? fromItem.y : fromFallback.y
                const fromWidth = fromItem ? fromItem.width : fromFallback.width
                const fromHeight = fromItem ? fromItem.height : fromFallback.height
                const toX = toItem ? toItem.x : toFallback.x
                const toY = toItem ? toItem.y : toFallback.y
                const toHeight = toItem ? toItem.height : toFallback.height

                const x1 = root.worldToViewportX(fromX + fromWidth)
                const y1 = root.worldToViewportY(fromY + fromHeight / 2)
                const x2 = root.worldToViewportX(toX)
                const y2 = root.worldToViewportY(toY + toHeight / 2)

                // Curves outside the viewport never hit the backing image at all.
                if (Math.max(x1, x2) < -margin
                        || Math.min(x1, x2) > width + margin
                        || Math.max(y1, y2) < -margin
                        || Math.min(y1, y2) > height + margin)
                    continue

                const dx = Math.max(28, (x2 - x1) * 0.45)

                // Canvas now lives in screen coordinates, so line width stays constant
                // and does not need to be divided by zoom.
                ctx.lineWidth = related ? 3.0 : 1.5
                ctx.strokeStyle = related ? highlightColor : normalColor
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
    }

    Flickable {
        id: flick
        anchors.fill: parent
        z: 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(width, root.graphWidth * root.zoom)
        contentHeight: Math.max(height, root.graphHeight * root.zoom)

        onContentXChanged: edgeCanvas.requestPaint()
        onContentYChanged: edgeCanvas.requestPaint()

        Item {
            id: world
            width: root.graphWidth
            height: root.graphHeight
            scale: root.zoom
            transformOrigin: Item.TopLeft

            Repeater {
                id: nodeRepeater
                model: root.nodes

                delegate: Rectangle {
                    id: nodeCard
                    required property var modelData

                    property bool selected: root.selectedType === modelData.type
                                            && root.selectedName === modelData.name
                    property bool related: root.isRelated(modelData.type, modelData.name)
                    property bool hasSelection: root.selectedName.length > 0
                    property var displayPosition: root.positionFor(modelData)
                    property real focusScale: selected ? 1.02 : 1.0

                    x: displayPosition.x
                    y: displayPosition.y
                    width: modelData.width
                    height: modelData.height
                    radius: 9
                    z: selected ? 3 : (related ? 2 : 1)
                    scale: focusScale
                    transformOrigin: Item.Center
                    opacity: !root.matches(modelData)
                             ? 0.10
                             : (hasSelection && !related ? 0.18 : 1.0)
                    color: modelData.type === "group" ? palette.alternateBase : palette.button
                    border.width: selected ? 3.0 : (related ? 2.0 : 1.0)
                    border.color: selected || related ? palette.highlight : palette.mid

                    // Focus mode only changes vertical order. Scene graph handles the
                    // cards; the lightweight viewport canvas follows them at frame rate.
                    Behavior on y {
                        NumberAnimation {
                            duration: root.moveDuration
                            easing.type: Easing.OutCubic
                        }
                    }
                    Behavior on opacity {
                        NumberAnimation {
                            duration: root.fadeDuration
                            easing.type: Easing.OutCubic
                        }
                    }
                    Behavior on focusScale {
                        NumberAnimation {
                            duration: 120
                            easing.type: Easing.OutCubic
                        }
                    }

                    Component.onCompleted: root.registerNode(modelData.id, nodeCard)
                    Component.onDestruction: root.unregisterNode(modelData.id, nodeCard)

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: Math.max(0, parent.radius - 2)
                        color: palette.highlight
                        opacity: nodeCard.selected ? 0.20 : (nodeCard.related ? 0.07 : 0.0)

                        Behavior on opacity {
                            NumberAnimation {
                                duration: root.fadeDuration
                                easing.type: Easing.OutCubic
                            }
                        }
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
                edgeCanvas.requestPaint()
                event.accepted = true
            }
        }
    }

    Connections {
        target: root
        function onEdgesChanged() { root.scheduleEdgeFrames() }
        function onZoomChanged() { edgeCanvas.requestPaint() }
        function onRelatedIdsChanged() { edgeCanvas.requestPaint() }
        function onSelectedNameChanged() { edgeCanvas.requestPaint() }
    }

    Row {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        spacing: 6
        z: 10

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
