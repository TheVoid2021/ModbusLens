import QtQuick
import QtQuick.Controls
import ModbusLens

// M10-D1 — DecimalField: the PRESENTATION component for a single decimal
// field (0x06 register address / register value).
//
// Authority discipline (frozen in M10-D Phase 1 §S / §T):
//   · this component is presentation ONLY. It holds the RAW text the user
//     typed and hands it over unchanged — it never decides whether the number
//     is legal, never clamps it, never drops a character and never converts it
//     with parseInt/Number;
//   · there is deliberately NO validator: a validator that stops "-1", "12x",
//     "65536" or "" from entering `text` would make QML the acceptance
//     authority and the core would end up judging a string it never saw;
//   · the core decimal parser (parseDecimalRegisterValue) and the controller's
//     typed validation stay the ONLY business authorities. This file knows no
//     protocol range.
//
// What it DOES own: focus presentation (the M9-F focus-ring channel), an
// error visual state, the accessible name and ordinary text-editing ergonomics
// (typing, select-all, copy/paste, delete, Tab/Shift+Tab traversal — a
// single-line TextField moves focus natively, so no key is re-routed here).
Item {
    id: root

    // The component itself carries the accessible name (this is the object the
    // accessibility oracles address); the inner TextField repeats it because
    // that is the element a screen reader actually focuses.
    Accessible.name: root.accessibleName.length > 0 ? root.accessibleName
                                                    : root.fieldLabel

    property alias text: field.text
    property alias placeholderText: field.placeholderText
    property string accessibleName: ""
    property string fieldLabel: ""
    // Presentation-only error state: the controller's projection decides
    // WHICH field is wrong (writeDraftErrorField) and the section wires it in.
    property bool hasError: false

    implicitWidth: 110
    implicitHeight: DS.controlHeight

    TextField {
        id: field
        // Deliberately UNNAMED: the component carries the identity (its
        // objectName), so focus/tab oracles see one control per field — the
        // same convention the surrounding SpinBoxes already follow, where Qt
        // moves focus to an internal editor child.
        anchors.fill: parent
        // No validator on purpose (see the header comment).
        selectByMouse: true
        Accessible.name: root.accessibleName.length > 0 ? root.accessibleName
                                                        : root.fieldLabel
        font.pixelSize: DS.fontBody
        color: DS.textPrimary
        leftPadding: 8
        rightPadding: 8

        background: Rectangle {
            radius: DS.radiusS
            color: DS.surface
            // The border IS the focus/error channel (M9-F F1 convention): a
            // fully custom background would otherwise draw no focus or error
            // indication at all.
            border.color: root.hasError ? DS.error
                                        : (field.activeFocus ? DS.primary : DS.border)
            border.width: (root.hasError || field.activeFocus) ? 2 : 1
        }
    }
}