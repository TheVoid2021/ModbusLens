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
// M9-C C2: the overview is now a COMPOSITION. Its two real visual
// responsibilities live in their own presentation pieces —
// StatisticsMetrics (primary metrics row) and StatisticsOutcomes (outcome
// count row) — and this wrapper keeps exactly its previous structure:
// SectionHeader + PanelCard[metrics row, outcomes row]. The wrapper is the
// only thing the two consumers (Legacy and Dashboard) instantiate, so the
// public contract is unchanged: same required properties, same instanceId
// scheme, same object names on the same visual items, same implicit size
// derived from the content (ISSUE-012 contract).
//
// Presentation only: every value, the rate/latency formatting and the
// "—" placeholders come from the injected authoritative controller. The
// overview keeps NO state copy, never recomputes successRate, never
// redefines the eligible denominator and never re-interprets
// ExpectedNoResponse.
//
// Two instances coexist (Legacy + Dashboard). `instanceId` is the
// presentation identity that keeps the test objectNames distinct — it is
// also forwarded to both pieces so their row identities stay unique per
// instance.
ColumnLayout {
    id: overview

    required property var analysisController
    required property string instanceId

    objectName: "statisticsOverview_" + overview.instanceId
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

        StatisticsMetrics {
            analysisController: overview.analysisController
            instanceId: overview.instanceId
        }

        StatisticsOutcomes {
            analysisController: overview.analysisController
            instanceId: overview.instanceId
        }
    }
}
