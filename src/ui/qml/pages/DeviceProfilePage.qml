import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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
    // M12-C C1a: the Manual Import owner is a SEPARATE controller. It owns no
    // ProfileController / ActiveProfileController reference by construction,
    // so an import can never change the editor draft, the dirty state, the
    // Active Profile or a persisted DeviceProfile JSON.
    required property ManualImportController manualController

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

    // ---- Register Map editor (second slice) ----
    // The selected register row (display order) and its DRAFT index — the
    // draft index is the only stable locator across re-sorts (T027 §17).
    property int selectedRegisterIndex: -1
    readonly property var selectedRegisterRow:
        selectedRegisterIndex >= 0
        && selectedRegisterIndex < registerMapRepeater.model.length
        ? registerMapRepeater.model[selectedRegisterIndex] : null
    // What the entry dialog is doing: "" (closed), "add" or the draft index
    // being edited (kept as a number in a string-tolerant var).
    property var entryEditorMode: ""
    readonly property bool entryEditorAdding: entryEditorMode === "add"
    // The entry editor's temporary draft: raw field texts exactly as typed.
    // Apply goes through profileController.add/editRegisterEntry (candidate +
    // full validation); a refused apply keeps every field as typed.
    property string entryFcText: ""
    property string entryAddressText: ""
    property string entryNameText: ""
    property string entryDescriptionText: ""
    property int entryDataTypeIndex: 2 // UInt16 (visible combo default)
    property int entryByteOrderIndex: 0
    property int entryWordOrderIndex: 0
    property string entryScaleText: ""
    property string entryOffsetText: ""
    property string entryUnitText: ""
    // The M11 seven-type matrix (T027 §8: no second type enum anywhere).
    readonly property var dataTypeChoices:
        ["HEX", "BIN", "UInt16", "Int16", "UInt32", "Int32", "Float32"]
    readonly property var byteOrderChoices:
        ["Normal 正序", "ByteSwapped 交换"]
    readonly property var wordOrderChoices:
        ["HighWordFirst 高字在前", "LowWordFirst 低字在前"]
    // Group 1-B: registerCount is DERIVED — Hex/Binary/16-bit → 1, 32-bit → 2.
    readonly property int entryDerivedRegisterCount:
        entryDataTypeIndex >= 4 ? 2 : 1
    readonly property bool entryIsTwoWord: entryDerivedRegisterCount === 2

    function openEntryAdd() {
        // No silent FC03: the function-code field starts EMPTY and the user
        // must type it (T027 §32/§35 no-silent-default discipline).
        profileController.clearActionError()
        entryFcText = ""
        entryAddressText = ""
        entryNameText = ""
        entryDescriptionText = ""
        entryDataTypeIndex = 2
        entryByteOrderIndex = 0
        entryWordOrderIndex = 0
        entryScaleText = ""
        entryOffsetText = ""
        entryUnitText = ""
        entryEditorMode = "add"
        entryEditorDialog.open()
    }

    function openEntryEdit(draftIndex) {
        const entry = profileController.registerEntryAt(draftIndex)
        if (!entry || entry.name === undefined) {
            return
        }
        profileController.clearActionError()
        entryFcText = Number(entry.readFunctionCode)
                          .toString(16).toUpperCase()
        entryAddressText = String(entry.address)
        entryNameText = entry.name
        entryDescriptionText = entry.description
        entryDataTypeIndex = entry.dataType
        entryByteOrderIndex = entry.byteOrder
        entryWordOrderIndex = entry.wordOrder
        entryScaleText = String(entry.scale)
        entryOffsetText = String(entry.offset)
        entryUnitText = entry.unit
        entryEditorMode = draftIndex
        entryEditorDialog.open()
    }

    function entryEditorFields() {
        return {
            "readFunctionCode": entryFcText,
            "address": entryAddressText,
            "name": entryNameText,
            "description": entryDescriptionText,
            "dataType": String(entryDataTypeIndex),
            "byteOrder": String(entryByteOrderIndex),
            // 1-word types keep the disabled control's -1: "not applicable"
            // must never pollute the data (controller keeps the M11 default).
            "wordOrder": entryIsTwoWord ? String(entryWordOrderIndex) : "-1",
            "scale": entryScaleText,
            "offset": entryOffsetText,
            "unit": entryUnitText
        }
    }

    function applyEntryEditor() {
        let ok = false
        if (entryEditorAdding) {
            ok = profileController.addRegisterEntry(entryEditorFields())
        } else {
            ok = profileController.editRegisterEntry(
                     Number(entryEditorMode), entryEditorFields())
        }
        if (ok) {
            entryEditorDialog.close()
            entryEditorMode = ""
        }
        // A refused apply keeps the dialog and EVERY typed value visible;
        // the error label shows the controller's human-readable reason.
    }

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
            id: profileWorkspaceRow
            objectName: "profileWorkspaceRow"
            Layout.fillWidth: true
            // VERTICAL BUDGET (M12-C C1b · Session J). The three Device Profile
            // columns keep a bounded, scrollable band instead of owning every
            // remaining pixel: as a fillHeight row they swallowed the page and
            // left the Manual Import reading area only its content minimum
            // (measured at 1000x700: row 400px vs preview viewport 36px, i.e.
            // 2-3 visible lines of a long manual). Nothing is lost — the
            // catalog and the register map already scroll inside their own
            // Flickable — and the freed height now belongs to the document
            // viewport, which is what grows when the window grows.
            Layout.fillHeight: false
            Layout.preferredHeight: Math.round(
                Math.min(deviceProfileRoot.height * 0.42, 460))
            Layout.minimumHeight: 180
            spacing: DS.spacingM

            // ---- left: managed catalog ----
            PanelCard {
                objectName: "profileCatalogCard"
                Layout.fillHeight: true
                Layout.preferredWidth: 250

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
                }
            }

            // ---- middle: identity editor ----
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

            // ---- right: Register Map (M12-B second slice) ----
            PanelCard {
                objectName: "profileRegisterCard"
                Layout.fillHeight: true
                Layout.preferredWidth: 330

                SectionHeader {
                    objectName: "profileRegisterHeader"
                    Layout.fillWidth: true
                    title: qsTr("寄存器映射")
                }

                ColumnLayout {
                    spacing: DS.spacingS
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Label {
                        objectName: "profileRegisterEmpty"
                        visible: profileController.registerMap.length === 0
                        text: profileController.hasOpenProfile
                              ? qsTr("尚无寄存器条目。点击「添加」创建第一个条目。")
                              : qsTr("未打开设备档案。")
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        objectName: "profileRegisterActions"
                        spacing: DS.spacingS

                        AppButton {
                            objectName: "profileRegisterAddButton"
                            Accessible.name: qsTr("添加寄存器条目")
                            text: qsTr("添加")
                            enabled: profileController.hasOpenProfile
                            onClicked: deviceProfileRoot.openEntryAdd()
                        }
                        AppButton {
                            objectName: "profileRegisterEditButton"
                            Accessible.name: qsTr("编辑寄存器条目")
                            text: qsTr("编辑")
                            enabled: deviceProfileRoot.selectedRegisterRow
                                     !== null
                            onClicked: deviceProfileRoot.openEntryEdit(
                                           deviceProfileRoot
                                               .selectedRegisterRow.draftIndex)
                        }
                        AppButton {
                            objectName: "profileRegisterDeleteButton"
                            Accessible.name: qsTr("删除寄存器条目")
                            text: qsTr("删除")
                            enabled: deviceProfileRoot.selectedRegisterRow
                                     !== null
                            onClicked: {
                                // Draft-only mutation (T027 §16): the JSON
                                // changes only through Save; Discard restores.
                                profileController.removeRegisterEntry(
                                    deviceProfileRoot
                                        .selectedRegisterRow.draftIndex)
                                deviceProfileRoot.selectedRegisterIndex = -1
                            }
                        }
                    }

                    Label {
                        objectName: "profileRegisterHint"
                        // Address authority reminder (T027 §5): the PDU /
                        // 0-based integer is the ONLY truth; no 40001 alias.
                        text: qsTr("地址为 PDU / 0-based（十进制输入）")
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        Layout.fillWidth: true
                    }

                    Flickable {
                        id: profileRegisterList
                        objectName: "profileRegisterList"
                        Accessible.name: qsTr("寄存器映射列表")
                        visible: profileController.registerMap.length > 0
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: width
                        contentHeight: profileRegisterColumn.height

                        ColumnLayout {
                            id: profileRegisterColumn
                            width: profileRegisterList.width
                            spacing: DS.spacingXS

                            Repeater {
                                id: registerMapRepeater
                                model: profileController.registerMap

                                delegate: ItemDelegate {
                                    id: registerDelegate
                                    objectName: "profileRegisterRow"

                                    readonly property var rowData:
                                        modelData !== undefined ? modelData : {}

                                    width: profileRegisterList.width
                                    highlighted:
                                        deviceProfileRoot
                                            .selectedRegisterIndex === index
                                    onClicked:
                                        deviceProfileRoot
                                            .selectedRegisterIndex = index

                                    contentItem: RowLayout {
                                        spacing: DS.spacingS

                                        Label {
                                            // FC + PDU address summary.
                                            text: registerDelegate
                                                  .rowData.fcLabel + " @"
                                                  + registerDelegate
                                                  .rowData.address
                                            color: DS.textSecondary
                                            font.pixelSize: DS.fontCaption
                                            Layout.preferredWidth: 96
                                        }
                                        Label {
                                            text: registerDelegate
                                                      .rowData.name
                                            color: DS.textPrimary
                                            font.pixelSize: DS.fontBody
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Label {
                                            // dataType + derived span.
                                            text: registerDelegate
                                                  .rowData.dataType + " "
                                                  + registerDelegate
                                                  .rowData.span
                                            color: DS.textSecondary
                                            font.pixelSize: DS.fontCaption
                                            Layout.preferredWidth: 110
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

    // ---- M12-C C1a: Manual Import (deterministic TXT / Markdown) ----
    // A separate, bounded area of the SAME Device Profile workspace. It is
    // deterministic by contract: no AI, no cloud, no credential, no
    // Candidate, no Accept/Edit/Reject and no Q&A. PDF / DOCX are C1b
    // (deferred, still in M12-C scope) and OCR is a later capability.
    // A plain Item wrapper keeps the area out of the page's implicit-size
    // propagation: an unconstrained PanelCard implicit width/height would
    // otherwise inflate this ColumnLayout and push the profile workspace out
    // of the 1000x700 window. The Item's implicit size is 0, so the layout
    // only ever sees the explicit preferredHeight below.
    Item {
        id: manualImportHost
        objectName: "manualImportHost"
        Layout.fillWidth: true
        Layout.preferredWidth: 600
        Layout.minimumWidth: 240
        // CONTENT-DRIVEN height. PanelCard's implicit height is derived from its
        // inner layout's implicit size plus padding, so the area can never be
        // handed less vertical space than its own content requires. The previous
        // fixed 160px was smaller than that content minimum under the REAL
        // windows font metrics, so the list and the preview spilled below the
        // window edge at 1000x700 (a defect the offscreen gate could not see).
        Layout.preferredHeight: manualImportCardItem.implicitHeight
        Layout.minimumHeight: 120
        // Session J: the Manual Import area is the long-document reading area,
        // so it takes the vertical space the profile row no longer claims. The
        // content-driven preferredHeight above stays as the stretch basis/floor
        // (the card can never be handed less than its own content needs).
        Layout.fillHeight: true
        clip: true

        PanelCard {
            id: manualImportCardItem
            objectName: "manualImportCard"
            anchors.fill: parent

        SectionHeader {
            objectName: "manualImportHeader"
            Layout.fillWidth: true
            title: qsTr("说明书导入（Manual Import）")
        }

        RowLayout {
            objectName: "manualImportActions"
            spacing: DS.spacingS
            Layout.fillWidth: true

            AppButton {
                objectName: "manualImportButton"
                Accessible.name: qsTr("导入说明书")
                text: qsTr("导入说明书…")
                onClicked: manualFileDialog.open()
            }
            Label {
                objectName: "manualImportCount"
                text: qsTr("已导入 %1 份")
                          .arg(manualController.manualDocuments.length)
                color: DS.textSecondary
                font.pixelSize: DS.fontCaption
            }
            Label {
                objectName: "manualImportScope"
                Layout.fillWidth: true
                text: manualController.scopeText
                color: DS.textSecondary
                font.pixelSize: DS.fontCaption
                elide: Text.ElideRight
            }
        }

        Label {
            objectName: "manualImportError"
            visible: manualController.lastErrorText !== ""
            text: manualController.lastErrorText
            color: DS.error
            font.pixelSize: DS.fontCaption
            Layout.fillWidth: true
            elide: Text.ElideRight
        }

        RowLayout {
            objectName: "manualImportBody"
            spacing: DS.spacingS
            Layout.fillWidth: true
            Layout.fillHeight: true
            // The body is the part that actually needs room: without a floor
            // the preview viewport collapses to 0 height inside the bounded
            // card (measured: manualPreview h=0 at 1000x700).
            Layout.minimumHeight: 60

            // ---- left: imported document list ----
            Flickable {
                id: manualDocumentList
                objectName: "manualDocumentList"
                Accessible.name: qsTr("已导入说明书列表")
                Layout.fillHeight: true
                Layout.preferredWidth: 250
                clip: true
                contentWidth: width
                contentHeight: manualDocumentColumn.height

                ColumnLayout {
                    id: manualDocumentColumn
                    width: manualDocumentList.width
                    spacing: DS.spacingXS

                    Label {
                        objectName: "manualDocumentEmpty"
                        visible: manualController.manualDocuments.length === 0
                        text: qsTr("尚无说明书。点击「导入说明书…」导入 TXT / Markdown。")
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: manualController.manualDocuments

                        delegate: ItemDelegate {
                            id: manualRow
                            objectName: "manualDocumentRow"

                            readonly property var rowData:
                                modelData !== undefined ? modelData : {}

                            width: manualDocumentList.width
                            highlighted: manualController.selectedIndex === index
                            onClicked: manualController.selectDocument(index)

                            contentItem: ColumnLayout {
                                spacing: 0
                                Label {
                                    text: manualRow.rowData.originalFileName
                                          !== undefined
                                          ? manualRow.rowData.originalFileName
                                          : ""
                                    font.pixelSize: DS.fontBody
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: (manualRow.rowData.documentType
                                           !== undefined
                                           ? manualRow.rowData.documentType : "")
                                          + " · "
                                          + (manualRow.rowData.statusToken
                                             !== undefined
                                             ? manualRow.rowData.statusToken
                                             : "")
                                    color: DS.textSecondary
                                    font.pixelSize: DS.fontCaption
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }

            // ---- right: detail + read-only plain-text preview ----
            ColumnLayout {
                objectName: "manualDocColumn"
                Layout.fillWidth: true
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                spacing: 0

                Label {
                    objectName: "manualDocName"
                    text: manualController.selectedDocument.originalFileName
                          !== undefined
                          ? manualController.selectedDocument.originalFileName
                          : qsTr("未选择说明书")
                    font.pixelSize: DS.fontBody
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    objectName: "manualDocHash"
                    // contentHash is provenance of the CONTENT identity: it is
                    // the only key of the managed copy and the text cache.
                    text: manualController.selectedDocument.contentHash
                          !== undefined
                          ? ("contentHash " + manualController
                                                  .selectedDocument.contentHash)
                          : ""
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Flickable {
                    id: manualPreview
                    objectName: "manualPreview"
                    Accessible.name: qsTr("说明书纯文本预览")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 36
                    clip: true
                    contentWidth: width
                    contentHeight: manualPreviewText.height

                    Text {
                        id: manualPreviewText
                        objectName: "manualPreviewText"
                        width: manualPreview.width
                        // PLAIN TEXT on purpose: Markdown is imported as
                        // deterministic source text — no HTML, no script, no
                        // remote resource is ever resolved or rendered.
                        textFormat: Text.PlainText
                        // M12-C C1b second slice (T027 §60.4): a no-text PDF /
                        // DOCX shows an EXPLICIT state — never a blank preview
                        // that would fake an ordinary extraction success.
                        text: manualController.previewStateToken
                                  === "no_extractable_text"
                                  ? qsTr("未发现可提取文本层（不做 OCR，不猜测文本）。")
                                  : manualController.previewText
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                        wrapMode: Text.Wrap
                    }
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

    // ---- Register Entry editor (Add / Edit share one bounded dialog) ----
    // The dialog is the temporary entry draft holder; Apply goes through the
    // controller's candidate + full-validation discipline, so a refused apply
    // leaves the profile draft untouched and keeps every typed value.
    Dialog {
        id: entryEditorDialog
        objectName: "entryEditorDialog"
        modal: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        width: 480
        title: deviceProfileRoot.entryEditorAdding
               ? qsTr("添加寄存器条目") : qsTr("编辑寄存器条目")

        onClosed: {
            if (deviceProfileRoot.entryEditorMode !== "")
                deviceProfileRoot.entryEditorMode = ""
        }

        ColumnLayout {
            width: parent.width
            spacing: DS.spacingS

            GridLayout {
                columns: 2
                columnSpacing: DS.spacingM
                rowSpacing: DS.spacingS
                Layout.fillWidth: true

                Label {
                    text: qsTr("读取功能码：")
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                }
                TextField {
                    objectName: "regFcField"
                    Accessible.name: qsTr("读取功能码")
                    Layout.fillWidth: true
                    implicitHeight: 24
                    // REQUIRED, keyboard-editable, accepts the M10 input
                    // conventions (03 / 41 / 0x41); starts EMPTY for Add —
                    // never a silent FC03 (T027 §32/§35).
                    placeholderText: qsTr("03 / 41 / 0x41（必填）")
                    text: deviceProfileRoot.entryFcText
                    onTextChanged: deviceProfileRoot.entryFcText = text
                }

                Label {
                    text: qsTr("寄存器地址：")
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                }
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    DecimalField {
                        objectName: "regAddressField"
                        accessibleName: qsTr("寄存器地址")
                        Layout.fillWidth: true
                        implicitHeight: 24
                        fieldLabel: qsTr("寄存器地址（PDU / 0-based）")
                        placeholderText: "0..65535"
                        text: deviceProfileRoot.entryAddressText
                        onTextChanged:
                            deviceProfileRoot.entryAddressText = text
                    }
                    Label {
                        // Display-only reminder of the address authority.
                        text: qsTr("PDU / 0-based，十进制（无 40001 别名）")
                        color: DS.textSecondary
                        font.pixelSize: 10
                    }
                }

                Label {
                    text: qsTr("名称：")
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                }
                TextField {
                    objectName: "regNameField"
                    Accessible.name: qsTr("寄存器名称")
                    Layout.fillWidth: true
                    implicitHeight: 24
                    text: deviceProfileRoot.entryNameText
                    onTextChanged: deviceProfileRoot.entryNameText = text
                }

                Label {
                    text: qsTr("描述：")
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                }
                TextField {
                    objectName: "regDescField"
                    Accessible.name: qsTr("寄存器描述")
                    Layout.fillWidth: true
                    implicitHeight: 24
                    text: deviceProfileRoot.entryDescriptionText
                    onTextChanged:
                        deviceProfileRoot.entryDescriptionText = text
                }

                Label {
                    text: qsTr("数据类型：")
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                }
                ComboBox {
                    id: regDataTypeCombo
                    objectName: "regDataTypeCombo"
                    Accessible.name: qsTr("数据类型")
                    Layout.fillWidth: true
                    model: deviceProfileRoot.dataTypeChoices
                    currentIndex: deviceProfileRoot.entryDataTypeIndex
                    onActivated: deviceProfileRoot.entryDataTypeIndex =
                                     currentIndex
                    // setCurrentIndex from C++ (the automated gate) fires
                    // currentIndexChanged but never activated.
                    onCurrentIndexChanged:
                        deviceProfileRoot.entryDataTypeIndex = currentIndex
                }

                Label {
                    text: qsTr("寄存器数量：")
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                }
                TextField {
                    objectName: "regCountField"
                    Accessible.name: qsTr("寄存器数量")
                    Layout.fillWidth: true
                    implicitHeight: 24
                    // Group 1-B: DERIVED from dataType, read-only; the schema
                    // validation keeps checking it strictly.
                    readOnly: true
                    text: String(deviceProfileRoot.entryDerivedRegisterCount)
                }

                Label {
                    text: qsTr("字节序：")
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                }
                ComboBox {
                    id: regByteOrderCombo
                    objectName: "regByteOrderCombo"
                    Accessible.name: qsTr("字节序")
                    Layout.fillWidth: true
                    model: deviceProfileRoot.byteOrderChoices
                    currentIndex: deviceProfileRoot.entryByteOrderIndex
                    onActivated: deviceProfileRoot.entryByteOrderIndex =
                                     currentIndex
                    onCurrentIndexChanged:
                        deviceProfileRoot.entryByteOrderIndex = currentIndex
                }

                Label {
                    text: qsTr("字序：")
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                }
                RowLayout {
                    spacing: DS.spacingS
                    Layout.fillWidth: true
                    ComboBox {
                        id: regWordOrderCombo
                        objectName: "regWordOrderCombo"
                        Accessible.name: qsTr("字序")
                        Layout.fillWidth: true
                        model: deviceProfileRoot.wordOrderChoices
                        // Group 1-C: SHOWN but disabled for 1-word types —
                        // never hidden, never removed from persistence.
                        enabled: deviceProfileRoot.entryIsTwoWord
                        currentIndex: deviceProfileRoot.entryWordOrderIndex
                        opacity: enabled ? 1.0 : 0.5
                        onActivated:
                            deviceProfileRoot.entryWordOrderIndex = currentIndex
                        onCurrentIndexChanged:
                            deviceProfileRoot.entryWordOrderIndex = currentIndex
                    }
                    Label {
                        objectName: "regWordOrderNaLabel"
                        // The Human-visible "not applicable" expression.
                        visible: !deviceProfileRoot.entryIsTwoWord
                        text: qsTr("不适用（1-word 类型）")
                        color: DS.textSecondary
                        font.pixelSize: DS.fontCaption
                    }
                }

                Label {
                    text: qsTr("缩放 scale：")
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                }
                TextField {
                    objectName: "regScaleField"
                    Accessible.name: qsTr("缩放系数")
                    Layout.fillWidth: true
                    implicitHeight: 24
                    placeholderText: qsTr("默认 1；0 合法")
                    text: deviceProfileRoot.entryScaleText
                    onTextChanged: deviceProfileRoot.entryScaleText = text
                }

                Label {
                    text: qsTr("偏移 offset：")
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                }
                TextField {
                    objectName: "regOffsetField"
                    Accessible.name: qsTr("偏移量")
                    Layout.fillWidth: true
                    implicitHeight: 24
                    placeholderText: qsTr("默认 0")
                    text: deviceProfileRoot.entryOffsetText
                    onTextChanged: deviceProfileRoot.entryOffsetText = text
                }

                Label {
                    text: qsTr("单位 unit：")
                    color: DS.textSecondary
                    font.pixelSize: DS.fontCaption
                }
                TextField {
                    objectName: "regUnitField"
                    Accessible.name: qsTr("单位")
                    Layout.fillWidth: true
                    implicitHeight: 24
                    placeholderText: qsTr("自由文本，如 Hz")
                    text: deviceProfileRoot.entryUnitText
                    onTextChanged: deviceProfileRoot.entryUnitText = text
                }
            }

            Label {
                objectName: "regEditorErrorLabel"
                // Human-readable rejection reason (T027 §24): the controller
                // produces the text; the stable token stays in
                // lastActionErrorToken for the tests.
                visible: profileController.lastActionError !== ""
                text: profileController.lastActionError
                color: DS.error
                font.pixelSize: DS.fontCaption
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }

        footer: DialogButtonBox {
            AppButton {
                objectName: "regApplyButton"
                Accessible.name: qsTr("应用寄存器条目")
                text: qsTr("应用")
                onClicked: deviceProfileRoot.applyEntryEditor()
            }
            AppButton {
                objectName: "regCancelButton"
                Accessible.name: qsTr("取消编辑寄存器条目")
                text: qsTr("取消")
                onClicked: {
                    deviceProfileRoot.entryEditorMode = ""
                    entryEditorDialog.close()
                }
            }
        }
    }

    // M12-C C1a: the file picker reuses the shipped QtQuick.Dialogs pattern.
    // The name filters only HELP the Human — the controller/store layer
    // validates the type independently, so a test never relies on them.
    FileDialog {
        id: manualFileDialog
        objectName: "manualFileDialog"
        // M12-C C1b second slice (T027 §60.1): one dialog, more selectable
        // types; the extension only routes to an extractor family — the
        // content is still strictly validated before anything is stored.
        title: qsTr("导入说明书（TXT / Markdown / PDF / DOCX）")
        nameFilters: [
            qsTr("文本与 Markdown (*.txt *.md *.markdown)"),
            qsTr("PDF 文档 (*.pdf)"),
            qsTr("Word 文档 (*.docx)"),
            qsTr("所有文件 (*)")
        ]
        onAccepted: manualController.importManualFile(selectedFile)
    }
}
