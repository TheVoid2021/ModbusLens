import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// M9-C C3 Dashboard workspace page.
//
// Page-root pattern (B1 lesson): this root is a plain Item that the
// StackLayout owns and sizes; the page does its own margins INSIDE.
//
// Ownership (T017 §30.9 / guardrail B): presentation only — layout,
// bindings and explicit user intent. The AnalysisController is injected
// by the shell; the page never creates one, never copies business state,
// never switches the source by itself and never parses display strings.
//
// M9-C C1: the page's vertical layout is a tight stack whose section gaps
// are DS.spacingM, with ONE explicit tail surplus owner at the end — the
// leftover height belongs to that spacer (reserved capacity), never to the
// gaps between sections.
//
// M9-C C3: the statistics section is now a DASHBOARD-SPECIFIC composition —
// SectionHeader + PanelCard[StatisticsMetrics, OutcomeDistribution,
// StatisticsOutcomes] — instead of the shared StatisticsOverview wrapper
// (which Legacy keeps using unchanged). The pieces keep the C2 injection
// decision (the controller reference flows down; instanceId stays
// "dashboard" so every row/segment identity remains unique and
// harness-stable). OutcomeDistribution divides by completedCount only
// (Review guardrail A) and disappears entirely in the zero state.
Item {
    id: page

    required property var analysisController

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: DS.spacingL
        spacing: DS.spacingM

        SectionHeader {
            objectName: "dashboardHeader"
            Layout.fillWidth: true
            title: qsTr("总览")
            subtitle: qsTr("当前会话概览")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: DS.spacingM

            // The ONLY Run Demo entry point (the Legacy button was removed
            // in the same step — one contextual command never has two
            // entries). Explicit user action only: nothing triggers this
            // on load, on visibility changes or on index changes.
            AppButton {
                objectName: "dashboardRunDemo"
                tone: "primary"
                text: qsTr("运行演示批次")
                onClicked: page.analysisController.runDemoBatch()
            }

            Item {
                Layout.fillWidth: true
            }
        }

        // Lightweight no-data hint (a single line; no EmptyState system).
        // M9-C C3 wording refresh: Communication and Replay are REACHABLE
        // workspaces now, so the hint names them instead of B2's "workbench"
        // (which predates the extraction). Pure navigation guidance —
        // switching workspaces never connects, loads or runs anything.
        Label {
            objectName: "dashboardEmptyHint"
            Layout.fillWidth: true
            visible: page.analysisController.observedCount === 0
            text: qsTr("暂无通信数据。可运行演示批次，或前往“通信”、“回放”工作区获取数据。")
            color: DS.textSecondary
            wrapMode: Text.Wrap
        }

        // ---- Statistics section (M9-C C3 dashboard composition) ----
        SectionHeader {
            objectName: "statisticsHeader_dashboard"
            Layout.fillWidth: true
            title: qsTr("运行统计")
        }

        PanelCard {
            objectName: "statisticsPanel_dashboard"
            Layout.fillWidth: true

            StatisticsMetrics {
                analysisController: page.analysisController
                instanceId: "dashboard"
            }

            OutcomeDistribution {
                analysisController: page.analysisController
                instanceId: "dashboard"
            }

            StatisticsOutcomes {
                analysisController: page.analysisController
                instanceId: "dashboard"
            }
        }

        // ------------------------------------------------------------------
        // Surplus-space owner (M9-C C1). Before this element existed the
        // page had NO fillHeight child, so ColumnLayout distributed the
        // leftover height into the section rows themselves — measured at
        // 1024x720: the header sat 11px inside its row and the gaps between
        // sections grew to 46 / 44 / 155 px instead of the 12px token. With
        // an explicit tail owner the surplus lands HERE, the content region
        // stays a tight stack whose gaps are exactly DS.spacingM, and the
        // remaining height becomes deliberate tail capacity for the later
        // M10/M11 work instead of an accident. No content is added here:
        // empty space with a stated owner is not a bug (T018 §C1). C3 makes
        // the statistics section taller (the distribution moved in), so this
        // spacer legitimately shrinks — it stays the ONLY stretch owner.
        // ------------------------------------------------------------------
        Item {
            objectName: "dashboardTailSpacer"
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
