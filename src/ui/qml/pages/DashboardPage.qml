import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// M9-B2 Dashboard workspace page (the first real extracted page).
//
// Page-root pattern (B1 lesson): this root is a plain Item that the
// StackLayout owns and sizes; the page does its own margins INSIDE.
//
// Ownership (T017 §30.9 / guardrail B): presentation only — layout,
// bindings and explicit user intent. The AnalysisController is injected
// by the shell; the page never creates one, never copies business state,
// never switches the source by itself and never parses display strings.
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
        // Wording points only at REACHABLE actions: the Replay workspace
        // is still disabled in B2, so the hint names the workbench page
        // instead of "loading a file" directly.
        Label {
            objectName: "dashboardEmptyHint"
            Layout.fillWidth: true
            visible: page.analysisController.observedCount === 0
            text: qsTr("暂无通信数据。可运行演示批次，或在工作台加载回放日志。")
            color: DS.textSecondary
            wrapMode: Text.Wrap
        }

        StatisticsOverview {
            analysisController: page.analysisController
            instanceId: "dashboard"
        }
    }
}