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

    // ------------------------------------------------------------------
    // M9-D D3 selection (page-local presentation state, T019 §D3.5/§D3.6).
    //
    // The authoritative model has exactly ONE mutation path —
    // setEntries() -> beginResetModel()/endResetModel() — and never emits
    // dataChanged, so a snapshot taken here cannot go stale: any change to
    // the transaction set resets the model, and the Connections handler
    // below clears the selection on modelReset. What is copied out is a
    // PRESENTATION COPY read from the delegate on screen at selection time;
    // it is never written back and never becomes an authority. No Controller
    // state is involved.
    // ------------------------------------------------------------------
    property int selectedRow: -1
    property var selectedEntry: null
    // A keyboard move can land on a row whose delegate is not instantiated
    // yet (the view creates it while scrolling it into view), so the snapshot
    // is retried once on the next event-loop turn instead of being dropped.
    property int pendingSelectionRow: -1

    readonly property bool hasTransactionSelection:
        selectedRow >= 0 && selectedEntry !== null

    function captureEntry(item) {
        return {
            deviceAddress: item.rowDeviceAddress,
            functionCode: item.rowFunctionCode,
            statusText: item.rowStatusText,
            elapsedMs: item.rowElapsedMs,
            hasExceptionCode: item.rowHasExceptionCode,
            exceptionCode: item.rowExceptionCode,
            issueText: item.rowIssueText
        };
    }

    function selectRow(row) {
        if (row < 0) {
            selectedRow = -1;
            selectedEntry = null;
            pendingSelectionRow = -1;
            return;
        }
        const item = transactionList.itemAtIndex(row);
        if (!item) {
            pendingSelectionRow = row;
            Qt.callLater(page.applyPendingSelection);
            return;
        }
        pendingSelectionRow = -1;
        selectedRow = row;
        selectedEntry = page.captureEntry(item);
    }

    function applyPendingSelection() {
        const row = pendingSelectionRow;
        if (row < 0)
            return;
        const item = transactionList.itemAtIndex(row);
        pendingSelectionRow = -1;
        if (!item) {
            selectedRow = -1;
            selectedEntry = null;
            return;
        }
        selectedRow = row;
        selectedEntry = page.captureEntry(item);
    }

    // Model replacement/reset invalidates the selection (never re-selects
    // row 0 to fake persistence); navigation alone does not, because the
    // page stays instantiated in the StackLayout.
    Connections {
        target: page.analysisController.transactionModel
        function onModelReset() {
            page.selectedRow = -1;
            page.selectedEntry = null;
            page.pendingSelectionRow = -1;
            // The model can reset before the list is created (initial batch
            // publication during startup), so the id may still be null here.
            if (transactionList)
                transactionList.currentIndex = -1;
        }
    }

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
            // D3: the read-only detail really exists now, so the subtitle
            // can state it (the D1 review's truthful-subtitle guardrail
            // closes here).
            subtitle: qsTr("通信记录与事务详情")
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
                        // D3: Qt's own Up/Down/Home/End handling moves
                        // currentIndex; the page mirrors it into the
                        // selection (no hand-written key state machine).
                        focus: true
                        // M9-F F1 (A): `focus: true` only names the initial
                        // focus item of the scope — it does NOT put the view
                        // into the Tab traversal, which is why a keyboard-only
                        // user could never reach the evidence table (measured:
                        // a 14-press Tab cycle on this workspace never landed
                        // here). activeFocusOnTab adds the view to the chain;
                        // Qt does not move currentIndex when a view gains
                        // focus, so entering the list never selects a row by
                        // itself (no select-on-focus) and the four-key
                        // navigation below stays the only keyboard move path.
                        activeFocusOnTab: true
                        onCurrentIndexChanged: page.selectRow(currentIndex)
                        // M9-D D6 correction: Qt navigates Up/Down natively
                        // but does NOT implement Home/End; the two missing
                        // keys are added on the view itself so every keyboard
                        // move flows through the same
                        // onCurrentIndexChanged -> selectRow path (no second
                        // detail-update logic). Empty models and the
                        // no-selection state are safe: the guards keep
                        // currentIndex within 0..count-1, and a key press is
                        // an explicit user action, not an implicit pick.
                        Keys.onPressed: (event) => {
                            // Qt Quick Keys has no onHomePressed/onEndPressed
                            // handlers, so Home/End use the generic handler.
                            if (event.key === Qt.Key_Home) {
                                if (count > 0)
                                    currentIndex = 0
                                event.accepted = true
                            } else if (event.key === Qt.Key_End) {
                                if (count > 0)
                                    currentIndex = count - 1
                                event.accepted = true
                            }
                        }
                        // Viewport containment (ISSUE-004): delegates must
                        // NEVER paint outside the list.
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: analysisController.transactionModel
                        spacing: 4

                        delegate: Rectangle {
                            // D3: the row's presented values, exposed so the
                            // page-local detail can snapshot them at selection
                            // time (presentation copy only).
                            readonly property int rowDeviceAddress: model.deviceAddress
                            readonly property int rowFunctionCode: model.functionCode
                            readonly property int rowElapsedMs: model.elapsedMs
                            readonly property bool rowHasExceptionCode: model.hasExceptionCode
                            readonly property int rowExceptionCode: model.exceptionCode
                            readonly property string rowStatusText: model.statusText
                            readonly property string rowIssueText: model.issueText
                            // `ListView.view` is attached to the DELEGATE ROOT only —
                            // nested children must read this flag instead.
                            readonly property bool rowSelected:
                                ListView.view.currentIndex === index
                            // M9-D D6 correction: QQuickItemView hands active
                            // focus to the current delegate when the view is
                            // focused, and a plain Item then swallows every
                            // key. Forward the keys to the view so Up/Down
                            // reach the native navigation and Home/End reach
                            // the view’s own handlers — all through the one
                            // currentIndex -> selectRow path.
                            Keys.forwardTo: [transactionList]

                            width: ListView.view.width
                            // T014/T015: rows carrying deterministic detail
                            // get a secondary line; a multi-request-issue
                            // join may wrap to two lines, so the delegate
                            // reserves 64px and the label wraps instead
                            // of clipping.
                            height: model.issueText !== "" ? 64 : 36
                            // restrained selected state: an existing design
                            // token plus a thin brand accent (no severity or
                            // health colour is introduced)
                            color: rowSelected ? DS.navigationSelectedSurface
                                               : DS.surfaceAlt
                            radius: 4

                            Rectangle {
                                visible: parent.rowSelected
                                width: 2
                                height: parent.height
                                radius: 1
                                color: DS.primary
                            }

                            TapHandler {
                                onTapped: {
                                    // M9-D D6 correction (manual interaction
                                    // HOLD): a TapHandler does NOT move
                                    // keyboard focus, so the last clicked
                                    // Control (e.g. Run Demo) kept activeFocus
                                    // even while hidden and the four arrow
                                    // keys died with it. Focus the list FIRST
                                    // (Qt's native Up/Down then navigates and
                                    // onCurrentIndexChanged keeps driving the
                                    // single selectRow path).
                                    transactionList.currentIndex = index
                                    transactionList.forceActiveFocus()
                                }
                            }

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

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: DS.separator
            }

            // ------------------------------------------------------------------
            // Read-only detail (T019 §D3.11/§D3.17): a vertical, natural-height
            // region below the full-width table. It shows ONLY fields the model
            // already carries, consumed as presented values — no raw bytes, no
            // request parameters, no issue parsing, no register decode. Status
            // and issue stay two separate fields (§D3.13).
            // ------------------------------------------------------------------
            ColumnLayout {
                objectName: "transactionDetail"
                Layout.fillWidth: true
                spacing: 4

                Label {
                    objectName: "transactionDetailEmpty"
                    Layout.fillWidth: true
                    visible: !page.hasTransactionSelection
                    text: qsTr("选择一条事务查看详情")
                    color: DS.textSecondary
                    wrapMode: Text.Wrap
                }

                GridLayout {
                    visible: page.hasTransactionSelection
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 16
                    rowSpacing: 2

                    Label {
                        text: qsTr("设备")
                        font.pixelSize: DS.fontCaption
                        color: DS.textMuted
                    }
                    Label {
                        objectName: "transactionDetailDevice"
                        text: page.hasTransactionSelection
                              ? qsTr("设备 %1").arg(page.selectedEntry.deviceAddress)
                              : ""
                        color: DS.textPrimary
                    }
                    Label {
                        text: qsTr("功能码")
                        font.pixelSize: DS.fontCaption
                        color: DS.textMuted
                    }
                    Label {
                        objectName: "transactionDetailFunction"
                        text: page.hasTransactionSelection
                              ? "0x" + ("0" + page.selectedEntry.functionCode.toString(16).toUpperCase()).slice(-2)
                              : ""
                        color: DS.textPrimary
                    }

                    Label {
                        text: qsTr("状态")
                        font.pixelSize: DS.fontCaption
                        color: DS.textMuted
                    }
                    Label {
                        objectName: "transactionDetailStatus"
                        text: page.hasTransactionSelection
                              ? page.selectedEntry.statusText
                              : ""
                        color: DS.textPrimary
                    }
                    Label {
                        text: qsTr("耗时")
                        font.pixelSize: DS.fontCaption
                        color: DS.textMuted
                    }
                    Label {
                        objectName: "transactionDetailLatency"
                        text: page.hasTransactionSelection
                              ? page.selectedEntry.elapsedMs + qsTr(" ms")
                              : ""
                        color: DS.textPrimary
                    }

                    Label {
                        text: qsTr("异常码")
                        font.pixelSize: DS.fontCaption
                        color: DS.textMuted
                    }
                    Label {
                        objectName: "transactionDetailException"
                        text: page.hasTransactionSelection
                              ? (page.selectedEntry.hasExceptionCode
                                 ? qsTr("异常码 0x%1").arg(
                                       ("0" + page.selectedEntry.exceptionCode.toString(16).toUpperCase()).slice(-2))
                                 : qsTr("—"))
                              : ""
                        color: page.hasTransactionSelection
                               && page.selectedEntry.hasExceptionCode
                               ? page.frozenErrorAccent
                               : DS.textSecondary
                    }
                    Item { Layout.fillWidth: true }
                    Item { }
                }

                Label {
                    objectName: "transactionDetailIssue"
                    Layout.fillWidth: true
                    visible: page.hasTransactionSelection
                    text: page.hasTransactionSelection
                          && page.selectedEntry.issueText !== ""
                          ? qsTr("详情：%1").arg(page.selectedEntry.issueText)
                          : qsTr("详情：—")
                    color: DS.textSecondary
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }

            // ------------------------------------------------------------------
            // M9-D D4 diagnosis EXISTENCE cue (T019 §14 option B): one text
            // line mirroring the Controller's authoritative
            // hasBaselineDiagnosis — the same semantic boundary and frozen
            // wording as the M9-C C4 Dashboard cue. Pure presentation: no
            // button, no click target, never findings or baseline text, and
            // never derived from the selected transaction (existence and
            // transaction outcome are different axes). Visibility follows the
            // Dashboard precedent: a session with no observed results owns
            // its own empty hints, so no cue. Navigation stays on the rail.
            // ------------------------------------------------------------------
            Label {
                objectName: "transactionsDiagnosisCue"
                Layout.fillWidth: true
                visible: analysisController.observedCount > 0
                text: analysisController.hasBaselineDiagnosis
                      ? qsTr("已有基线诊断结果，可在诊断工作区查看。")
                      : qsTr("尚未运行基线诊断。")
                color: DS.textSecondary
                wrapMode: Text.Wrap
            }
        }
    }
}
}