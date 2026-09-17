import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// M9-C C2 StatisticsMetrics: the PRIMARY METRICS row of the session
// statistics (已观测 / 已完成 / 进行中 / 成功率 / 平均延迟), extracted
// verbatim from StatisticsOverview's first row.
//
// Presentation only: every value and its formatting come from the injected
// authoritative controller. This component keeps NO state copy, never
// recomputes the success rate, never re-derives the eligible denominator and
// never reformats the latency unit — the optional semantics stay exactly
// hasX ? value : "—".
//
// The root IS the row (no extra wrapper layer), so the extracted item keeps
// the object name and geometry the row always had; `instanceId` flows down
// from the StatisticsOverview wrapper so both runtime instances (legacy and
// dashboard) keep distinct, harness-stable identities.
RowLayout {
    id: metricsRow

    required property var analysisController
    required property string instanceId

    objectName: "statisticsRow1_" + metricsRow.instanceId
    Layout.fillWidth: true
    spacing: DS.spacingM

    Repeater {
        model: [
            { label: qsTr("已观测"), value: metricsRow.analysisController.observedCount },
            { label: qsTr("已完成"), value: metricsRow.analysisController.completedCount },
            { label: qsTr("进行中"), value: metricsRow.analysisController.pendingCount }
        ]

        delegate: StatCard {
            objectName: "statCard_" + index + "_" + metricsRow.instanceId
            label: modelData.label
            valueText: String(modelData.value)
        }
    }

    StatCard {
        objectName: "statCard_rate_" + metricsRow.instanceId
        Layout.preferredWidth: 180
        label: qsTr("成功率")
        valueText: metricsRow.analysisController.hasSuccessRate
              ? (metricsRow.analysisController.successRate * 100).toFixed(1) + "%"
              : qsTr("—")
    }

    StatCard {
        objectName: "statCard_latency_" + metricsRow.instanceId
        Layout.preferredWidth: 180
        label: qsTr("平均延迟")
        valueText: metricsRow.analysisController.hasAverageSuccessLatency
              ? metricsRow.analysisController.averageSuccessLatencyMs.toFixed(1) + qsTr(" ms")
              : qsTr("—")
    }

    Item {
        Layout.fillWidth: true
    }
}
