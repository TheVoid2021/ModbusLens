import QtQuick
import ModbusLens

// M9-B4 Replay workspace page — B4.1 SHELL ONLY (T017 §38.31.2).
//
// Page-root pattern (B1–B3 precedent): a plain Item that the StackLayout
// owns and sizes; the page will do its own margins INSIDE once content
// arrives.
//
// Deliberately EMPTY in B4.1: the Replay workflow (Load Replay button,
// FileDialog, replay error/notice presentation) still lives in the Legacy
// workbench and migrates here ATOMICALLY WITH the 回放 navigation
// enablement in B4.2 (T017 §38.31.3) — committing the move while this
// page is unreachable would temporarily remove a user capability.
//
// Ownership (consistent with B2/B3): the AnalysisController is injected
// by the shell; this page will never create one, copy business state or
// switch the source by itself. Until B4.2 the page has zero bindings,
// zero commands and zero business side effects.
Item {
    id: page

    required property var analysisController
}