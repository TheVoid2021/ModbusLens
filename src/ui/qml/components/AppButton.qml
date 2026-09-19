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
        // M9-F F1 correction (P1, focus visibility): the fully custom
        // background meant NO focus rendering existed at all — measured as a
        // zero-pixel diff between unfocused and keyboard-focused states (the
        // comment below used to claim a "platform outline" that a custom
        // background never draws). The border IS the focus channel now:
        // visualFocus is true for keyboard focus only, so selected/hover/
        // pressed states stay exactly what they were.
        border.color: {
            if (control.visualFocus) {
                // on the primary (blue) fill a blue border would vanish, so
                // the primary tone gets the light contrast border
                return control.tone === "primary" ? DS.background : DS.primary;
            }
            return control.tone === "primary" ? "transparent" : DS.border;
        }
        border.width: control.visualFocus ? 2 : 1
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

    // focus policy note (F1 correction): `focusPolicy: StrongFocus` below is
    // the Tab/click policy only; the focus VISUAL lives in the background's
    // border above (bound to visualFocus, i.e. keyboard focus).
    focusPolicy: Qt.StrongFocus
}