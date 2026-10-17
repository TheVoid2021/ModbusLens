#pragma once

// T035-C Round C1 — the EngineeringReady readiness contract.
//
// The ONLY trusted run-time configuration is the saved Device Profile (never
// an AI Candidate, a PDF, a prompt, a provider response or a UI default).
// This module answers, deterministically and side-effect-freely:
//
//   · Readable            — the entry carries everything a raw Modbus read
//                           of THIS application's supported read paths needs;
//   · EngineeringReady    — Readable PLUS every condition the selected
//                           engineering decoder needs, with the engineering
//                           fields EXPLICITLY configured (the Round O
//                           provenance flags, never "value != default").
//
// C1 decoder boundary: Raw and Linear only. VariableDecimal and Enum are
// STRUCTURALLY REFUSED (a fixed scale must never impersonate a dynamic
// decimal-point rule) — the refusal is part of the contract, not an
// afterthought.

#include <string>
#include <string_view>
#include <vector>

#include "core/profile/DeviceProfile.h"

namespace modbuslens::core {

// The decoder kind now lives in the data model (DeviceProfile.h: it is a
// PERSISTED RegisterEntry member since T035-C C1.2); this header consumes it.

enum class RegisterReadinessLevel { NotReadable, Readable, EngineeringReady };

[[nodiscard]] std::string_view
registerReadinessLevelToken(RegisterReadinessLevel level) noexcept;

struct RegisterReadinessResult {
    bool readable{false};
    bool engineeringReady{false};
    RegisterReadinessLevel level{RegisterReadinessLevel::NotReadable};
    EngineeringDecoderKind decoderKind{EngineeringDecoderKind::Linear};
    // Stable snake_case field tokens (machine-readable; UI maps to zh).
    std::vector<std::string> missingFields;
    std::vector<std::string> invalidFields;
    // Complete, Human-readable reasons — ONE per problem, never only the
    // first ("缺多个必填项时应完整列出").
    std::vector<std::string> reasons;
};

// T035-C C2: the ONE frozen Linear formula — engineeringValue =
// decodedValue * scale + offset — with honest finiteness gates at every
// step. NO default filling happens here: the caller must already hold the
// explicitly configured scale/offset (the Readiness gate proves it), and a
// NaN/Inf decoded value, a non-finite scale/offset or a non-finite result
// is a REFUSAL, never a displayed number. Deterministic, pure.
struct LinearEngineeringValue {
    bool ok{false};
    double value{0.0};
};

[[nodiscard]] LinearEngineeringValue computeLinearEngineeringValue(
    double decodedValue, double scale, double offset) noexcept;

// Pure, deterministic, side-effect-free: never mutates the inputs, never
// touches the filesystem, the serial port or any provider. Identical inputs
// always yield identical results.
//
// `profile` is part of the frozen C1 signature contract; the C1 decisions
// are per-entry today, so it is passed through but not consulted (a profile
// level check will ride this parameter in a later slice — no second data
// model is created here).
// T035-C C1.2: the decoder kind is read from `entry.decoderKind` — the
// PERSISTED, Human-declared configuration — never from a UI default, a call
// parameter, or any inferred value. Unknown entries can be Readable but are
// never EngineeringReady.
[[nodiscard]] RegisterReadinessResult validateRegisterForEngineeringDisplay(
    [[maybe_unused]] const DeviceProfile &profile, const RegisterEntry &entry);

} // namespace modbuslens::core
