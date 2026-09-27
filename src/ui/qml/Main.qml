import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ModbusLens

ApplicationWindow {
    id: root

    // T013 Phase C: FIXED LIGHT presentation palette (user-approved
    // direction). No dark/light switch; final hexes keep HUMAN VISUAL
    // REVIEW REQUIRED status until the user re-reviews.
    // M9-A Phase 2: the same values are now CENTRALIZED in the DesignSystem
    // singleton — these aliases stay so the rest of the Legacy workbench
    // (Serial / Transactions) keeps compiling and rendering
    // pixel-identically.
    readonly property color pageBackground: DS.background
    readonly property color surface: DS.surface
    readonly property color surfaceAlt: DS.surfaceAlt
    readonly property color textPrimary: DS.textPrimary
    readonly property color textSecondary: DS.textSecondary
    readonly property color border: DS.border
    readonly property color separator: DS.separator
    readonly property color aiAccent: DS.primary
    readonly property color errorAccent: "#C0392B"
    // M9-B5.2: baselineAccent/agentAccent/busyAccent/activeTabBorder/
    // scrollThumb were removed here — their only consumers were the
    // Diagnosis panes, which now own those values locally in DiagnosisPage.

    // ---- Workspace index contract (M9-B2, presentation-only) ----
    // Single source of truth for the workspace indexes: the NavigationRail
    // entry order and the StackLayout child order below must match these
    // constants 1:1. Automated checks read THESE properties (never a
    // hardcoded 0/1 of their own).
    // M9-D D5: the Legacy workspace is RETIRED (removed from the runtime
    // tree, not hidden) and the rail is compacted — Transactions takes
    // index 0 and becomes the default workspace, Device returns to 5.
    // There is deliberately NO workspaceLegacyIndex alias left behind.
    readonly property int workspaceTransactionsIndex: 0
    readonly property int workspaceDashboardIndex: 1
    readonly property int workspaceCommunicationIndex: 2
    readonly property int workspaceReplayIndex: 3
    readonly property int workspaceDiagnosisIndex: 4
    readonly property int workspaceDeviceIndex: 5

    width: 1024
    height: 720
    minimumWidth: 1000
    minimumHeight: 700
    visible: true
    title: qsTr("ModbusLens")

    palette.window: root.pageBackground
    palette.windowText: root.textPrimary
    palette.text: root.textPrimary
    palette.base: root.surface
    palette.alternateBase: root.surfaceAlt
    palette.button: root.surfaceAlt
    palette.buttonText: root.textPrimary
    palette.highlight: root.aiAccent
    palette.highlightedText: "#FFFFFF"
    palette.mid: root.border
    background: Rectangle { color: root.pageBackground }

    // M12-B (T027 §33.2): closing the app with an unsaved profile draft must
    // offer Save / Discard / Cancel. Cancel and a failed Save genuinely block
    // the close (close.accepted = false); Discard proceeds. Other pages have
    // no unsaved state, so they never intercept closing.
    onClosing: function(close) {
        if (profileController.hasOpenProfile && profileController.dirty) {
            close.accepted = false
            profileExitDialog.open()
        }
    }

    Dialog {
        id: profileExitDialog
        objectName: "profileExitDialog"
        modal: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        width: 380
        title: qsTr("未保存修改")

        ColumnLayout {
            width: parent.width
            Label {
                text: qsTr("设备档案有未保存修改。退出前要如何处理？")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        footer: DialogButtonBox {
            AppButton {
                objectName: "profileExitSaveButton"
                text: qsTr("保存")
                onClicked: {
                    if (profileController.saveCurrent())
                        root.close()
                }
            }
            AppButton {
                objectName: "profileExitDiscardButton"
                text: qsTr("放弃修改")
                onClicked: root.close()
            }
            AppButton {
                objectName: "profileExitCancelButton"
                text: qsTr("取消")
                onClicked: profileExitDialog.close()
            }
        }
    }

    AnalysisController {
        id: analysisController
        // Test seam (nav check reads authoritative properties/calls
        // existing invokable commands through this name).
        objectName: "analysisController"
        // M12-B slice 4: the semantic layer reads ONLY the session Active
        // Profile's persisted content through this injection.
        activeProfileController: activeProfileController
    }

    ProfileController {
        id: profileController
        objectName: "profileController"
    }

    // M12-B third slice (T027 §31.3/§33.3): session Active Profile. A
    // SEPARATE owner from the editor on purpose — selecting/creating/editing
    // profiles in the editor never changes this state, and this state never
    // touches the editor. Session-only: startup = No Profile Selected.
    ActiveProfileController {
        id: activeProfileController
        objectName: "activeProfileController"
        profileController: profileController
    }

    // M12-C C1a: deterministic Manual Import (TXT / Markdown) owner. It is a
    // separate controller from both profile controllers on purpose — it owns
    // no profile reference and never selects, infers or binds a profile.
    ManualImportController {
        id: manualController
        objectName: "manualController"
    }

    // ------------------------------------------------------------------
    // Application Shell (M9-B1): AppBar + compact NavigationRail +
    // WorkspaceHost (StackLayout) -> LegacyWorkspace.
    // The legacy workspace still carries ALL current functionality; the
    // real pages are extracted in M9-B2..B5. Nothing in the shell owns
    // business state: the AppBar consumes existing display properties and
    // the rail owns navigation presentation only.
    // ------------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- AppBar ----
        Rectangle {
            objectName: "appBar"
            Layout.fillWidth: true
            implicitHeight: DS.appBarHeight
            color: root.surface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: DS.spacingL
                anchors.rightMargin: DS.spacingL
                spacing: DS.spacingM

                Label {
                    text: qsTr("ModbusLens")
                    font.pixelSize: DS.fontSection
                    font.bold: true
                    color: root.textPrimary
                }

                Item {
                    Layout.fillWidth: true
                }

                // Session/source display: consumes ONLY the authoritative
                // display properties (modeLabel / sourceLabel /
                // serialConnected). The source is never inferred from the
                // label strings and QML never keeps a second source truth
                // (T017 guardrail A).
                Label {
                    objectName: "sessionChipMode"
                    text: analysisController.modeLabel
                    font.bold: true
                    color: root.textPrimary
                }
                Label {
                    text: qsTr("·")
                    color: root.textSecondary
                }
                Label {
                    objectName: "sessionChipSource"
                    text: analysisController.sourceLabel
                    color: root.textSecondary
                }
                // M10-E4 Human Visual correction (serial state semantics):
                // serialConnected is the LOCAL transport fact — the port is
                // OPEN. Modbus RTU has no connection handshake, so an open
                // port proves nothing about a remote slave, and the old "已连接"
                // read as "the device is connected". The chip now states
                // exactly what is known.
                Label {
                    objectName: "sessionChipConnection"
                    visible: analysisController.serialConnected
                    text: qsTr("· 串口已打开")
                    color: DS.success
                }

                // Clear Results is a SESSION-level action (clearResults never
                // switches the source nor closes the transport — r08/s08
                // contracts), so it lives in the AppBar; wiring identical.
                AppButton {
                    objectName: "appBarClearResults"
                    tone: "secondary"
                    text: qsTr("清空结果")
                    onClicked: analysisController.clearResults()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: root.border
        }

        // ---- Body: compact rail + workspace host ----
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            NavigationRail {
                id: navigationRail
                objectName: "navigationRail"
                Layout.fillHeight: true
                Layout.preferredWidth: DS.compactNavWidth
            }

            Rectangle {
                Layout.fillHeight: true
                width: 1
                color: root.separator
            }

            // WorkspaceHost: StackLayout, all pages instantiated once. The
            // guard-verified invariant is "selection index stays legal", so
            // binding currentIndex to the rail is safe: a disabled entry can
            // never change it (NavigationRail.activate guards).
            StackLayout {
                objectName: "workspaceHost"
                id: workspaceHost
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: navigationRail.currentWorkspaceIndex

                // Page root pattern (since M9-B1): StackLayout children are
                // plain Items and the layout owns their geometry (no anchors
                // and no Layout.margins on the child itself — Layout.margins
                // is not honored by StackLayout, measured). The page inset
                // lives INSIDE each page, anchored to a plain non-layout
                // parent.
                //
                // M9-F F1 (B/C) focus gating lives HERE, at the workspace
                // boundary, and not in every control: a page that is not the
                // current StackLayout child is disabled. Qt Quick then (a)
                // never puts its controls into the Tab traversal, and (b)
                // clears activeFocus inside it, so a control that held focus
                // when the workspace was left stops receiving keys instead of
                // executing while hidden (measured defect: a hidden 运行基线诊断
                // button consumed Space and ran the baseline). This is
                // presentation/interaction gating only — "not current" is NOT
                // "cleared": source, statistics, diagnosis, drafts, selection
                // and every page-local contract persist exactly as before
                // (T021 §13).
                //
                // M9-D D5: the Legacy workspace (M9-B1's not-yet-split V1
                // content, statistics-only since D2) is RETIRED — the Item
                // and its StatisticsOverview instanceId "legacy" plus the
                // legacyTailSpacer surplus owner are removed from the runtime
                // tree. Its two capabilities keep their M9-B2..D2 homes:
                // statistics on the Dashboard, transactions below.

                // Transactions workspace page (M9-D D1 shell, D2 migration,
                // D3 selection/detail, D4 diagnosis cue) — StackLayout
                // child 0 since the D5 compact reindex, matching
                // workspaceTransactionsIndex above. Being index 0 also makes
                // Transactions the DEFAULT workspace: it only ever presents
                // the current authoritative state (no run/load/connect/clear
                // happens by virtue of being shown).
                TransactionsPage {
                    objectName: "transactionsPage"
                    analysisController: analysisController
                    enabled: workspaceHost.currentIndex === workspaceTransactionsIndex
                }

                // Dashboard workspace page (M9-B2) — StackLayout child 1,
                // matching workspaceDashboardIndex above.
                DashboardPage {
                    objectName: "dashboardWorkspace"
                    analysisController: analysisController
                    enabled: workspaceHost.currentIndex === workspaceDashboardIndex
                }

                // Communication workspace page (M9-B3) — StackLayout child 2,
                // matching workspaceCommunicationIndex above.
                CommunicationPage {
                    objectName: "communicationWorkspace"
                    analysisController: analysisController
                    profileController: profileController
                    activeProfileController: activeProfileController
                    enabled: workspaceHost.currentIndex === workspaceCommunicationIndex
                }

                // Replay workspace page (M9-B4.1) — StackLayout child 3.
                // SHELL ONLY: the Replay workflow (Load Replay / FileDialog /
                // error / notice) still lives in the Legacy workbench and
                // migrates here atomically WITH the 回放 navigation
                // enablement in B4.2 (T017 §38.31.3). Until then this page
                // is intentionally empty, has zero bindings/commands, and is
                // unreachable (回放 stays disabled).
                ReplayPage {
                    objectName: "replayWorkspace"
                    analysisController: analysisController
                    enabled: workspaceHost.currentIndex === workspaceReplayIndex
                }

                // Diagnosis workspace page (M9-B5.2) — StackLayout child 4,
                // matching workspaceDiagnosisIndex above. Holds the whole
                // Diagnosis workflow (诊断 TabBar, baseline run/clear, AI
                // controls, Agent draft/controls, bounded Flickables) moved
                // out of the Legacy workbench, and became reachable in the
                // same change (T017 §43.13).
                DiagnosisPage {
                    objectName: "diagnosisPage"
                    analysisController: analysisController
                    enabled: workspaceHost.currentIndex === workspaceDiagnosisIndex
                }

                // Device Profile workspace (M12-B first slice, T027 §31/§33):
                // profile identity + file lifecycle. Index 5 = the reserved
                // 设备 rail entry, which this slice enables.
                DeviceProfilePage {
                    objectName: "deviceProfileWorkspace"
                    profileController: profileController
                    manualController: manualController
                    enabled: workspaceHost.currentIndex === workspaceDeviceIndex
                }
            }
        }
    }
}
