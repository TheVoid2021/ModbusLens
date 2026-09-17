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

                        // Demo controls (M9-B2: Run Demo moved to the
                        // Statistics (M9-B2): extracted to the shared
                        // feature component; during the migration the
                        // Legacy and Dashboard instances render the SAME
                        // authoritative facts (view duplication, never
                        // state duplication).
                        StatisticsOverview {
                            analysisController: analysisController
                            instanceId: "legacy"
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: "#D0D0D0"
                        }

                        // ------------------------------------------------------------------
                        // Lower workspace (ISSUE-004 layout fix; M9-B5.2 mechanical closure):
                        // the Diagnosis workflow moved to the Diagnosis workspace page, so the
                        // horizontal SplitView is gone and Recent Transactions takes the whole
                        // remaining height directly. Root layout does NOT scroll; the
                        // transaction list scrolls inside its own ListView.
                        // ------------------------------------------------------------------
                        Rectangle {
                            id: transactionsPane
                            objectName: "legacyTransactionsPane"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 520
                            color: root.surface
                            border.color: root.border
                            border.width: 1
                            radius: 6
                            clip: true

                            // Single column-geometry owner (Phase D): header AND
                            // every row reference exactly these widths — no
                            // independent layout distribution may drift columns.
                            // Phase E: proportional widths derived from ONE owner.
                            // mins protect the 1000x700 minimum window.
                            readonly property int tableUsableWidth:
                                Math.max(transactionsPane.width - 24, 0)
                            readonly property int deviceColumnWidth:
                                Math.max(64, Math.round(tableUsableWidth * 0.15))
                            readonly property int functionColumnWidth:
                                Math.max(60, Math.round(tableUsableWidth * 0.15))
                            readonly property int statusColumnWidth:
                                Math.max(96, Math.round(tableUsableWidth * 0.20))
                            readonly property int latencyColumnWidth:
                                Math.max(84, Math.round(tableUsableWidth * 0.18))
                            readonly property int leadingColumnsWidth:
                                deviceColumnWidth + functionColumnWidth
                                + statusColumnWidth + latencyColumnWidth
                            readonly property int exceptionColumnWidth:
                                Math.max(tableUsableWidth - leadingColumnsWidth, 96)

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                Label {
                                    text: qsTr("最近通信记录")
                                    font.pixelSize: 18
                                    font.bold: true
                                    color: root.textPrimary
                                }

                                // Fixed header row — same column geometry source as
                                // the row delegate below (single owner, Phase D).
                                Row {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Label {
                                        text: qsTr("设备"); width: transactionsPane.deviceColumnWidth
                                        leftPadding: 6; font.bold: true
                                        color: root.textSecondary; font.pixelSize: 12
                                    }
                                    Label {
                                        text: qsTr("功能码"); width: transactionsPane.functionColumnWidth
                                        leftPadding: 6; font.bold: true
                                        color: root.textSecondary; font.pixelSize: 12
                                    }
                                    Label {
                                        text: qsTr("状态"); width: transactionsPane.statusColumnWidth
                                        leftPadding: 6; font.bold: true
                                        color: root.textSecondary; font.pixelSize: 12
                                    }
                                    Label {
                                        text: qsTr("耗时"); width: transactionsPane.latencyColumnWidth
                                        leftPadding: 6; font.bold: true
                                        color: root.textSecondary; font.pixelSize: 12
                                    }
                                    Label {
                                        text: qsTr("异常码")
                                        width: parent.width - transactionsPane.leadingColumnsWidth
                                        leftPadding: 6; font.bold: true
                                        color: root.textSecondary; font.pixelSize: 12
                                    }
                                }

                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.minimumHeight: 120

                                    ListView {
                                        id: transactionList
                                        anchors.fill: parent
                                        // Viewport containment (ISSUE-004): delegates must
                                        // NEVER paint outside the list.
                                        clip: true
                                        boundsBehavior: Flickable.StopAtBounds
                                        model: analysisController.transactionModel
                                        spacing: 4

                                        delegate: Rectangle {
                                            width: ListView.view.width
                                            // T014/T015: rows carrying deterministic detail
                                            // get a secondary line; a multi-request-issue
                                            // join may wrap to two lines, so the delegate
                                            // reserves 64px and the label wraps instead
                                            // of clipping.
                                            height: model.issueText !== "" ? 64 : 36
                                            color: root.surfaceAlt
                                            radius: 4

                                            Column {
                                                anchors.fill: parent

                                                Row {
                                                    width: parent.width
                                                    height: 36
                                                    spacing: 0

                                                    Label {
                                                        text: qsTr("设备 %1").arg(model.deviceAddress)
                                                        width: transactionsPane.deviceColumnWidth
                                                        leftPadding: 6
                                                        elide: Text.ElideRight
                                                    }
                                                    Label {
                                                        text: "0x" + ("0" + model.functionCode.toString(16).toUpperCase()).slice(-2)
                                                        width: transactionsPane.functionColumnWidth
                                                        leftPadding: 6
                                                    }
                                                    Label {
                                                        text: model.statusText
                                                        width: transactionsPane.statusColumnWidth
                                                        leftPadding: 6
                                                        elide: Text.ElideRight
                                                    }
                                                    Label {
                                                        text: model.elapsedMs + qsTr(" ms")
                                                        width: transactionsPane.latencyColumnWidth
                                                        leftPadding: 6
                                                        elide: Text.ElideRight
                                                    }
                                                    Label {
                                                        text: model.hasExceptionCode
                                                              ? qsTr("异常码 0x%1").arg(
                                                                    ("0" + model.exceptionCode.toString(16).toUpperCase()).slice(-2))
                                                              : qsTr("—")
                                                        color: model.hasExceptionCode ? root.errorAccent : root.textSecondary
                                                        width: parent.width - transactionsPane.leadingColumnsWidth
                                                        leftPadding: 6
                                                        elide: Text.ElideRight
                                                    }
                                                }

                                                // T014: deterministic detail, only for
                                                // ProtocolError rows (adapter-formatted
                                                // observed facts, never root-cause prose).
                                                Label {
                                                    width: parent.width
                                                    height: 28
                                                    visible: model.issueText !== ""
                                                    text: model.issueText
                                                    color: root.textSecondary
                                                    leftPadding: 6
                                                    rightPadding: 6
                                                    font.pixelSize: 11
                                                    wrapMode: Text.Wrap
                                                    // Two wrapped lines must never clip.
                                                    maximumLineCount: 2
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }
                                    }

                                    Label {
                                        anchors.centerIn: parent
                                        visible: transactionList.count === 0
                                        text: qsTr("暂无通信记录")
                                        color: root.textSecondary
                                    }
                                }
                            }
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
            }
        }
    }
}
