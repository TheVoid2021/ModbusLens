#pragma once

#include <QString>

#include "core/diagnosis/DiagnosisContext.h"
#include "core/diagnosis/RuleBasedDiagnosis.h"

// T011 Part B: deterministic prompt assembly (app layer).
//
// Input is ONLY structured deterministic facts — DiagnosisContext +
// DiagnosisReport. It never reads QML, the transaction model, replay
// filenames/comments, source labels or any free text, so the prompt
// injection surface has no entry point by construction.
struct DiagnosisPrompt {
    QString systemInstructions;
    QString userPrompt;
};

// Deterministic: the same context/report always produce the same strings.
// The full statistics and ALL baseline findings are always included; the
// per-transaction detail is bounded (at most 20, non-Success first in
// original batch order, then Success to fill) with explicit truncation
// accounting. Raw wire bytes never enter the prompt.
DiagnosisPrompt buildDiagnosisPrompt(
    const modbuslens::core::DiagnosisContext& context,
    const modbuslens::core::DiagnosisReport& report);