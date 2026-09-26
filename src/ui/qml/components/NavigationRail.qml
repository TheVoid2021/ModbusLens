import QtQuick
import QtQuick.Controls
import ModbusLens

// M9-B1 NavigationRail: compact workspace rail (presentation-only).
//
// Deliberately NOT interactive for anything that does not exist yet: the
// legacy workspace ("工作台" — the not-yet-split V1 content) is the ONLY
// real entry today; every future workspace entry is disabled and can never
// change the selection (no fake "coming soon" pages — T017 §21-C).
//
// The rail owns presentation state only (selection index, focus). It never
// reads or writes business/session state; the WorkspaceHost binds
// StackLayout.currentIndex to currentWorkspaceIndex.
Rectangle {
    id: rail

    // Selection state (presentation). Guarded: disabled entries cannot
    // change it; `activate()` is the single mutation path (mouse click and
    // Enter/Space both route through it).
    property int currentWorkspaceIndex: 0

    readonly property var entries: [
        // M9-D D5: the rail is COMPACTED to its final order. The M9-B1
        // extraction order (工作台 first, 事务 last) existed only to keep
        // every migration atomic; with the Legacy workspace retired the
        // entry order is the USER order: Transactions (the evidence table)
        // leads, Device stays a disabled placeholder for M12.
        { label: qsTr("事务"), enabled: true },     // Transactions (0)
        { label: qsTr("总览"), enabled: true },     // Dashboard (1)
        { label: qsTr("通信"), enabled: true },     // Communication (2)
        { label: qsTr("回放"), enabled: true },     // Replay (3)
        { label: qsTr("诊断"), enabled: true },     // Diagnosis (4)
        { label: qsTr("设备"), enabled: true }      // Device profiles (5, M12-B)
    ]

    // Compact rail only in B1 (no expanded mode — T017 §21-D).
    implicitWidth: DS.compactNavWidth
    color: DS.surface

    readonly property int itemHeight: 40

    Column {
        id: itemsColumn
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: DS.spacingS
        spacing: DS.spacingXS

        Repeater {
            model: rail.entries

            // M9-F F1 (D/E/F/G): the delegate is a standard actionable
            // Control (`Button`) instead of a bare Item with a MouseArea.
            // A bare Item has no accessible object at all in the Qt
            // accessibility bridge, which is why the measured rail stops
            // exposed as five anonymous windows (Name "ModbusLens",
            // ControlType Window, no Invoke action) and had no place to hang
            // a keyboard-focus visual. Going through AbstractButton keeps the
            // activation semantics in one path and makes the accessibility
            // identity, the activation action and the visual focus state
            // properties of the SAME object — no hand-written accessible
            // glue, and the frozen geometry/appearance is preserved below.
            delegate: Button {
                id: navItem

                objectName: "navItem_" + index
                property int workspaceIndex: index
                property bool navEnabled: modelData.enabled
                readonly property bool selected:
                    navItem.navEnabled
                    && rail.currentWorkspaceIndex === navItem.workspaceIndex

                width: itemsColumn.width
                height: rail.itemHeight
                // Disabled entries inherit disabled to children: no mouse
                // events, no key events, no focus.
                enabled: navItem.navEnabled
                // F1 (D): Tab-reachable when enabled, and deliberately NOT
                // focus-on-click: a mouse click on a rail entry moves the
                // workspace but never steals keyboard focus (the behaviour
                // the bare-Item delegate had, because its MouseArea consumed
                // the press before the item's click-focus path could run).
                focusPolicy: navItem.navEnabled ? Qt.TabFocus : Qt.NoFocus

                // F1 (E): the accessible NAME must belong to the actionable
                // object itself, not to a child Label. AbstractButton takes
                // its accessible name from `text`, so each stop now reports
                // its own workspace label instead of the window name.
                text: modelData.label
                // F1 (E): the whole visual is owned by the three children
                // below (frozen surface/accent/label), so the Control's
                // default background and content item must not paint.
                background: null
                contentItem: null
                padding: 0

                // Single activation path: guards the disabled case and is the
                // same code path for click and Enter/Space (guard-tested).
                function activate() {
                    if (!navItem.enabled)
                        return;
                    rail.currentWorkspaceIndex = navItem.workspaceIndex;
                }

                // Space: AbstractButton's own activation (press/release)
                // reaches this handler — one activation, no double-trigger,
                // so no Keys.onSpacePressed is added here.
                onClicked: navItem.activate()

                // F1 (D): Enter/Return are NOT part of AbstractButton's
                // keyboard activation, so the two enter keys keep their
                // explicit handlers (measured before F1: both activate).
                Keys.onReturnPressed: navItem.activate()
                Keys.onEnterPressed: navItem.activate()

                // Selection is never color-only: surface + 3px accent bar +
                // bold label (M9-A "颜色只是辅助通道" principle).
                Rectangle {
                    anchors.fill: parent
                    color: navItem.selected ? DS.navigationSelectedSurface
                                            : "transparent"
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 3
                    height: parent.height - 8
                    visible: navItem.selected
                    color: DS.primary
                }

                Label {
                    anchors.centerIn: parent
                    text: modelData.label
                    font.pixelSize: DS.fontBody
                    font.bold: navItem.selected
                    color: !navItem.enabled ? DS.disabledText
                         : navItem.selected ? DS.textPrimary
                                            : DS.textSecondary
                }

                // F1 (G): keyboard-focus indication. `visualFocus` is the
                // Control property that is true for KEYBOARD focus only, so
                // this ring is a third, independent channel: the selected
                // entry keeps its fill + accent bar + bold label, hover has
                // no visual at all, and a focused-but-not-selected entry now
                // shows this ring (the measured defect was that focus was
                // invisible whenever focus and selection differed). Insets by
                // 1px so it costs no layout space and cannot be clipped at
                // 125% DPI.
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    color: "transparent"
                    border.width: 2
                    border.color: DS.primary
                    visible: navItem.visualFocus
                }
            }
        }
    }
}