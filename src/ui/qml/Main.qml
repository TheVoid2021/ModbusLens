import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ModbusLens

ApplicationWindow {
    id: root

    // T013 Phase B visual polish: theme constants (candidate values;
    // HUMAN VISUAL REVIEW REQUIRED for final color acceptance). Style
    // only — no layout / semantic change.
    readonly property color metaSecondary: "#9AA3B2"   // was #606060
    readonly property color metaPlaceholder: "#8F97A3" // was #909090
    readonly property color busyInfo: "#6FA8D8"        // was #6080a0
    readonly property color errorText: "#E0685C"       // was #B03030
    readonly property color accentBaseline: "#4FC3A1"
    readonly property color accentAi: "#7C9FE8"
    readonly property color accentAgent: "#F0B35C"

    width: 1024
    height: 720
    minimumWidth: 1000
    minimumHeight: 700
    visible: true
    title: qsTr("ModbusLens")

    AnalysisController {
        id: analysisController
    }

    FileDialog {
        id: replayFileDialog
        title: qsTr("加载回放日志")
        nameFilters: [
            qsTr("ModbusLens 回放日志 (*.mlog)"),
            qsTr("所有文件 (*)")
        ]
        onAccepted: analysisController.loadReplayFile(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // Header
        RowLayout {
            Layout.fillWidth: true

            Label {
                text: qsTr("ModbusLens")
                font.pixelSize: 24
                font.bold: true
            }
            Item {
                Layout.fillWidth: true
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignRight
                Label {
                    Layout.alignment: Qt.AlignRight
                    text: analysisController.modeLabel
                    font.bold: true
                }
                Label {
                    Layout.alignment: Qt.AlignRight
                    text: analysisController.sourceLabel
                    font.pixelSize: 11
                    color: "#606060"
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#D0D0D0"
        }

        // Demo controls
        RowLayout {
            Layout.fillWidth: true

            Button {
                text: qsTr("运行演示批次")
                onClicked: analysisController.runDemoBatch()
            }
            Button {
                text: qsTr("加载回放...")
                onClicked: replayFileDialog.open()
            }
            Button {
                text: qsTr("清空结果")
                palette.buttonText: "#303030"
                onClicked: analysisController.clearResults()
            }
            Item {
                Layout.fillWidth: true
            }
        }

        // Replay error area (lightweight, visible only on failure)
        Label {
            visible: analysisController.hasReplayError
            text: analysisController.replayErrorMessage
            color: "#B03030"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // Serial controls (T010 Part B) — one lightweight GroupBox, no new
        // page, no second dashboard. QSerialPort never appears in QML.
        GroupBox {
            title: qsTr("串口控制")
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("串口") }
                    ComboBox {
                        id: serialPortCombo
                        model: analysisController.serialPortNames
                        enabled: !analysisController.serialConnected
                        Layout.preferredWidth: 140
                    }
                    Button {
                        text: qsTr("刷新串口")
                        onClicked: analysisController.refreshSerialPorts()
                    }
                    Label { text: qsTr("波特率") }
                    ComboBox {
                        id: serialBaudCombo
                        model: [9600, 19200, 38400, 57600, 115200]
                        currentIndex: 0
                        enabled: !analysisController.serialConnected
                        Layout.preferredWidth: 110
                    }
                    Label {
                        text: qsTr("8N1")
                        color: root.metaSecondary
                        font.pixelSize: 11
                    }
                    Button {
                        text: qsTr("连接")
                        enabled: !analysisController.serialConnected
                                 && serialPortCombo.currentIndex >= 0
                        onClicked: analysisController.connectSerial(
                            serialPortCombo.currentText,
                            Number(serialBaudCombo.currentText))
                    }
                    Button {
                        text: qsTr("断开")
                        enabled: analysisController.serialConnected
                        onClicked: analysisController.disconnectSerial()
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("从站地址") }
                    SpinBox {
                        id: serialSlaveSpin
                        from: 1
                        to: 247
                        value: 1
                        enabled: !analysisController.serialBusy
                    }
                    Label { text: qsTr("起始地址") }
                    SpinBox {
                        id: serialStartSpin
                        from: 0
                        to: 65535
                        value: 0
                        enabled: !analysisController.serialBusy
                    }
                    Label { text: qsTr("寄存器数量") }
                    SpinBox {
                        id: serialQuantitySpin
                        from: 1
                        to: 125
                        value: 2
                        enabled: !analysisController.serialBusy
                    }
                    Label { text: qsTr("超时 (ms)") }
                    SpinBox {
                        id: serialTimeoutSpin
                        from: 100
                        to: 10000
                        value: 1000
                        enabled: !analysisController.serialBusy
                    }
                    Button {
                        text: analysisController.serialBusy
                              ? qsTr("读取中...") : qsTr("读取保持寄存器")
                        enabled: analysisController.serialConnected
                                 && !analysisController.serialBusy
                        onClicked: analysisController.readHoldingRegistersOnce(
                            serialSlaveSpin.value,
                            serialStartSpin.value,
                            serialQuantitySpin.value,
                            serialTimeoutSpin.value)
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
            }
        }

        // Serial transport error (separate lane from Replay error and from
        // Transaction rows — a transport failure is never a Modbus status).
        Label {
            visible: analysisController.hasSerialError
            text: analysisController.serialErrorMessage
            color: "#B03030"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // Statistics area
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Repeater {
                model: [
                    { label: qsTr("已观测"), value: analysisController.observedCount },
                    { label: qsTr("已完成"), value: analysisController.completedCount },
                    { label: qsTr("进行中"), value: analysisController.pendingCount }
                ]

                delegate: Rectangle {
                    Layout.preferredWidth: 140
                    Layout.preferredHeight: 72
                    color: "#F4F4F4"
                    radius: 6

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 2

                        Label {
                            text: modelData.label
                            color: "#606060"
                        }
                        Label {
                            text: modelData.value
                            font.pixelSize: 20
                            font.bold: true
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 180
                Layout.preferredHeight: 72
                color: "#F4F4F4"
                radius: 6

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 2

                    Label {
                        text: qsTr("成功率")
                        color: "#606060"
                    }
                    Label {
                        text: analysisController.hasSuccessRate
                              ? (analysisController.successRate * 100).toFixed(1) + "%"
                              : qsTr("—")
                        font.pixelSize: 20
                        font.bold: true
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 180
                Layout.preferredHeight: 72
                color: "#F4F4F4"
                radius: 6

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 2

                    Label {
                        text: qsTr("平均延迟")
                        color: "#606060"
                    }
                    Label {
                        text: analysisController.hasAverageSuccessLatency
                              ? analysisController.averageSuccessLatencyMs.toFixed(1) + qsTr(" ms")
                              : qsTr("—")
                        font.pixelSize: 20
                        font.bold: true
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }

        // Status count cards
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Repeater {
                model: [
                    { label: qsTr("成功"), value: analysisController.successCount, color: "#306030" },
                    { label: qsTr("异常"), value: analysisController.exceptionCount, color: "#806000" },
                    { label: qsTr("CRC 错误"), value: analysisController.crcErrorCount, color: "#803030" },
                    { label: qsTr("超时"), value: analysisController.timeoutCount, color: "#604080" },
                    { label: qsTr("协议错误"), value: analysisController.protocolErrorCount, color: "#606060" },
                ]

                delegate: Rectangle {
                    Layout.preferredWidth: 110
                    Layout.preferredHeight: 64
                    color: "#F0F0F0"
                    radius: 6

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 2

                        Label {
                            text: modelData.label
                            font.pixelSize: 11
                            color: modelData.color
                        }
                        Label {
                            text: modelData.value
                            font.pixelSize: 18
                            font.bold: true
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }
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
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            // ---- LEFT: Diagnosis pane ----
            GroupBox {
                id: diagnosisGroup
                title: qsTr("诊断")
                SplitView.fillHeight: true
                SplitView.minimumWidth: 300
                SplitView.preferredWidth: 400
                // Containment safety net (ISSUE-004): no Diagnosis child
                // may EVER paint outside this pane.
                clip: true

                ColumnLayout {
                    id: diagnosisColumn
                    // Proven fix (ISSUE-004 r4): fill the GroupBox content
                    // area so Layout.fillHeight inside this column means
                    // the real remaining height.
                    anchors.fill: parent
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 4

                    // Fixed control rows.
                    RowLayout {
                        Layout.fillWidth: true
                        Button {
                            text: qsTr("运行基线诊断")
                            onClicked: analysisController.runBaselineDiagnosis()
                        }
                        Button {
                            text: qsTr("清除诊断")
                            onClicked: analysisController.clearDiagnosis()
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    RowLayout {
                        spacing: 5
                        Rectangle {
                            width: 3
                            height: 14
                            color: root.accentAi
                        }
                        Label {
                            text: qsTr("AI 解释")
                            font.pixelSize: 13
                            font.bold: true
                        }
                    }
                    Label {
                        text: analysisController.aiConfigured
                              ? qsTr("模型配置：ModelScope — 模型：%1").arg(analysisController.aiModelName)
                              : qsTr("模型配置：ModelScope — 未配置")
                        color: "#606060"
                        font.pixelSize: 11
                    }
                    RowLayout {
                        Layout.fillWidth: true
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
                            color: root.busyInfo
                            font.bold: true
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    // ---- T012 Phase 2: Agent 问答（只读诊断）----
                    RowLayout {
                        spacing: 5
                        Rectangle {
                            width: 3
                            height: 14
                            color: root.accentAgent
                        }
                        Label {
                            text: qsTr("Agent 问答（只读诊断）")
                            font.pixelSize: 13
                            font.bold: true
                        }
                    }
                    TextArea {
                        id: agentQuestionInput
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                        placeholderText: qsTr("例如：本批次主要有什么异常？")
                        wrapMode: TextArea.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
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
                            color: root.busyInfo
                            font.bold: true
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    // Growing content: vertical viewport with explicit
                    // content extent (verified scrolling mechanics).
                    Flickable {
                        id: diagnosisFlick
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 0
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: Flickable.VerticalFlick
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AlwaysOn
                        }
                        contentWidth: width
                        contentHeight: diagnosisContent.childrenRect.height

                        Column {
                            id: diagnosisContent
                            width: diagnosisFlick.width
                            spacing: 6

                            RowLayout {
                                spacing: 5
                                width: parent.width
                                visible: analysisController.hasBaselineDiagnosis
                                Rectangle {
                                    width: 3
                                    height: 14
                                    color: root.accentBaseline
                                }
                                Label {
                                    text: qsTr("确定性基线诊断")
                                    font.pixelSize: 13
                                    font.bold: true
                                }
                            }
                            Label {
                                visible: analysisController.hasBaselineDiagnosis
                                text: analysisController.baselineDiagnosisText
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                lineHeight: 1.35
                                width: parent.width
                            }
                            Label {
                                visible: analysisController.aiDiagnosisErrorMessage !== ""
                                text: analysisController.aiDiagnosisErrorMessage
                                color: root.errorText
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
                            }
                            Label {
                                visible: analysisController.agentErrorText !== ""
                                text: analysisController.agentErrorText
                                color: root.errorText
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
                            }
                            Label {
                                visible: !analysisController.hasBaselineDiagnosis
                                         && !analysisController.hasAiDiagnosis
                                text: qsTr("尚未运行诊断")
                                color: root.metaPlaceholder
                                width: parent.width
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }

            // ---- RIGHT: Recent Transactions pane (primary data view) ----
            ColumnLayout {
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumWidth: 520
                spacing: 6

                Label {
                    text: qsTr("最近通信记录")
                    font.pixelSize: 16
                    font.bold: true
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
                            height: 36
                            color: "#FAFAFA"
                            radius: 4

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 6

                                Label { text: qsTr("设备 %1").arg(model.deviceAddress); Layout.preferredWidth: 90 }
                                Label {
                                    text: "0x" + ("0" + model.functionCode.toString(16).toUpperCase()).slice(-2)
                                    Layout.preferredWidth: 60
                                }
                                Label { text: model.statusText; Layout.preferredWidth: 120 }
                                Label { text: model.elapsedMs + qsTr(" ms"); Layout.preferredWidth: 90 }
                                Label {
                                    text: model.hasExceptionCode
                                          ? qsTr("异常码 0x%1").arg(
                                                ("0" + model.exceptionCode.toString(16).toUpperCase()).slice(-2))
                                          : qsTr("—")
                                    color: "#803030"
                                }
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: transactionList.count === 0
                        text: qsTr("暂无通信记录")
                        color: "#909090"
                    }
                }
            }
        }
    }
}
