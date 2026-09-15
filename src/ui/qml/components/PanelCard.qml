import QtQuick
import QtQuick.Layouts
import ModbusLens

// M9-A PanelCard: generic visual container (surface + border + radius +
// padding). Knows NOTHING about Statistics / Serial / Diagnosis business.
//
// Size contract (ISSUE-012): the card's implicit size is computed from the
// inner layout's IMPLICIT size — which the layout engine derives from the
// CONTENT's preferred sizes, independent of any anchors — plus the padding.
// Anchoring content to the card (as the previous version relied on) never
// feeds the card's own implicit size, so a parent Layout sized the card to
// 0 heights and the whole section collapsed. Content is placed via
// contentData (children land in the inner layout) and does NOT anchor to
// the card.
Rectangle {
    id: card

    property real padding: DS.spacingM
    property bool toned: false // subtle surfaceAlt fill vs plain surface

    default property alias contentData: contentLayout.data

    implicitWidth: contentLayout.implicitWidth + 2 * padding
    implicitHeight: contentLayout.implicitHeight + 2 * padding

    color: toned ? DS.surfaceAlt : DS.surface
    border.color: DS.border
    border.width: 1
    radius: DS.radiusM

    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: card.padding
        spacing: DS.spacingS
    }
}