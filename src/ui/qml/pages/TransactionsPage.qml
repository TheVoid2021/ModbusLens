import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// M9-D D2 Transactions workspace page (T019 §D2): the transaction
// presentation moved here verbatim from the Legacy workbench in the same
// change that enabled the 事务 navigation entry — the capability is
// reachable before and after, and exactly one presentation owns it.
//
// Page-root pattern (B1–B5 precedent): a plain Item that the StackLayout
// owns and sizes; the page does its own margins INSIDE. StackLayout child 5
// (workspaceTransactionsIndex).
//
// The presentation is a MOVE, not a redesign (T019 §6): same
// TransactionListModel, same role bindings, same formatter usage, same
// column-width owner, same row heights, same issueText semantics, same
// empty state. D2 adds no detail pane, no selection and no filters.
//
// Ownership (identical to the other pages): the AnalysisController is
// injected by the shell; this page never creates one, never copies
// business state, and never touches the source/revision authority.
Item {
    id: page

    required property var analysisController

    // Frozen V1 visual value moved with the presentation (no DS token yet —
    // M9-C/M9-D candidates were not resolved; M9-C §C6.13 keeps the literal
    // cleanup out of default scope).
    readonly property color frozenErrorAccent: "#C0392B"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: DS.spacingL
        spacing: DS.spacingM

        SectionHeader {
            objectName: "transactionsPageHeader"
            Layout.fillWidth: true
            title: qsTr("事务")
            // D2 truthfulness: the detail pane does not exist until D3, so
            // this page promises only what it really shows.
            subtitle: qsTr("通信记录")
        }

    Rectangle {
        id: transactionsPane
        objectName: "transactionsPane"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumWidth: 520
        color: DS.surface
        border.color: DS.border
        border.width: 1
        radius: 6
        clip: true

        // Single column-geometry owner (Phase D): header AND
        // every row reference exactly these widths — no
        // independent layout distribution may drift columns.
        // Phase E: proportional widths derived from ONE owner.
        // mins protect the 1000x700 minimum window.
        readonly property int tableUsableWidth:
            Math.max(transactionsPane.width - 24, 0)
        readonly property int deviceColumnWidth:
            Math.max(64, Math.round(tableUsableWidth * 0.15))
        readonly property int functionColumnWidth:
            Math.max(60, Math.round(tableUsableWidth * 0.15))
        readonly property int statusColumnWidth:
            Math.max(96, Math.round(tableUsableWidth * 0.20))
        readonly property int latencyColumnWidth:
            Math.max(84, Math.round(tableUsableWidth * 0.18))
        readonly property int leadingColumnsWidth:
            deviceColumnWidth + functionColumnWidth
            + statusColumnWidth + latencyColumnWidth
        readonly property int exceptionColumnWidth:
            Math.max(tableUsableWidth - leadingColumnsWidth, 96)

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Label {
                text: qsTr("最近通信记录")
                font.pixelSize: 18
                font.bold: true
                color: DS.textPrimary
            }

            // Fixed header row — same column geometry source as
            // the row delegate below (single owner, Phase D).
            Row {
                objectName: "transactionsTableHeader"
                Layout.fillWidth: true
                spacing: 0
                Label {
                    text: qsTr("设备"); width: transactionsPane.deviceColumnWidth
                    leftPadding: 6; font.bold: true
                    color: DS.textSecondary; font.pixelSize: 12
                }
                Label {
                    text: qsTr("功能码"); width: transactionsPane.functionColumnWidth
                    leftPadding: 6; font.bold: true
                    color: DS.textSecondary; font.pixelSize: 12
                }
                Label {
                    text: qsTr("状态"); width: transactionsPane.statusColumnWidth
                    leftPadding: 6; font.bold: true
                    color: DS.textSecondary; font.pixelSize: 12
                }
                Label {
                    text: qsTr("耗时"); width: transactionsPane.latencyColumnWidth
                    leftPadding: 6; font.bold: true
                    color: DS.textSecondary; font.pixelSize: 12
                }
                Label {
                    text: qsTr("异常码")
                    width: parent.width - transactionsPane.leadingColumnsWidth
                    leftPadding: 6; font.bold: true
                    color: DS.textSecondary; font.pixelSize: 12
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120

                ListView {
                    id: transactionList
                    objectName: "transactionsList"
                    anchors.fill: parent
                    // Viewport containment (ISSUE-004): delegates must
                    // NEVER paint outside the list.
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: analysisController.transactionModel
                    spacing: 4

                    delegate: Rectangle {
                        width: ListView.view.width
                        // T014/T015: rows carrying deterministic detail
                        // get a secondary line; a multi-request-issue
                        // join may wrap to two lines, so the delegate
                        // reserves 64px and the label wraps instead
                        // of clipping.
                        height: model.issueText !== "" ? 64 : 36
                        color: DS.surfaceAlt
                        radius: 4

                        Column {
                            anchors.fill: parent

                            Row {
                                width: parent.width
                                height: 36
                                spacing: 0

                                Label {
                                    text: qsTr("设备 %1").arg(model.deviceAddress)
                                    width: transactionsPane.deviceColumnWidth
                                    leftPadding: 6
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: "0x" + ("0" + model.functionCode.toString(16).toUpperCase()).slice(-2)
                                    width: transactionsPane.functionColumnWidth
                                    leftPadding: 6
                                }
                                Label {
                                    text: model.statusText
                                    width: transactionsPane.statusColumnWidth
                                    leftPadding: 6
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: model.elapsedMs + qsTr(" ms")
                                    width: transactionsPane.latencyColumnWidth
                                    leftPadding: 6
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: model.hasExceptionCode
                                          ? qsTr("异常码 0x%1").arg(
                                                ("0" + model.exceptionCode.toString(16).toUpperCase()).slice(-2))
                                          : qsTr("—")
                                    color: model.hasExceptionCode ? page.frozenErrorAccent : DS.textSecondary
                                    width: parent.width - transactionsPane.leadingColumnsWidth
                                    leftPadding: 6
                                    elide: Text.ElideRight
                                }
                            }

                            // T014: deterministic detail, only for
                            // ProtocolError rows (adapter-formatted
                            // observed facts, never root-cause prose).
                            Label {
                                width: parent.width
                                height: 28
                                visible: model.issueText !== ""
                                text: model.issueText
                                color: DS.textSecondary
                                leftPadding: 6
                                rightPadding: 6
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                                // Two wrapped lines must never clip.
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Label {
                    objectName: "transactionsEmptyHint"
                    anchors.centerIn: parent
                    visible: transactionList.count === 0
                    text: qsTr("暂无通信记录")
                    color: DS.textSecondary
                }
            }
        }
    }
    }
}