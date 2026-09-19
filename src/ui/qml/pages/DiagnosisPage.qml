import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// M9-B5 Diagnosis workspace page.
//
// Page-root pattern (B1–B4 precedent): a plain Item that the StackLayout
// owns and sizes; the page does its own margins INSIDE. A trailing
// flexible spacer is not used here — the TabBar/StackLayout chain already
// fills the remaining height via Layout.fillHeight, keeping the bounded
// Flickable contract (T017 §43.16).
//
// Ownership (T017 §43.7/§43.4): presentation only. The AnalysisController
// is injected by the shell; this page never creates one, never copies
// baseline/AI/Agent facts, and never touches the source/revision
// authority. Page-local presentation state (tab selection, question
// draft, scroll positions) lives on the page.
//
// B5.2 moved the Diagnosis workflow here VERBATIM from the Legacy
// workbench in the same stage that enabled the 诊断 navigation entry
// (T017 §38.31.1 lesson — no committed state may leave a user capability
// unreachable). The only textual substitutions are root alias → DS
// mappings (identical values by construction) plus three frozen V1
// literals (#98A2B3 / #C0392B / #B6BDC8) that have no DS token yet.
Item {
    id: page

    required property var analysisController

    // Frozen V1 visual values (not yet DS tokens — M9-C candidate).
    readonly property color frozenActiveTabBorder: "#98A2B3"
    readonly property color frozenScrollThumb: "#B6BDC8"
    readonly property color frozenErrorAccent: "#C0392B"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: DS.spacingL
        spacing: DS.spacingM

        SectionHeader {
            objectName: "diagnosisPageHeader"
            Layout.fillWidth: true
            title: qsTr("诊断")
            subtitle: qsTr("基线诊断 · AI 解释 · Agent 问答")
        }

        TabBar {
            id: diagnosisTabs
            objectName: "diagnosisTabs"
            Layout.fillWidth: true

            background: Rectangle {
                color: "transparent"
            }

            TabButton {
                id: tabBaseline
                text: qsTr("基线诊断")
                background: Rectangle {
                    radius: 3
                    color: tabBaseline.checked ? DS.surface : DS.surfaceAlt
                    border.color: tabBaseline.checked ? page.frozenActiveTabBorder : DS.border
                    border.width: 1
                }
                contentItem: Text {
                    text: tabBaseline.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: tabBaseline.checked ? DS.textPrimary : DS.textSecondary
                    font.pixelSize: 13
                    font.bold: tabBaseline.checked
                }
            }
            TabButton {
                id: tabAi
                text: qsTr("AI 解释")
                background: Rectangle {
                    radius: 3
                    color: tabAi.checked ? DS.surface : DS.surfaceAlt
                    border.color: tabAi.checked ? page.frozenActiveTabBorder : DS.border
                    border.width: 1
                }
                contentItem: Text {
                    text: tabAi.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: tabAi.checked ? DS.textPrimary : DS.textSecondary
                    font.pixelSize: 13
                    font.bold: tabAi.checked
                }
            }
            TabButton {
                id: tabAgent
                text: qsTr("Agent 问答")
                background: Rectangle {
                    radius: 3
                    color: tabAgent.checked ? DS.surface : DS.surfaceAlt
                    border.color: tabAgent.checked ? page.frozenActiveTabBorder : DS.border
                    border.width: 1
                }
                contentItem: Text {
                    text: tabAgent.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: tabAgent.checked ? DS.textPrimary : DS.textSecondary
                    font.pixelSize: 13
                    font.bold: tabAgent.checked
                }
            }
        }

        StackLayout {
            objectName: "diagnosisTabContent"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            currentIndex: diagnosisTabs.currentIndex

            // ---- Tab 1: Baseline ----
            ColumnLayout {
                objectName: "diagnosisBaselineTab"
                spacing: 6
                RowLayout {
                    spacing: 6
                    Button {
                        objectName: "diagnosisRunBaselineButton"
                        text: qsTr("运行基线诊断")
                        onClicked: page.analysisController.runBaselineDiagnosis()
                    }
                    Button {
                        objectName: "diagnosisClearDiagnosisButton"
                        text: qsTr("清除诊断")
                        onClicked: page.analysisController.clearDiagnosis()
                    }
                    Item { Layout.fillWidth: true }
                }
                Flickable {
                    objectName: "diagnosisBaselineViewport"
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
                            color: page.frozenScrollThumb
                        }
                    }
                    contentWidth: width
                    contentHeight: baselineContent.childrenRect.height

                    Column {
                        id: baselineContent
                        width: parent.width
                        spacing: 6

                        Label {
                            visible: page.analysisController.hasBaselineDiagnosis
                            text: page.analysisController.baselineDiagnosisText
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            lineHeight: 1.35
                            width: parent.width
                            color: DS.textPrimary
                        }
                        Label {
                            visible: !page.analysisController.hasBaselineDiagnosis
                            text: qsTr("尚未运行基线诊断")
                            color: DS.textSecondary
                            width: parent.width
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            // ---- Tab 2: AI 解释 ----
            ColumnLayout {
                objectName: "diagnosisAiTab"
                spacing: 6
                Label {
                    text: page.analysisController.aiConfigured
                          ? qsTr("模型配置：ModelScope — 模型：%1").arg(page.analysisController.aiModelName)
                          : qsTr("模型配置：ModelScope — 未配置")
                    color: DS.textSecondary
                    font.pixelSize: 12
                }
                RowLayout {
                    spacing: 6
                    Button {
                        objectName: "diagnosisAiAskButton"
                        text: qsTr("生成 AI 解释")
                        enabled: page.analysisController.aiConfigured
                                 && page.analysisController.hasBaselineDiagnosis
                                 && !page.analysisController.cloudAiBusy
                        onClicked: page.analysisController.askAiDiagnosis()
                    }
                    Button {
                        objectName: "diagnosisAiCancelButton"
                        text: qsTr("取消")
                        enabled: page.analysisController.aiDiagnosisBusy
                        onClicked: page.analysisController.cancelAiDiagnosis()
                    }
                    Label {
                        visible: page.analysisController.aiDiagnosisBusy
                        text: qsTr("请求中...")
                        color: DS.primary
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                }
                Flickable {
                    objectName: "diagnosisAiViewport"
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
                            color: page.frozenScrollThumb
                        }
                    }
                    contentWidth: width
                    contentHeight: aiContent.childrenRect.height

                    Column {
                        id: aiContent
                        width: parent.width
                        spacing: 6

                        Label {
                            visible: page.analysisController.aiDiagnosisErrorMessage !== ""
                            text: page.analysisController.aiDiagnosisErrorMessage
                            color: page.frozenErrorAccent
                            font.bold: true
                            wrapMode: Text.Wrap
                            width: parent.width
                        }
                        Label {
                            visible: page.analysisController.hasAiDiagnosis
                            text: page.analysisController.aiDiagnosisText
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            lineHeight: 1.35
                            width: parent.width
                            color: DS.textPrimary
                        }
                        Label {
                            visible: !page.analysisController.hasAiDiagnosis
                                     && page.analysisController.aiDiagnosisErrorMessage === ""
                            text: qsTr("尚未生成 AI 解释")
                            color: DS.textSecondary
                            width: parent.width
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            // ---- Tab 3: Agent 问答 ----
            ColumnLayout {
                objectName: "diagnosisAgentTab"
                spacing: 6
                TextArea {
                    id: agentQuestionInput
                    objectName: "diagnosisAgentQuestion"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 68
                    placeholderText: qsTr("例如：本批次主要有什么异常？")
                    wrapMode: TextArea.Wrap
                    // M9-F F1 (H): a multi-line editor consumes Tab to insert
                    // a literal tab stop, which trapped keyboard traversal
                    // (measured before F1: 14 forward and 6 reverse Tab
                    // presses all stayed here and wrote \t into the draft).
                    // Qt Quick has no `tabChangesFocus` — that property exists
                    // in QtWidgets only (audited against this Qt 6.11.1) — so
                    // the two traversal keys are re-routed through Qt's own
                    // focus-chain API instead of being swallowed. Only Tab and
                    // Backtab change meaning: every editing key (arrows, Home,
                    // End, selection, typing) keeps its text semantics.
                    Keys.onTabPressed: (event) => {
                        var next = agentQuestionInput.nextItemInFocusChain(true)
                        if (next) {
                            next.forceActiveFocus(Qt.TabFocusReason)
                            event.accepted = true
                        }
                    }
                    Keys.onBacktabPressed: (event) => {
                        var prev = agentQuestionInput.nextItemInFocusChain(false)
                        if (prev) {
                            prev.forceActiveFocus(Qt.BacktabFocusReason)
                            event.accepted = true
                        }
                    }
                }
                RowLayout {
                    spacing: 6
                    Button {
                        objectName: "diagnosisAgentAskButton"
                        text: qsTr("询问 Agent")
                        enabled: page.analysisController.agentAvailable
                                 && !page.analysisController.cloudAiBusy
                                 && agentQuestionInput.text.trim() !== ""
                        onClicked: page.analysisController.askAgent(agentQuestionInput.text)
                    }
                    Button {
                        objectName: "diagnosisAgentCancelButton"
                        text: qsTr("取消")
                        enabled: page.analysisController.agentBusy
                        onClicked: page.analysisController.cancelAgent()
                    }
                    Label {
                        visible: page.analysisController.agentBusy
                        text: qsTr("分析中...")
                        color: DS.primary
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                }
                Flickable {
                    objectName: "diagnosisAgentViewport"
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
                            color: page.frozenScrollThumb
                        }
                    }
                    contentWidth: width
                    contentHeight: agentContent.childrenRect.height

                    Column {
                        id: agentContent
                        width: parent.width
                        spacing: 6

                        Label {
                            visible: page.analysisController.agentErrorText !== ""
                            text: page.analysisController.agentErrorText
                            color: page.frozenErrorAccent
                            font.bold: true
                            wrapMode: Text.Wrap
                            width: parent.width
                        }
                        Label {
                            visible: page.analysisController.hasAgentAnswer
                            text: page.analysisController.agentAnswerText
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            lineHeight: 1.35
                            width: parent.width
                            color: DS.textPrimary
                        }
                    }
                }
            }
        }
    }
}