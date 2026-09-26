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
    // M12-B third slice: the managed catalog (already valid-only) and the
    // session Active Profile owner, injected by the shell like every other
    // controller (the page owns no business state itself).
    required property var profileController
    required property var activeProfileController

    // Selector choices = the "none" sentinel + the managed catalog rows.
    // The catalog roles (displayPrimary/displaySecondary) ARE the frozen
    // duplicate-displayName disambiguation; nothing is re-derived here.
    readonly property var profileChoices: {
        const rows = [{
            profileId: "",
            displayPrimary: qsTr("未选择设备档案"),
            displaySecondary: ""
        }]
        const catalog = profileController.profileCatalog
        for (let i = 0; i < catalog.length; ++i)
            rows.push(catalog[i])
        return rows
    }
    // The active identity (full profileId) resolves to a row position; the
    // active state stays authoritative even when the ComboBox is touched.
    readonly property int selectedProfileChoice: {
        const activeId = activeProfileController.activeProfileId
        for (let i = 0; i < profileChoices.length; ++i)
            if (profileChoices[i].profileId === activeId)
                return i
        return 0
    }
    // The ComboBox internally resets currentIndex whenever its model array
    // is replaced (a catalog refresh) — and a QML binding whose value did
    // not change re-evaluates WITHOUT notifying, so the control would stay
    // at the reset position. The selector therefore re-asserts the
    // authoritative position after every active/catalog change, reading the
    // identity back from the controllers (never from the widget).
    function syncSelectorIndex() {
        // Re-asserting the position re-emits activated(index) for a CHANGED
        // index; the handler is idempotent for the same profileId, so the
        // chain settles after one pass (no loop: an unchanged index emits
        // nothing).
        commProfileSelector.currentIndex = page.selectedProfileChoice
    }
    Connections {
        target: page.activeProfileController
        function onActiveChanged() {
            page.syncSelectorIndex()
        }
    }
    Connections {
        target: page.profileController
        function onCatalogChanged() {
            page.syncSelectorIndex()
        }
    }
    Component.onCompleted: page.syncSelectorIndex()

    ColumnLayout {
        objectName: "communicationContentLayout"
        anchors.fill: parent
        // M12-B third slice: vertical margins tightened (16→10) so the
        // profile-selector row keeps the whole stack — including the C4
        // write panel at 1000x700 — inside the window. Horizontal margins
        // are untouched (cross-page alignment).
        anchors.leftMargin: DS.spacingL
        anchors.rightMargin: DS.spacingL
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        // spacingS (8) rather than spacingM (12): this page carries four
        // stacked sections (two headers, connection, request) PLUS the write
        // section, and at the 1000x700 minimum the whole stack must fit the
        // window without clipping the write panel (C4 geometry). Six gaps at
        // 12px cost 72px of the ~627px budget; 8px keeps the same grouping and
        // returns 24px. The grouping itself is unchanged.
        // M12-B third slice: the profile-selector row adds one more stack
        // entry (~24px). The gap and the vertical margins are tightened
        // (7→6px gaps, 16→4px top/bottom margins) and the selector itself
        // uses the 24px control height (same as the identity editor
        // fields), so the C4 1000x700 contract still holds with the
        // selector present.
        spacing: 6

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
                    // M10 correction (Human): 1200/2400/4800 added; the
                    // default stays 9600 (index 3).
                    model: [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200]
                    currentIndex: 3
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

        // ---- Current profile selector (M12-B third slice) ----
        RowLayout {
            objectName: "communicationProfileSelectorRow"
            Layout.fillWidth: true
            spacing: DS.spacingM

            Label {
                objectName: "commProfileSelectorLabel"
                text: qsTr("当前设备档案")
                color: DS.textPrimary
            }
            ComboBox {
                id: commProfileSelector
                objectName: "commProfileSelector"
                Accessible.name: qsTr("当前设备档案")
                Layout.preferredWidth: 260
                implicitHeight: 24
                model: page.profileChoices
                textRole: "displayPrimary"
                currentIndex: page.selectedProfileChoice
                // Identity discipline: the handler reads the row's profileId
                // (never the index) and calls the session-active API.
                onActivated: function(index) {
                    const chosen = page.profileChoices[index]
                    if (chosen.profileId === "")
                        page.activeProfileController.clearActive()
                    else
                        page.activeProfileController.selectProfile(
                            chosen.profileId)
                }
                background: Rectangle {
                    radius: 3
                    color: DS.surface
                    border.color: DS.border
                    border.width: 1
                }
                contentItem: Text {
                    leftPadding: 8
                    verticalAlignment: Text.AlignVCenter
                    text: commProfileSelector.displayText
                    color: DS.textPrimary
                    elide: Text.ElideRight
                }
                // Two-line dropdown rows: primary = displayName, secondary =
                // manufacturer·model·revision (short profileId on ambiguity) —
                // the catalog's own frozen disambiguation, reused verbatim.
                delegate: ItemDelegate {
                    id: profileChoiceDelegate
                    width: commProfileSelector.width
                    highlighted: commProfileSelector.highlightedIndex === index
                    required property var model
                    required property int index
                    contentItem: ColumnLayout {
                        spacing: 0
                        Label {
                            text: profileChoiceDelegate.model.displayPrimary
                            color: DS.textPrimary
                            font.pixelSize: DS.fontBody
                            elide: Text.ElideRight
                        }
                        Label {
                            visible:
                                profileChoiceDelegate.model.displaySecondary
                                !== ""
                            text:
                                profileChoiceDelegate.model.displaySecondary
                            color: DS.textSecondary
                            font.pixelSize: DS.fontCaption
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            Label {
                objectName: "commProfileSelectorHint"
                text: qsTr("用于后续寄存器语义解读（会话内有效）")
                color: DS.textSecondary
                font.pixelSize: DS.fontCaption
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

            // M10 correction (Human): the Request area is PLAIN TEXT INPUT.
            // Every parameter — slave, function code, start, quantity, timeout
            // — is a keyboard-editable text field; there is no SpinBox and no
            // up/down stepper here any more. The field component is the
            // repository's reusable single-line raw-text field (DecimalField:
            // presentation only — it never parses, never clamps and never
            // converts), and the CORE parsers + the controller's typed guards
            // stay the only authorities (the exact discipline the write
            // drafts already follow). The function code is HEX ("03" / "04" /
            // "41" / "0x41", case-insensitive) with default "03"; the four
            // decimal fields keep their historical defaults.
            //
            // The FIXED "Function: FC03 …" title is GONE on purpose: the
            // function is no longer fixed, so a static FC03 claim would lie.
            // The dynamic, compact helper text sits beside the function field
            // (one row, never a full-width banner) and comes from the SAME
            // controller mapping the preview uses.
            //
            // TWO rows, not one (a RowLayout never wraps; the M10-F overflow
            // lesson): Row 1 = slave + function, Row 2 = start + quantity +
            // timeout + the read action. The start-address HEX echo moved
            // BELOW the rows and is now a projection of the controller's
            // preview map — a QML-side Number() conversion of raw text would
            // be a second parser, which the authority discipline forbids.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: DS.spacingS

                // Row 1 — target device and the editable function code.
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("从站地址") }
                    DecimalField {
                        id: serialSlaveField
                        objectName: "commSlaveField"
                        // Vertical budget: the 1000x700 minimum leaves the
                        // write panel ~25px of slack; the previous SpinBox
                        // rows were 24px tall, and 34px fields pushed
                        // writeFoundationPanel past the window (real-Windows
                        // QPA measured 711 > 700). Same height as the controls
                        // this row replaces.
                        implicitHeight: 24
                        text: "1"
                        fieldLabel: qsTr("从站地址")
                        accessibleName: qsTr("从站地址")
                        enabled: !page.analysisController.serialBusy
                    }
                    Label { text: qsTr("读取功能码") }
                    DecimalField {
                        id: serialFunctionField
                        objectName: "commFunctionField"
                        implicitHeight: 24
                                                text: "03"
                        fieldLabel: qsTr("读取功能码")
                        accessibleName: qsTr("读取功能码")
                        enabled: !page.analysisController.serialBusy
                    }
                    // Dynamic, compact (elided, same row) — never a static
                    // FC03 title, never a full-line banner.
                    Label {
                        objectName: "commFunctionHint"
                        Layout.fillWidth: true
                        text: page.analysisController.readFunctionLabel(
                                  serialFunctionField.text)
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        elide: Text.ElideRight
                    }
                }

                // Row 2 — transfer parameters and the action.
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("起始地址") }
                    DecimalField {
                        id: serialStartField
                        objectName: "commStartField"
                        implicitHeight: 24
                                                text: "0"
                        fieldLabel: qsTr("起始地址")
                        accessibleName: qsTr("起始地址")
                        enabled: !page.analysisController.serialBusy
                    }
                    Label { text: qsTr("寄存器数量") }
                    DecimalField {
                        id: serialQuantityField
                        objectName: "commQuantityField"
                        implicitHeight: 24
                                                text: "2"
                        fieldLabel: qsTr("寄存器数量")
                        accessibleName: qsTr("寄存器数量")
                        enabled: !page.analysisController.serialBusy
                    }
                    Label { text: qsTr("超时(ms)") }
                    DecimalField {
                        id: serialTimeoutField
                        objectName: "commTimeoutField"
                        implicitHeight: 24
                                                text: "1000"
                        fieldLabel: qsTr("超时(ms)")
                        accessibleName: qsTr("超时(ms)")
                        enabled: !page.analysisController.serialBusy
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    Button {
                        objectName: "commReadButton"
                        text: page.analysisController.serialBusy
                              ? qsTr("读取中...") : qsTr("读取寄存器")
                        enabled: page.analysisController.serialConnected
                                 && !page.analysisController.serialBusy
                        onClicked: page.analysisController.readRegisterRequest(
                            serialSlaveField.text,
                            serialFunctionField.text,
                            serialStartField.text,
                            serialQuantityField.text,
                            serialTimeoutField.text)
                    }
                }

                // HEX address echo: a projection of the controller's preview
                // (single authority), shown only while the request parses.
                Label {
                    objectName: "commStartHexEcho"
                    visible: requestPreviewPanel.previewOk
                    // The QML engine's String.arg() accepts only ONE argument
                    // per call (multi-arg form throws "Invalid arguments" at
                    // runtime), so the two placeholders are chained. The
                    // chained values never contain %n, so the substitution
                    // order cannot corrupt the second placeholder.
                    text: qsTr("起始地址 HEX %1 ｜ %2")
                              .arg(requestPreviewPanel.previewStartHex)
                              .arg(requestPreviewPanel.previewFunctionLabel)
                    color: DS.textSecondary
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.fillWidth: true
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
                    page.analysisController.previewReadDraft(
                        serialSlaveField.text, serialFunctionField.text,
                        serialStartField.text, serialQuantityField.text,
                        serialTimeoutField.text)

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
                // Typed projection of the optional startAddressHex key: a
                // Label must never bind an absent map key (ISSUE-017 form).
                readonly property string previewStartHex:
                    requestPreviewPanel.previewOk
                        ? requestPreviewPanel.preview.startAddressHex : ""
                readonly property string previewFunctionLabel:
                    requestPreviewPanel.previewOk
                        ? requestPreviewPanel.preview.functionLabel : "" 

                // One row, not two: at the 1000x700 minimum the page has ~627px
                // of content height and every vertical line counts. PDU and
                // RTU Frame are shown side by side (each elided) so both stay
                // visible and readable without spending a second line.
                RowLayout {
                    visible: requestPreviewPanel.previewOk
                    Layout.fillWidth: true
                    spacing: DS.spacingS

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("PDU  %1").arg(requestPreviewPanel.previewPduText)
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        font.family: "Consolas"
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("RTU Frame  %1").arg(requestPreviewPanel.previewRtuText)
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        font.family: "Consolas"
                        elide: Text.ElideRight
                    }
                }
                Label {
                    visible: !requestPreviewPanel.previewOk
                    text: qsTr("预览不可用：%1").arg(requestPreviewPanel.previewErrorText)
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                    wrapMode: Text.Wrap
                }

                // ------------------------------------------------------------------
                // T023 / M10 correction: FC03 READ RESULT.
                //
                // Human's discovery: a successful read said only 「成功」, so the
                // result was uninterpretable — no values, no bytes, no parameters.
                //
                // Information architecture (frozen, T023 §7.2 option B): ONE
                // always-visible conclusion line plus a reachable DETAILS entry
                // point, with the long evidence (up to 255 RX bytes) living in
                // the bounded, scrollable dialog below.
                //
                // WHY IT IS NESTED IN THIS COLUMN AND WHY IT IS A TEXT ROW:
                // 1000x700 leaves the write panel ~25px of slack (measured), so
                // the always-visible addition must cost ~one text line. Placing
                // it inside the EXISTING preview column costs only this column's
                // 2px spacing, and a 34px AppButton here would have cost 42px in
                // total and pushed the write panel's bottom edge to 717 — past
                // the window (the ISSUE-016 / ISSUE-018 defect form, reproduced
                // and rejected during implementation). The details entry is
                // therefore a compact clickable text row instead.
                //
                // Every fact shown here is a PROJECTION of the controller's
                // canonical read-result snapshot; this file never decodes bytes,
                // never decides a status and never counts registers.
                // ------------------------------------------------------------------
                RowLayout {
                    objectName: "readResultPanel"
                    Layout.fillWidth: true
                    spacing: DS.spacingS

                    // Layer 1: the always-visible conclusion (T023 READ-UI-6).
                    Label {
                        objectName: "readResultSummary"
                        text: page.analysisController.readResultSummaryText
                        color: page.analysisController.readResultTone === "success"
                               ? DS.success
                               : (page.analysisController.readResultTone === "error"
                                  ? DS.error : DS.textSecondary)
                        font.pixelSize: DS.fontCaption
                        font.bold: page.analysisController.readResultTone !== "neutral"
                    }
                    Label {
                        objectName: "readResultSummaryFact"
                        Layout.fillWidth: true
                        visible: page.analysisController.hasReadResult
                        text: page.analysisController.readResultFactLine
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        elide: Text.ElideRight
                    }
                    // Details entry point: only offered when a result exists (an
                    // empty state has nothing to expand into). A text row, not a
                    // 34px button — the vertical budget is the whole reason.
                    Label {
                        objectName: "readResultDetailsButton"
                        visible: page.analysisController.hasReadResult
                        text: qsTr("查看详情")
                        color: DS.primary
                        font.pixelSize: DS.fontCaption
                        font.underline: readResultDetailsHover.hovered
                        HoverHandler { id: readResultDetailsHover }
                        TapHandler {
                            onTapped: readResultDialog.open()
                        }
                    }
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

    // ----------------------------------------------------------------------
    // T023 / M10 correction: FC03 READ RESULT DETAILS.
    //
    // Layer 2 of the frozen information architecture: a BOUNDED, SCROLLABLE
    // evidence view. It is a Popup (not part of the page column) precisely so
    // that its content can never push the write panel out of the 1000x700
    // window — the defect form of ISSUE-016 / ISSUE-018.
    //
    // What it shows, in the frozen order (T023 §7):
    //   1. request echo (unit / PDU start address DEC+HEX / quantity / timeout)
    //   2. Actual TX bytes (the send-time descriptor wire) + disposition band
    //   3. Actual RX bytes, or the honest 「未观测到任何字节」 statement
    //   4. terminal fact + the class's deterministic basis
    //   5. a labelled 「可能原因」 line (never a root-cause claim)
    //   6. CLASS-10 only: the RAW uint16 register table (idle / address / DEC / HEX)
    //
    // No value is ever shown outside CLASS-10 (T023 READ-RX-5), and the bytes
    // are already-decided facts — nothing is re-parsed here.
    // ----------------------------------------------------------------------
    Dialog {
        id: readResultDialog
        objectName: "readResultDialog"
        anchors.centerIn: parent
        width: Math.min(page.width - 2 * DS.spacingL, 720)
        height: Math.min(page.height - 2 * DS.spacingL, 520)
        modal: true
        closePolicy: Popup.CloseOnEscape
        title: qsTr("读取结果详情")

        // Typed projections: the controller's optional keys must never be bound
        // directly (an absent key would log "Unable to assign [undefined] to
        // QString"). Empty text is the honest "not applicable" here.
        function valuesModel() {
            return page.analysisController.readResultValues
        }

        contentItem: ColumnLayout {
            spacing: DS.spacingS

            Label {
                objectName: "readResultDetailTitle"
                Layout.fillWidth: true
                text: page.analysisController.readResultTitle
                color: DS.textPrimary
                font.pixelSize: DS.fontSection
                font.bold: true
            }

            // The whole evidence block is bounded + scrollable: 125 registers
            // must be fully reachable without ever growing the window.
            ScrollView {
                objectName: "readResultDetailScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: readResultDialog.width - 2 * DS.spacingM

                    // ---- 1. request echo ----
                    // M10 correction: the function identity in this echo is
                    // the request's OWN wire code (transaction truth) — the
                    // evidence view must never imply "expected = FC03".
                    Label {
                        objectName: "readResultRequestEcho"
                        Layout.fillWidth: true
                        visible: page.analysisController.readResultHasRequestEcho
                        text: qsTr("请求：设备 %1 ｜ 功能码 %2 ｜ 起始地址 %3 (%4) ｜ 数量 %5 ｜ 超时 %6 ms")
                                  .arg(page.analysisController.readResultUnitId)
                                  .arg(page.analysisController.readResultFunctionLabel)
                                  .arg(page.analysisController.readResultStartAddress)
                                  .arg(page.analysisController.readResultStartAddressHex)
                                  .arg(page.analysisController.readResultQuantity)
                                  .arg(page.analysisController.readResultTimeoutMs)
                        color: DS.textPrimary
                        font.pixelSize: DS.fontCaption
                        wrapMode: Text.Wrap
                    }
                    // Received function: shown only when it is determinable
                    // from the transaction (schema-conforming success, or an
                    // unexpected-function mismatch carrying the actual code).
                    Label {
                        objectName: "readResultReceivedFunction"
                        Layout.fillWidth: true
                        visible: page.analysisController.readResultHasReceivedFunction
                        text: qsTr("响应功能码：%1")
                                  .arg(page.analysisController.readResultReceivedFunctionLabel)
                        color: DS.textPrimary
                        font.pixelSize: DS.fontCaption
                        wrapMode: Text.Wrap
                    }

                    // ---- 2. Actual TX ----
                    Label {
                        objectName: "readResultTxLabel"
                        text: qsTr("实际发送（含 CRC）")
                        color: DS.textMuted
                        font.pixelSize: DS.fontCaption
                    }
                    Label {
                        objectName: "readResultTxText"
                        Layout.fillWidth: true
                        text: page.analysisController.readResultTxLine
                        color: DS.textPrimary
                        font.pixelSize: DS.fontCaption
                        font.family: "Consolas"
                        wrapMode: Text.Wrap
                    }

                    // ---- 3. Actual RX ----
                    Label {
                        objectName: "readResultRxLabel"
                        text: qsTr("实际接收（逐字节，未截断）")
                        color: DS.textMuted
                        font.pixelSize: DS.fontCaption
                    }
                    Label {
                        objectName: "readResultRxText"
                        Layout.fillWidth: true
                        text: page.analysisController.readResultRxText
                        color: DS.textPrimary
                        font.pixelSize: DS.fontCaption
                        font.family: "Consolas"
                        wrapMode: Text.Wrap
                    }
                    Label {
                        objectName: "readResultRxByteCount"
                        Layout.fillWidth: true
                        text: qsTr("接收字节数：%1")
                                  .arg(page.analysisController.readResultRxByteCount)
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                    }

                    // ---- 4. deterministic basis ----
                    Label {
                        objectName: "readResultBasis"
                        Layout.fillWidth: true
                        text: page.analysisController.readResultFactLine
                        color: DS.textPrimary
                        font.pixelSize: DS.fontCaption
                        wrapMode: Text.Wrap
                    }
                    // Evidence availability: Simulator / Replay carry no wire
                    // evidence, so the surface states that plainly instead of
                    // showing empty hex (T023 READ-TXN-5).
                    Label {
                        objectName: "readResultNoEvidence"
                        Layout.fillWidth: true
                        visible: page.analysisController.readResultAwaitingEvidenceSource
                        text: qsTr("当前数据源不提供收发字节（该源没有真实收发）。")
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        wrapMode: Text.Wrap
                    }

                    // ---- 5. possible causes (explicitly labelled) ----
                    Label {
                        objectName: "readResultPossibleCauses"
                        Layout.fillWidth: true
                        visible: page.analysisController.readResultHasPossibleCauses
                        text: qsTr("可能原因：%1")
                                  .arg(page.analysisController.readResultPossibleCauses)
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        wrapMode: Text.Wrap
                    }

                    // ---- 6. CLASS-10 raw register table ----
                    Label {
                        objectName: "readResultValuesHeader"
                        visible: page.analysisController.readResultHasValues
                        text: qsTr("原始寄存器值（uint16，未解释）：共 %1 个")
                                  .arg(page.analysisController.readResultValueCount)
                        color: DS.textPrimary
                        font.pixelSize: DS.fontCaption
                        font.bold: true
                    }
                    // ---- 6b. M11 decode configuration (T024 §22 C1/C2) ----
                    // A DERIVED view over the same canonical raw words:
                    // changing it never mutates the raw columns (raw stays the
                    // device's actual answer, T024 §22 C4). The controls live
                    // inside the details dialog on purpose — the main page
                    // must stay inside 1000x700 (T024 §22 C2).
                    RowLayout {
                        objectName: "readDecodeControls"
                        visible: page.analysisController.readResultHasValues
                        spacing: DS.spacingM

                        Label {
                            text: qsTr("解析类型：")
                            color: DS.textSecondary
                            font.pixelSize: DS.fontCaption
                        }
                        ComboBox {
                            objectName: "readDecodeTypeCombo"
                            Accessible.name: qsTr("解析类型")
                            editable: false
                            // Index order mirrors core::RegisterDecodeType
                            // (Hex / Binary / UInt16 / Int16 / UInt32 /
                            // Int32 / Float32); UInt16 is the frozen default
                            // (T024 §22 C1).
                            model: [qsTr("十六进制"), qsTr("二进制"),
                                    qsTr("无符号16位整数"), qsTr("有符号16位整数"),
                                    qsTr("无符号32位整数"), qsTr("有符号32位整数"),
                                    qsTr("32位浮点数")]
                            currentIndex: page.analysisController.readDecodeType
                            onActivated:
                                page.analysisController.readDecodeType = currentIndex
                            font.pixelSize: DS.fontCaption
                        }
                        Label {
                            text: qsTr("寄存器内字节顺序：")
                            color: DS.textSecondary
                            font.pixelSize: DS.fontCaption
                        }
                        ComboBox {
                            objectName: "readDecodeByteOrderCombo"
                            Accessible.name: qsTr("寄存器内字节顺序")
                            editable: false
                            // Index order mirrors core::RegisterByteOrder.
                            model: [qsTr("正常"), qsTr("字节交换")]
                            currentIndex:
                                page.analysisController.readDecodeByteOrder
                            onActivated:
                                page.analysisController.readDecodeByteOrder = currentIndex
                            font.pixelSize: DS.fontCaption
                        }
                        Label {
                            // Meaningful only for 2-register types (T024
                            // §22 C2): the label stays visible so Human can
                            // see the axis exists, while the combo below is
                            // disabled until a 32-bit type is selected.
                            text: qsTr("32位寄存器顺序：")
                            color: DS.textSecondary
                            font.pixelSize: DS.fontCaption
                        }
                        ComboBox {
                            objectName: "readDecodeWordOrderCombo"
                            Accessible.name: qsTr("32位寄存器顺序")
                            editable: false
                            // Index order mirrors core::RegisterWordOrder.
                            // Enabled only when the selected type consumes
                            // 2 registers (UInt32 / Int32 / Float32).
                            enabled:
                                page.analysisController.readDecodeWordOrderEnabled
                            opacity: enabled ? 1.0 : 0.4
                            model: [qsTr("高字在前（AB CD）"),
                                    qsTr("低字在前（CD AB）")]
                            currentIndex:
                                page.analysisController.readDecodeWordOrder
                            onActivated:
                                page.analysisController.readDecodeWordOrder = currentIndex
                            font.pixelSize: DS.fontCaption
                        }
                    }
                    // Three-layer legend (M12-B slice 4): raw → generic
                    // decode → device-profile semantic. Labels, not colors,
                    // carry the distinction.
                    Label {
                        objectName: "readResultLayerLegend"
                        Layout.fillWidth: true
                        text: qsTr("每行三层：原始值（DEC/HEX）→ 通用解析 → 设备档案语义")
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                    }

                    ListView {
                        objectName: "readResultValuesList"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 200)
                        visible: page.analysisController.readResultHasValues
                        clip: true
                        model: page.analysisController.readResultValues
                        delegate: ColumnLayout {
                            spacing: 0
                            width: ListView.view ? ListView.view.width : 0
                            // Tolerant role reads (ISSUE-017): a model reset
                            // re-evaluates every delegate binding once with an
                            // invalid index, when EVERY role is undefined.
                            required property var modelData
                            required property int index
                            readonly property int rowIndex:
                                modelData && modelData.index !== undefined
                                    ? modelData.index : (index + 1)
                            readonly property int rowAddress:
                                modelData && modelData.address !== undefined
                                    ? modelData.address : 0
                            readonly property string rowAddressHex:
                                modelData && modelData.addressHex !== undefined
                                    ? modelData.addressHex : ""
                            readonly property int rowDec:
                                modelData && modelData.dec !== undefined
                                    ? modelData.dec : 0
                            readonly property string rowHex:
                                modelData && modelData.hex !== undefined
                                    ? modelData.hex : ""
                            // M11: the DERIVED decode view (empty unless the
                            // decode succeeded — an empty cell never fakes a
                            // value, and the raw columns stay untouched).
                            readonly property string rowDecoded:
                                modelData && modelData.decoded !== undefined
                                    ? modelData.decoded : ""
                            readonly property string rowDecodeStatus:
                                modelData && modelData.decodeStatus !== undefined
                                    ? modelData.decodeStatus : ""
                            // M11 second slice: for a 2-register decode this
                            // carries the consumed address range ("1000-1001")
                            // so Human can always see WHICH registers a
                            // derived 32-bit value used; empty for 1-register
                            // types.
                            readonly property string rowDecodeSpan:
                                modelData && modelData.decodeSpan !== undefined
                                    ? modelData.decodeSpan : ""
                            readonly property string decodedDisplay:
                                rowDecodeStatus === "ok" && !rowDecoded.isEmpty
                                    ? rowDecoded : qsTr("无法解析")
                            readonly property string spanSuffix:
                                rowDecodeSpan !== ""
                                    ? qsTr("  范围 %1").arg(rowDecodeSpan) : ""
                            Label {
                                text: qsTr("#%1  地址 %2 (%3)  值 %4 (%5)  解析 %6")
                                          .arg(rowIndex)
                                          .arg(rowAddress)
                                          .arg(rowAddressHex)
                                          .arg(rowDec)
                                          .arg(rowHex)
                                          .arg(decodedDisplay)
                                      + spanSuffix
                                color: DS.textPrimary
                                font.pixelSize: DS.fontCaption
                                font.family: "Consolas"
                            }

                            // ---- Layer 3: M12 device-profile semantic ----
                            // An ADDITIVE, always-labeled line. It never
                            // replaces the raw/generic columns above and is
                            // driven by the ACTIVE PERSISTED profile only.
                            Label {
                                objectName: "readSemanticCell"
                                Layout.fillWidth: true
                                font.pixelSize: DS.fontCaption
                                font.family: "Consolas"
                                color: DS.textSecondary
                                text: {
                                    const status =
                                        modelData && modelData.semanticStatus
                                            !== undefined
                                            ? modelData.semanticStatus : ""
                                    const name =
                                        modelData && modelData.semanticName
                                            !== undefined
                                            ? modelData.semanticName : ""
                                    const text =
                                        modelData && modelData.semanticText
                                            !== undefined
                                            ? modelData.semanticText : ""
                                    const span =
                                        modelData && modelData.semanticSpan
                                            !== undefined
                                            ? modelData.semanticSpan : ""
                                    const start =
                                        modelData
                                        && modelData.semanticStartAddress
                                            !== undefined
                                            ? modelData.semanticStartAddress
                                            : -1
                                    if (status === "mapped_start")
                                        return "档案语义 " + name + " = " + text
                                               + (span !== ""
                                                  ? "（" + span + "）" : "")
                                    if (status === "mapped_continuation")
                                        return "档案语义：属于 " + name
                                               + "（起始地址 " + start
                                               + "）；语义值显示在起始地址行"
                                    if (status === "insufficient_words")
                                        return "档案语义 " + name
                                               + "：数据不足（需 " + span
                                               + " 全部原始字）"
                                    if (status === "unmapped")
                                        return "档案语义：未匹配设备档案"
                                    if (status === "decode_error")
                                        return "档案语义 " + name
                                               + "：无法按档案解码"
                                    return "档案语义：未选择设备档案"
                                }
                            }
                        }
                    }
                }
            }
        }

        footer: RowLayout {
            spacing: DS.spacingS
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "readResultDetailCloseButton"
                text: qsTr("关闭")
                onClicked: readResultDialog.close()
            }
        }
    }
}