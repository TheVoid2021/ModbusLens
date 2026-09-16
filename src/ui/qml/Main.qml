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
    // singleton — these aliases stay so the rest of Main.qml (Serial /
    // Diagnosis / Transactions, not yet migrated) keeps compiling and
    // rendering pixel-identically.
    readonly property color pageBackground: DS.background
    readonly property color surface: DS.surface
    readonly property color surfaceAlt: DS.surfaceAlt
    readonly property color textPrimary: DS.textPrimary
    readonly property color textSecondary: DS.textSecondary
    readonly property color border: DS.border
    readonly property color separator: DS.separator
    readonly property color baselineAccent: "#0F8A6D"
    readonly property color aiAccent: DS.primary
    readonly property color agentAccent: "#C88719"
    readonly property color errorAccent: "#C0392B"
    readonly property color busyAccent: DS.primary
    readonly property color activeTabBorder: "#98A2B3"
    readonly property color scrollThumb: "#B6BDC8"

    // ---- Workspace index contract (M9-B2, presentation-only) ----
    // Single source of truth for the Legacy/Dashboard indexes: the
    // NavigationRail entry order and the StackLayout child order below
    // must match these constants 1:1. Automated checks read THESE
    // properties (never a hardcoded 0/1 of their own).
    readonly property int workspaceLegacyIndex: 0
    readonly property int workspaceDashboardIndex: 1
    readonly property int workspaceCommunicationIndex: 2
    readonly property int workspaceReplayIndex: 3

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
                        // Lower workspace (ISSUE-004 workspace layout fix): a horizontal
                        // SplitView gives the whole remaining height to the two panes.
                        // Root layout does NOT scroll; Diagnosis scrolls inside its own
                        // viewport; Recent Transactions scroll inside its own ListView.
                        // ------------------------------------------------------------------
                        SplitView {
                            objectName: "diagnosisWorkspace"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            orientation: Qt.Horizontal

                            // ---- LEFT: Diagnosis pane ----
                            Rectangle {
                                SplitView.fillHeight: true
                                SplitView.minimumWidth: 300
                                SplitView.preferredWidth: 400
                                color: root.surface
                                border.color: root.border
                                border.width: 1
                                radius: 6
                                clip: true

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 8

                                    Label {
                                        text: qsTr("诊断")
                                        font.pixelSize: 18
                                        font.bold: true
                                        color: root.textPrimary
                                    }

                                    TabBar {
                                        id: diagnosisTabs
                                        Layout.fillWidth: true

                                        background: Rectangle {
                                            color: "transparent"
                                        }

                                        TabButton {
                                            id: tabBaseline
                                            text: qsTr("基线诊断")
                                            background: Rectangle {
                                                radius: 3
                                                color: tabBaseline.checked ? root.surface : root.surfaceAlt
                                                border.color: tabBaseline.checked ? root.activeTabBorder : root.border
                                                border.width: 1
                                            }
                                            contentItem: Text {
                                                text: tabBaseline.text
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                                color: tabBaseline.checked ? root.textPrimary : root.textSecondary
                                                font.pixelSize: 13
                                                font.bold: tabBaseline.checked
                                            }
                                        }
                                        TabButton {
                                            id: tabAi
                                            text: qsTr("AI 解释")
                                            background: Rectangle {
                                                radius: 3
                                                color: tabAi.checked ? root.surface : root.surfaceAlt
                                                border.color: tabAi.checked ? root.activeTabBorder : root.border
                                                border.width: 1
                                            }
                                            contentItem: Text {
                                                text: tabAi.text
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                                color: tabAi.checked ? root.textPrimary : root.textSecondary
                                                font.pixelSize: 13
                                                font.bold: tabAi.checked
                                            }
                                        }
                                        TabButton {
                                            id: tabAgent
                                            text: qsTr("Agent 问答")
                                            background: Rectangle {
                                                radius: 3
                                                color: tabAgent.checked ? root.surface : root.surfaceAlt
                                                border.color: tabAgent.checked ? root.activeTabBorder : root.border
                                                border.width: 1
                                            }
                                            contentItem: Text {
                                                text: tabAgent.text
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                                color: tabAgent.checked ? root.textPrimary : root.textSecondary
                                                font.pixelSize: 13
                                                font.bold: tabAgent.checked
                                            }
                                        }
                                    }

                                    StackLayout {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        Layout.minimumHeight: 0
                                        currentIndex: diagnosisTabs.currentIndex

                                        // ---- Tab 1: Baseline ----
                                        ColumnLayout {
                                            spacing: 6
                                            RowLayout {
                                                spacing: 6
                                                Button {
                                                    text: qsTr("运行基线诊断")
                                                    onClicked: analysisController.runBaselineDiagnosis()
                                                }
                                                Button {
                                                    text: qsTr("清除诊断")
                                                    onClicked: analysisController.clearDiagnosis()
                                                }
                                                Item { Layout.fillWidth: true }
                                            }
                                            Flickable {
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                Layout.minimumHeight: 0
                                                clip: true
                                                boundsBehavior: Flickable.StopAtBounds
                                                flickableDirection: Flickable.VerticalFlick
                                                ScrollBar.vertical: ScrollBar {
                                                    policy: ScrollBar.AlwaysOn
                                                    width: 8
                                                    minimumSize: 0.15
                                                    background: Rectangle { color: "transparent" }
                                                    contentItem: Rectangle {
                                                        implicitWidth: 8
                                                        implicitHeight: 8
                                                        radius: 4
                                                        color: root.scrollThumb
                                                    }
                                                }
                                                contentWidth: width
                                                contentHeight: baselineContent.childrenRect.height

                                                Column {
                                                    id: baselineContent
                                                    width: parent.width
                                                    spacing: 6

                                                    Label {
                                                        visible: analysisController.hasBaselineDiagnosis
                                                        text: analysisController.baselineDiagnosisText
                                                        textFormat: Text.PlainText
                                                        wrapMode: Text.Wrap
                                                        lineHeight: 1.35
                                                        width: parent.width
                                                        color: root.textPrimary
                                                    }
                                                    Label {
                                                        visible: !analysisController.hasBaselineDiagnosis
                                                        text: qsTr("尚未运行基线诊断")
                                                        color: root.textSecondary
                                                        width: parent.width
                                                        wrapMode: Text.Wrap
                                                    }
                                                }
                                            }
                                        }

                                        // ---- Tab 2: AI 解释 ----
                                        ColumnLayout {
                                            spacing: 6
                                            Label {
                                                text: analysisController.aiConfigured
                                                      ? qsTr("模型配置：ModelScope — 模型：%1").arg(analysisController.aiModelName)
                                                      : qsTr("模型配置：ModelScope — 未配置")
                                                color: root.textSecondary
                                                font.pixelSize: 12
                                            }
                                            RowLayout {
                                                spacing: 6
                                                Button {
                                                    text: qsTr("生成 AI 解释")
                                                    enabled: analysisController.aiConfigured
                                                             && analysisController.hasBaselineDiagnosis
                                                             && !analysisController.cloudAiBusy
                                                    onClicked: analysisController.askAiDiagnosis()
                                                }
                                                Button {
                                                    text: qsTr("取消")
                                                    enabled: analysisController.aiDiagnosisBusy
                                                    onClicked: analysisController.cancelAiDiagnosis()
                                                }
                                                Label {
                                                    visible: analysisController.aiDiagnosisBusy
                                                    text: qsTr("请求中...")
                                                    color: root.busyAccent
                                                    font.bold: true
                                                }
                                                Item { Layout.fillWidth: true }
                                            }
                                            Flickable {
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                Layout.minimumHeight: 0
                                                clip: true
                                                boundsBehavior: Flickable.StopAtBounds
                                                flickableDirection: Flickable.VerticalFlick
                                                ScrollBar.vertical: ScrollBar {
                                                    policy: ScrollBar.AlwaysOn
                                                    width: 8
                                                    minimumSize: 0.15
                                                    background: Rectangle { color: "transparent" }
                                                    contentItem: Rectangle {
                                                        implicitWidth: 8
                                                        implicitHeight: 8
                                                        radius: 4
                                                        color: root.scrollThumb
                                                    }
                                                }
                                                contentWidth: width
                                                contentHeight: aiContent.childrenRect.height

                                                Column {
                                                    id: aiContent
                                                    width: parent.width
                                                    spacing: 6

                                                    Label {
                                                        visible: analysisController.aiDiagnosisErrorMessage !== ""
                                                        text: analysisController.aiDiagnosisErrorMessage
                                                        color: root.errorAccent
                                                        font.bold: true
                                                        wrapMode: Text.Wrap
                                                        width: parent.width
                                                    }
                                                    Label {
                                                        visible: analysisController.hasAiDiagnosis
                                                        text: analysisController.aiDiagnosisText
                                                        textFormat: Text.PlainText
                                                        wrapMode: Text.Wrap
                                                        lineHeight: 1.35
                                                        width: parent.width
                                                        color: root.textPrimary
                                                    }
                                                    Label {
                                                        visible: !analysisController.hasAiDiagnosis
                                                                 && analysisController.aiDiagnosisErrorMessage === ""
                                                        text: qsTr("尚未生成 AI 解释")
                                                        color: root.textSecondary
                                                        width: parent.width
                                                        wrapMode: Text.Wrap
                                                    }
                                                }
                                            }
                                        }

                                        // ---- Tab 3: Agent 问答 ----
                                        ColumnLayout {
                                            spacing: 6
                                            TextArea {
                                                id: agentQuestionInput
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: 68
                                                placeholderText: qsTr("例如：本批次主要有什么异常？")
                                                wrapMode: TextArea.Wrap
                                            }
                                            RowLayout {
                                                spacing: 6
                                                Button {
                                                    text: qsTr("询问 Agent")
                                                    enabled: analysisController.agentAvailable
                                                             && !analysisController.cloudAiBusy
                                                             && agentQuestionInput.text.trim() !== ""
                                                    onClicked: analysisController.askAgent(agentQuestionInput.text)
                                                }
                                                Button {
                                                    text: qsTr("取消")
                                                    enabled: analysisController.agentBusy
                                                    onClicked: analysisController.cancelAgent()
                                                }
                                                Label {
                                                    visible: analysisController.agentBusy
                                                    text: qsTr("分析中...")
                                                    color: root.busyAccent
                                                    font.bold: true
                                                }
                                                Item { Layout.fillWidth: true }
                                            }
                                            Flickable {
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                Layout.minimumHeight: 0
                                                clip: true
                                                boundsBehavior: Flickable.StopAtBounds
                                                flickableDirection: Flickable.VerticalFlick
                                                ScrollBar.vertical: ScrollBar {
                                                    policy: ScrollBar.AlwaysOn
                                                    width: 8
                                                    minimumSize: 0.15
                                                    background: Rectangle { color: "transparent" }
                                                    contentItem: Rectangle {
                                                        implicitWidth: 8
                                                        implicitHeight: 8
                                                        radius: 4
                                                        color: root.scrollThumb
                                                    }
                                                }
                                                contentWidth: width
                                                contentHeight: agentContent.childrenRect.height

                                                Column {
                                                    id: agentContent
                                                    width: parent.width
                                                    spacing: 6

                                                    Label {
                                                        visible: analysisController.agentErrorText !== ""
                                                        text: analysisController.agentErrorText
                                                        color: root.errorAccent
                                                        font.bold: true
                                                        wrapMode: Text.Wrap
                                                        width: parent.width
                                                    }
                                                    Label {
                                                        visible: analysisController.hasAgentAnswer
                                                        text: analysisController.agentAnswerText
                                                        textFormat: Text.PlainText
                                                        wrapMode: Text.Wrap
                                                        lineHeight: 1.35
                                                        width: parent.width
                                                        color: root.textPrimary
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // ---- RIGHT: Recent Transactions pane (primary data view) ----
                            Rectangle {
                                id: transactionsPane
                                SplitView.fillWidth: true
                                SplitView.fillHeight: true
                                SplitView.minimumWidth: 520
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
            }
        }
    }
}
