import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// T035-C Round C1 — the Engineering workspace: select a SAVED Device Profile,
// select ONE of its registers, see the Readiness verdict and the COMPLETE
// list of unmet conditions.
//
// Boundaries (C1 authorization): nothing here reads the device, computes a
// value, polls, or displays a live engineering value. The gate control is a
// permanent placeholder — it never becomes a live read trigger. The only
// authority is the saved Device Profile (never a Candidate, a draft, a PDF
// or a UI default).
Item {
    id: page
    objectName: "engineeringWorkspace"

    property var profileController: null
    // T035-C C2: the shipped serial session owner + the engineering-read
    // controller (injected by Main.qml; both are C++ objects).
    property var analysisController: null
    property var readController: null
    // The explicit communication target for the read (never inferred).
    property string slaveIdText: "1"
    property string timeoutText: "1000"

    readonly property bool readGateConnected:
        analysisController !== null && analysisController.serialConnected === true
    readonly property bool readGateReady:
        readinessResult !== null && readinessResult.available === true
        && readinessResult.engineeringReady === true
        && readinessResult.decoderKind === "linear"
    readonly property bool readGateBusy:
        analysisController !== null && analysisController.serialBusy === true

    // The Human asked to complete the configuration: Main.qml routes this to
    // the Device workspace.
    signal navigateToDeviceProfile()

    // Session-scoped selection state. The AUTHORITATIVE key is
    // (profileId, functionCode, address) — a list index is never the
    // identity. Switching profiles clears everything derived from the old
    // one.
    property string selectedProfileId: ""
    property int selectedFunctionCode: -1
    property int selectedAddress: -1
    property var registersResult: null
    property var readinessResult: null

    readonly property bool hasSavedProfiles:
        profileController !== null
        && profileController.profileCatalog.length > 0

    function refreshRegisters() {
        readinessResult = null
        selectedFunctionCode = -1
        selectedAddress = -1
        registerCombo.currentIndex = -1
        if (selectedProfileId === "") {
            registersResult = null
            return
        }
        registersResult = profileController.savedProfileRegisters(
            selectedProfileId)
    }

    // C1.3: the visible text of the profile selector follows the REAL
    // selection state; the placeholder is explicit, and an unnamed row gets
    // an honest "(未命名档案)" marker — never a fabricated name.
    function profileNameFor(profileId) {
        var catalog = profileController !== null
                ? profileController.profileCatalog : []
        for (var i = 0; i < catalog.length; ++i) {
            if (catalog[i].profileId === profileId) {
                var name = String(catalog[i].displayPrimary ?? "")
                return name.length > 0 ? name : qsTr("（未命名档案）")
            }
        }
        return qsTr("请选择设备档案")
    }

    // C1.3: same for the register selector — keyed by (fc, address), which
    // is the selection's real identity.
    function registerLabelFor(functionCode, address) {
        var regs = registersResult !== null
                && registersResult.available === true
                ? registersResult.registers : []
        for (var i = 0; i < regs.length; ++i) {
            if (regs[i].functionCode === functionCode
                    && regs[i].address === address) {
                return String(regs[i].label ?? "")
            }
        }
        return qsTr("请选择寄存器")
    }

    // T035-C C1.2: display names for the persisted decoder declaration.
    function decoderDisplayName(token) {
        var map = {
            "unknown": "Unknown（未确认）",
            "raw": "Raw（仅原始值）",
            "linear": "Linear（线性换算）",
            "variable_decimal": "VariableDecimal（可变小数点）",
            "enum": "Enum（枚举映射）"
        }
        return map[token] !== undefined ? map[token] : token
    }

    function selectProfile(profileId) {
        selectedProfileId = profileId
        if (readController !== null) {
            readController.invalidateResult()
        }
        refreshRegisters()
    }

    // Programmatic register selection (used by the automated gate; the
    // ComboBox routes its onActivated through the SAME function).
    function selectRegister(functionCode, address) {
        selectedFunctionCode = functionCode
        selectedAddress = address
        // C2: a NEW target means the previous result is invalid — before and
        // after the readiness refresh.
        if (readController !== null) {
            readController.invalidateResult()
        }
        readinessResult = profileController.savedRegisterReadiness(
            selectedProfileId, functionCode, address)
    }

    // C2: refusal tokens -> Human-readable reason (the controller decides,
    // the page only phrases it).
    function refusalTextFor(token) {
        switch (token) {
        case "invalid_request_parameters":
            return qsTr("参数无效：请检查站号（1-247）与超时（1-60000 ms）。")
        case "profile_unavailable":
            return qsTr("设备档案不可用（可能已被删除或未保存）。")
        case "not_engineering_ready":
            return qsTr("工程换算配置未就绪：请先在设备档案中确认换算方式与字段。")
        case "decoder_not_linear":
            return qsTr("解码方式不是 Linear，当前版本不执行该换算。")
        case "serial_not_connected":
            return qsTr("工程配置已就绪，但串口尚未连接。")
        case "serial_busy":
            return qsTr("串口正忙：已有通信进行中，请稍后重试。")
        case "register_not_found":
            return qsTr("寄存器在最新保存版本中不存在（档案已变更）。")
        case "controller_unavailable":
        default:
            return qsTr("读取功能当前不可用。")
        }
    }

    // C2: the ONE read action. Every gate is re-checked by the controller at
    // click time (saved profile -> readiness -> serial session).
    function requestSingleRead() {
        if (readController === null
                || analysisController === null) {
            return
        }
        const slave = parseInt(slaveIdText, 10)
        const timeout = parseInt(timeoutText, 10)
        readController.readOnce(
            selectedProfileId, selectedFunctionCode, selectedAddress,
            isNaN(slave) ? -1 : slave, isNaN(timeout) ? -1 : timeout)
    }

    // The catalog changed (save / delete / rename elsewhere): a selection
    // that no longer resolves is dropped instead of dangling.
    Connections {
        target: page.profileController
        function onCatalogChanged() {
            if (page.selectedProfileId === "") {
                return
            }
            var found = false
            var catalog = page.profileController.profileCatalog
            for (var i = 0; i < catalog.length; ++i) {
                if (catalog[i].profileId === page.selectedProfileId) {
                    found = true
                    break
                }
            }
            if (!found) {
                page.selectedProfileId = ""
                profileCombo.currentIndex = -1
                if (page.readController !== null) {
                    page.readController.invalidateResult()
                }
                page.refreshRegisters()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: DS.spacingL
        spacing: DS.spacingM

        Label {
            text: qsTr("工程值（配置校验）")
            color: DS.textPrimary
            font.pixelSize: DS.fontTitle
        }
        Label {
            // The C1 contract, stated on the surface itself.
            text: qsTr("仅使用已保存的设备档案；本页只做读取与工程显示的就绪校验，"
                       + "不进行真实读取、不计算数值、不显示模拟值。")
            color: DS.textSecondary
            font.pixelSize: DS.fontBody
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // ---------------------------------------------------------- selectors
        RowLayout {
            Layout.fillWidth: true
            spacing: DS.spacingM
            Label {
                text: qsTr("设备档案：")
                color: DS.textSecondary
                font.pixelSize: DS.fontBody
                Layout.preferredWidth: 90
            }
            ComboBox {
                id: profileCombo
                objectName: "engineeringProfileCombo"
                Accessible.name: qsTr("设备档案")
                Layout.fillWidth: true
                implicitHeight: 32
                font.pixelSize: 15
                // C1.3: the catalog row's REAL field is `displayPrimary`
                // (ProfileController::refreshCatalog) — the old textRole
                // "displayName" matched no key, so both the popup rows and
                // the closed combo rendered BLANK while the (existing)
                // profileId key kept selection working.
                textRole: "displayPrimary"
                // C1.3: no pseudo-selection — the combo starts with NOTHING
                // chosen (a default index-0 would fake "the first profile is
                // selected" and mask state).
                currentIndex: -1
                model: page.profileController !== null
                       ? page.profileController.profileCatalog : []
                delegate: ItemDelegate {
                    width: profileCombo.width
                    text: modelData.displayPrimary !== undefined
                          && String(modelData.displayPrimary).length > 0
                          ? String(modelData.displayPrimary)
                          : qsTr("（未命名档案）")
                    highlighted: profileCombo.highlightedIndex === index
                }
                // The visible name follows the REAL selection state
                // (selectedProfileId) — never a blank cell and never a
                // fabricated name.
                displayText: page.profileNameFor(page.selectedProfileId)
                onActivated: {
                    page.selectProfile(model[currentIndex].profileId)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: DS.spacingM
            Label {
                text: qsTr("寄存器：")
                color: DS.textSecondary
                font.pixelSize: DS.fontBody
                Layout.preferredWidth: 90
            }
            ComboBox {
                id: registerCombo
                objectName: "engineeringRegisterCombo"
                Accessible.name: qsTr("寄存器")
                Layout.fillWidth: true
                implicitHeight: 32
                font.pixelSize: 15
                textRole: "label"
                currentIndex: -1
                enabled: page.registersResult !== null
                         && page.registersResult.available === true
                         && page.registersResult.registers.length > 0
                model: page.registersResult !== null
                       && page.registersResult.available === true
                       ? page.registersResult.registers : []
                delegate: ItemDelegate {
                    width: registerCombo.width
                    text: modelData.label !== undefined
                          ? String(modelData.label)
                          : qsTr("（未命名寄存器）")
                    highlighted: registerCombo.highlightedIndex === index
                }
                displayText: page.registerLabelFor(page.selectedFunctionCode,
                                                   page.selectedAddress)
                onActivated: {
                    var row = model[currentIndex]
                    page.selectRegister(row.functionCode, row.address)
                }
            }
        }

        // -------------------------------------------------------- empty state
        Label {
            objectName: "engineeringEmptyHint"
            visible: !page.hasSavedProfiles
                     || (page.selectedProfileId !== ""
                         && (page.registersResult === null
                             || (page.registersResult.available === true
                                 && page.registersResult.registers.length
                                    === 0)))
            text: !page.hasSavedProfiles
                  ? qsTr("没有已保存的设备档案。请先在「设备」工作区新建并保存档案。")
                  : qsTr("该档案没有寄存器条目。可在「设备」工作区补充后保存。")
            color: DS.textSecondary
            font.pixelSize: DS.fontBody
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // ------------------------------------------------------ readiness box
        Frame {
            objectName: "engineeringReadinessBox"
            visible: page.readinessResult !== null
                     && page.readinessResult.available === true
            Layout.fillWidth: true

            ColumnLayout {
                id: readinessColumn
                width: parent.width
                spacing: DS.spacingS

                RowLayout {
                    spacing: DS.spacingS
                    Label {
                        text: qsTr("读取配置：")
                        color: DS.textSecondary
                        font.pixelSize: 14
                    }
                    Label {
                        objectName: "engineeringReadableLabel"
                        text: page.readinessResult !== null
                              && page.readinessResult.readable === true
                              ? qsTr("完整（Readable）")
                              : qsTr("不完整（Not Readable）")
                        color: page.readinessResult !== null
                               && page.readinessResult.readable === true
                               ? DS.success : DS.error
                        font.pixelSize: 15
                    }
                }
                Label {
                    objectName: "engineeringReadableHint"
                    visible: page.readinessResult !== null
                             && page.readinessResult.available === true
                    text: qsTr("表示已具备发送读取请求所需的配置，不代表设备已连接或已读取成功。")
                    color: DS.textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    spacing: DS.spacingS
                    Label {
                        text: qsTr("工程换算：")
                        color: DS.textSecondary
                        font.pixelSize: 14
                    }
                    Label {
                        objectName: "engineeringReadyLabel"
                        text: page.readinessResult !== null
                              && page.readinessResult.engineeringReady === true
                              ? qsTr("配置已齐全（EngineeringReady）")
                              : qsTr("尚未准备好（Not Ready）")
                        color: page.readinessResult !== null
                               && page.readinessResult.engineeringReady
                                  === true
                               ? DS.success : DS.error
                        font.pixelSize: 15
                    }
                }

                RowLayout {
                    spacing: DS.spacingS
                    Label {
                        text: qsTr("Decoder：")
                        color: DS.textSecondary
                        font.pixelSize: 14
                    }
                    Label {
                        objectName: "engineeringDecoderLabel"
                        // T035-C C1.2: the ACTUAL declaration from the saved
                        // profile (never a UI-side default).
                        text: page.readinessResult !== null
                              && page.readinessResult.available === true
                              ? page.decoderDisplayName(
                                    page.readinessResult.decoderKind)
                              : ""
                        color: DS.textPrimary
                        font.pixelSize: 15
                    }
                }

                Label {
                    objectName: "engineeringMissingLabel"
                    visible: page.readinessResult !== null
                             && (page.readinessResult.missing.length > 0
                                 || page.readinessResult.invalid.length > 0)
                    text: qsTr("未满足条件：")
                    color: DS.textSecondary
                    font.pixelSize: 14
                }
                Repeater {
                    model: page.readinessResult !== null
                           && page.readinessResult.available === true
                           ? page.readinessResult.reasons : []
                    delegate: Label {
                        required property string modelData
                        text: "• " + modelData
                        color: DS.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }

                // T035-C C2: the ONE read action. Enabled only when the full
                // gate chain holds; the click still re-checks every gate in
                // the controller (a stale UI can never send a request).
                RowLayout {
                    Layout.fillWidth: true
                    spacing: DS.spacingM
                    Button {
                        objectName: "engineeringGateButton"
                        enabled: page.readGateReady && page.readGateConnected
                                 && !page.readGateBusy
                                 && (page.readController === null
                                     || page.readController
                                            .readState !== "reading")
                        text: qsTr("读取一次")
                        onClicked: page.requestSingleRead()
                    }
                    Label {
                        objectName: "engineeringCommStatus"
                        text: page.analysisController === null
                              ? qsTr("通信状态：不可用")
                              : (page.readGateConnected
                                 ? qsTr("通信状态：已连接 %1 @ %2")
                                       .arg(page.analysisController.serialPortName)
                                       .arg(page.analysisController.serialBaudRate)
                                 : qsTr("通信状态：未连接（请在「通信」工作区连接串口）"))
                        color: page.readGateConnected ? DS.success
                                                      : DS.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: DS.spacingM
                    Label {
                        text: qsTr("站号（slaveId）：")
                        color: DS.textSecondary
                        font.pixelSize: 13
                        Layout.preferredWidth: 110
                    }
                    TextField {
                        objectName: "engineeringSlaveField"
                        Layout.preferredWidth: 90
                        implicitHeight: 28
                        font.pixelSize: 14
                        text: page.slaveIdText
                        onTextChanged: page.slaveIdText = text
                    }
                    Label {
                        text: qsTr("超时（ms）：")
                        color: DS.textSecondary
                        font.pixelSize: 13
                        Layout.preferredWidth: 90
                    }
                    TextField {
                        objectName: "engineeringTimeoutField"
                        Layout.preferredWidth: 90
                        implicitHeight: 28
                        font.pixelSize: 14
                        text: page.timeoutText
                        onTextChanged: page.timeoutText = text
                    }
                    Item { Layout.fillWidth: true }
                }
                Label {
                    objectName: "engineeringGateHint"
                    text: page.readGateReady
                          ? qsTr("配置校验通过；连接串口后点击「读取一次」发送单次请求。"
                                 + "是否符合设备手册，仍需人工确认。")
                          : qsTr("未达到 EngineeringReady：请按上方列表到设备档案中"
                                 + "设置换算方式与字段后保存，再回到本页复核。")
                    color: DS.textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        // ---- T035-C C2: the single-read result surface -------------------
        Frame {
            objectName: "engineeringValueBox"
            visible: page.readController !== null
            Layout.fillWidth: true

            ColumnLayout {
                width: parent.width
                spacing: DS.spacingS

                RowLayout {
                    spacing: DS.spacingS
                    Label {
                        objectName: "engineeringValueLabel"
                        // The BIG number. Before any successful read it shows
                        // an em dash — never a fabricated value.
                        text: page.readController !== null
                              && page.readController.hasValue === true
                              ? page.readController.valueText
                              : "—"
                        color: page.readController !== null
                               && page.readController.hasValue === true
                               ? DS.textPrimary : DS.textSecondary
                        font.pixelSize: 56
                        font.bold: true
                    }
                    Label {
                        objectName: "engineeringValueUnit"
                        text: page.readController !== null
                              && page.readController.hasValue === true
                              ? page.readController.unitText : ""
                        color: DS.textSecondary
                        font.pixelSize: 24
                        Layout.alignment: Qt.AlignBottom
                    }
                }
                Label {
                    objectName: "engineeringReadStatus"
                    text: page.readController !== null
                          ? page.readController.statusLineText : ""
                    color: DS.textSecondary
                    font.pixelSize: 14
                }
                Label {
                    objectName: "engineeringReadDetail"
                    visible: page.readController !== null
                             && page.readController.hasValue === true
                    text: page.readController !== null
                          && page.readController.hasValue === true
                          ? qsTr("原始值（解码后）：%1 ｜ 倍率：%2 ｜ 偏移量：%3 ｜ Decoder：%4")
                                .arg(page.readController.decodedValueText)
                                .arg(page.readController.scaleText)
                                .arg(page.readController.offsetText)
                                .arg(page.readController.decoderKindText)
                          : ""
                    color: DS.textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label {
                    objectName: "engineeringReadFailure"
                    visible: page.readController !== null
                             && page.readController.readState === "failed"
                    text: page.readController !== null
                          && page.readController.readState === "failed"
                          ? qsTr("读取失败：%1").arg(
                                page.readController.failureText)
                          : ""
                    color: DS.error
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label {
                    objectName: "engineeringReadRefusal"
                    visible: page.readController !== null
                             && page.readController.refusalToken !== ""
                    text: page.readController !== null
                          && page.readController.refusalToken !== ""
                          ? page.refusalTextFor(
                                page.readController.refusalToken)
                          : ""
                    color: DS.error
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label {
                    objectName: "engineeringSemanticNotice"
                    // The standing semantic boundary (C2 §10): static Linear
                    // only — special values / dynamic rules are NOT applied.
                    text: qsTr("语义限制：结果仅为「原始值 × 倍率 + 偏移量」的静态线性换算，"
                               + "不包含特殊值、动态小数点或跨寄存器规则；"
                               + "不得用于自动控制、报警或安全决策。")
                    color: DS.textSecondary
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: DS.spacingM
            Button {
                objectName: "engineeringOpenDeviceButton"
                text: qsTr("前往设备档案编辑器")
                onClicked: page.navigateToDeviceProfile()
            }
            Item { Layout.fillWidth: true }
        }

        Item { Layout.fillHeight: true }
    }
}
