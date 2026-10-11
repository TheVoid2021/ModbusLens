import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// T034-B2.1 — the DEDICATED AI Settings workspace (Human §2/§3/§4).
//
// A standalone page (rail index 6): Provider / API Key / Test Connection /
// Model / Save / Remove credential + the privacy notice. It shares NOTHING
// with the business pages — this is the whole point of the slice: AI
// configuration is a first-class settings surface, not an add-on crammed into
// an already-full workspace.
//
// SECRET BOUNDARY (enforceable here): the password field holds a NEWLY
// ENTERED key only; on submit (Enter or the unified Save button) the
// plaintext is handed to aiSettingsController.saveSettings() ONCE and the
// field is cleared immediately. A stored key is NEVER displayed — only the
// state notice "API Key 已安全保存" — and it can never appear in this page's
// text.
Item {
    id: aiSettingsRoot

    objectName: "aiSettingsWorkspace"

    // ---- D8 (T034-B2.1): navigation-away transient secret clearing --------
    // The shell owns this page as a PERSISTENT StackLayout child: navigation
    // away flips visible true→false WITHOUT destroying the object, so the
    // transient plaintext must not survive leaving the visible workspace.
    // Presentation-only: no controller/credential/business mutation. Clearing
    // on an initial visible=false (if the shell creates the page while
    // another workspace is selected) is harmless — the field is empty then.
    // This is UI lifetime control, NOT secure-memory zeroization.
    onVisibleChanged: {
        if (!visible) {
            aiApiKeyField.clear() // D8: leaving the workspace invalidates the transient key
            // E1b: navigation-away ALSO invalidates any in-flight or terminal
            // Test Connection (best-effort cancel if testing; status →
            // not_tested). Test-connection transient state only — zero
            // credential/config/navigation-architecture changes.
            aiSettingsController.notifyTestConnectionInputChanged()
        }
    }

    // The Global AI Configuration owner (context property from main.cpp) and
    // the page controller. The Candidate chain binds the SAME owner, so a
    // Save here hot-switches extraction without a restart.
    required property AiRuntimeConfiguration runtimeConfiguration

    AiSettingsController {
        id: aiSettingsController
        objectName: "aiSettingsController"
        runtimeConfiguration: aiSettingsRoot.runtimeConfiguration
    }

    // ---- D7 (T034-B2.1): provider-identity-bound transient secret ---------
    // The transient API-key plaintext lives ONLY in aiApiKeyField.text —
    // this tracker stores the last OBSERVED PROVIDER IDENTITY, never a
    // secret. When the identity ACTUALLY changes (Human ComboBox activation
    // OR a programmatic controller-driven change), the transient field is
    // cleared immediately: a key entered under provider A must never be
    // submitted under provider B. formChanged alone is NOT the trigger
    // (modelText shares that NOTIFY — a model-only edit never clears), and
    // same-provider re-activation never clears (setProviderName
    // short-circuits on the normalized identity). Seeded at completion with
    // the controller's current identity so even the first transition
    // (unset/default → chosen provider) is covered.
    property string d7ObservedProvider: ""
    Component.onCompleted: aiSettingsRoot.d7ObservedProvider =
                               aiSettingsController.providerName
    Connections {
        target: aiSettingsController
        function onFormChanged() {
            const currentProvider = aiSettingsController.providerName
            if (currentProvider !== aiSettingsRoot.d7ObservedProvider) {
                aiApiKeyField.clear() // D7: identity change invalidates the transient key
            }
            aiSettingsRoot.d7ObservedProvider = currentProvider
        }
    }

    // ---- page inset (page-root pattern: anchored to the plain parent) ----
    Flickable {
        id: contentScroll
        anchors.fill: parent
        anchors.margins: 24
        clip: true
        contentWidth: width
        contentHeight: settingsColumn.implicitHeight

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        ColumnLayout {
            id: settingsColumn
            width: contentScroll.width
            spacing: 18

            // ---- header ------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label {
                    text: qsTr("AI 设置")
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Label {
                    text: qsTr("配置 ModbusLens 使用的 AI Provider、模型与凭据。未配置时，AI 功能保持未配置状态，所有确定性 Modbus 功能不受影响。")
                    color: DS.textSecondary
                    wrapMode: Text.Wrap
                    Layout.maximumWidth: contentScroll.width
                }
            }

            // ---- Provider ----------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { text: qsTr("Provider") }
                ComboBox {
                    objectName: "aiProviderComboBox"
                    Layout.preferredWidth: 320
                    model: aiSettingsController.availableProviders
                    currentIndex: aiSettingsController.availableProviders.indexOf(
                                      aiSettingsController.providerName) >= 0
                                  ? aiSettingsController.availableProviders.indexOf(
                                      aiSettingsController.providerName) : 0
                    onActivated: function(index) {
                        aiSettingsController.providerName =
                            aiSettingsController.availableProviders[index]
                    }
                }
            }

            // ---- API Key -----------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { text: qsTr("API Key") }
                TextField {
                    objectName: "aiApiKeyField"
                    id: aiApiKeyField
                    Layout.preferredWidth: 420
                    echoMode: TextInput.Password
                    placeholderText: aiSettingsController.hasCredentialForCurrentProvider
                                     ? qsTr("输入新的 Key 以替换已保存的凭据")
                                     : qsTr("输入你的 Provider API Key")
                    onAccepted: {
                        // U2b: Enter uses the SAME Unified Save authority as
                        // the Save button; D6 — the plaintext never outlives
                        // the attempt (success OR failure).
                        aiSettingsController.saveSettings(text)
                        aiApiKeyField.clear()
                    }
                    // E1b: a REAL Human edit invalidates any in-flight or
                    // terminal Test Connection result (non-secret hook; no
                    // field copy). Programmatic privacy clears (D6/D7/D8) do
                    // NOT route through this Human-edit signal.
                    onTextEdited: aiSettingsController.notifyTestConnectionInputChanged()
                }
                RowLayout {
                    spacing: 10
                    Button {
                        objectName: "aiDeleteCredentialButton"
                        text: qsTr("删除已保存 API Key")
                        enabled: aiSettingsController.hasCredentialForCurrentProvider
                        onClicked: deleteConfirmDialog.open()
                    }
                    Label {
                        objectName: "aiCredentialState"
                        text: aiSettingsController.hasCredentialForCurrentProvider
                              ? qsTr("API Key 已安全保存")
                              : qsTr("当前 Provider 尚未保存 API Key")
                        color: aiSettingsController.hasCredentialForCurrentProvider
                               ? DS.success : DS.textSecondary
                    }
                }
            }

            // ---- Test Connection ----------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { text: qsTr("测试连接") }
                RowLayout {
                    spacing: 12
                    Button {
                        objectName: "aiTestConnectionButton"
                        text: qsTr("测试连接")
                        // E1b: the CURRENT transient field value is tested
                        // WITHOUT being saved (blank → stored credential for
                        // the CURRENT FORM provider; controller authority).
                        // The field is NOT cleared by Test Connection.
                        onClicked: aiSettingsController.testConnection(
                                       aiApiKeyField.text)
                    }
                    Label {
                        objectName: "aiConnectionStatus"
                        text: aiSettingsController.connectionStatusText
                        color: aiSettingsController.connectionStatusToken
                               === "connection_ok" ? DS.success : DS.textPrimary
                        wrapMode: Text.Wrap
                        Layout.maximumWidth: contentScroll.width - 200
                    }
                }
            }

            // ---- Model --------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { text: qsTr("Model") }
                TextField {
                    objectName: "aiModelField"
                    Layout.preferredWidth: 420
                    text: aiSettingsController.modelText
                    onTextEdited: aiSettingsController.modelText = text
                    placeholderText: qsTr("Provider 的模型 ID（可随时修改）")
                }
            }

            // ---- Save (U2b: the ONE unified persistence action) ---------
            RowLayout {
                spacing: 12
                Button {
                    objectName: "aiSaveConfigurationButton"
                    text: qsTr("保存")
                    onClicked: {
                        // U2b: ONE authority for Provider + optional transient
                        // API Key + Model; D6 — the plaintext never outlives
                        // the attempt (success OR failure).
                        aiSettingsController.saveSettings(aiApiKeyField.text)
                        aiApiKeyField.clear()
                    }
                }
                Label {
                    objectName: "aiSaveStatus"
                    // U2b stale-status guard: while a NEW transient key sits
                    // in the field, a previous "saved" result must not vouch
                    // for it (D6/D7/D8 unchanged).
                    text: aiApiKeyField.text.length > 0
                          ? ""
                          : aiSettingsController.saveStatusText
                    color: aiSettingsController.saveStatusToken === "saved"
                           && aiApiKeyField.text.length === 0
                           ? DS.success : DS.textSecondary
                    visible: text.length > 0
                    wrapMode: Text.Wrap
                    Layout.maximumWidth: contentScroll.width - 200
                }
                Label {
                    objectName: "aiSaveHint"
                    text: qsTr("保存后，所有 AI 功能立即使用新配置（无需重启）。")
                    color: DS.textSecondary
                    wrapMode: Text.Wrap
                    Layout.maximumWidth: contentScroll.width - 200
                }
            }

            // ---- Privacy notice (§4: brief, no over-promising) ----------
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: privacyColumn.implicitHeight + 24
                color: DS.surfaceAlt
                radius: 6
                ColumnLayout {
                    id: privacyColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4
                    Label {
                        text: qsTr("隐私说明")
                        font.weight: Font.DemiBold
                    }
                    Label {
                        objectName: "aiPrivacyNotice"
                        text: qsTr("API Key 仅保存在本机 Windows 凭据管理器中；不写入项目文件，不写入日志。配置 AI 不会自动上传任何数据；只有你主动使用某个 AI 功能时，该功能所需的输入才会发送给你当前选择的 Provider。")
                        color: DS.textSecondary
                        wrapMode: Text.Wrap
                        Layout.maximumWidth: privacyColumn.width
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }

    // ---- Remove-credential confirmation (§3F: explicit double confirm) ----
    Dialog {
        id: deleteConfirmDialog
        objectName: "aiDeleteCredentialConfirmDialog"
        modal: true
        closePolicy: Popup.CloseOnEscape
        anchors.centerIn: parent
        width: 420
        title: qsTr("删除已保存 API Key")
        Label {
            width: parent.width
            text: qsTr("将删除当前 Provider 已保存的 API Key。删除后 AI 功能回到未配置状态。确定删除？")
            wrapMode: Text.Wrap
        }
        footer: DialogButtonBox {
            Button {
                objectName: "aiDeleteCredentialConfirm"
                text: qsTr("删除")
                onClicked: {
                    aiSettingsController.deleteSavedCredential()
                    deleteConfirmDialog.close()
                }
            }
            Button {
                objectName: "aiDeleteCredentialCancel"
                text: qsTr("取消")
                onClicked: deleteConfirmDialog.close()
            }
        }
    }
}
