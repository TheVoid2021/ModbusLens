#include "core/replay/ReplayLog.h"

#include <charconv>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace modbuslens::core {

namespace {

// --- small text helpers; nothing here is a public parser framework ---

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

// Splits on '|' and KEEPS empty fields: a record shape error must be
// detectable, not silently re-aligned.
std::vector<std::string_view> splitFields(std::string_view line)
{
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    for (;;) {
        const auto pos = line.find('|', start);
        if (pos == std::string_view::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return fields;
}

// Whole-text decimal/hex integer parse via std::from_chars: rejects signs
// (unsigned types), leading whitespace, trailing garbage and overflow by
// construction — never atoi(). Returns false unless EVERY character was
// consumed as one value.
template <typename Int>
bool parseInteger(std::string_view text, int base, Int& out)
{
    const auto [ptr, ec] =
        std::from_chars(text.data(), text.data() + text.size(), out, base);
    return ec == std::errc{} && ptr == text.data() + text.size();
}

// One whitespace-separated 2-hex-digit-per-byte field ("01 03 C4 0B").
// Nullopt on any malformed token; no CRC / length judgment here.
std::optional<std::vector<std::uint8_t>> parseHexWire(std::string_view field)
{
    std::vector<std::uint8_t> bytes;
    std::size_t pos = 0;
    while (pos < field.size()) {
        while (pos < field.size() && isSpace(field[pos])) {
            ++pos;
        }
        if (pos == field.size()) {
            break;
        }
        const auto tokenBegin = pos;
        while (pos < field.size() && !isSpace(field[pos])) {
            ++pos;
        }
        const auto token = field.substr(tokenBegin, pos - tokenBegin);
        if (token.size() != 2) {
            return std::nullopt;
        }
        unsigned int value = 0;
        if (!parseInteger(token, 16, value) || value > 0xFF) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
    return bytes;
}

bool fitsMilliseconds(std::uint64_t value)
{
    return value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
}

} // namespace

ReplayParseResult parseReplayLog(std::string_view text)
{
    ReplayLog log;
    bool headerSeen = false;
    std::size_t lineNumber = 1;
    std::size_t pos = 0;

    const auto fail = [](ReplayParseErrorCode code, std::size_t line) {
        return ReplayParseResult{ReplayParseError{code, line}};
    };

    // Iterate physical lines; blank/comment lines only bump the counter, so
    // reported lineNumbers always refer to the real file position.
    while (pos < text.size()) {
        const auto lineEnd = text.find('\n', pos);
        const auto lineBodyEnd = (lineEnd == std::string_view::npos) ? text.size() : lineEnd;
        const auto line = trim(text.substr(pos, lineBodyEnd - pos));
        pos = (lineEnd == std::string_view::npos) ? text.size() : lineEnd + 1;
        const auto currentLine = lineNumber++;

        if (line.empty() || line.front() == '#') {
            continue;
        }

        if (!headerSeen) {
            const auto fields = splitFields(line);
            // Wrong shape or wrong format ID here means the file does not
            // provide a header at all (recorded rule, see T009 doc §Header).
            if (fields.size() != 3 || fields[0] != "MODBUSLENS_MLOG") {
                return fail(ReplayParseErrorCode::MissingHeader, currentLine);
            }
            std::uint64_t version = 0;
            if (!parseInteger(fields[1], 10, version)) {
                return fail(ReplayParseErrorCode::InvalidHeader, currentLine);
            }
            if (version != 1) {
                return fail(ReplayParseErrorCode::UnsupportedVersion, currentLine);
            }
            constexpr std::string_view kTimeoutKey = "timeout_ms=";
            if (!fields[2].starts_with(kTimeoutKey)) {
                return fail(ReplayParseErrorCode::InvalidHeader, currentLine);
            }
            std::uint64_t timeout = 0;
            if (!parseInteger(fields[2].substr(kTimeoutKey.size()), 10, timeout)
                || timeout == 0 || !fitsMilliseconds(timeout)) {
                return fail(ReplayParseErrorCode::InvalidHeader, currentLine);
            }
            log.timeoutThreshold = std::chrono::milliseconds{static_cast<std::int64_t>(timeout)};
            headerSeen = true;
            continue;
        }

        const auto fields = splitFields(line);
        if (fields.size() != 4 || fields[0] != "TXN") {
            return fail(ReplayParseErrorCode::InvalidRecord, currentLine);
        }

        std::uint64_t elapsed = 0;
        if (!parseInteger(fields[1], 10, elapsed) || !fitsMilliseconds(elapsed)) {
            return fail(ReplayParseErrorCode::InvalidElapsed, currentLine);
        }

        const auto requestField = trim(fields[2]);
        if (requestField.empty()) {
            return fail(ReplayParseErrorCode::MissingRequest, currentLine);
        }
        auto requestWire = parseHexWire(requestField);
        if (!requestWire.has_value()) {
            return fail(ReplayParseErrorCode::InvalidHex, currentLine);
        }

        const auto responseField = trim(fields[3]);
        if (responseField.empty()) {
            return fail(ReplayParseErrorCode::InvalidResponseField, currentLine);
        }
        std::optional<std::vector<std::uint8_t>> responseWire;
        if (responseField == "NO_RESPONSE") {
            responseWire = std::nullopt;
        } else {
            auto parsed = parseHexWire(responseField);
            if (!parsed.has_value()) {
                return fail(ReplayParseErrorCode::InvalidHex, currentLine);
            }
            responseWire = std::move(*parsed);
        }

        ReplayTransactionRecord record;
        record.elapsed = std::chrono::milliseconds{static_cast<std::int64_t>(elapsed)};
        record.requestWire = std::move(*requestWire);
        record.responseWire = std::move(responseWire);
        log.transactions.push_back(std::move(record));
    }

    if (!headerSeen) {
        // Nothing meaningful anywhere: no specific offending line exists.
        return fail(ReplayParseErrorCode::MissingHeader, 0);
    }
    return ReplayParseResult{std::move(log)};
}

} // namespace modbuslens::core