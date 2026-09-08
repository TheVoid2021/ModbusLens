import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ModbusLens

ApplicationWindow {
    id: root

    width: 1024
    height: 720
    visible: true
    title: qsTr("ModbusLens")

    AnalysisController {
        id: analysisController
    }

    FileDialog {
        id: replayFileDialog
        title: qsTr("Load Replay Log")
        nameFilters: [
            qsTr("ModbusLens Replay Logs (*.mlog)"),
            qsTr("All Files (*)")
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
                text: qsTr("Run Demo Batch")
                onClicked: analysisController.runDemoBatch()
            }
            Button {
                text: qsTr("Load Replay...")
                onClicked: replayFileDialog.open()
            }
            Button {
                text: qsTr("Clear")
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
            title: qsTr("Serial Controls")
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Port") }
                    ComboBox {
                        id: serialPortCombo
                        model: analysisController.serialPortNames
                        enabled: !analysisController.serialConnected
                        Layout.preferredWidth: 140
                    }
                    Button {
                        text: qsTr("Refresh Ports")
                        onClicked: analysisController.refreshSerialPorts()
                    }
                    Label { text: qsTr("Baud") }
                    ComboBox {
                        id: serialBaudCombo
                        model: [9600, 19200, 38400, 57600, 115200]
                        currentIndex: 0
                        enabled: !analysisController.serialConnected
                        Layout.preferredWidth: 110
                    }
                    Label {
                        text: qsTr("8N1")
                        color: "#606060"
                        font.pixelSize: 11
                    }
                    Button {
                        text: qsTr("Connect")
                        enabled: !analysisController.serialConnected
                                 && serialPortCombo.currentIndex >= 0
                        onClicked: analysisController.connectSerial(
                            serialPortCombo.currentText,
                            Number(serialBaudCombo.currentText))
                    }
                    Button {
                        text: qsTr("Disconnect")
                        enabled: analysisController.serialConnected
                        onClicked: analysisController.disconnectSerial()
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Slave") }
                    SpinBox {
                        id: serialSlaveSpin
                        from: 1
                        to: 247
                        value: 1
                        enabled: !analysisController.serialBusy
                    }
                    Label { text: qsTr("Start") }
                    SpinBox {
                        id: serialStartSpin
                        from: 0
                        to: 65535
                        value: 0
                        enabled: !analysisController.serialBusy
                    }
                    Label { text: qsTr("Quantity") }
                    SpinBox {
                        id: serialQuantitySpin
                        from: 1
                        to: 125
                        value: 2
                        enabled: !analysisController.serialBusy
                    }
                    Label { text: qsTr("Timeout (ms)") }
                    SpinBox {
                        id: serialTimeoutSpin
                        from: 100
                        to: 10000
                        value: 1000
                        enabled: !analysisController.serialBusy
                    }
                    Button {
                        text: analysisController.serialBusy
                              ? qsTr("Reading...") : qsTr("Read Holding Registers Once")
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
                    { label: qsTr("Observed"), value: analysisController.observedCount },
                    { label: qsTr("Completed"), value: analysisController.completedCount },
                    { label: qsTr("Pending"), value: analysisController.pendingCount }
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
                        text: qsTr("Success Rate")
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
                        text: qsTr("Avg Latency")
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
                    { label: qsTr("Success"), value: analysisController.successCount, color: "#306030" },
                    { label: qsTr("Exception"), value: analysisController.exceptionCount, color: "#806000" },
                    { label: qsTr("CRC Error"), value: analysisController.crcErrorCount, color: "#803030" },
                    { label: qsTr("Timeout"), value: analysisController.timeoutCount, color: "#604080" },
                    { label: qsTr("Protocol Error"), value: analysisController.protocolErrorCount, color: "#606060" },
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

        // Diagnosis area (T011 Part A: deterministic baseline ONLY — no AI,
        // no provider, no prompt lives here)
        GroupBox {
            title: qsTr("Diagnosis")
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: qsTr("Run Baseline Diagnosis")
                        onClicked: analysisController.runBaselineDiagnosis()
                    }
                    Button {
                        text: qsTr("Clear Diagnosis")
                        onClicked: analysisController.clearDiagnosis()
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }

                Label {
                    text: qsTr("Deterministic Baseline")
                    font.bold: true
                    visible: analysisController.hasBaselineDiagnosis
                }
                Label {
                    visible: analysisController.hasBaselineDiagnosis
                    text: analysisController.baselineDiagnosisText
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label {
                    visible: !analysisController.hasBaselineDiagnosis
                    text: qsTr("No diagnosis run yet")
                    color: "#909090"
                }
            }
        }

        // Recent transactions area
        Label {
            text: qsTr("Recent Transactions")
            font.pixelSize: 16
            font.bold: true
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: transactionList

                anchors.fill: parent
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

                        Label { text: qsTr("Device %1").arg(model.deviceAddress); Layout.preferredWidth: 90 }
                        Label {
                            text: "0x" + ("0" + model.functionCode.toString(16).toUpperCase()).slice(-2)
                            Layout.preferredWidth: 60
                        }
                        Label { text: model.statusText; Layout.preferredWidth: 120 }
                        Label { text: model.elapsedMs + qsTr(" ms"); Layout.preferredWidth: 90 }
                        Label {
                            text: model.hasExceptionCode
                                  ? qsTr("Code 0x%1").arg(
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
                text: qsTr("No transactions yet")
                color: "#909090"
            }
        }
    }
}