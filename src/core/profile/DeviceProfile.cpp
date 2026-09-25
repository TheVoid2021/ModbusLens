#include "core/profile/DeviceProfile.h"

#include <cmath>
#include <utility>

namespace modbuslens::core {

namespace {

// Case-exact JSON contract values (T027 §25). These deliberately mirror the
// enum names; the snake_case helpers in RegisterDecode serve other layers.
struct TokenPair {
    std::string_view token;
    int value;
};

constexpr TokenPair kDataTypeTokens[] = {
    {"Hex", static_cast<int>(RegisterDecodeType::Hex)},
    {"Binary", static_cast<int>(RegisterDecodeType::Binary)},
    {"UInt16", static_cast<int>(RegisterDecodeType::UInt16)},
    {"Int16", static_cast<int>(RegisterDecodeType::Int16)},
    {"UInt32", static_cast<int>(RegisterDecodeType::UInt32)},
    {"Int32", static_cast<int>(RegisterDecodeType::Int32)},
    {"Float32", static_cast<int>(RegisterDecodeType::Float32)},
};
constexpr TokenPair kByteOrderTokens[] = {
    {"Normal", static_cast<int>(RegisterByteOrder::Normal)},
    {"ByteSwapped", static_cast<int>(RegisterByteOrder::ByteSwapped)},
};
constexpr TokenPair kWordOrderTokens[] = {
    {"HighWordFirst", static_cast<int>(RegisterWordOrder::HighWordFirst)},
    {"LowWordFirst", static_cast<int>(RegisterWordOrder::LowWordFirst)},
};

std::string_view tokenOf(const TokenPair* pairs, std::size_t count, int value)
{
    for (std::size_t i = 0; i < count; ++i) {
        if (pairs[i].value == value) {
            return pairs[i].token;
        }
    }
    return std::string_view{};
}

bool valueOfToken(const TokenPair* pairs, std::size_t count,
                  std::string_view token, int& out)
{
    for (std::size_t i = 0; i < count; ++i) {
        if (pairs[i].token == token) {
            out = pairs[i].value;
            return true;
        }
    }
    return false;
}

template <std::size_t N>
std::string_view tokenOfArray(const TokenPair (&pairs)[N], int value)
{
    return tokenOf(pairs, N, value);
}

template <std::size_t N>
bool valueOfTokenArray(const TokenPair (&pairs)[N], std::string_view token,
                       int& out)
{
    return valueOfToken(pairs, N, token, out);
}

} // namespace

std::string_view profileDataTypeToken(RegisterDecodeType type)
{
    return tokenOfArray(kDataTypeTokens, static_cast<int>(type));
}

std::string_view profileByteOrderToken(RegisterByteOrder byteOrder)
{
    return tokenOfArray(kByteOrderTokens, static_cast<int>(byteOrder));
}

std::string_view profileWordOrderToken(RegisterWordOrder wordOrder)
{
    return tokenOfArray(kWordOrderTokens, static_cast<int>(wordOrder));
}

bool profileDataTypeFromToken(std::string_view token, RegisterDecodeType& out)
{
    int value = 0;
    if (!valueOfTokenArray(kDataTypeTokens, token, value)) {
        return false;
    }
    out = static_cast<RegisterDecodeType>(value);
    return true;
}

bool profileByteOrderFromToken(std::string_view token, RegisterByteOrder& out)
{
    int value = 0;
    if (!valueOfTokenArray(kByteOrderTokens, token, value)) {
        return false;
    }
    out = static_cast<RegisterByteOrder>(value);
    return true;
}

bool profileWordOrderFromToken(std::string_view token, RegisterWordOrder& out)
{
    int value = 0;
    if (!valueOfTokenArray(kWordOrderTokens, token, value)) {
        return false;
    }
    out = static_cast<RegisterWordOrder>(value);
    return true;
}

std::string_view profileValidationCodeName(ProfileValidationCode code)
{
    switch (code) {
    case ProfileValidationCode::Ok:
        return "ok";
    case ProfileValidationCode::ProfileIdMissing:
        return "profile_id_missing";
    case ProfileValidationCode::DisplayNameMissing:
        return "display_name_missing";
    case ProfileValidationCode::UnsupportedSchemaVersion:
        return "unsupported_schema_version";
    case ProfileValidationCode::RegisterNameMissing:
        return "register_name_missing";
    case ProfileValidationCode::UnsupportedDataType:
        return "unsupported_data_type";
    case ProfileValidationCode::RegisterCountMismatch:
        return "register_count_mismatch";
    case ProfileValidationCode::AddressOutOfRange:
        return "address_out_of_range";
    case ProfileValidationCode::SpanOutOfRange:
        return "span_out_of_range";
    case ProfileValidationCode::DuplicateAddress:
        return "duplicate_address";
    case ProfileValidationCode::OverlappingSpan:
        return "overlapping_span";
    case ProfileValidationCode::InvalidByteOrder:
        return "invalid_byte_order";
    case ProfileValidationCode::InvalidWordOrder:
        return "invalid_word_order";
    case ProfileValidationCode::NonFiniteScale:
        return "non_finite_scale";
    case ProfileValidationCode::NonFiniteOffset:
        return "non_finite_offset";
    }
    // Defensive: an unknown enum value gets the generic fallback token rather
    // than a fabricated meaning (same discipline as RegisterDecodeStatus).
    return "unsupported_schema_version";
}

ProfileValidationResult validateDeviceProfile(const DeviceProfile& profile)
{
    if (profile.profileId.empty()) {
        return {ProfileValidationCode::ProfileIdMissing, -1};
    }
    if (profile.displayName.empty()) {
        return {ProfileValidationCode::DisplayNameMissing, -1};
    }
    if (profile.schemaVersion != 1) {
        return {ProfileValidationCode::UnsupportedSchemaVersion, -1};
    }

    // Placed spans of the entries already accepted, as closed intervals
    // [start, last]. Sizes are tiny (registerCount is validated to 1 or 2
    // BEFORE this loop reaches the span checks), so int arithmetic cannot
    // overflow here.
    std::vector<std::pair<int, int>> placedSpans;
    placedSpans.reserve(profile.registers.size());
    for (std::size_t i = 0; i < profile.registers.size(); ++i) {
        const RegisterEntry& entry = profile.registers[i];
        const int index = static_cast<int>(i);

        if (entry.name.empty()) {
            return {ProfileValidationCode::RegisterNameMissing, index};
        }
        // RegisterCount must equal the type's word count — never auto-fixed.
        const int requiredWords = registerDecodeTypeWordCount(entry.dataType);
        if (requiredWords == 0) {
            return {ProfileValidationCode::UnsupportedDataType, index};
        }
        if (entry.registerCount != requiredWords) {
            return {ProfileValidationCode::RegisterCountMismatch, index};
        }
        // Enum values outside the M11 matrices (a programmatic caller bug) are
        // rejected here; JSON parsing rejects unknown tokens earlier.
        if (entry.byteOrder != RegisterByteOrder::Normal
            && entry.byteOrder != RegisterByteOrder::ByteSwapped) {
            return {ProfileValidationCode::InvalidByteOrder, index};
        }
        if (entry.wordOrder != RegisterWordOrder::HighWordFirst
            && entry.wordOrder != RegisterWordOrder::LowWordFirst) {
            return {ProfileValidationCode::InvalidWordOrder, index};
        }
        // Span: the last covered register must stay inside PDU 0..65535.
        // address is uint16_t (always >= 0), so only the top edge can fail.
        const int firstAddress = static_cast<int>(entry.address);
        const int lastAddress = firstAddress + entry.registerCount - 1;
        if (lastAddress > 65535) {
            return {ProfileValidationCode::SpanOutOfRange, index};
        }
        // T027 29 (HUMAN-APPROVED): every cross-entry address-range overlap is
        // forbidden - ONE PDU ADDRESS BELONGS TO AT MOST ONE ENTRY SPAN. The
        // exact-start case keeps its own token; intersecting (but differently
        // started) spans report OverlappingSpan. No priority / alias / winner
        // resolution exists.
        for (const auto& placed : placedSpans) {
            if (placed.first == firstAddress) {
                return {ProfileValidationCode::DuplicateAddress, index};
            }
            if (firstAddress <= placed.second && placed.first <= lastAddress) {
                return {ProfileValidationCode::OverlappingSpan, index};
            }
        }
        placedSpans.emplace_back(firstAddress, lastAddress);
        if (!std::isfinite(entry.scale)) {
            return {ProfileValidationCode::NonFiniteScale, index};
        }
        if (!std::isfinite(entry.offset)) {
            return {ProfileValidationCode::NonFiniteOffset, index};
        }
    }
    return {ProfileValidationCode::Ok, -1};
}

double profileSemanticValue(double decodedValue, double scale, double offset)
{
    // Frozen order (T027 §24.3): multiply, then add. Natural IEEE arithmetic
    // keeps NaN / +-Inf special values special — never disguised as ordinary
    // physical numbers.
    return decodedValue * scale + offset;
}

std::string_view profileLookupStatusName(ProfileLookupStatus status)
{
    switch (status) {
    case ProfileLookupStatus::Found:
        return "found";
    case ProfileLookupStatus::NotFound:
        return "not_found";
    case ProfileLookupStatus::InvalidProfile:
        return "invalid_profile";
    case ProfileLookupStatus::Ambiguous:
        return "ambiguous";
    }
    return "not_found";
}

namespace {

ProfileLookupResult lookupInValidatedProfile(const DeviceProfile& profile,
                                             std::uint16_t address,
                                             bool covering)
{
    ProfileLookupResult result;
    for (std::size_t i = 0; i < profile.registers.size(); ++i) {
        const RegisterEntry& entry = profile.registers[i];
        const int start = static_cast<int>(entry.address);
        const int last = start + entry.registerCount - 1;
        const int target = static_cast<int>(address);
        const bool hit = covering ? (target >= start && target <= last)
                                  : (target == start);
        if (!hit) {
            continue;
        }
        if (result.status == ProfileLookupStatus::Found) {
            // Defensive only: a validated v1 profile cannot reach this (T027
            // 29 forbids every overlap); kept so a caller that skips
            // validation can never receive a silently picked winner.
            result.status = ProfileLookupStatus::Ambiguous;
            result.entryIndex = -1;
            result.offsetWithinSpan = 0;
            return result;
        }
        result.status = ProfileLookupStatus::Found;
        result.entryIndex = static_cast<int>(i);
        result.offsetWithinSpan = target - start;
    }
    return result;
}

} // namespace

ProfileLookupResult findProfileEntryByStartAddress(const DeviceProfile& profile,
                                                   std::uint16_t address)
{
    if (!validateDeviceProfile(profile).ok()) {
        ProfileLookupResult result;
        result.status = ProfileLookupStatus::InvalidProfile;
        return result;
    }
    return lookupInValidatedProfile(profile, address, /*covering=*/false);
}

ProfileLookupResult findProfileEntryCoveringAddress(const DeviceProfile& profile,
                                                    std::uint16_t address)
{
    if (!validateDeviceProfile(profile).ok()) {
        ProfileLookupResult result;
        result.status = ProfileLookupStatus::InvalidProfile;
        return result;
    }
    return lookupInValidatedProfile(profile, address, /*covering=*/true);
}

std::string_view profileSemanticClassName(ProfileSemanticClass valueClass)
{
    switch (valueClass) {
    case ProfileSemanticClass::Finite:
        return "finite";
    case ProfileSemanticClass::NotANumber:
        return "not_a_number";
    case ProfileSemanticClass::PositiveInfinity:
        return "positive_infinity";
    case ProfileSemanticClass::NegativeInfinity:
        return "negative_infinity";
    }
    return "not_a_number";
}

ProfileSemanticClass profileSemanticClassOf(double value)
{
    if (std::isnan(value)) {
        return ProfileSemanticClass::NotANumber;
    }
    if (std::isinf(value)) {
        return value > 0.0 ? ProfileSemanticClass::PositiveInfinity
                           : ProfileSemanticClass::NegativeInfinity;
    }
    return ProfileSemanticClass::Finite;
}

ProfileSemanticProjection projectProfileSemanticValue(
    const RegisterEntry& entry, double decodedValue)
{
    // Pure projection over verified metadata: the frozen formula, then a
    // NUMERIC class (never derived back from formatted text), plus the unit
    // verbatim. No wire, no decode, no M11 call, no mutation of either input.
    ProfileSemanticProjection projection;
    projection.semanticValue =
        profileSemanticValue(decodedValue, entry.scale, entry.offset);
    projection.valueClass = profileSemanticClassOf(projection.semanticValue);
    projection.unit = entry.unit;
    return projection;
}

} // namespace modbuslens::core