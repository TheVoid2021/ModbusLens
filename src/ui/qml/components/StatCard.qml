import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// M9-A StatCard: presentation-only statistics tile. All VALUES and their
// formatting come from the caller (Controller bindings / Main.qml) — this
// component never computes statistics, success rates or status semantics.
Rectangle {
    id: card

    property string label
    property string valueText
    // Label accent color (semantic tone). Color is an ACCESSORY channel:
    // the label text always remains visible next to it.
    property color tone: DS.textMuted
    property bool emphasized: true

    // Natural size contract (ISSUE-012): derived from the label column's
    // IMPLICIT size, so the card keeps real geometry even outside a
    // Layout (the failing measurement showed implicit=0x0 when only
    // Layout.preferredWidth/Height carried the size).
    implicitWidth: labelColumn.implicitWidth + 2 * DS.spacingM
    implicitHeight: labelColumn.implicitHeight + 2 * DS.spacingM

    // Layout hints: first-choice sizes when managed by a RowLayout (the
    // statistics call site overrides them per card kind).
    Layout.preferredWidth: 140
    Layout.preferredHeight: 72
    color: DS.cardSurface
    radius: DS.radiusM

    ColumnLayout {
        id: labelColumn
        anchors.centerIn: parent
        spacing: 2

        Label {
            text: card.label
            font.pixelSize: DS.fontCaption
            color: card.tone
        }
        Label {
            text: card.valueText
            font.pixelSize: DS.fontMetric
            font.bold: card.emphasized
            color: DS.textPrimary
        }
    }
}