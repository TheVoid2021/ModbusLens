import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// M9-C C3 OutcomeDistribution: a single horizontal stacked bar showing how
// the COMPLETED transactions of the current session decompose into the six
// terminal outcomes.
//
// Classification (T017 §31 guardrail A / T018): a ModbusLens FEATURE
// presentation component — it carries outcome vocabulary and the completed
// denominator semantics — so it lives in components/, never in DS/. It has
// one consumer today (the Dashboard); it still earns its place as an
// independently testable presentation responsibility.
//
// DENOMINATOR (Review guardrail A, frozen): the bar divides by
// completedCount — NOT by observedCount and NOT by the successRate
// denominator (completed − expectedNoResponse). The six segments are
// Success / Exception / CrcError / Timeout / ProtocolError /
// ExpectedNoResponse, in that frozen order, and their counts sum to
// completedCount. Pending never enters the bar. A batch whose outcomes are
// all ExpectedNoResponse renders a 100% ExpectedNoResponse bar while
// successRate stays undefined ("—") — the two denominators must never be
// confused.
//
// Zero state: completedCount == 0 renders nothing (visible: false). All
// width math is guarded, so a hidden bar never produces NaN or Infinity.
//
// Colour is an accessory channel only: the six DS status colours match the
// outcome cards below, which keep every label and number visible — the bar
// is never the only information carrier.
ColumnLayout {
    id: distribution

    required property var analysisController
    required property string instanceId

    objectName: "outcomeDistribution_" + distribution.instanceId
    Layout.fillWidth: true
    visible: distribution.completedTotal > 0
    spacing: DS.spacingS

    // The frozen segment order: Success → Exception → CRC → Timeout →
    // ProtocolError → ExpectedNoResponse (same order as the outcome cards).
    readonly property var outcomeSegments: [
        { label: qsTr("成功"), count: distribution.analysisController.successCount, color: DS.success },
        { label: qsTr("异常"), count: distribution.analysisController.exceptionCount, color: DS.exception },
        { label: qsTr("CRC 错误"), count: distribution.analysisController.crcErrorCount, color: DS.crcError },
        { label: qsTr("超时"), count: distribution.analysisController.timeoutCount, color: DS.timeout },
        { label: qsTr("协议错误"), count: distribution.analysisController.protocolErrorCount, color: DS.protocolError },
        { label: qsTr("预期无响应"), count: distribution.analysisController.expectedNoResponseCount, color: DS.expectedNoResponse }
    ]

    // Denominator of the bar (guardrail A): completedCount only.
    readonly property int completedTotal: distribution.analysisController.completedCount

    // Logical pixel width of one completed transaction; 0 when the bar is in
    // its zero state, so no binding can divide by zero.
    readonly property real unitWidth: distribution.completedTotal > 0
                                      ? barTrack.width / distribution.completedTotal
                                      : 0

    // Cumulative completed count BEFORE segment `index` (frozen order).
    function segmentStartCount(index) {
        var start = 0;
        for (var i = 0; i < index && i < outcomeSegments.length; ++i)
            start += outcomeSegments[i].count;
        return start;
    }

    // Screen-reader summary: the bar must be meaningful without sight, and
    // the outcome cards below remain the full text legend.
    readonly property string accessibleSummary: {
        var parts = [];
        for (var i = 0; i < outcomeSegments.length; ++i)
            parts.push(outcomeSegments[i].label + " " + outcomeSegments[i].count);
        return qsTr("已完成结果分布（共 %1 笔）：%2")
                .arg(distribution.completedTotal)
                .arg(parts.join("，"));
    }

    Label {
        objectName: "outcomeDistributionCaption_" + distribution.instanceId
        Layout.fillWidth: true
        text: qsTr("已完成结果分布")
        font.pixelSize: DS.fontCaption
        color: DS.textMuted
    }

    Item {
        id: barTrack
        objectName: "outcomeDistributionBar_" + distribution.instanceId
        Layout.fillWidth: true
        Layout.preferredHeight: 12
        visible: distribution.completedTotal > 0
        clip: true

        Accessible.role: Accessible.Graphic
        Accessible.name: distribution.accessibleSummary

        Repeater {
            model: distribution.outcomeSegments

            delegate: Rectangle {
                objectName: "outcomeSegment_" + index + "_" + distribution.instanceId
                x: distribution.unitWidth * distribution.segmentStartCount(index)
                width: index === distribution.outcomeSegments.length - 1
                           // the last segment closes on the remaining width,
                           // so float rounding can never leave a gap
                           ? Math.max(0, parent.width - x)
                           : distribution.unitWidth * modelData.count
                height: parent.height
                color: modelData.color
            }
        }
    }
}
