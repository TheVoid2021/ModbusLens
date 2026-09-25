#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/analysis/RegisterDecode.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// M12-A first slice: Device Profile domain model (Pure C++20, Zero Qt).
//
// A DeviceProfile is HUMAN-authored / HUMAN-verified configuration metadata
// (T027 §8, layer 3). It can NEVER rewrite the wire truth (Actual TX / RX,
// TransactionAnalysis.values, raw DEC/HEX) nor the M11 generic decode result —
// it only describes what a register means for one device and feeds a future
// derived semantic view. Deleting or losing a profile leaves layers 1-2 fully
// usable.
//
// The register-entry dataType / byteOrder / wordOrder fields REUSE the M11
// enums (RegisterDecodeType / RegisterByteOrder / RegisterWordOrder) — this
// layer never invents a second decoder. The JSON value spellings (T027 §25,
// M12 IMPLEMENTATION CONTRACT DETAIL) are exposed through the token helpers
// below; the existing snake_case helpers in RegisterDecode (status names,
// word-order machine tokens) serve other layers and stay unchanged.
//
// Validation is strict by contract (T027 §26): registerCount must match the
// type's word count (never auto-corrected), duplicate addresses are errors
// (an address-to-entry mapping must stay deterministic in v1), and a
// 2-register span must stay inside the PDU 0..65535 range.
// ---------------------------------------------------------------------------

// JSON contract tokens (frozen in T027 §25). Empty / false on anything outside
// the v1 matrix — the caller must not guess.
[[nodiscard]] std::string_view profileDataTypeToken(RegisterDecodeType type);
[[nodiscard]] std::string_view profileByteOrderToken(RegisterByteOrder byteOrder);
[[nodiscard]] std::string_view profileWordOrderToken(RegisterWordOrder wordOrder);
[[nodiscard]] bool profileDataTypeFromToken(std::string_view token,
                                            RegisterDecodeType& out);
[[nodiscard]] bool profileByteOrderFromToken(std::string_view token,
                                             RegisterByteOrder& out);
[[nodiscard]] bool profileWordOrderFromToken(std::string_view token,
                                             RegisterWordOrder& out);

// One register-map entry (T027 §9). `address` is the PDU / 0-based authority
// address (never a 40001-style presentation alias). scale/offset/unit are the
// D2 fields; the formula is frozen (see profileSemanticValue) but the values
// themselves are pure metadata here.
struct RegisterEntry {
    std::uint16_t address{0};
    std::string name;
    std::string description;
    RegisterDecodeType dataType{RegisterDecodeType::UInt16};
    int registerCount{1};
    RegisterByteOrder byteOrder{RegisterByteOrder::Normal};
    RegisterWordOrder wordOrder{RegisterWordOrder::HighWordFirst};
    double scale{1.0};
    double offset{0.0};
    std::string unit;

    bool operator==(const RegisterEntry&) const = default;
};

struct DeviceProfile {
    int schemaVersion{1};
    std::string profileId;   // REQUIRED; program-generated stable identity
    std::string displayName; // REQUIRED; the human-visible name
    std::string manufacturer; // OPTIONAL
    std::string model;        // OPTIONAL
    std::string revision;     // OPTIONAL
    std::string description;  // OPTIONAL
    std::vector<RegisterEntry> registers;

    bool operator==(const DeviceProfile&) const = default;
};

enum class ProfileValidationCode {
    Ok,
    ProfileIdMissing,           // profileId empty
    DisplayNameMissing,         // displayName empty
    UnsupportedSchemaVersion,   // schemaVersion != 1 (v1 matrix)
    RegisterNameMissing,        // entry name empty
    UnsupportedDataType,        // dataType outside the M11 type matrix
    RegisterCountMismatch,      // registerCount != type word count
    AddressOutOfRange,          // address outside PDU 0..65535
    SpanOutOfRange,             // address + registerCount - 1 > 65535
    DuplicateAddress,           // two entries share one address
    InvalidByteOrder,           // byteOrder outside the M11 enum
    InvalidWordOrder,           // wordOrder outside the M11 enum
    NonFiniteScale,             // scale is NaN / +-Inf
    NonFiniteOffset,            // offset is NaN / +-Inf
};

// Stable machine tokens ("profile_id_missing", ...) — never human UI prose.
[[nodiscard]] std::string_view profileValidationCodeName(ProfileValidationCode code);

struct ProfileValidationResult {
    ProfileValidationCode code{ProfileValidationCode::Ok};
    // The entry that failed (0-based), or -1 for a profile-level failure.
    int registerIndex{-1};

    [[nodiscard]] bool ok() const { return code == ProfileValidationCode::Ok; }
    bool operator==(const ProfileValidationResult&) const = default;
};

// Full v1 validation (T027 §26). Pure: never mutates, never "fixes" input.
[[nodiscard]] ProfileValidationResult validateDeviceProfile(
    const DeviceProfile& profile);

// P0-c frozen formula (T027 §24.3): decodedValue * scale + offset, applied in
// exactly that order. IEEE special values propagate naturally (NaN stays NaN,
// +-Inf stays +-Inf); this helper NEVER turns a special value into an ordinary
// physical number, and it touches no wire / raw / M11 structure — it is a pure
// derived computation.
[[nodiscard]] double profileSemanticValue(double decodedValue, double scale,
                                          double offset);

} // namespace modbuslens::core