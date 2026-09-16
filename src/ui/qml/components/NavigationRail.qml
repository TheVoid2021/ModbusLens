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
        // Entry ORDER is the workspace index contract 1:1 (see the
        // workspaceLegacyIndex / workspaceDashboardIndex constants in
        // Main.qml).
        { label: qsTr("工作台"), enabled: true },   // the legacy workspace (B1)
        { label: qsTr("总览"), enabled: true },     // Dashboard (M9-B2)
        { label: qsTr("通信"), enabled: true },     // Communication (M9-B3)
        { label: qsTr("回放"), enabled: false },    // future Replay
        { label: qsTr("诊断"), enabled: false },    // future Diagnosis
        { label: qsTr("设备"), enabled: false }     // future Device (M12)
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

            delegate: Item {
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
                focusPolicy: navItem.navEnabled ? Qt.StrongFocus : Qt.NoFocus

                // Single activation path: guards the disabled case and is the
                // same code path for click and Enter/Space (guard-tested).
                function activate() {
                    if (!navItem.enabled)
                        return;
                    rail.currentWorkspaceIndex = navItem.workspaceIndex;
                }

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

                MouseArea {
                    anchors.fill: parent
                    onClicked: navItem.activate()
                }

                Keys.onReturnPressed: navItem.activate()
                Keys.onEnterPressed: navItem.activate()
                Keys.onSpacePressed: navItem.activate()
            }
        }
    }
}