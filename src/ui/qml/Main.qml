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
    // Single source of truth for the Legacy/Dashboard indexes: the
    // NavigationRail entry order and the StackLayout child order below
    // must match these constants 1:1. Automated checks read THESE
    // properties (never a hardcoded 0/1 of their own).
    readonly property int workspaceLegacyIndex: 0
    readonly property int workspaceDashboardIndex: 1
    readonly property int workspaceCommunicationIndex: 2
    readonly property int workspaceReplayIndex: 3
    readonly property int workspaceDiagnosisIndex: 4
    // M9-D D1: the Transactions workspace shell takes index 5 (disabled
    // until D2) and the disabled Device entry moves to index 6. The active
    // workspace indices 0..4 are unchanged.
    readonly property int workspaceTransactionsIndex: 5
    readonly property int workspaceDeviceIndex: 6

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

    AnalysisController {
        id: analysisController
        // Test seam (nav check reads authoritative properties/calls
        // existing invokable commands through this name).
        objectName: "analysisController"
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
                Label {
                    objectName: "sessionChipConnection"
                    visible: analysisController.serialConnected
                    text: qsTr("· 已连接")
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
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: navigationRail.currentWorkspaceIndex

                // ---- LegacyWorkspace (M9-B1): the entire not-yet-split V1
                // content — minus the old header (now the AppBar) and the
                // Clear button (now in the AppBar). Extracted page by page
                // in M9-B2..B5; until then it is the ONLY real workspace.
                // Page root pattern: StackLayout children are plain Items
                // and the layout owns their geometry (no anchors and no
                // Layout.margins on the child itself — Layout.margins is not
                // honored by StackLayout, measured). The page inset lives
                // INSIDE this Item, anchored to a plain non-layout parent.
                Item {
                    objectName: "legacyWorkspace"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: DS.spacingL
                        spacing: DS.spacingM

                        // M9-D D2: the Legacy workbench is STATISTICS
                        // ONLY now — the transactions presentation moved
                        // to the Transactions workspace in the same
                        // change that enabled its navigation entry (no
                        // window where the capability is unreachable).
                        // The legacy workspace itself is retired in D5.
                        StatisticsOverview {
                            analysisController: analysisController
                            instanceId: "legacy"
                        }

                        // M9-D D2 mechanical closure: with the transactions
                        // pane gone this column has NO fillHeight child left,
                        // so Qt would spread the surplus into the row and
                        // push the statistics block down (measured: y=226).
                        // The same surplus owner the Dashboard got in M9-C C1
                        // keeps the statistics block at the page top.
                        Item {
                            objectName: "legacyTailSpacer"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }

                    }
                }

                // Dashboard workspace page (M9-B2) — StackLayout child 1,
                // matching workspaceDashboardIndex above.
                DashboardPage {
                    objectName: "dashboardWorkspace"
                    analysisController: analysisController
                }

                // Communication workspace page (M9-B3) — StackLayout child 2,
                // matching workspaceCommunicationIndex above.
                CommunicationPage {
                    objectName: "communicationWorkspace"
                    analysisController: analysisController
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
                }

                // Transactions workspace page (M9-D D1) — StackLayout child 5.
                // SHELL ONLY: the transaction table (header, delegates, issue
                // text, empty state) still lives in the Legacy workbench and
                // migrates here ATOMICALLY WITH the 事务 navigation
                // enablement in D2 (T019 §33). Until then the rail entry is
                // disabled and this page is unreachable with zero bindings.
                TransactionsPage {
                    objectName: "transactionsPage"
                    analysisController: analysisController
                }
            }
        }
    }
}
