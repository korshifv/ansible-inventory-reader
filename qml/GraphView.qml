import QtQuick
import QtQuick.Controls

Item {
    id: root

    property var nodes: []
    property var edges: []
    property var pingStates: ({})
    property bool problemsFirst: false
    property real graphWidth: 1000
    property real graphHeight: 700
    property string searchText: ""
    property string selectedType: ""
    property string selectedName: ""
    property color dangerColor: "#e05252"
    property color warningColor: "#d69a3a"

    signal nodeSelected(string type, string name)

    property real zoom: 1.0
    property int moveDuration: 210
    property int fadeDuration: 140
    property int edgeFrameInterval: edges.length > 300 ? 24 : 16
    property var relatedIds: buildRelationSet(selectedType, selectedName, edges)
    property var displayPositions: buildDisplayPositions(nodes, relatedIds, selectedName,
                                                         pingStates, problemsFirst)
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

    function problemOrderedHosts(hosts, states) {
        const unreachable = []
        const failed = []
        const rest = []

        for (let i = 0; i < hosts.length; ++i) {
            const node = hosts[i]
            const info = states[node.name] || ({})
            if (info.state === "unreachable" || info.state === "error")
                unreachable.push(node)
            else if (info.state === "failed")
                failed.push(node)
            else
                rest.push(node)
        }

        return unreachable.concat(failed, rest)
    }

    function buildDisplayPositions(graphNodes, relationSet, selectionName, states, sortProblems) {
        const positions = ({})

        if (selectionName.length === 0 && !sortProblems) {
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
                if (selectionName.length > 0 && relationSet[node.id] === true)
                    highlighted.push(node)
                else
                    rest.push(node)
            }

            const ordered = selectionName.length > 0 ? highlighted.concat(rest) : column
            for (let i = 0; i < ordered.length; ++i) {
                const node = ordered[i]
                positions[node.id] = {
                    x: node.x,
                    y: slots[i],
                    width: node.width,
                    height: node.height
                }
            }

            if (sortProblems) {
                const hosts = []
                const hostSlots = []
                for (let i = 0; i < ordered.length; ++i) {
                    const node = ordered[i]
                    if (node.type !== "host")
                        continue
                    hosts.push(node)
                    hostSlots.push(positions[node.id].y)
                }

                const problemOrdered = problemOrderedHosts(hosts, states)
                for (let i = 0; i < problemOrdered.length; ++i) {
                    const node = problemOrdered[i]
                    positions[node.id] = {
                        x: node.x,
                        y: hostSlots[i],
                        width: node.width,
                        height: node.height
                    }
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
        edgeFrameTimer.running = false
        edgeFrameTimer.elapsed = 0
        edgeCanvas.requestPaint()
        edgeFrameTimer.running = true
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
                edgeCanvas.requestPaint()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: palette.base
        z: -2
    }

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

                if (motionActive && hasSelection && !related)
                    continue

                const fromItem = root.nodeItems[edge.from]
                const toItem = root.nodeItems[edge.to]
                const fromFallback = root.displayPositions[edge.from]
                const toFallback = root.displayPositions[edge.to]

                if ((!fromItem && !fromFallback) || (!toItem && !toFallback))
                    continue

                const fromX = fromItem ? fromItem.x : fromFallback.x * root.zoom
                const fromY = fromItem ? fromItem.y : fromFallback.y * root.zoom
                const fromWidth = fromItem ? fromItem.width : fromFallback.width * root.zoom
                const fromHeight = fromItem ? fromItem.height : fromFallback.height * root.zoom
                const toX = toItem ? toItem.x : toFallback.x * root.zoom
                const toY = toItem ? toItem.y : toFallback.y * root.zoom
                const toHeight = toItem ? toItem.height : toFallback.height * root.zoom

                const x1 = fromX + fromWidth - flick.contentX
                const y1 = fromY + fromHeight / 2 - flick.contentY
                const x2 = toX - flick.contentX
                const y2 = toY + toHeight / 2 - flick.contentY

                if (Math.max(x1, x2) < -margin
                        || Math.min(x1, x2) > width + margin
                        || Math.max(y1, y2) < -margin
                        || Math.min(y1, y2) > height + margin)
                    continue

                const dx = Math.max(28 * root.zoom, (x2 - x1) * 0.45)
                ctx.lineWidth = (related ? 3.0 : 1.5) * Math.max(0.7, root.zoom)
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
            width: root.graphWidth * root.zoom
            height: root.graphHeight * root.zoom

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
                    property real layoutX: displayPosition.x
                    property real layoutY: displayPosition.y
                    property var pingInfo: modelData.type === "host"
                                           ? (root.pingStates[modelData.name] || ({}))
                                           : ({})
                    property bool pingUnreachable: pingInfo.state === "unreachable"
                                                   || pingInfo.state === "error"
                    property bool pingFailed: pingInfo.state === "failed"
                    property bool pingBad: pingUnreachable || pingFailed
                    property bool pingChecking: pingInfo.state === "checking"
                    property color statusColor: pingUnreachable
                                                ? root.dangerColor
                                                : (pingFailed ? root.warningColor : palette.placeholderText)
                    property real focusScale: selected ? 1.02 : 1.0
                    property real cardZoom: root.zoom * focusScale

                    x: layoutX * root.zoom
                    y: layoutY * root.zoom
                    width: modelData.width * root.zoom
                    height: modelData.height * root.zoom
                    radius: Math.max(4, Math.round(9 * root.zoom))
                    z: selected ? 3 : (related ? 2 : 1)
                    transformOrigin: Item.Center
                    opacity: !root.matches(modelData)
                             ? 0.10
                             : (hasSelection && !related ? 0.18 : 1.0)
                    color: pingUnreachable
                           ? Qt.rgba(0.88, 0.22, 0.22, 0.13)
                           : (pingFailed
                              ? Qt.rgba(0.84, 0.55, 0.20, 0.13)
                              : (modelData.type === "group" ? palette.alternateBase : palette.button))
                    border.width: Math.max(1, (selected ? 3.0 : (related || pingBad ? 2.0 : 1.0)) * root.zoom)
                    border.color: pingBad
                                  ? statusColor
                                  : (selected || related ? palette.highlight : palette.mid)

                    Behavior on layoutY {
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

                    Component.onCompleted: root.registerNode(modelData.id, nodeCard)
                    Component.onDestruction: root.unregisterNode(modelData.id, nodeCard)

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: Math.max(1, Math.round(2 * root.zoom))
                        radius: Math.max(2, parent.radius - Math.max(1, Math.round(2 * root.zoom)))
                        color: nodeCard.pingBad ? nodeCard.statusColor : palette.highlight
                        opacity: nodeCard.pingBad
                                 ? 0.08
                                 : (nodeCard.selected ? 0.20 : (nodeCard.related ? 0.07 : 0.0))

                        Behavior on opacity {
                            NumberAnimation {
                                duration: root.fadeDuration
                                easing.type: Easing.OutCubic
                            }
                        }
                    }

                    Column {
                        anchors.fill: parent
                        anchors.margins: Math.max(4, Math.round(9 * root.zoom))
                        spacing: Math.max(1, Math.round(2 * root.zoom))

                        Text {
                            width: parent.width
                            text: (modelData.type === "group" ? "▣  " : (nodeCard.pingChecking ? "◌  " : "●  "))
                                  + modelData.name
                            color: nodeCard.pingBad ? nodeCard.statusColor : palette.text
                            font.pixelSize: Math.max(9, Math.round(13 * root.zoom * nodeCard.focusScale))
                            font.weight: nodeCard.selected ? Font.Bold : Font.DemiBold
                            elide: Text.ElideRight
                            renderType: Text.QtRendering
                        }

                        Text {
                            width: parent.width
                            visible: displayText.length > 0 && root.zoom >= 0.42
                            property string displayText: {
                                const base = modelData.subtitle || ""
                                if (nodeCard.pingBad && nodeCard.pingInfo.reason)
                                    return base.length > 0 ? base + " · " + nodeCard.pingInfo.reason : nodeCard.pingInfo.reason
                                if (nodeCard.pingChecking)
                                    return base.length > 0 ? base + " · checking…" : "checking…"
                                return base
                            }
                            text: displayText
                            color: nodeCard.pingBad ? nodeCard.statusColor : palette.placeholderText
                            font.pixelSize: Math.max(8, Math.round(11 * root.zoom * nodeCard.focusScale))
                            elide: Text.ElideRight
                            renderType: Text.QtRendering
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
