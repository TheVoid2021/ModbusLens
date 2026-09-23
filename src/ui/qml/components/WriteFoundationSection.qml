import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// M10-D4 / M10-E4 — the WRITE section, part of the PRODUCTION UI for both
// 0x06 (Write Single Register) and 0x10 (Write Multiple Registers).
//
// This is the SAME component the hidden foundation used: production and the
// harness differ only by `testFoundationMode`, never by a second copied file.
// A parallel ProductionWrite06/10.qml would be a second draft/validation/dialog
// authority, which is exactly what this project forbids.
//
// Mode (chosen by WHO instantiated the section, never by runtime state):
//   · production      — FC06 + FC16. The function sub-tabs are revealed by the
//                       STRUCTURAL capability (write10Supported), and Confirm
//                       performs the Controller's atomic confirm+dispatch.
//                       This is the shipped UI.
//   · test-foundation — the hidden M10-C/D1/D2 foundation (FC06 + FC10 drafts,
//                       confirmation-only Confirm) kept so C01-C37 / E1 / E2
//                       keep testing zero-dispatch safety.
//
// Capability != availability (M10-E4 §6): `write10Supported` is a structural
// fact and only governs WHETHER the FC16 controls exist at all. The ACTION
// enable rules below mirror the 0x06 ones exactly (connected && !busy), so a
// disconnected / busy / Replay / Simulator context disables the CONTROLS
// without ever unloading the section (which would drop the user's draft).
//
// Protocol truth still lives ONLY in the Controller/core: this file owns
// presentation, page-local drafts and the opaque token hand-back.
//
// Ownership (Phase 1 §I/§J/§K frozen):
//   · drafts below are PAGE-LOCAL presentation state, one set per function,
//     never shared, never auto-cleared by navigation / Clear / tab switch;
//   · the confirmation summary reads the CONTROLLER projection only — never
//     these drafts — so what the user confirms is exactly what was validated;
//   · Confirm hands back nothing but the opaque token.
Item {
    id: section
    objectName: "writeFoundationSection"

    required property var analysisController

    // See the header note. Production never exposes 0x10 (no encoder, no
    // dispatch capability), so 0x10 controls exist only in the test
    // foundation.
    property bool testFoundationMode: false
    readonly property bool productionMode: !testFoundationMode

    // ---- page-local drafts (two independent sets) ----
    // 0x06 address/value are RAW TEXT drafts (M10-D1): the user's exact input
    // is preserved here ("00010", " 1234 ", "-1", "12x", "65536", "") and the
    // core parser decides what it means. unit/timeout stay numeric drafts (a
    // small range where stepping is practical).
    property int unit06: 1
    property string addressText06: ""
    property string valueText06: ""
    property int timeout06: 1000
    property int unit10: 1
    property int start10: 0
    property int timeout10: 1000
    property string valuesText10: ""

    property int activeFunctionIndex: 0 // 0 = 0x06, 1 = 0x10

    // Presentation facts about the confirmation dialog. A Dialog is a QObject
    // (not an Item), so anything outside this file — including the runtime
    // harness — reads its state through these read-only properties instead of
    // reaching into the popup object tree.
    readonly property bool confirmationVisible: confirmationDialog.visible
    readonly property bool confirmationOpened: confirmationDialog.opened

    // M10-F: live request previews (single source of truth = the production
    // encoder via the controller; QML never builds bytes itself). These are
    // pure reads over the same core parse/validate/encode chain the prepare
    // path runs, and re-evaluate with every draft keystroke.
    readonly property var write06Preview:
        section.analysisController.previewWrite06Draft(
            section.unit06, section.addressText06, section.valueText06)
    readonly property var write10Preview:
        section.analysisController.previewWrite10Draft(
            section.unit10, section.start10, section.valuesText10)
    // Authoritative DISPATCH preview: the prepared snapshot encoded by the
    // SAME encoder confirmAndDispatchPreparedWrite uses. Re-evaluates through
    // the preparedWriteChanged projection (function/unit are NOTIFY-bearing).
    readonly property var preparedPreview: {
        const fn = section.analysisController.preparedWriteFunction
        const unit = section.analysisController.preparedWriteUnitId
        return section.analysisController.previewPreparedWrite()
    }
    // The map above carries pduHex/rtuHex ONLY while a snapshot is actually
    // prepared; with nothing prepared `previewPreparedWrite` returns
    // {ok:false, state:"none"}, which is the normal state before the user opens
    // Confirm. The confirmation labels therefore can never bind a key that is
    // absent — that assignment logs "Unable to assign [undefined] to QString"
    // on every load in every mode. Project the optional keys into typed text
    // instead (the same pattern the FC03 read preview uses).
    readonly property bool preparedPreviewOk: preparedPreview.ok === true
    readonly property string preparedPreviewPduText:
        preparedPreviewOk ? preparedPreview.pduHex : ""
    readonly property string preparedPreviewRtuText:
        preparedPreviewOk ? preparedPreview.rtuHex : ""

    // ---- write activation: validate through the controller, then open ----
    function activateWrite() {
        if (section.analysisController.hasPreparedWrite) {
            // Repeated Write: never a second snapshot / second dialog — bring
            // the existing confirmation back to the foreground instead, and
            // report success (the prepared write IS on screen).
            confirmationDialog.open()
            return true
        }
        // M10-E4: the FC16 draft is reachable in production because the
        // structural capability is present (same predicate the Loaders use).
        const fc16Exposed = section.testFoundationMode
                            || section.analysisController.write10Supported
        const wants10 = fc16Exposed && section.activeFunctionIndex === 1
        const accepted = wants10
            ? section.analysisController.prepareWrite10(unit10, start10, valuesText10, timeout10)
            : section.analysisController.prepareWrite06Draft(
                  unit06, addressText06, valueText06, timeout06)
        if (accepted)
            confirmationDialog.open()
        return accepted
    }

    // Cancel / Escape authority: explicit cancel of the prepared snapshot.
    function cancelPreparedWrite() {
        const token = section.analysisController.preparedWriteToken
        if (token !== 0)
            section.analysisController.cancelPreparedWriteToken(token)
    }

    // Confirm authority: opaque token only, no draft fields.
    //
    // The return value answers exactly one question — "was a request issued for
    // a real token?" It is NOT a send result and NOT a Modbus outcome, and no
    // caller may read it as one. In production the atomic call returns nothing
    // at all, precisely so that "the transport accepted the bytes" can never be
    // mistaken for "the write succeeded":
    //   · the dialog leaves the flow because the SNAPSHOT left Prepared
    //     (the onPreparedWriteChanged authority chain, proven by DLG1-DLG5);
    //   · what actually happened to the request is reported through the
    //     outcome lane (writeDispatchNotice) and the transaction history.
    function confirmPreparedWrite() {
        const token = section.analysisController.preparedWriteToken
        if (token === 0)
            return false
        if (section.productionMode) {
            // ONE atomic Controller authority step: consume + encode + start.
            section.analysisController.requestPreparedWriteDispatch(token)
        } else {
            // Test foundation: confirmation only, so the M10-C oracles keep
            // asserting zero write dispatch.
            section.analysisController.confirmPreparedWriteToken(token)
        }
        return true
    }

    // The controller is the ONLY authority: when it stops being Prepared (an
    // invalidation from a disconnect / new session / busy / source change) the
    // dialog must go away with it. The dialog never infers state itself.
    Connections {
        target: section.analysisController
        function onPreparedWriteChanged() {
            if (!section.analysisController.hasPreparedWrite && confirmationDialog.opened)
                confirmationDialog.close()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: DS.spacingM

        SectionHeader {
            objectName: "writeFoundationHeader"
            Layout.fillWidth: true
            title: qsTr("写入")
        }

        PanelCard {
            objectName: "writeFoundationPanel"
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true
                spacing: DS.spacingS

                // Write 内两个子 Tab (0x06 / 0x10), reusing the TabButton
                // pattern already proven on the Diagnosis page.
                //
                // TEST FOUNDATION ONLY, and loaded as a unit so production
                // NEVER CREATES it. `visible: false` would not be enough: a
                // hidden item still exists in the object tree and still answers
                // the accessibility interface, while D4's contract is that 0x10
                // has no production node at all — not hidden, absent.
                // M10-E4: revealed by the STRUCTURAL product capability in
                // production (write10Supported), or by the hidden-foundation
                // flag. It is never driven by connection/busy/source state —
                // those govern each ACTION's enabled state, below.
                Loader {
                    objectName: "writeFunctionTabsLoader"
                    Layout.fillWidth: true
                    active: section.testFoundationMode
                            || section.analysisController.write10Supported
                    sourceComponent: writeFunctionTabsComponent
                }
                Component {
                    id: writeFunctionTabsComponent
                    TabBar {
                        id: writeTabs
                        objectName: "writeFunctionTabs"
                        Layout.fillWidth: true
                        onCurrentIndexChanged: section.activeFunctionIndex = currentIndex

                        TabButton {
                            objectName: "writeTab06"
                            Accessible.name: qsTr("0x06 写单寄存器（Write Single Register）")
                            text: qsTr("FC06 (0x06) 单寄存器")
                            width: implicitWidth
                            focusPolicy: Qt.TabFocus
                        }
                        TabButton {
                            objectName: "writeTab10"
                            Accessible.name: qsTr("0x10 写多寄存器（Write Multiple Registers）")
                            text: qsTr("FC16 (0x10) 多寄存器")
                            width: implicitWidth
                            focusPolicy: Qt.TabFocus
                        }
                    }
                }

                // ---------- 0x06 draft ----------
                RowLayout {
                    objectName: "write06DraftRow"
                    Layout.fillWidth: true
                    visible: section.activeFunctionIndex === 0
                    Label { text: qsTr("从站地址") }
                    SpinBox {
                        objectName: "write06UnitSpin"
                        Accessible.name: qsTr("0x06 从站地址")
                        from: 1; to: 247; value: section.unit06
                        enabled: !section.analysisController.serialBusy
                        onValueModified: section.unit06 = value
                    }
                    Label { text: qsTr("寄存器地址") }
                    DecimalField {
                        objectName: "write06AddressField"
                        fieldLabel: qsTr("寄存器地址")
                        accessibleName: qsTr("0x06 寄存器地址（十进制）")
                        text: section.addressText06
                        enabled: !section.analysisController.serialBusy
                        // Presentation identity only: the controller says
                        // WHICH field its typed error belongs to.
                        hasError: section.analysisController.writeDraftErrorField
                                  === "address"
                        onTextChanged: section.addressText06 = text
                    }
                    Label { text: qsTr("寄存器原始值（uint16，DEC）") }
                    DecimalField {
                        objectName: "write06ValueField"
                        fieldLabel: qsTr("写入值")
                        accessibleName: qsTr("0x06 写入值（十进制）")
                        text: section.valueText06
                        enabled: !section.analysisController.serialBusy
                        hasError: section.analysisController.writeDraftErrorField
                                  === "value"
                        onTextChanged: section.valueText06 = text
                    }
                    Label { text: qsTr("超时 (ms)") }
                    SpinBox {
                        objectName: "write06TimeoutSpin"
                        Accessible.name: qsTr("0x06 超时（毫秒）")
                        from: 100; to: 10000; value: section.timeout06; stepSize: 100
                        enabled: !section.analysisController.serialBusy
                        onValueModified: section.timeout06 = value
                    }
                    Item { Layout.fillWidth: true }
                }

                // M10-F: FC06 request preview. PDU = Function + Data;
                // RTU Frame = Slave + PDU + CRC. The bytes come from the SAME
                // production encoder the dispatch path uses.
                ColumnLayout {
                    visible: section.activeFunctionIndex === 0
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        visible: write06Preview.ok
                        Layout.fillWidth: true
                        text: qsTr("寄存器原始值: %1 (DEC) / %2 (HEX)")
                                  .arg(write06Preview.value)
                                  .arg(write06Preview.valueHex)
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                    }
                    Label {
                        visible: write06Preview.ok
                        Layout.fillWidth: true
                        text: qsTr("PDU  %1").arg(write06Preview.pduHex)
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        font.family: "Consolas"
                        elide: Text.ElideRight
                    }
                    Label {
                        visible: write06Preview.ok
                        Layout.fillWidth: true
                        text: qsTr("RTU Frame  %1").arg(write06Preview.rtuHex)
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        font.family: "Consolas"
                        elide: Text.ElideRight
                    }
                    Label {
                        visible: !write06Preview.ok
                        text: qsTr("预览不可用：%1").arg(write06Preview.error)
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        wrapMode: Text.Wrap
                    }
                }

                // ---------- 0x10 draft (TEST FOUNDATION ONLY) ----------
                // Loaded as a unit for the same reason as the tabs: production
                // must create NO 0x10 draft control and NO 0x10 accessible node.
                Loader {
                    objectName: "writeMultiDraftLoader"
                    Layout.fillWidth: true
                    // M10-E4: same structural capability as the tabs above.
                    active: section.testFoundationMode
                            || section.analysisController.write10Supported
                    // Two independent concerns, deliberately kept apart:
                    //   active  -> is this object CREATED AT ALL (production: no)
                    //   visible -> is it on screen right now (inactive tab: no).
                    // The second one matters: an invisible item is excluded from
                    // keyboard traversal, which is what keeps the inactive
                    // tab's controls unreachable (frozen M10-C contract).
                    visible: active && section.activeFunctionIndex === 1
                    sourceComponent: write10DraftComponent
                }
                Component {
                    id: write10DraftComponent
                    ColumnLayout {
                        objectName: "write10DraftColumn"
                        Layout.fillWidth: true
                        spacing: DS.spacingS

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("从站地址") }
                        SpinBox {
                            objectName: "write10UnitSpin"
                            Accessible.name: qsTr("0x10 从站地址")
                            from: 1; to: 247; value: section.unit10
                            enabled: !section.analysisController.serialBusy
                            onValueModified: section.unit10 = value
                        }
                        Label { text: qsTr("起始地址（PDU / 0-based）") }
                        SpinBox {
                            objectName: "write10StartSpin"
                            Accessible.name: qsTr("0x10 起始地址")
                            from: 0; to: 65535; value: section.start10
                            enabled: !section.analysisController.serialBusy
                            onValueModified: section.start10 = value
                        }
                        Label { text: qsTr("超时 (ms)") }
                        SpinBox {
                            objectName: "write10TimeoutSpin"
                            Accessible.name: qsTr("0x10 超时（毫秒）")
                            from: 100; to: 10000; value: section.timeout10; stepSize: 100
                            enabled: !section.analysisController.serialBusy
                            onValueModified: section.timeout10 = value
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Label {
                        text: qsTr("寄存器原始值（uint16，每行一个十进制数值 DEC）")
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                    }
                    // Typed validation belongs to ONE field: the controller says
                    // WHICH one (writeDraftErrorField === "values"), exactly as
                    // the 0x06 fields do. Presentation only — the parser stays
                    // the authority and the text is never re-interpreted here.
                    Rectangle {
                        objectName: "write10ValuesErrorFrame"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 82
                        color: "transparent"
                        radius: 4
                        border.width: 1
                        border.color: section.analysisController.writeDraftErrorField
                                      === "values" ? DS.error : DS.border

                        ScrollView {
                            objectName: "write10ValuesScroll"
                            anchors.fill: parent
                            anchors.margins: 1
                            Layout.preferredHeight: 80 // bounded: never grows the window
                            clip: true
                        TextArea {
                            id: valuesArea
                            objectName: "write10ValuesArea"
                            Accessible.name: qsTr("0x10 寄存器值（每行一个）")
                            text: section.valuesText10
                            enabled: !section.analysisController.serialBusy
                            wrapMode: TextArea.NoWrap
                            selectByMouse: true
                            onTextChanged: section.valuesText10 = text
                            // A multi-line editor would otherwise swallow Tab
                            // forever. Only Tab/Backtab change meaning here
                            // (the same pattern the Agent draft uses); every
                            // editing key keeps its text semantics, so the
                            // line-per-value input contract is unchanged.
                            Keys.onTabPressed: (event) => {
                                var next = valuesArea.nextItemInFocusChain(true)
                                if (next) {
                                    next.forceActiveFocus(Qt.TabFocusReason)
                                    event.accepted = true
                                }
                            }
                            Keys.onBacktabPressed: (event) => {
                                var prev = valuesArea.nextItemInFocusChain(false)
                                if (prev) {
                                    prev.forceActiveFocus(Qt.BacktabFocusReason)
                                    event.accepted = true
                                }
                            }
                        } // TextArea
                        } // ScrollView
                    } // write10ValuesErrorFrame
                     // M10-F: derived values are COMPUTED, never entered —
                     // quantity and byteCount come from the values list itself
                     // (the single authority; the intent carries only values),
                     // shown here for transparency.
                     Label {
                         visible: write10Preview.ok
                         Layout.fillWidth: true
                         text: qsTr("Quantity（自动）: %1 · Byte Count（自动）: %2")
                                   .arg(write10Preview.quantity)
                                   .arg(write10Preview.byteCount)
                         color: DS.textSecondary
                         font.pixelSize: DS.fontCaption
                     }
                     // FC16 request preview (PDU / RTU Frame) — the same
                     // production encoder the dispatch path uses. Per-value
                     // transparency (index / address / DEC / HEX) is rendered
                     // by the confirmation summary below.
                     Label {
                         visible: write10Preview.ok
                         Layout.fillWidth: true
                         text: qsTr("PDU  %1").arg(write10Preview.pduHex)
                         color: DS.textSecondary
                         font.pixelSize: DS.fontCaption
                         font.family: "Consolas"
                         elide: Text.ElideRight
                     }
                     Label {
                         visible: write10Preview.ok
                         Layout.fillWidth: true
                         text: qsTr("RTU Frame  %1").arg(write10Preview.rtuHex)
                         color: DS.textSecondary
                         font.pixelSize: DS.fontCaption
                         font.family: "Consolas"
                         elide: Text.ElideRight
                     }
                     Label {
                         visible: !write10Preview.ok
                         text: qsTr("预览不可用：%1").arg(write10Preview.error)
                         color: DS.textSecondary
                         font.pixelSize: DS.fontCaption
                         wrapMode: Text.Wrap
                     }
                    } // write10DraftColumn
                } // write10DraftComponent

                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        objectName: "writeActivateButton"
                        Accessible.name: qsTr("写入（打开确认对话框）")
                        text: qsTr("写入")
                        tone: "primary"
                        enabled: section.analysisController.serialConnected
                                 && !section.analysisController.serialBusy
                        onClicked: section.activateWrite()
                    }
                    Item { Layout.fillWidth: true }
                }

                // Typed validation result -> presentation text (mapped in the
                // Qt adapter layer; never a raw enum token).
                Label {
                    objectName: "writeValidationError"
                    Layout.fillWidth: true
                    visible: section.analysisController.hasWriteDraftError
                    text: section.analysisController.writeDraftError
                    color: DS.error
                    wrapMode: Text.Wrap
                }

                // ---- M10-D4 OUTCOME lane (a different fact from the above) ----
                // A draft error says "your input is wrong"; this says "here is
                // what happened to the request". Only NON-SUCCESS states appear,
                // so this label can never be read as a success toast: a full
                // submission says nothing here (its outcome is the transaction
                // row), and nothing is ever claimed from the transport's byte
                // acceptance alone. Uses DS.notice (warning), never DS.error —
                // an incomplete submission is not an input error.
                Label {
                    objectName: "writeDispatchNotice"
                    Layout.fillWidth: true
                    visible: section.analysisController.hasWriteDispatchNotice
                    text: section.analysisController.writeDispatchNotice
                    color: DS.notice
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Confirmation dialog. Everything shown here comes from the CONTROLLER
    // snapshot projection, never from the drafts above.
    // ------------------------------------------------------------------
    Dialog {
        id: confirmationDialog
        objectName: "writeConfirmationDialog"
        anchors.centerIn: parent
        width: Math.min(section.width - 2 * DS.spacingL, 560)
        modal: true
        // Verified against the installed Qt 6.11 sources: ClosePolicy is a
        // flag enum; outside-press close is deliberately EXCLUDED so the only
        // ways out are Escape (-> Cancel authority) and the explicit Cancel.
        closePolicy: Popup.CloseOnEscape
        title: qsTr("确认写入")
        // No standardButtons: a custom footer keeps the default-button /
        // auto-Enter semantics out of the destructive path.
        onRejected: section.cancelPreparedWrite()

        function forceCancelFocus() {
            cancelButton.forceActiveFocus()
        }

        // Function identity for the summary: the protocol display convention
        // already used elsewhere in the app (0xNN), plus the decimal-16 note
        // for 0x10 required by the naming discipline.
        function functionText() {
            const fn = section.analysisController.preparedWriteFunction
            if (fn === 6)
                return qsTr("0x06 单寄存器写入（Write Single Register）")
            if (fn === 16)
                return qsTr("0x10 多寄存器写入（十进制 16，Write Multiple Registers）")
            return ""
        }

        onOpened: forceCancelFocus()

        contentItem: ColumnLayout {
            spacing: DS.spacingS

            Label {
                objectName: "writeSummaryFunction"
                Layout.fillWidth: true
                text: confirmationDialog.functionText()
                color: DS.textPrimary
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: DS.spacingM
                rowSpacing: 2

                Label { text: qsTr("目标从站"); color: DS.textMuted; font.pixelSize: DS.fontCaption }
                Label {
                    objectName: "writeSummaryUnit"
                    text: qsTr("设备 %1").arg(section.analysisController.preparedWriteUnitId)
                    color: DS.textPrimary
                }
                Label { text: qsTr("寄存器地址"); color: DS.textMuted; font.pixelSize: DS.fontCaption }
                Label {
                    objectName: "writeSummaryAddress"
                    text: String(section.analysisController.preparedWriteAddress)
                    color: DS.textPrimary
                }
                Label {
                    objectName: "writeSummaryValueLabel"
                    text: qsTr("写入值")
                    visible: section.analysisController.preparedWriteFunction === 6
                    color: DS.textMuted; font.pixelSize: DS.fontCaption
                }
                Label {
                    objectName: "writeSummaryValue"
                    visible: section.analysisController.preparedWriteFunction === 6
                    text: String(section.analysisController.preparedWriteValue)
                    color: DS.textPrimary
                }
                Label {
                    objectName: "writeSummaryQuantityLabel"
                    text: qsTr("寄存器数量")
                    visible: section.analysisController.preparedWriteFunction === 16
                    color: DS.textMuted; font.pixelSize: DS.fontCaption
                }
                Label {
                    objectName: "writeSummaryQuantity"
                    visible: section.analysisController.preparedWriteFunction === 16
                    text: String(section.analysisController.preparedWriteQuantity)
                    color: DS.textPrimary
                }
                // M10-E4 Human Visual correction: the field holds the LOCAL
                // serial port label ("COM3 @ 9600") taken from the immutable
                // prepared snapshot — no re-derivation here. Labelled "串口"
                // rather than "连接" because "连接" reads as a completed
                // remote-device connection, which this value cannot prove.
                Label { text: qsTr("串口"); color: DS.textMuted; font.pixelSize: DS.fontCaption }
                Label {
                    objectName: "writeSummaryConnection"
                    text: section.analysisController.preparedWriteConnectionLabel
                    color: DS.textPrimary
                    elide: Text.ElideRight
                }
            }

            // 0x10: ALL values, read-only, bounded height, scrollable — never
            // a "... and N more" summary.
            ListView {
                objectName: "writeSummaryValues"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 120)
                visible: section.analysisController.preparedWriteFunction === 16
                clip: true
                model: section.analysisController.preparedWriteValues
                delegate: Label {
                    required property var modelData
                    required property int index
                    // M10-F per-value transparency: 1-based index, the target
                    // PDU address, and the raw uint16 value in DEC + HEX.
                    text: qsTr("值 %1 @%2（%3）：%4")
                              .arg(index + 1)
                              .arg(section.analysisController
                                       .preparedWriteAddress + index)
                              .arg("0x" + Number(modelData).toString(16)
                                            .toUpperCase().padStart(4, "0"))
                              .arg(modelData)
                    color: DS.textPrimary
                    font.pixelSize: DS.fontBody
                }
            }
            // M10-F: the AUTHORITATIVE dispatch preview — the prepared
            // snapshot encoded by the SAME encoder the dispatch path uses, so
            // what the user confirms here is byte-for-byte what would travel.
            ColumnLayout {
                visible: preparedPreview.ok
                spacing: 2

                Label {
                    text: qsTr("PDU")
                    color: DS.textMuted
                    font.pixelSize: DS.fontCaption
                }
                Label {
                    objectName: "writeSummaryPdu"
                    Layout.fillWidth: true
                    text: preparedPreviewPduText
                    color: DS.textPrimary
                    font.pixelSize: DS.fontCaption
                    font.family: "Consolas"
                    elide: Text.ElideRight
                }
                Label {
                    text: qsTr("RTU Frame")
                    color: DS.textMuted
                    font.pixelSize: DS.fontCaption
                }
                Label {
                    objectName: "writeSummaryRtu"
                    Layout.fillWidth: true
                    text: preparedPreviewRtuText
                    color: DS.textPrimary
                    font.pixelSize: DS.fontCaption
                    font.family: "Consolas"
                    elide: Text.ElideRight
                }
            }
        }

        footer: RowLayout {
            spacing: DS.spacingS
            Item { Layout.fillWidth: true }
            AppButton {
                id: cancelButton
                objectName: "writeConfirmCancelButton"
                Accessible.name: qsTr("取消写入（不发送任何请求）")
                text: qsTr("取消")
                tone: "secondary"
                onClicked: {
                    section.cancelPreparedWrite()
                    confirmationDialog.close()
                }
            }
            AppButton {
                id: confirmButton
                objectName: "writeConfirmAcceptButton"
                Accessible.name: qsTr("确认写入意图")
                text: qsTr("确认写入")
                tone: "primary"
                // Never the default/highlighted button: an accidental Enter
                // right after opening must not confirm. Enter is wired
                // explicitly and therefore acts ONLY while this button itself
                // holds active focus (never because the dialog is open).
                Keys.onReturnPressed: (event) => {
                    if (confirmButton.activeFocus) {
                        confirmButton.clicked()
                        event.accepted = true
                    }
                }
                onClicked: {
                    if (section.confirmPreparedWrite())
                        confirmationDialog.close()
                }
            }
        }
    }
}
