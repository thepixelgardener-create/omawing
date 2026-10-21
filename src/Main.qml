import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window
import Omawing

ApplicationWindow {
    id: win
    width: 1280
    height: 760
    minimumWidth: 640
    minimumHeight: 400
    visible: true
    title: "Omawing"
    color: "#0e1020"

    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    // The toolbar shows while the pointer moves, then gets out of the way.
    property bool pointerAwake: true

    function showScores() {
        if (board.visible) return
        board.wasPaused = backend.paused
        // The flight waits while the board is open; the music plays on.
        backend.setPaused(true, true)
        player.text = backend.scoreBoard.playerName
        board.open()
        if (backend.scoreBoard.pendingScore > 0) player.forceActiveFocus()
    }
    function localizedNumber(value) {
        // Reading the language makes bindings that use this re-run when it changes.
        const language = translator.language
        return Number(value).toLocaleString(Qt.locale(), 'f', 0)
    }
    // A run that made the board opens it, to ask for a name.
    Connections {
        target: backend.scoreBoard
        function onPending() { Qt.callLater(win.showScores) }
    }

    function toggleFullScreen() { win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
    function wake() { pointerAwake = true; sleepTimer.restart(); backend.wake() }
    function toggleVolume() {
        if (volume.visible) volume.close()
        else { volume.open(); musicSlider.forceActiveFocus() }
    }

    onActiveChanged: if (!active) backend.clearInput()
    onVisibilityChanged: backend.setHidden(win.visibility === Window.Minimized || win.visibility === Window.Hidden)

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: backend.togglePause() }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: backend.toggleDemo() }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: backend.nextScene() }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: backend.toggleMusic() }
    Shortcut { sequence: "E"; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: backend.toggleEffects() }
    Shortcut { sequence: "V"; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: win.toggleVolume() }
    Shortcut { sequence: "P"; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: backend.nextTrack() }
    Shortcut { sequence: "U"; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: backend.toggleTitles() }
    Shortcut { sequence: "L"; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: { backend.toggleToolbar(); if (backend.toolbarShown) win.wake() } }
    Shortcut { sequence: "B"; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: backend.toggleBanner() }
    Shortcut { sequences: ["+", "="]; context: Qt.ApplicationShortcut; enabled: !board.visible && !volume.visible && !win.helpVisible; onActivated: backend.zoom(1) }
    Shortcut { sequence: "-"; context: Qt.ApplicationShortcut; enabled: !board.visible && !volume.visible && !win.helpVisible; onActivated: backend.zoom(-1) }
    Shortcut { sequence: "0"; context: Qt.ApplicationShortcut; enabled: !board.visible && !volume.visible && !win.helpVisible; onActivated: backend.resetZoom() }
    Shortcut { sequence: "H"; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: win.showScores() }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: backend.restart() }
    Shortcut { sequences: ["F", "F11"]; context: Qt.ApplicationShortcut; enabled: !board.visible; onActivated: win.toggleFullScreen() }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (volume.visible) volume.close()
            else if (board.visible) board.close()
            else if (win.helpVisible) win.helpVisible = false
            else if (win.visibility === Window.FullScreen) win.visibility = Window.Windowed
        }
    }

    FrameAnimation {
        running: win.visibility !== Window.Minimized && win.visibility !== Window.Hidden
        onTriggered: backend.tick(frameTime)
    }

    Timer { id: sleepTimer; interval: 2500; running: true; onTriggered: win.pointerAwake = false }

    FlightView {
        id: view
        anchors.fill: parent
        // Not named backend: inside the view that would resolve to its own property.
        source: backend
        focus: true
        Component.onCompleted: forceActiveFocus()

        // Flight keys are held, so they need press and release, not shortcuts.
        Keys.onPressed: event => { if (!event.isAutoRepeat && backend.keyDown(event.key)) event.accepted = true }
        Keys.onReleased: event => { if (!event.isAutoRepeat && backend.keyUp(event.key)) event.accepted = true }

        MouseArea {
            id: drag
            anchors.fill: parent
            hoverEnabled: true
            // Hold the left button to boost; drag with the right one to steer.
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onWheel: wheel => {
                if (board.visible || volume.visible || win.helpVisible) { wheel.accepted = false; return }
                const steps = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y / 40 : wheel.angleDelta.y / 120
                backend.zoom(steps)
                wheel.accepted = true
            }
            property point origin
            property bool steering: false
            onPressed: mouse => {
                view.forceActiveFocus()
                if (mouse.button === Qt.LeftButton) {
                    backend.mouseBoost(true)
                } else {
                    steering = true
                    origin = Qt.point(mouse.x, mouse.y)
                    backend.stickStart()
                }
            }
            onPositionChanged: mouse => {
                win.wake()
                // Dragging a sixth of the window's width is full stick.
                if (steering) backend.stickMove((mouse.x - origin.x) / (width * 0.16), (mouse.y - origin.y) / (width * 0.16))
            }
            onReleased: mouse => {
                if (mouse.button === Qt.LeftButton) {
                    backend.mouseBoost(false)
                } else {
                    steering = false
                    backend.stickEnd()
                }
            }
            onCanceled: { backend.mouseBoost(false); steering = false; backend.stickEnd() }
        }
    }

    // Right-to-left languages mirror the controls; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            // Sits just above the scroller band, which is 60 units of the
            // scene's canvas (at least 540 tall and 860 wide).
            anchors.bottomMargin: (backend.banner ? Math.min(parent.height / 540, parent.width / 860) * 64 : 0) + 12
            spacing: 8

            Label {
                Layout.alignment: Qt.AlignHCenter
                visible: backend.soundError !== "" || backend.settingsError !== ""
                Layout.maximumWidth: win.width - 40
                wrapMode: Text.WordWrap
                text: backend.settingsError || backend.soundError
                textFormat: Text.PlainText
                color: "#ffb4a9"
                padding: 10
                background: Rectangle { color: "#d0101024"; radius: Math.min(4, theme.radius) }
            }

            Button {
                objectName: "nextMusicTrack"
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: win.width - 40
                focusPolicy: Qt.NoFocus
                Material.roundedScale: theme.radius
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Next track") + " (P)"
                onClicked: backend.nextTrack()
                opacity: backend.music && toolbar.shown ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 350; easing.type: Easing.InOutQuad } }
                text: "♫  " + backend.trackTitle
                contentItem: Label { text: parent.text; textFormat: Text.PlainText; elide: Text.ElideRight }
                padding: 8
                background: Rectangle { color: "#d0101024"; radius: Math.min(4, theme.radius) }
            }

            Pane {
                id: toolbar
                objectName: "toolbar"
                Layout.preferredWidth: Math.min(win.width - 24, 1120)
                Layout.alignment: Qt.AlignHCenter
                padding: 6
                // Fades and rises in when the pointer moves, and sinks away
                // again a few seconds later. L switches it off altogether.
                readonly property bool shown: volume.visible || backend.toolbarShown
                    && (win.pointerAwake || backend.paused || toolbarHover.hovered || win.helpVisible || volume.visible)
                opacity: shown ? 1 : 0
                visible: opacity > 0
                transform: Translate { y: (1 - toolbar.opacity) * 14 }
                Behavior on opacity { NumberAnimation { duration: 350; easing.type: Easing.InOutQuad } }
                background: Rectangle { color: "#e0101024"; radius: theme.radius; border.color: "#2f3250" }
                HoverHandler { id: toolbarHover }

                // Music and effects levels, just above the toolbar. The arrow
                // keys move the focused slider; Tab switches between them.
                Popup {
                    id: volume
                    objectName: "volumePanel"
                    x: (toolbar.width - width) / 2
                    y: -height - 8
                    padding: 16
                    closePolicy: Popup.CloseOnPressOutsideParent
                    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200; easing.type: Easing.OutQuad } }
                    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 200; easing.type: Easing.InQuad } }
                    onClosed: view.forceActiveFocus()
                    background: Rectangle { color: "#f0101024"; radius: theme.radius; border.color: "#2f3250" }

                    GridLayout {
                        columns: 3
                        columnSpacing: 12
                        rowSpacing: 4
                        Label { text: qsTr("Music") }
                        Slider {
                            id: musicSlider
                            objectName: "musicVolume"
                            Layout.preferredWidth: 220
                            stepSize: 0.05
                            value: backend.musicVolume
                            onMoved: backend.musicVolume = value
                            KeyNavigation.tab: effectsSlider
                        }
                        Label { text: win.localizedNumber(Math.round(musicSlider.value * 100)) + " %"; Layout.preferredWidth: 48; opacity: backend.music ? 1 : 0.5 }
                        Label { text: qsTr("Effects") }
                        Slider {
                            id: effectsSlider
                            objectName: "effectsVolume"
                            Layout.preferredWidth: 220
                            stepSize: 0.05
                            value: backend.effectsVolume
                            onMoved: backend.effectsVolume = value
                            KeyNavigation.tab: musicSlider
                        }
                        Label { text: win.localizedNumber(Math.round(effectsSlider.value * 100)) + " %"; Layout.preferredWidth: 48; opacity: backend.effects ? 1 : 0.5 }
                    }
                }

                GridLayout {
                    width: toolbar.availableWidth
                    columns: win.width < 800 ? 4 : win.width < 1200 ? 6 : 11
                    columnSpacing: 4
                    rowSpacing: 0
                    BarButton { text: backend.demo ? qsTr("Take control") : qsTr("Watch demo"); key: "T"; highlighted: backend.demo; onClicked: backend.toggleDemo() }
                    BarButton { text: backend.paused ? qsTr("Play") : qsTr("Pause"); key: "Space"; onClicked: backend.togglePause() }
                    BarButton { text: qsTr("Next scene"); key: "N"; onClicked: backend.nextScene() }
                    BarButton { text: qsTr("Music"); key: "M"; checkable: true; checked: backend.music; onClicked: backend.toggleMusic() }
                    BarButton { text: qsTr("Effects"); key: "E"; checkable: true; checked: backend.effects; onClicked: backend.toggleEffects() }
                    BarButton { text: qsTr("Volume"); key: "V"; checkable: true; checked: volume.visible; onClicked: win.toggleVolume() }
                    BarButton { text: qsTr("Banner"); key: "B"; checkable: true; checked: backend.banner; onClicked: backend.toggleBanner() }
                    BarButton { text: qsTr("High scores"); key: "H"; onClicked: win.showScores() }
                    BarButton { text: qsTr("Restart"); key: "R"; onClicked: backend.restart() }
                    BarButton { text: qsTr("Full screen"); key: "F"; checkable: true; checked: win.visibility === Window.FullScreen; onClicked: win.toggleFullScreen() }
                    BarButton { text: "?"; key: "?"; onClicked: win.helpVisible = !win.helpVisible }
                }
            }
        }


        Rectangle {
            anchors.fill: parent
            opacity: win.helpVisible ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            color: "#b0060714"
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

            Pane {
                anchors.centerIn: parent
                padding: 20
                width: Math.min(parent.width - 32, 640)
                height: Math.min(parent.height - 32, helpContent.implicitHeight + 40)
                background: Rectangle { color: "#14162a"; radius: theme.radius; border.color: "#2f3250" }

                ScrollView {
                    id: helpScroll
                    ScrollBar.vertical.policy: ScrollBar.AlwaysOn
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    contentHeight: helpContent.implicitHeight
                    ColumnLayout {
                        id: helpContent
                        width: helpScroll.availableWidth
                        spacing: 12
                        Label { text: qsTr("Keyboard shortcuts"); font.pixelSize: 20; font.bold: true }
                        // Grouped like the game: flying, sound, what shows on
                        // screen, and the rest. Keys are keycaps, like the
                        // switch keys in the corner of the picture; the key
                        // column is as wide as the widest key in the language.
                        FontMetrics { id: keyMetrics; font.family: "monospace"; font.bold: true }
                        readonly property real keyColumn: {
                            let widest = 0
                            for (const group of shortcutGroups)
                                for (const key of group.keys)
                                    widest = Math.max(widest, keyMetrics.advanceWidth(key[0]))
                            return Math.min(widest + 16, helpContent.width * 0.45)
                        }
                        readonly property var shortcutGroups: [
                                {title: qsTr("Flying"), keys: [
                                    ["A D  ← →", qsTr("Steer")],
                                    ["W S  ↑ ↓", qsTr("Climb or descend")],
                                    ["Shift", qsTr("Boost")],
                                    [qsTr("Left button"), qsTr("Boost")],
                                    [qsTr("Right drag"), qsTr("Steer with the mouse")],
                                    ["T", qsTr("Take control or watch the demo")]]},
                                {title: qsTr("Sound"), keys: [
                                    ["M", qsTr("Turn music on or off")],
                                    ["E", qsTr("Turn sound effects on or off")],
                                    ["V", qsTr("Music and effects volume")],
                                    ["P", qsTr("Next track")]]},
                                {title: qsTr("View"), keys: [
                                    ["+ / =", qsTr("Zoom in")],
                                    ["−", qsTr("Zoom out")],
                                    ["0", qsTr("Reset zoom")],
                                    [qsTr("Mouse wheel"), qsTr("Zoom in / out")],
                                    ["U", qsTr("Show or hide the titles and score")],
                                    ["L", qsTr("Show or hide the toolbar")],
                                    ["B", qsTr("Show or hide the banner")],
                                    ["F", qsTr("Full screen on or off")],
                                    ["N", qsTr("Next scene")]]},
                                {title: qsTr("Game"), keys: [
                                    ["Space", qsTr("Pause or play")],
                                    ["H", qsTr("High scores")],
                                    ["R", qsTr("Restart from the beginning")],
                                    ["?", qsTr("Show or hide shortcuts")],
                                    ["Q", qsTr("Quit")]]}
                            ]
                        Repeater {
                            model: helpContent.shortcutGroups
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 8
                                Label {
                                    Layout.topMargin: 6
                                    text: modelData.title
                                    color: theme.accent
                                    font.pixelSize: 12
                                    font.bold: true
                                    // Spaced out only in scripts whose letters stand
                                    // alone; spacing would break up joined ones.
                                    font.letterSpacing: /^[\u0000-\u024F\u0370-\u04FF\s]*$/.test(text) ? 2 : 0
                                    font.capitalization: Font.AllUppercase
                                }
                                Repeater {
                                    model: modelData.keys
                                    delegate: RowLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 16
                                        Item {
                                            Layout.preferredWidth: helpContent.keyColumn
                                            implicitHeight: keycap.height
                                            Rectangle {
                                                id: keycap
                                                width: Math.min(keyLabel.implicitWidth + 14, helpContent.keyColumn)
                                                height: keyLabel.height + 6
                                                radius: Math.min(4, theme.radius + 2)
                                                color: Qt.lighter(theme.accent, 1.15)
                                                Label {
                                                    id: keyLabel
                                                    anchors.centerIn: parent
                                                    width: Math.min(implicitWidth, helpContent.keyColumn - 14)
                                                    wrapMode: Text.WordWrap
                                                    horizontalAlignment: Text.AlignHCenter
                                                    text: modelData[0]
                                                    color: theme.accentForeground === "black" ? "#14122a" : "white"
                                                    font.family: "monospace"
                                                    font.bold: true
                                                }
                                            }
                                        }
                                        Label { text: modelData[1]; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: board
        objectName: "scoreBoard"
        popupType: Popup.Item
        Material.theme: Material.Dark
        Material.accent: theme.accent
        Material.roundedScale: theme.radius
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(win.width - 32, 580)
        height: Math.min(win.height - 32, implicitHeaderHeight + implicitFooterHeight + topPadding + bottomPadding + scoreContent.implicitHeight)
        leftPadding: 24
        rightPadding: 24
        topPadding: 12
        bottomPadding: 12
        modal: true
        focus: true
        title: qsTr("High scores")
        header: Label {
            text: board.title
            padding: 24
            bottomPadding: 12
            font.pixelSize: 24
            color: "#f4f0ff"
        }
        Overlay.modal: Rectangle { color: "#b0060714" }
        footer: DialogButtonBox {
            background: Rectangle { color: "transparent" }
            Button { text: qsTr("Close"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            onRejected: board.close()
        }
        closePolicy: Popup.CloseOnEscape
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 220; easing.type: Easing.OutQuad }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 220; easing.type: Easing.OutQuad }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 180; easing.type: Easing.InQuad }
            NumberAnimation { property: "scale"; from: 1; to: 0.97; duration: 180; easing.type: Easing.InQuad }
        }
        property bool wasPaused: false
        background: Rectangle { color: "#14162a"; radius: theme.radius; border.color: "#454867" }
        onClosed: {
            if (backend.scoreBoard.pendingScore > 0) backend.scoreBoard.save(player.text)
            backend.setPaused(wasPaused)
            view.forceActiveFocus()
        }
        contentItem: ScrollView {
            id: scoreScroll
            clip: true
            contentWidth: availableWidth
            contentHeight: scoreContent.implicitHeight
            ColumnLayout {
                id: scoreContent
                width: scoreScroll.availableWidth
                height: implicitHeight
                spacing: 10
                Label {
                    visible: backend.scoreBoard.pendingScore > 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("New high score: %1").arg(win.localizedNumber(backend.scoreBoard.pendingScore))
                    color: theme.accent
                    font.pixelSize: 22
                }
                RowLayout {
                    visible: backend.scoreBoard.pendingScore > 0
                    Layout.fillWidth: true
                    TextField {
                        id: player
                        objectName: "playerName"
                        Layout.fillWidth: true
                        maximumLength: backend.scoreBoard.nameLength
                        placeholderText: qsTr("Your name")
                        onAccepted: backend.scoreBoard.save(text)
                    }
                    Button { text: qsTr("Save score"); onClicked: backend.scoreBoard.save(player.text) }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    visible: backend.settingsError !== ""
                    text: backend.settingsError
                    textFormat: Text.PlainText
                    color: "#ffb4a9"
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Fly through rings. Boost doubles points. Chain rings for up to 8×.")
                    opacity: 0.8
                }
                Label {
                    visible: backend.scoreBoard.count === 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("No scores yet. Take control and fly!")
                    padding: 12
                }
                Repeater {
                    model: backend.scoreBoard
                    delegate: Rectangle {
                        required property int index
                        required property string name
                        required property int score
                        required property int rings
                        objectName: "scoreRow"
                        Layout.fillWidth: true
                        implicitHeight: 44
                        Layout.minimumHeight: 44
                        color: index % 2 ? "#22253e" : "#1b1e34"
                        radius: Math.min(4, theme.radius)
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            Label { text: win.localizedNumber(index + 1); Layout.preferredWidth: 28; color: theme.accent }
                            // Names are typed by players: shown as text, never as markup.
                            Label { text: name; textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideRight }
                            Label { text: win.localizedNumber(score); font.bold: true }
                            Label { text: qsTr("Rings: %1").arg(win.localizedNumber(rings)); Layout.preferredWidth: 95; horizontalAlignment: Text.AlignRight }
                        }
                    }
                }
            }
        }
    }

    component BarButton: Button {
        property string key
        Material.roundedScale: theme.radius
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        contentItem: Label { text: parent.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        flat: true
        // Clicking must not take keyboard focus from the flight.
        focusPolicy: Qt.NoFocus
        ToolTip.visible: hovered
        ToolTip.delay: 400
        ToolTip.text: key
    }
}
