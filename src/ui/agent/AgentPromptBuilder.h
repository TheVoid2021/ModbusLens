#pragma once

#include <QJsonArray>
#include <QString>

// T012 Part B — Agent system instruction + fixed tool schemas.
// Deliberately SEPARATE from the T011 DiagnosisPromptBuilder (whose prompt
// and contract stay untouched): the Agent has a different role — a
// read-only, tool-using diagnostic agent — and its prompt is never merged
// into the one-shot Ask AI path.
namespace modbuslens::agent {

struct AgentPrompt {
    QString systemInstructions;
    QJsonArray toolSchemas; // fixed three read-only tools
};

// Deterministic, tool-call-instruction-free at the user layer: the user
// question is appended at run time as a plain user message. The schema is
// the ONLY capability surface the model ever sees — no injection can add
// tools because C++ owns this array.
AgentPrompt buildAgentPrompt();

} // namespace modbuslens::agent