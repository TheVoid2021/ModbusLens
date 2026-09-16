import QtQuick
import ModbusLens

// M9-B5 Diagnosis workspace page — B5.1 SHELL ONLY (T017 §43.23/§43.13).
//
// Page-root pattern (B1–B4 precedent): a plain Item that the StackLayout
// owns and sizes; the page will do its own margins INSIDE once content
// arrives.
//
// Deliberately EMPTY in B5.1: the Diagnosis workflow (诊断 TabBar, baseline
// run/clear, AI controls, Agent draft/controls, bounded Flickables) still
// lives in the Legacy workbench and migrates here ATOMICALLY WITH the
// 诊断 navigation enablement in B5.2 (T017 §38.31.1 lesson — committing
// the move while this page is unreachable would temporarily remove a
// user capability).
//
// Ownership (consistent with B2–B4): the AnalysisController is injected
// by the shell; this page will never create one, copy baseline/AI/Agent
// facts, or touch the source/revision authority. B5.1 has zero bindings,
// zero commands and zero business side effects.
Item {
    id: page

    required property var analysisController
}