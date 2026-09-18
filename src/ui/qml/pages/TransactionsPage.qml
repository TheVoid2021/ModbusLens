import QtQuick
import QtQuick.Layouts
import ModbusLens

// M9-D D1 Transactions workspace page — SHELL ONLY (T019 §D1.5/§D1.2).
//
// Page-root pattern (B1–B5 precedent): a plain Item that the StackLayout
// owns and sizes; the page does its own margins INSIDE. Registered as
// StackLayout child 5 (workspaceTransactionsIndex) so D2 can migrate the
// transaction presentation onto a stable page identity.
//
// Deliberately EMPTY in D1: the transaction table, its header, delegates,
// issue text and empty state still live in the Legacy workbench and move
// here ATOMICALLY WITH the 事务 navigation enablement in D2 (T019 §33 —
// committing a move while this page is unreachable would remove a user
// capability, the §38.31.1 lesson from M9-B). Until then the 事务 rail
// entry stays disabled, this page is unreachable, and it has zero
// bindings, zero commands and zero business side effects.
//
// Ownership (identical to the other pages): the AnalysisController is
// injected by the shell; this page never creates one, never copies
// business state, and never touches the source/revision authority. D1
// adds no transaction content and no placeholder cards — the page is
// unreachable by design, so there is nothing to make "look nice" yet.
Item {
    id: page

    required property var analysisController

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: DS.spacingL
        spacing: DS.spacingM

        SectionHeader {
            objectName: "transactionsPageHeader"
            Layout.fillWidth: true
            title: qsTr("事务")
            subtitle: qsTr("通信记录与事务详情")
        }
    }
}