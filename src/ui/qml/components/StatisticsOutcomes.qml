import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// M9-C C2 StatisticsOutcomes: the OUTCOME COUNT row of the session
// statistics (成功 / 异常 / CRC 错误 / 超时 / 协议错误 / 预期无响应),
// extracted verbatim from StatisticsOverview's second row.
//
// Presentation only: the six counts come from the injected authoritative
// controller. This component adds NO interpretation — no anomaly predicate,
// no severity, no health score, no percentage, no chart. ExpectedNoResponse
// stays exactly what it is today: one outcome count whose semantics live in
// the core, not here (Review guardrail B).
//
// The root IS the row (no extra wrapper layer), so the extracted item keeps
// the object name and geometry the row always had; `instanceId` flows down
// from the StatisticsOverview wrapper so both runtime instances (legacy and
// dashboard) keep distinct, harness-stable identities.
RowLayout {
    id: outcomesRow

    required property var analysisController
    required property string instanceId

    objectName: "statisticsRow2_" + outcomesRow.instanceId
    Layout.fillWidth: true
    spacing: DS.spacingM

    Repeater {
        model: [
            { label: qsTr("成功"), value: outcomesRow.analysisController.successCount, color: DS.success },
            { label: qsTr("异常"), value: outcomesRow.analysisController.exceptionCount, color: DS.exception },
            { label: qsTr("CRC 错误"), value: outcomesRow.analysisController.crcErrorCount, color: DS.crcError },
            { label: qsTr("超时"), value: outcomesRow.analysisController.timeoutCount, color: DS.timeout },
            { label: qsTr("协议错误"), value: outcomesRow.analysisController.protocolErrorCount, color: DS.protocolError },
            { label: qsTr("预期无响应"), value: outcomesRow.analysisController.expectedNoResponseCount, color: DS.expectedNoResponse }
        ]

        delegate: StatCard {
            objectName: "statusCard_" + index + "_" + outcomesRow.instanceId
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
