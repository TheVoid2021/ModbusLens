import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// M9-B2 StatisticsOverview: feature/presentation section for the session
// statistics (section header + panel + 11 cards).
//
// Classification (T017 §31 guardrail A): this is a ModbusLens FEATURE
// component, not a Design System primitive — it carries business
// vocabulary (transaction status names, success-rate semantics) and
// therefore lives in components/, never in DS/.
//
// Presentation only: every value, the rate/latency formatting and the
// "—" placeholders come from the injected authoritative controller. The
// overview keeps NO state copy, never recomputes successRate, never
// redefines the eligible denominator and never re-interprets
// ExpectedNoResponse.
//
// Two instances coexist during the B2..B4 migration (Legacy + Dashboard).
// `instanceId` is the presentation identity that keeps the test
// objectNames distinct — tests must address a specific instance instead
// of "whichever same-named object is found first".
ColumnLayout {
    id: overview

    required property var analysisController
    required property string instanceId

    Layout.fillWidth: true
    spacing: DS.spacingM

    SectionHeader {
        objectName: "statisticsHeader_" + overview.instanceId
        Layout.fillWidth: true
        title: qsTr("运行统计")
    }

    PanelCard {
        objectName: "statisticsPanel_" + overview.instanceId
        Layout.fillWidth: true

        RowLayout {
            objectName: "statisticsRow1_" + overview.instanceId
            Layout.fillWidth: true
            spacing: DS.spacingM

            Repeater {
                model: [
                    { label: qsTr("已观测"), value: overview.analysisController.observedCount },
                    { label: qsTr("已完成"), value: overview.analysisController.completedCount },
                    { label: qsTr("进行中"), value: overview.analysisController.pendingCount }
                ]

                delegate: StatCard {
                    objectName: "statCard_" + index + "_" + overview.instanceId
                    label: modelData.label
                    valueText: String(modelData.value)
                }
            }

            StatCard {
                objectName: "statCard_rate_" + overview.instanceId
                Layout.preferredWidth: 180
                label: qsTr("成功率")
                valueText: overview.analysisController.hasSuccessRate
                      ? (overview.analysisController.successRate * 100).toFixed(1) + "%"
                      : qsTr("—")
            }

            StatCard {
                objectName: "statCard_latency_" + overview.instanceId
                Layout.preferredWidth: 180
                label: qsTr("平均延迟")
                valueText: overview.analysisController.hasAverageSuccessLatency
                      ? overview.analysisController.averageSuccessLatencyMs.toFixed(1) + qsTr(" ms")
                      : qsTr("—")
            }

            Item {
                Layout.fillWidth: true
            }
        }

        // Status count cards
        RowLayout {
            objectName: "statisticsRow2_" + overview.instanceId
            Layout.fillWidth: true
            spacing: DS.spacingM

            Repeater {
                model: [
                    { label: qsTr("成功"), value: overview.analysisController.successCount, color: DS.success },
                    { label: qsTr("异常"), value: overview.analysisController.exceptionCount, color: DS.exception },
                    { label: qsTr("CRC 错误"), value: overview.analysisController.crcErrorCount, color: DS.crcError },
                    { label: qsTr("超时"), value: overview.analysisController.timeoutCount, color: DS.timeout },
                    { label: qsTr("协议错误"), value: overview.analysisController.protocolErrorCount, color: DS.protocolError },
                    { label: qsTr("预期无响应"), value: overview.analysisController.expectedNoResponseCount, color: DS.expectedNoResponse }
                ]

                delegate: StatCard {
                    objectName: "statusCard_" + index + "_" + overview.instanceId
                    Layout.preferredWidth: 110
                    Layout.preferredHeight: 64
                    label: modelData.label
                    valueText: String(modelData.value)
                    tone: modelData.color
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }
}