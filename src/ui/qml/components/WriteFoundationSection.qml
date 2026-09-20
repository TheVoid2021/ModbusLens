import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// M10-C2 — HIDDEN write foundation (draft + validation presentation + the
// modal confirmation dialog). It is instantiated ONLY when the harness
// visibility seam is on: normal production never loads this file, so no write
// control exists in the production scene at all (no Tab entry, no Space
// activation, no clickable area, nothing to misread as a capability).
//
// It is NOT a write capability: there is no 0x06 / 0x10 encoder and no
// dispatch. Confirming consumes an immutable PreparedWriteSnapshot in the
// controller (a UI acknowledgement), and the transport is never touched here.
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

    // ---- page-local drafts (two independent sets) ----
    property int unit06: 1
    property int address06: 0
    property int value06: 0
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

    // ---- write activation: validate through the controller, then open ----
    function activateWrite() {
        if (section.analysisController.hasPreparedWrite) {
            // Repeated Write: never a second snapshot / second dialog — bring
            // the existing confirmation back to the foreground instead, and
            // report success (the prepared write IS on screen).
            confirmationDialog.open()
            return true
        }
        const accepted = section.activeFunctionIndex === 0
            ? section.analysisController.prepareWrite06(unit06, address06, value06, timeout06)
            : section.analysisController.prepareWrite10(unit10, start10, valuesText10, timeout10)
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
    function confirmPreparedWrite() {
        const token = section.analysisController.preparedWriteToken
        if (token === 0)
            return false
        return section.analysisController.confirmPreparedWriteToken(token)
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
                TabBar {
                    id: writeTabs
                    objectName: "writeFunctionTabs"
                    Layout.fillWidth: true
                    onCurrentIndexChanged: section.activeFunctionIndex = currentIndex

                    TabButton {
                        objectName: "writeTab06"
                        text: qsTr("0x06 单寄存器")
                        width: implicitWidth
                        focusPolicy: Qt.TabFocus
                    }
                    TabButton {
                        objectName: "writeTab10"
                        text: qsTr("0x10 多寄存器")
                        width: implicitWidth
                        focusPolicy: Qt.TabFocus
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
                        from: 1; to: 247; value: section.unit06
                        enabled: !section.analysisController.serialBusy
                        onValueModified: section.unit06 = value
                    }
                    Label { text: qsTr("寄存器地址") }
                    SpinBox {
                        objectName: "write06AddressSpin"
                        from: 0; to: 65535; value: section.address06
                        enabled: !section.analysisController.serialBusy
                        onValueModified: section.address06 = value
                    }
                    Label { text: qsTr("写入值") }
                    SpinBox {
                        objectName: "write06ValueSpin"
                        from: 0; to: 65535; value: section.value06
                        enabled: !section.analysisController.serialBusy
                        onValueModified: section.value06 = value
                    }
                    Label { text: qsTr("超时 (ms)") }
                    SpinBox {
                        objectName: "write06TimeoutSpin"
                        from: 100; to: 10000; value: section.timeout06; stepSize: 100
                        enabled: !section.analysisController.serialBusy
                        onValueModified: section.timeout06 = value
                    }
                    Item { Layout.fillWidth: true }
                }

                // ---------- 0x10 draft ----------
                ColumnLayout {
                    objectName: "write10DraftColumn"
                    Layout.fillWidth: true
                    visible: section.activeFunctionIndex === 1
                    spacing: DS.spacingS

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("从站地址") }
                        SpinBox {
                            objectName: "write10UnitSpin"
                            from: 1; to: 247; value: section.unit10
                            enabled: !section.analysisController.serialBusy
                            onValueModified: section.unit10 = value
                        }
                        Label { text: qsTr("起始地址") }
                        SpinBox {
                            objectName: "write10StartSpin"
                            from: 0; to: 65535; value: section.start10
                            enabled: !section.analysisController.serialBusy
                            onValueModified: section.start10 = value
                        }
                        Label { text: qsTr("超时 (ms)") }
                        SpinBox {
                            objectName: "write10TimeoutSpin"
                            from: 100; to: 10000; value: section.timeout10; stepSize: 100
                            enabled: !section.analysisController.serialBusy
                            onValueModified: section.timeout10 = value
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Label {
                        text: qsTr("寄存器值（每行一个十进制数值）")
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                    }
                    ScrollView {
                        objectName: "write10ValuesScroll"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96 // bounded: never grows the window
                        clip: true
                        TextArea {
                            id: valuesArea
                            objectName: "write10ValuesArea"
                            text: section.valuesText10
                            enabled: !section.analysisController.serialBusy
                            wrapMode: TextArea.NoWrap
                            selectByMouse: true
                            onTextChanged: section.valuesText10 = text
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        objectName: "writeActivateButton"
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
                Label { text: qsTr("连接"); color: DS.textMuted; font.pixelSize: DS.fontCaption }
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
                    text: qsTr("值 %1：%2").arg(index + 1).arg(modelData)
                    color: DS.textPrimary
                    font.pixelSize: DS.fontBody
                }
            }
        }

        footer: RowLayout {
            spacing: DS.spacingS
            Item { Layout.fillWidth: true }
            AppButton {
                id: cancelButton
                objectName: "writeConfirmCancelButton"
                text: qsTr("取消")
                tone: "secondary"
                onClicked: {
                    section.cancelPreparedWrite()
                    confirmationDialog.close()
                }
            }
            AppButton {
                objectName: "writeConfirmAcceptButton"
                text: qsTr("确认写入")
                tone: "primary"
                // Never the default/highlighted button: an accidental Enter
                // right after opening must not confirm.
                onClicked: {
                    if (section.confirmPreparedWrite())
                        confirmationDialog.close()
                }
            }
        }
    }
}
