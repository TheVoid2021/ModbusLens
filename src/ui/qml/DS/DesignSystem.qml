import QtQuick

// ModbusLens M9-A Design System tokens (Phase 2, first edition).
//
// Deliberately NOT a module singleton: qmldir-singleton lookups failed at
// runtime in this exe-attached qrc module (ISSUE-010). The engine
// instantiates this file ONCE in main.cpp and exposes it as the "DS"
// context property, so all QML sees the same token object.
//
// Values deliberately mirror the V1 fixed-light palette hexes (T013 Phase C,
// HUMAN VISUAL REVIEW REQUIRED status carries over): this phase is about
// centralizing the tokens, NOT redefining the visual language. New visual
// language adjustments belong to M9-C after the tokens are consumed.
QtObject {
    id: ds

    // ---- Spacing (limited scale) ----
    readonly property real spacingXS: 4
    readonly property real spacingS: 8
    readonly property real spacingM: 12
    readonly property real spacingL: 16
    readonly property real spacingXL: 24

    // ---- Radius ----
    readonly property real radiusS: 4
    readonly property real radiusM: 6
    readonly property real radiusL: 8

    // ---- Control ----
    readonly property real controlHeight: 34
    readonly property real controlPadding: 12

    // ---- Shell (M9-B1: minimal additions only; navigation visuals belong
    // to M9-C) ----
    readonly property real appBarHeight: 40
    readonly property real compactNavWidth: 56
    // Selected navigation entry surface. Same value as surfaceAlt today;
    // kept as its own token so M9-C can restyle navigation alone.
    readonly property color navigationSelectedSurface: "#F5F7FA"

    // ---- Neutral colors（沿用 V1 fixed-light hexes）----
    readonly property color background: "#FFFFFF"
    readonly property color surface: "#FFFFFF"
    readonly property color surfaceAlt: "#F5F7FA"
    readonly property color cardSurface: "#F4F4F4"
    readonly property color cardSurfaceAlt: "#F0F0F0"
    readonly property color border: "#D8DDE4"
    readonly property color separator: "#EDF0F4"
    readonly property color textPrimary: "#1B1F26"
    readonly property color textSecondary: "#4A5568"
    readonly property color textMuted: "#606060"
    readonly property color disabledBg: "#EDF0F4"
    readonly property color disabledText: "#98A2B3"

    // ---- Brand (deep blue / teal-blue direction) ----
    readonly property color primary: "#2F6FB7"
    readonly property color primaryHover: "#3D7FC4"
    readonly property color primaryPressed: "#24527F"

    // ---- Semantic status colors（颜色只是辅助通道，绝不替代文字）----
    readonly property color success: "#306030"
    readonly property color exception: "#806000"
    readonly property color crcError: "#803030"
    readonly property color timeout: "#604080"
    readonly property color protocolError: "#606060"
    readonly property color expectedNoResponse: "#406060"
    readonly property color pending: "#4A5568"
    readonly property color error: "#B03030"
    readonly property color notice: "#806000"

    // ---- Typography（当前真实需要的层级）----
    readonly property int fontTitle: 24
    readonly property int fontSection: 15
    readonly property int fontBody: 13
    readonly property int fontCaption: 11
    readonly property int fontMetric: 20
}