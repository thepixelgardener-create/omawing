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

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    // The toolbar shows while the pointer moves, then gets out of the way.
    property bool pointerAwake: true

    function toggleFullScreen() { win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
    function wake() { pointerAwake = true; sleepTimer.restart() }

    onActiveChanged: if (!active) backend.clearInput()
    onVisibilityChanged: backend.setHidden(win.visibility === Window.Minimized || win.visibility === Window.Hidden)

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: backend.togglePause() }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; onActivated: backend.toggleDemo() }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; onActivated: backend.nextScene() }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; onActivated: backend.toggleSound() }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; onActivated: backend.restart() }
    Shortcut { sequences: ["F", "F11"]; context: Qt.ApplicationShortcut; onActivated: win.toggleFullScreen() }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (win.visibility === Window.FullScreen) win.visibility = Window.Windowed
        }
    }

    FrameAnimation {
        running: true
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
            property point origin
            onPressed: mouse => { view.forceActiveFocus(); origin = Qt.point(mouse.x, mouse.y); backend.stickStart() }
            onPositionChanged: mouse => {
                win.wake()
                // Dragging a sixth of the window's width is full stick.
                if (pressed) backend.stickMove((mouse.x - origin.x) / (width * 0.16), (mouse.y - origin.y) / (width * 0.16))
            }
            onReleased: backend.stickEnd()
            onCanceled: backend.stickEnd()
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
            anchors.bottomMargin: Math.min(parent.height / 540, parent.width / 860) * 64 + 12
            spacing: 8

            Label {
                Layout.alignment: Qt.AlignHCenter
                visible: backend.soundError !== ""
                text: backend.soundError
                color: "#ffb4a9"
                padding: 10
                background: Rectangle { color: "#d0101024"; radius: Math.min(4, theme.radius) }
            }

            Pane {
                id: toolbar
                Layout.alignment: Qt.AlignHCenter
                padding: 6
                opacity: win.pointerAwake || backend.paused || toolbarHover.hovered || win.helpVisible ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 220 } }
                background: Rectangle { color: "#e0101024"; radius: theme.radius; border.color: "#2f3250" }
                HoverHandler { id: toolbarHover }

                RowLayout {
                    spacing: 4
                    BarButton { text: backend.demo ? qsTr("Take control") : qsTr("Watch demo"); key: "T"; highlighted: backend.demo; onClicked: backend.toggleDemo() }
                    BarButton { text: backend.paused ? qsTr("Play") : qsTr("Pause"); key: "Space"; onClicked: backend.togglePause() }
                    BarButton { text: qsTr("Next scene"); key: "N"; onClicked: backend.nextScene() }
                    BarButton { text: qsTr("Sound"); key: "M"; checkable: true; checked: backend.sound; onClicked: backend.toggleSound() }
                    BarButton { text: qsTr("Restart"); key: "R"; onClicked: backend.restart() }
                    BarButton { text: qsTr("Full screen"); key: "F"; checkable: true; checked: win.visibility === Window.FullScreen; onClicked: win.toggleFullScreen() }
                    BarButton { text: "?"; key: "?"; onClicked: win.helpVisible = !win.helpVisible }
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: "#b0060714"
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

            Pane {
                anchors.centerIn: parent
                padding: 24
                background: Rectangle { color: "#14162a"; radius: theme.radius; border.color: "#2f3250" }

                ColumnLayout {
                    spacing: 16
                    Label { text: qsTr("Keyboard shortcuts"); font.pixelSize: 20; font.bold: true }
                    GridLayout {
                        columns: 2
                        columnSpacing: 24
                        rowSpacing: 8
                        Repeater {
                            model: [
                                ["A D  ← →", qsTr("Steer")],
                                ["W S  ↑ ↓", qsTr("Climb or descend")],
                                ["Shift", qsTr("Boost")],
                                [qsTr("Drag"), qsTr("Steer with the mouse")],
                                ["Space", qsTr("Pause or play")],
                                ["T", qsTr("Take control or watch the demo")],
                                ["N", qsTr("Next scene")],
                                ["M", qsTr("Turn sound on or off")],
                                ["R", qsTr("Restart from the beginning")],
                                ["F", qsTr("Full screen on or off")],
                                ["?", qsTr("Show or hide shortcuts")],
                                ["Q", qsTr("Quit")]
                            ]
                            delegate: RowLayout {
                                required property var modelData
                                Layout.columnSpan: 2
                                spacing: 24
                                Label {
                                    Layout.preferredWidth: 110
                                    text: modelData[0]
                                    color: theme.accent
                                    font.family: "monospace"
                                }
                                Label { text: modelData[1] }
                            }
                        }
                    }
                }
            }
        }
    }

    component BarButton: Button {
        property string key
        flat: true
        // Clicking must not take keyboard focus from the flight.
        focusPolicy: Qt.NoFocus
        ToolTip.visible: hovered
        ToolTip.delay: 400
        ToolTip.text: key
    }
}
