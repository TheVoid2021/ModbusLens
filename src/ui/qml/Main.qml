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
    readonly property color pageBackground: "#FFFFFF"
    readonly property color surface: "#FFFFFF"
    readonly property color surfaceAlt: "#F5F7FA"
    readonly property color textPrimary: "#1B1F26"
    readonly property color textSecondary: "#4A5568"
    readonly property color border: "#D8DDE4"
    readonly property color separator: "#EDF0F4"
    readonly property color baselineAccent: "#0F8A6D"
    readonly property color aiAccent: "#2F6FB7"
    readonly property color agentAccent: "#C88719"
    readonly property color errorAccent: "#C0392B"
    readonly property color busyAccent: "#2F6FB7"
    readonly property color activeTabBorder: "#98A2B3"
    readonly property color scrollThumb: "#B6BDC8"

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
                    Item {
                        Layout.preferredWidth: 140
                        Layout.preferredHeight: serialPortCombo.implicitHeight
                        ComboBox {
                            id: serialPortCombo
                            anchors.fill: parent
                            model: analysisController.serialPortNames
                            enabled: !analysisController.serialConnected
                            background: Rectangle {
                                radius: 3
                                color: root.surface
                                border.color: root.border
                                border.width: 1
                            }
                            contentItem: Text {
                                leftPadding: 8
                                verticalAlignment: Text.AlignVCenter
                                text: serialPortCombo.currentText
                                color: root.textPrimary
                                elide: Text.ElideRight
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: analysisController.serialPortNames.length === 0
                            text: qsTr("未检测到串口")
                            color: root.textSecondary
                            font.pixelSize: 12
                        }
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
                        color: root.textSecondary
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
                                height: 36
                                color: root.surfaceAlt
                                radius: 4

                                Row {
                                    anchors.fill: parent
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
