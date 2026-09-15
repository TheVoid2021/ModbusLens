import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ModbusLens

// M9-A SectionHeader: unified section title hierarchy. First edition keeps
// it deliberately simple: title + optional subtitle. No toolbar/action
// slots until a real need materializes.
RowLayout {
    id: header

    property string title
    property string subtitle

    spacing: DS.spacingS

    Label {
        text: header.title
        font.pixelSize: DS.fontSection
        font.bold: true
        color: DS.textPrimary
    }

    Label {
        visible: header.subtitle !== ""
        text: header.subtitle
        font.pixelSize: DS.fontCaption
        color: DS.textMuted
    }

    Item {
        Layout.fillWidth: true
    }
}