import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ModbusLens

// M9-B4 Replay workspace page.
//
// Page-root pattern (B1–B3 precedent): a plain Item that the StackLayout
// owns and sizes; the page does its own margins INSIDE. A trailing
// flexible spacer consumes the vertical slack so the content stacks
// tightly at the top (T017 §37.7 — without an expanding child a
// ColumnLayout spreads the slack between its children).
//
// Ownership (T017 §38.16 / §38.30): presentation only. The
// AnalysisController is injected by the shell; this page never creates
// one, never copies business state and never switches the source by
// itself.
//
// DRAFT vs AUTHORITATIVE (T017 §38.6): the FileDialog's selectedFile is a
// TRANSIENT workflow candidate consumed at onAccepted — it is not the
// session source and is not persisted anywhere. The authoritative Replay
// session facts (mode/source, batch, error, notice) always come from the
// Controller.
//
// B4.2 moved the workflow here VERBATIM from the Legacy workbench in the
// same stage that enabled the 回放 navigation entry (T017 §38.31.3 — no
// committed state may leave the workflow unreachable). The only textual
// substitutions are the theme accessor equivalents for the two literal
// colors: #B03030 == DS.error and #806000 == DS.notice (identical values
// by construction).
Item {
    id: page

    required property var analysisController

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: DS.spacingL
        spacing: DS.spacingM

        SectionHeader {
            objectName: "replayHeader"
            Layout.fillWidth: true
            title: qsTr("回放")
            subtitle: qsTr("加载回放日志")
        }

        // ---- Load action (moved verbatim from the Legacy workbench) ----
        PanelCard {
            objectName: "replayActionsSection"
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true

                AppButton {
                    objectName: "replayLoadButton"
                    tone: "secondary"
                    text: qsTr("加载回放...")
                    onClicked: replayFileDialog.open()
                }
                Item {
                    Layout.fillWidth: true
                }
            }
        }

        // Replay error area (lightweight, visible only on failure) — moved
        // verbatim; the Controller owns the error lifecycle.
        Label {
            objectName: "replayErrorLabel"
            visible: page.analysisController.hasReplayError
            text: page.analysisController.replayErrorMessage
            color: DS.error
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // T015 non-fatal disclosure: some records are valid Modbus but not
        // supported for analysis yet (they never enter the statistics
        // pool). Belongs to the currently published replay session — moved
        // verbatim; the Controller owns the notice lifecycle.
        Label {
            objectName: "replayNoticeLabel"
            visible: page.analysisController.hasReplayNotice
            text: page.analysisController.replayNoticeText
            color: DS.notice
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // Trailing flexible spacer (T017 §37.7): consumes the vertical
        // slack; the space stays at the end of the page as future workflow
        // capacity (M10 request/result area) — no filler content.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    // FileDialog moved verbatim from the Legacy workbench (title/name
    // filters/onAccepted wiring unchanged; selectedFile remains a
    // transient workflow candidate).
    FileDialog {
        id: replayFileDialog
        title: qsTr("加载回放日志")
        nameFilters: [
            qsTr("ModbusLens 回放日志 (*.mlog)"),
            qsTr("所有文件 (*)")
        ]
        onAccepted: page.analysisController.loadReplayFile(selectedFile)
    }
}