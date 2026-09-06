import QtQuick
import QtQuick.Controls
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
            Label {
                text: qsTr("Simulator Mode")
                color: "#606060"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#D0D0D0"
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

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#D0D0D0"
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
                        Label { text: qsTr("FC %1").arg(model.functionCode); Layout.preferredWidth: 60 }
                        Label { text: model.statusText; Layout.preferredWidth: 120 }
                        Label { text: model.elapsedMs + qsTr(" ms"); Layout.preferredWidth: 90 }
                        Label {
                            text: model.hasExceptionCode
                                  ? qsTr("Exception Code: %1").arg(model.exceptionCode)
                                  : ""
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