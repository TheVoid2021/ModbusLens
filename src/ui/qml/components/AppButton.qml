import QtQuick
import QtQuick.Controls
import ModbusLens

// M9-A AppButton: presentation-only button over Qt Quick Controls Button.
// Tones: "primary" / "secondary". Business semantics (onClicked wiring,
// enabled bindings) stay in the CALL SITE — this component never knows what
// a button does.
Button {
    id: control

    property string tone: "secondary"

    implicitHeight: DS.controlHeight
    leftPadding: DS.controlPadding
    rightPadding: DS.controlPadding

    background: Rectangle {
        radius: DS.radiusS
        color: {
            if (!control.enabled) {
                return DS.disabledBg;
            }
            if (control.down) {
                return control.tone === "primary" ? DS.primaryPressed
                                                 : DS.surfaceAlt;
            }
            if (control.hovered) {
                return control.tone === "primary" ? DS.primaryHover
                                                 : DS.border;
            }
            return control.tone === "primary" ? DS.primary : DS.surfaceAlt;
        }
        border.color: control.tone === "primary" ? "transparent" : DS.border
        border.width: 1
    }

    contentItem: Text {
        text: control.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: DS.fontBody
        color: {
            if (!control.enabled) {
                return DS.disabledText;
            }
            return control.tone === "primary" ? DS.background : DS.textPrimary;
        }
        elide: Text.ElideRight
    }

    // focus visual: keep the platform outline so keyboard navigation and
    // accessibility never regress relative to the plain Button.
    focusPolicy: Qt.StrongFocus
}