import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// M9-B3 Communication workspace page.
//
// Page-root pattern (B1 lesson): plain Item that the StackLayout owns and
// sizes; the page does its own margins INSIDE.
//
// Ownership (T017 §34.13 / guardrail B): presentation only — form/draft
// presentation, bindings and explicit user intent. The AnalysisController
// is injected by the shell; the page never creates one, never copies
// business state and never switches the source by itself.
//
// DRAFT vs AUTHORITATIVE (T017 §34.3/§34.4): the port/baud selection and
// the four FC03 parameters below are PAGE-LOCAL command drafts. The
// authoritative session facts (serialConnected / serialBusy / source /
// error / port names) always come from the Controller.
//
// B3.1 moved the serial controls here VERBATIM from the Legacy workbench
// (control types, ranges, models, enabled/visible expressions, texts,
// onClicked wiring and arguments unchanged). The only textual
// substitution is the theme accessor: the block used the window's root
// aliases (root.surface / root.border / root.textPrimary /
// root.textSecondary), which alias DS.* — a separate file has no window
// root id, so it reads DS.* directly (identical values by construction).
//
// B3.2 restructures the presentation ONLY: one GroupBox becomes two
// PanelCard sections (Connection / Request) so M10 can grow the request
// side without touching the connection side. Every control, expression
// and binding is unchanged from B3.1.
Item {
    id: page

    required property var analysisController

    ColumnLayout {
        objectName: "communicationContentLayout"
        anchors.fill: parent
        anchors.margins: DS.spacingL
        spacing: DS.spacingM

        SectionHeader {
            objectName: "communicationHeader"
            Layout.fillWidth: true
            title: qsTr("通信")
            subtitle: qsTr("串口连接与请求")
        }

        // ---- Connection section (B3.2) ----
        SectionHeader {
            objectName: "communicationConnectionHeader"
            Layout.fillWidth: true
            title: qsTr("连接")
        }

        PanelCard {
            objectName: "communicationConnectionSection"
            Layout.fillWidth: true

            // Controls verbatim from B3.1 / the Legacy workbench.
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("串口") }
                Item {
                    Layout.preferredWidth: 140
                    Layout.preferredHeight: serialPortCombo.implicitHeight
                    ComboBox {
                        id: serialPortCombo
                        objectName: "commPortCombo"
                        anchors.fill: parent
                        model: page.analysisController.serialPortNames
                        enabled: !page.analysisController.serialConnected
                        background: Rectangle {
                            radius: 3
                            color: DS.surface
                            border.color: DS.border
                            border.width: 1
                        }
                        contentItem: Text {
                            leftPadding: 8
                            verticalAlignment: Text.AlignVCenter
                            text: serialPortCombo.currentText
                            color: DS.textPrimary
                            elide: Text.ElideRight
                        }
                        // M9-F F1 correction (P1, focus visibility): the custom
                        // background replaced the style's own (and this
                        // palette's) focus indication, measured as a
                        // zero-pixel diff between unfocused and keyboard-
                        // focused states. This overlay is the keyboard-focus
                        // channel only — unfocused rendering is untouched.
                        Rectangle {
                            objectName: "commPortComboFocusRing"
                            anchors.fill: parent
                            anchors.margins: 1
                            color: "transparent"
                            border.color: DS.primary
                            border.width: 2
                            visible: parent.activeFocus
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: page.analysisController.serialPortNames.length === 0
                        text: qsTr("未检测到串口")
                        color: DS.textSecondary
                        font.pixelSize: 12
                    }
                }
                Button {
                    text: qsTr("刷新串口")
                    onClicked: page.analysisController.refreshSerialPorts()
                }
                Label { text: qsTr("波特率") }
                ComboBox {
                    id: serialBaudCombo
                    objectName: "commBaudCombo"
                    model: [9600, 19200, 38400, 57600, 115200]
                    currentIndex: 0
                    enabled: !page.analysisController.serialConnected
                    Layout.preferredWidth: 110
                    // M9-F F1 correction (P1, focus visibility): same overlay
                    // as commPortCombo — the style's own focus ring measured
                    // imperceptible under this palette.
                    Rectangle {
                        objectName: "commBaudComboFocusRing"
                        anchors.fill: parent
                        anchors.margins: 1
                        color: "transparent"
                        border.color: DS.primary
                        border.width: 2
                        visible: parent.activeFocus
                    }
                }
                Label {
                    text: qsTr("8N1")
                    color: DS.textSecondary
                    font.pixelSize: 11
                }
                Button {
                    text: qsTr("连接")
                    enabled: !page.analysisController.serialConnected
                             && serialPortCombo.currentIndex >= 0
                    onClicked: page.analysisController.connectSerial(
                        serialPortCombo.currentText,
                        Number(serialBaudCombo.currentText))
                }
                Button {
                    text: qsTr("断开")
                    enabled: page.analysisController.serialConnected
                    onClicked: page.analysisController.disconnectSerial()
                }
                Item {
                    Layout.fillWidth: true
                }
                // M10-E4 Human Visual correction (serial state semantics): the
                // LOCAL transport fact, stated in words. `serialConnected` means
                // the serial PORT is open — Modbus RTU has no connection
                // handshake, so it can never mean "the remote device is
                // connected". Same single authority as the rest of the page;
                // this label adds presentation, not a second truth.
                Label {
                    objectName: "communicationSerialState"
                    text: page.analysisController.serialConnected
                          ? qsTr("串口已打开") : qsTr("串口未打开")
                    color: page.analysisController.serialConnected
                           ? DS.success : DS.textSecondary
                }
            }

            // M10-E4 Human Visual correction: a CONNECTION/open failure belongs
            // to the Connection section — it must never render between the
            // Request and Write sections, where it reads as if the WRITE had
            // failed. Same single authority (hasSerialError / serialErrorMessage),
            // only the presentation placement moved; no string inspection.
            Label {
                objectName: "communicationSerialError"
                Layout.fillWidth: true
                visible: page.analysisController.hasSerialError
                text: page.analysisController.serialErrorMessage
                color: "#B03030"
                wrapMode: Text.Wrap
            }
        }

        // ---- Request section (B3.2): the existing FC03 read only ----
        SectionHeader {
            objectName: "communicationRequestHeader"
            Layout.fillWidth: true
            title: qsTr("请求")
        }

        PanelCard {
            objectName: "communicationRequestSection"
            Layout.fillWidth: true

            // M10-F: the function code is a FIRST-CLASS request parameter and
            // is always shown in the unified FCnn (0xNN) form. FC03 reads
            // holding registers — a read request has NO register-value field.
            Label {
                text: qsTr("Function: FC03 (0x03) Read Holding Registers 读取保持寄存器")
                color: DS.textSecondary
                font.pixelSize: DS.fontCaption
            }

            // Controls verbatim from B3.1 / the Legacy workbench.
            //
            // TWO rows, not one. A RowLayout never wraps, so a single row must
            // be at least as wide as the sum of its children; with the M10-F
            // additions (the "起始地址（PDU / 0-based）" label and the HEX echo)
            // that sum exceeds the 1000x700 minimum window, so the row
            // overflowed the card and pushed `commReadButton` past the window
            // edge — the page's primary action was unreachable by mouse at the
            // supported minimum size. Splitting the parameters across two rows
            // keeps every control, label, text and binding identical while
            // staying inside the minimum width.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: DS.spacingS

                // Row 1 — target address and the PDU / 0-based HEX echo.
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("从站地址") }
                    SpinBox {
                        id: serialSlaveSpin
                        objectName: "commSlaveSpin"
                        from: 1
                        to: 247
                        value: 1
                        enabled: !page.analysisController.serialBusy
                    }
                    Label { text: qsTr("起始地址（PDU / 0-based）") }
                    SpinBox {
                        id: serialStartSpin
                        objectName: "commStartSpin"
                        from: 0
                        to: 65535
                        value: 0
                        enabled: !page.analysisController.serialBusy
                    }
                    Label {
                        text: qsTr("HEX %1").arg(
                            "0x" + Number(serialStartSpin.value).toString(16)
                                      .toUpperCase().padStart(4, "0"))
                        color: DS.textSecondary
                        font.pixelSize: 11
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }

                // Row 2 — transfer parameters and the action.
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("寄存器数量") }
                    SpinBox {
                        id: serialQuantitySpin
                        objectName: "commQuantitySpin"
                        from: 1
                        to: 125
                        value: 2
                        enabled: !page.analysisController.serialBusy
                    }
                    Label { text: qsTr("超时 (ms)") }
                    SpinBox {
                        id: serialTimeoutSpin
                        objectName: "commTimeoutSpin"
                        from: 100
                        to: 10000
                        value: 1000
                        enabled: !page.analysisController.serialBusy
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    Button {
                        objectName: "commReadButton"
                        text: page.analysisController.serialBusy
                              ? qsTr("读取中...") : qsTr("读取保持寄存器")
                        enabled: page.analysisController.serialConnected
                                 && !page.analysisController.serialBusy
                        onClicked: page.analysisController.readHoldingRegistersOnce(
                            serialSlaveSpin.value,
                            serialStartSpin.value,
                            serialQuantitySpin.value,
                            serialTimeoutSpin.value)
                    }
                }
            }

            // M10-F request preview (single source of truth): the bytes come
            // from the SAME production encoder the dispatch path uses
            // (encodeActiveRequest), so PREVIEW == WIRE by construction.
            // PDU = Function + Data; RTU Frame = Slave + PDU + CRC.
            //
            // Ownership: the preview result belongs to THIS block, so it stays
            // a property of the block rather than of the page root. QML
            // resolves an unqualified name against the object itself and the
            // component ROOT object only — a property declared on an
            // intermediate object is NOT in scope for nested children. Every
            // read below therefore goes through the block id explicitly.
            ColumnLayout {
                id: requestPreviewPanel
                Layout.fillWidth: true
                spacing: 2

                readonly property bool previewOk:
                    requestPreviewPanel.preview.ok === true

                readonly property var preview:
                    page.analysisController.previewReadRequest(
                        serialSlaveSpin.value, serialStartSpin.value,
                        serialQuantitySpin.value, serialTimeoutSpin.value)

                // The controller's map carries pduHex/rtuHex only when the
                // request is valid, and error only when it is not. Project the
                // optional keys into typed text so a Label never binds an
                // absent key (that prints "Unable to assign [undefined] to
                // QString" and would be a new diagnostic of our own making).
                readonly property string previewPduText:
                    requestPreviewPanel.previewOk
                        ? requestPreviewPanel.preview.pduHex : ""
                readonly property string previewRtuText:
                    requestPreviewPanel.previewOk
                        ? requestPreviewPanel.preview.rtuHex : ""
                readonly property string previewErrorText:
                    requestPreviewPanel.previewOk
                        ? "" : requestPreviewPanel.preview.error

                Label {
                    visible: requestPreviewPanel.previewOk
                    Layout.fillWidth: true
                    text: qsTr("PDU  %1").arg(requestPreviewPanel.previewPduText)
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                    font.family: "Consolas"
                    elide: Text.ElideRight
                }
                Label {
                    visible: requestPreviewPanel.previewOk
                    Layout.fillWidth: true
                    text: qsTr("RTU Frame  %1").arg(requestPreviewPanel.previewRtuText)
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                    font.family: "Consolas"
                    elide: Text.ElideRight
                }
                Label {
                    visible: !requestPreviewPanel.previewOk
                    text: qsTr("预览不可用：%1").arg(requestPreviewPanel.previewErrorText)
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                    wrapMode: Text.Wrap
                }
            }
        }

        // ---- M10-D4 write section ----
        // The section EXISTS because of a STRUCTURAL capability, never because
        // of runtime availability: `write06Supported` is a constant of this
        // build, while serialConnected / serialBusy / source / draft validity
        // only decide whether the ACTION is enabled. Binding existence to any
        // of those would unload the whole panel — and the user's page-local
        // draft with it — on every disconnect or busy blip.
        //
        // `writeFoundationVisible` is the TEST-FOUNDATION seam owned by
        // main.cpp. It is NOT a capability flag: it cannot make
        // write06Supported true and it cannot reveal 0x10 in production. It
        // only selects the hidden FC06+FC10 / confirmation-only mode. With it
        // off, a capability-carrying build instantiates this section in
        // PRODUCTION mode (FC06 only, atomic confirm+dispatch).
        Loader {
            id: writeFoundationLoader
            objectName: "writeFoundationLoader"
            Layout.fillWidth: true
            // M10-E4 correction: the section exists when AT LEAST ONE write
            // function has a structural product capability. Gating only on
            // 0x06 would make the FC16 production surface unreachable in a
            // hypothetical build where 0x10 is ready and 0x06 is not — the
            // outer gate must not silently re-introduce a 0x06 dependency.
            active: page.analysisController.write06Supported
                    || page.analysisController.write10Supported
                    || writeFoundationVisible
            // An inline Component (not a URL source) so the required
            // controller property is satisfied at creation time; while
            // `active` is false the component is never instantiated.
            sourceComponent: writeFoundationComponent
        }
        Component {
            id: writeFoundationComponent
            WriteFoundationSection {
                analysisController: page.analysisController
                // WHO instantiated us decides the mode: the harness seam asks
                // for the test foundation; everyone else gets the shipped UI.
                testFoundationMode: writeFoundationVisible
            }
        }

        // Trailing flexible spacer: consumes the vertical slack so the
        // sections stack tightly at the top. Without an expanding child
        // this ColumnLayout spreads the slack BETWEEN its children — the
        // Legacy column was immune only because its SplitView carried
        // Layout.fillHeight (observed via the geometry dump; T017 §35.6).
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}