import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// M12-B first slice (T027 §31/§33): the Device Profile workspace.
//
// IDENTITY + FILE LIFECYCLE ONLY (hard slice boundary): profile list,
// New/Open/Save/Delete with dirty-state protection, and the profile identity
// editor (displayName required; manufacturer/model/revision/description
// optional). The register-map editor, the Communication selector and the
// semantic overlay are later slices and are deliberately absent here.
//
// Layering: this page never touches the filesystem or the DeviceProfile
// directly — every operation goes through profileController, and profileId is
// program-generated and read-only (never an editable field).
Item {
    id: deviceProfileRoot

    // No clip: the 1000x700 reachability contract is measured on real scene
    // geometry (see --qml-profile-editor-check stage 11), never hidden by
    // clipping. The only intentional clip in this page is the catalog
    // Flickable's own viewport.
    objectName: "deviceProfileWorkspace"

    required property ProfileController profileController

    function refreshProfileCatalog() {
        profileController.refreshCatalog()
    }

    // Three-way dirty resolution (Save / Discard / Cancel) before any action
    // that would abandon the current draft. pendingAction carries what to run
    // after the resolution.
    property string pendingAction: ""
    property string pendingProfileId: ""
    // Flickable has no currentIndex: track the selected catalog row here.
    property int selectedCatalogIndex: -1
    // Out-of-range-safe selection: the catalog shrinks on delete, so a stale
    // index must read as "nothing selected" instead of evaluating
    // profileCatalog[index].profileId on an empty slot (a QML TypeError).
    readonly property var selectedCatalogEntry:
        selectedCatalogIndex >= 0
        && selectedCatalogIndex < profileController.profileCatalog.length
        ? profileController.profileCatalog[selectedCatalogIndex]
        : null

    function requestOpen(profileId) {
        pendingAction = "open"
        pendingProfileId = profileId
        if (profileController.dirty)
            dirtyDialog.open()
        else
            runPendingAction()
    }

    function requestNew() {
        pendingAction = "new"
        pendingProfileId = ""
        if (profileController.dirty)
            dirtyDialog.open()
        else
            runPendingAction()
    }

    function requestDelete(profileId) {
        pendingProfileId = profileId
        if (profileController.dirty
                && profileController.currentProfileId === profileId) {
            pendingAction = "delete"
            dirtyDialog.open()
            return
        }
        deleteDialog.open()
    }

    function runPendingAction() {
        if (pendingAction === "open")
            profileController.openProfile(pendingProfileId)
        else if (pendingAction === "new")
            profileController.newProfile()
        else if (pendingAction === "delete") {
            // Dirty resolution for the CURRENT profile is done; the explicit
            // destructive confirmation still applies.
            deleteDialog.open()
        }
        pendingAction = ""
        pendingProfileId = ""
    }

    // Save-before-continue used by the dirty dialog's Save branch. A failed
    // save keeps the dialog open (blocking the pending action and keeping the
    // reason visible via lastActionError); a successful save closes it BEFORE
    // running the pending action, so the pending action's own dialog (the
    // delete confirmation) is never stacked under an open modal.
    function saveThenRunPendingAction() {
        if (!profileController.saveCurrent())
            return false
        dirtyDialog.close()
        runPendingAction()
        return true
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: DS.spacingM

        SectionHeader {
            objectName: "deviceProfileHeader"
            Layout.fillWidth: true
            title: qsTr("设备档案")
        }

        RowLayout {
            objectName: "deviceProfileActions"
            spacing: DS.spacingM

            AppButton {
                objectName: "profileNewButton"
                Accessible.name: qsTr("新建设备档案")
                text: qsTr("新建")
                onClicked: deviceProfileRoot.requestNew()
            }
            AppButton {
                objectName: "profileOpenButton"
                Accessible.name: qsTr("打开设备档案")
                text: qsTr("打开")
                enabled: deviceProfileRoot.selectedCatalogEntry !== null
                onClicked: deviceProfileRoot.requestOpen(
                               deviceProfileRoot.selectedCatalogEntry.profileId)
            }
            AppButton {
                objectName: "profileSaveButton"
                Accessible.name: qsTr("保存设备档案")
                text: qsTr("保存")
                enabled: profileController.hasOpenProfile
                onClicked: profileController.saveCurrent()
            }
            AppButton {
                objectName: "profileDeleteButton"
                Accessible.name: qsTr("删除设备档案")
                text: qsTr("删除")
                enabled: deviceProfileRoot.selectedCatalogEntry !== null
                onClicked: deviceProfileRoot.requestDelete(
                               deviceProfileRoot.selectedCatalogEntry.profileId)
            }
            Label {
                objectName: "profileDirtyIndicator"
                visible: profileController.dirty
                text: qsTr("未保存修改")
                color: DS.error
                font.pixelSize: DS.fontCaption
            }
        }

        Label {
            objectName: "profileCatalogIssues"
            visible: profileController.catalogIssueCount > 0
            text: profileController.catalogIssueText
                  + (profileController.catalogIssueCount > 0
                     ? qsTr("（损坏的档案文件已忽略，不会自动删除）") : "")
            color: DS.error
            font.pixelSize: DS.fontCaption
            Layout.fillWidth: true
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: DS.spacingM

            // ---- left: managed catalog ----
            PanelCard {
                objectName: "profileCatalogCard"
                Layout.fillHeight: true
                Layout.preferredWidth: 340

                SectionHeader {
                    objectName: "profileCatalogHeader"
                    Layout.fillWidth: true
                    title: qsTr("设备档案列表")
                }

                ColumnLayout {
                    spacing: DS.spacingS
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Label {
                        objectName: "profileCatalogEmpty"
                        visible: profileController.profileCatalog.length === 0
                        text: qsTr("暂无设备档案。点击「新建」创建第一个档案。")
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }

                    Flickable {
                        id: profileCatalogList
                        objectName: "profileCatalogList"
                        Accessible.name: qsTr("设备档案列表")
                        visible: profileController.profileCatalog.length > 0
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: width
                        contentHeight: profileCatalogColumn.height

                        ColumnLayout {
                            id: profileCatalogColumn
                            width: profileCatalogList.width
                            spacing: DS.spacingXS

                            Repeater {
                                model: profileController.profileCatalog

                                delegate: ItemDelegate {
                                    id: catalogDelegate
                                    objectName: "profileCatalogRow"

                                    // Tolerant role reads (ISSUE-017
                                    // discipline): a model reset re-evaluates
                                    // every delegate binding once with an
                                    // invalid index, when EVERY role is
                                    // undefined.
                                    readonly property var entryData:
                                        modelData !== undefined ? modelData : {}

                                    width: profileCatalogList.width
                                    highlighted:
                                        deviceProfileRoot
                                            .selectedCatalogIndex === index
                                    onClicked:
                                        deviceProfileRoot
                                            .selectedCatalogIndex = index

                                    contentItem: ColumnLayout {
                                        spacing: 0

                                        Label {
                                            text: catalogDelegate.entryData
                                                      .displayPrimary
                                            color: DS.textPrimary
                                            font.pixelSize: DS.fontBody
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Label {
                                            visible:
                                                catalogDelegate.entryData
                                                    .displaySecondary !== ""
                                            text: catalogDelegate.entryData
                                                      .displaySecondary
                                            color: DS.textSecondary
                                            font.pixelSize: DS.fontCaption
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Label {
                        objectName: "profileRegisterMapPlaceholder"
                        text: qsTr("寄存器映射编辑将在后续版本提供")
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }
                }
            }

            // ---- right: identity editor ----
            PanelCard {
                objectName: "profileIdentityCard"
                Layout.fillWidth: true
                Layout.fillHeight: true
                SectionHeader {
                    objectName: "profileIdentityHeader"
                    Layout.fillWidth: true
                    title: profileController.hasOpenProfile
                           ? qsTr("档案信息")
                           : qsTr("档案信息（未打开）")
                }

                ColumnLayout {
                    spacing: DS.spacingS
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Label {
                        objectName: "profileEditorEmptyState"
                        visible: !profileController.hasOpenProfile
                        text: qsTr("未打开设备档案。选择「新建」或从左侧列表「打开」。")
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }

                    GridLayout {
                        visible: profileController.hasOpenProfile
                        columns: 2
                        columnSpacing: DS.spacingM
                        rowSpacing: DS.spacingS
                        Layout.fillWidth: true

                        Label {
                            text: qsTr("设备名称：")
                            color: DS.textSecondary
                            font.pixelSize: DS.fontCaption
                        }
                        TextField {
                            objectName: "profileDisplayNameField"
                            Accessible.name: qsTr("设备名称")
                            Layout.fillWidth: true
                            implicitHeight: 24
                            text: profileController.displayName
                                onTextChanged: profileController.displayName = text
                            enabled: profileController.hasOpenProfile
                        }

                        Label {
                            text: qsTr("厂商：")
                            color: DS.textSecondary
                            font.pixelSize: DS.fontCaption
                        }
                        TextField {
                            objectName: "profileManufacturerField"
                            Accessible.name: qsTr("厂商")
                            Layout.fillWidth: true
                            implicitHeight: 24
                            text: profileController.manufacturer
                                onTextChanged: profileController.manufacturer = text
                            enabled: profileController.hasOpenProfile
                        }

                        Label {
                            text: qsTr("型号：")
                            color: DS.textSecondary
                            font.pixelSize: DS.fontCaption
                        }
                        TextField {
                            objectName: "profileModelField"
                            Accessible.name: qsTr("型号")
                            Layout.fillWidth: true
                            implicitHeight: 24
                            text: profileController.model
                                onTextChanged: profileController.model = text
                            enabled: profileController.hasOpenProfile
                        }

                        Label {
                            text: qsTr("版本：")
                            color: DS.textSecondary
                            font.pixelSize: DS.fontCaption
                        }
                        TextField {
                            objectName: "profileRevisionField"
                            Accessible.name: qsTr("版本")
                            Layout.fillWidth: true
                            implicitHeight: 24
                            text: profileController.revision
                                onTextChanged: profileController.revision = text
                            enabled: profileController.hasOpenProfile
                        }

                        Label {
                            text: qsTr("描述：")
                            color: DS.textSecondary
                            font.pixelSize: DS.fontCaption
                        }
                        TextField {
                            objectName: "profileDescriptionField"
                            Accessible.name: qsTr("描述")
                            Layout.fillWidth: true
                            implicitHeight: 24
                            text: profileController.description
                                onTextChanged: profileController.description = text
                            enabled: profileController.hasOpenProfile
                        }

                        Label {
                            text: qsTr("档案标识：")
                            color: DS.textSecondary
                            font.pixelSize: DS.fontCaption
                        }
                        Label {
                            objectName: "profileIdReadOnly"
                            Accessible.name: qsTr("档案标识")
                            text: profileController.currentProfileId
                            color: DS.textSecondary
                            font.pixelSize: DS.fontCaption
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }

                        Label {
                            objectName: "profileValidationText"
                            visible: profileController.validationText !== ""
                            text: profileController.validationText
                            color: DS.error
                            font.pixelSize: DS.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        Label {
                            objectName: "profileActionErrorText"
                            visible: profileController.lastActionError !== ""
                            text: profileController.lastActionError
                            color: DS.error
                            font.pixelSize: DS.fontCaption
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }

    // ---- dirty-state three-way resolution (Save / Discard / Cancel) ----
    Dialog {
        id: dirtyDialog
        objectName: "profileDirtyDialog"
        modal: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        width: 380
        title: qsTr("未保存修改")

        ColumnLayout {
            width: parent.width
            Label {
                text: qsTr("当前设备档案有未保存修改。要如何处理？")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        footer: DialogButtonBox {
            AppButton {
                objectName: "profileDirtySaveButton"
                Accessible.name: qsTr("保存并继续")
                text: qsTr("保存")
                onClicked: deviceProfileRoot.saveThenRunPendingAction()
            }
            AppButton {
                objectName: "profileDirtyDiscardButton"
                Accessible.name: qsTr("放弃修改并继续")
                text: qsTr("放弃修改")
                onClicked: {
                    profileController.discardCurrentChanges()
                    dirtyDialog.close()
                    deviceProfileRoot.runPendingAction()
                }
            }
            AppButton {
                objectName: "profileDirtyCancelButton"
                Accessible.name: qsTr("取消")
                text: qsTr("取消")
                onClicked: {
                    deviceProfileRoot.pendingAction = ""
                    deviceProfileRoot.pendingProfileId = ""
                    dirtyDialog.close()
                }
            }
        }
    }

    // ---- destructive delete confirmation ----
    Dialog {
        id: deleteDialog
        objectName: "profileDeleteDialog"
        modal: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        width: 380
        title: qsTr("删除设备档案")

        ColumnLayout {
            width: parent.width
            Label {
                objectName: "profileDeleteConfirmText"
                text: qsTr("确定删除设备档案“%1”吗？此操作不可撤销。")
                          .arg(deviceProfileRoot.pendingProfileId
                               !== "" ? deviceProfileRoot.pendingProfileId
                                      : qsTr("所选档案"))
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        footer: DialogButtonBox {
            AppButton {
                objectName: "profileDeleteConfirmButton"
                Accessible.name: qsTr("确认删除")
                text: qsTr("删除")
                onClicked: {
                    profileController.deleteProfile(
                        deviceProfileRoot.pendingProfileId)
                    // The deleted row no longer exists: drop the selection so
                    // Open/Delete cannot act on a stale index.
                    deviceProfileRoot.selectedCatalogIndex = -1
                    deleteDialog.close()
                }
            }
            AppButton {
                objectName: "profileDeleteCancelButton"
                Accessible.name: qsTr("取消删除")
                text: qsTr("取消")
                onClicked: {
                    deviceProfileRoot.pendingProfileId = ""
                    deleteDialog.close()
                }
            }
        }
    }
}
