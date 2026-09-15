import QtQuick
import ModbusLens

// M9-A PanelCard: generic visual container (surface + border + radius +
// padding). Knows NOTHING about Statistics / Serial / Diagnosis business.
Rectangle {
    id: card

    property real padding: DS.spacingM
    property bool toned: false // subtle surfaceAlt fill vs plain surface

    color: toned ? DS.surfaceAlt : DS.surface
    border.color: DS.border
    border.width: 1
    radius: DS.radiusM
}