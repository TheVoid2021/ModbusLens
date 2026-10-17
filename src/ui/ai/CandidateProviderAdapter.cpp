#include "ui/ai/CandidateProviderAdapter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QFile>
#include <QStandardPaths>

#include <charconv>
#include <cstddef>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ui/ai/AiProviderProfile.h"
#include "core/candidate/CandidateExtraction.h"
#include "core/profile/DeviceProfile.h"

namespace modbuslens::ui {
namespace {

using core::CandidateProposal;
using core::ProfileFieldTarget;
using core::ProviderExtractionFailure;
using core::ProviderExtractionResult;

// ---------------------------------------------------------------------------
// Minimal strict JSON reader.
//
// Deliberately hand-written and strict: this is the RESPONSE reader, and it
// must stay network-free (fail-closed parsing is frozen H8 semantics; T034 B1
// moved only the REQUEST-BODY serialization to QJsonDocument). The reader
// only supports what the provider contract needs (object / array / string /
// number / bool / null) and it NEVER coerces: integer 5 is not the string "5".
// ---------------------------------------------------------------------------
struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind{Kind::Null};
    std::string text;   // String payload (raw, unescaped); also Bool/Number raw
    std::vector<JsonValue> items;                                  // Array
    std::vector<std::pair<std::string, JsonValue>> members;        // Object

    [[nodiscard]] const JsonValue *find(std::string_view name) const
    {
        for (const auto &member : members) {
            if (member.first == name) {
                return &member.second;
            }
        }
        return nullptr;
    }
};

class JsonReader
{
public:
    explicit JsonReader(std::string_view input) : input_(input) {}

    [[nodiscard]] bool parse(JsonValue &out)
    {
        skipWhitespace();
        if (!parseValue(out)) {
            return false;
        }
        skipWhitespace();
        return pos_ == input_.size(); // trailing garbage => not the document
    }

private:
    void skipWhitespace()
    {
        while (pos_ < input_.size()) {
            const char c = input_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
                continue;
            }
            break;
        }
    }

    [[nodiscard]] bool fail()
    {
        failed_ = true;
        return false;
    }

    [[nodiscard]] bool parseValue(JsonValue &out)
    {
        skipWhitespace();
        if (pos_ >= input_.size()) {
            return fail();
        }
        switch (input_[pos_]) {
        case '{':
            return parseObject(out);
        case '[':
            return parseArray(out);
        case '"':
            out.kind = JsonValue::Kind::String;
            return parseString(out.text);
        case 't':
            return parseLiteral("true", out, JsonValue::Kind::Bool);
        case 'f':
            return parseLiteral("false", out, JsonValue::Kind::Bool);
        case 'n':
            return parseLiteral("null", out, JsonValue::Kind::Null);
        default:
            return parseNumber(out);
        }
    }

    [[nodiscard]] bool parseLiteral(std::string_view literal, JsonValue &out,
                                    JsonValue::Kind kind)
    {
        if (input_.substr(pos_, literal.size()) != literal) {
            return fail();
        }
        pos_ += literal.size();
        out.kind = kind;
        out.text = std::string(literal);
        return true;
    }

    // Strict JSON number grammar (RFC 8259). No leading '+', no leading zeros,
    // no hex, no Infinity / NaN spellings.
    [[nodiscard]] bool parseNumber(JsonValue &out)
    {
        const std::size_t start = pos_;
        if (pos_ < input_.size() && input_[pos_] == '-') {
            ++pos_;
        }
        if (pos_ >= input_.size() || !isDigit(input_[pos_])) {
            return fail();
        }
        if (input_[pos_] == '0') {
            ++pos_;
        } else {
            while (pos_ < input_.size() && isDigit(input_[pos_])) {
                ++pos_;
            }
        }
        if (pos_ < input_.size() && input_[pos_] == '.') {
            ++pos_;
            if (pos_ >= input_.size() || !isDigit(input_[pos_])) {
                return fail();
            }
            while (pos_ < input_.size() && isDigit(input_[pos_])) {
                ++pos_;
            }
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) {
                ++pos_;
            }
            if (pos_ >= input_.size() || !isDigit(input_[pos_])) {
                return fail();
            }
            while (pos_ < input_.size() && isDigit(input_[pos_])) {
                ++pos_;
            }
        }
        out.kind = JsonValue::Kind::Number;
        out.text = std::string(input_.substr(start, pos_ - start));
        return true;
    }

    [[nodiscard]] bool parseHex4(unsigned &value)
    {
        if (pos_ + 4 > input_.size()) {
            return false;
        }
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = input_[pos_ + static_cast<std::size_t>(i)];
            unsigned digit = 0;
            if (c >= '0' && c <= '9') {
                digit = static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<unsigned>(c - 'a') + 10u;
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<unsigned>(c - 'A') + 10u;
            } else {
                return false;
            }
            value = (value << 4) | digit;
        }
        pos_ += 4;
        return true;
    }

    static void appendUtf8(std::string &out, unsigned codePoint)
    {
        if (codePoint <= 0x7Fu) {
            out.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7FFu) {
            out.push_back(static_cast<char>(0xC0u | (codePoint >> 6)));
            out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xE0u | (codePoint >> 12)));
            out.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
        }
    }

    [[nodiscard]] bool parseString(std::string &out)
    {
        if (pos_ >= input_.size() || input_[pos_] != '"') {
            return fail();
        }
        ++pos_;
        out.clear();
        while (true) {
            if (pos_ >= input_.size()) {
                return fail(); // unterminated string
            }
            const char c = input_[pos_];
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c == '\\') {
                ++pos_;
                if (pos_ >= input_.size()) {
                    return fail();
                }
                const char esc = input_[pos_];
                switch (esc) {
                case '"':
                    out.push_back('"');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case '/':
                    out.push_back('/');
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    ++pos_;
                    unsigned codePoint = 0;
                    if (!parseHex4(codePoint)) {
                        return fail();
                    }
                    appendUtf8(out, codePoint);
                    continue; // parseHex4 already advanced pos_
                }
                default:
                    return fail(); // unknown escape: refuse, never guess
                }
                ++pos_;
                continue;
            }
            // Raw control characters are illegal inside JSON strings.
            const unsigned char uc = static_cast<unsigned char>(c);
            if (uc < 0x20u) {
                return fail();
            }
            out.push_back(c);
            ++pos_;
        }
    }

    [[nodiscard]] bool parseArray(JsonValue &out)
    {
        out.kind = JsonValue::Kind::Array;
        ++pos_; // '['
        skipWhitespace();
        if (pos_ < input_.size() && input_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            JsonValue element;
            if (!parseValue(element)) {
                return fail();
            }
            out.items.push_back(std::move(element));
            skipWhitespace();
            if (pos_ >= input_.size()) {
                return fail();
            }
            if (input_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (input_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return fail();
        }
    }

    [[nodiscard]] bool parseObject(JsonValue &out)
    {
        out.kind = JsonValue::Kind::Object;
        ++pos_; // '{'
        skipWhitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skipWhitespace();
            std::string name;
            if (!parseString(name)) {
                return fail();
            }
            // A duplicate member would make "which one wins" ambiguous; refuse.
            if (out.find(name) != nullptr) {
                return fail();
            }
            skipWhitespace();
            if (pos_ >= input_.size() || input_[pos_] != ':') {
                return fail();
            }
            ++pos_;
            JsonValue value;
            if (!parseValue(value)) {
                return fail();
            }
            out.members.emplace_back(std::move(name), std::move(value));
            skipWhitespace();
            if (pos_ >= input_.size()) {
                return fail();
            }
            if (input_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (input_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return fail();
        }
    }

    static bool isDigit(char c)
    {
        return c >= '0' && c <= '9';
    }

    std::string_view input_;
    std::size_t pos_{0};
    bool failed_{false};
};

bool parseJsonStrict(std::string_view text, JsonValue &out)
{
    JsonReader reader(text);
    if (!reader.parse(out)) {
        return false;
    }
    return out.kind != JsonValue::Kind::Null || text.find("null") != std::string_view::npos;
}

[[nodiscard]] ProviderExtractionResult schemaFailure()
{
    ProviderExtractionResult result;
    result.ok = false;
    result.failure = ProviderExtractionFailure::SchemaViolation;
    return result;
}

[[nodiscard]] ProviderExtractionResult malformedFailure()
{
    ProviderExtractionResult result;
    result.ok = false;
    result.failure = ProviderExtractionFailure::MalformedResponse;
    return result;
}

[[nodiscard]] RegisterProviderExtractionResult registerSchemaFailure()
{
    RegisterProviderExtractionResult result;
    result.ok = false;
    result.failure = core::ProviderExtractionFailure::SchemaViolation;
    return result;
}

// Read a REQUIRED string member. Wrong JSON type is a schema violation (never
// coerced), and an empty value is invalid where the contract says it is
// required to carry meaning.
[[nodiscard]] bool readRequiredNonEmptyString(const JsonValue &object,
                                              std::string_view name,
                                              std::string &out)
{
    const JsonValue *member = object.find(name);
    if (member == nullptr || member->kind != JsonValue::Kind::String) {
        return false;
    }
    if (member->text.empty()) {
        return false;
    }
    out = member->text;
    return true;
}

// The extraction payload carries a STRICT set of members. Any extra member —
// including a provider-reported numeric confidence / score / probability — is
// a schema violation rather than something to silently ignore (H5 / §12).
[[nodiscard]] bool hasOnlyMembers(
    const JsonValue &object,
    const std::vector<std::string_view> &allowed)
{
    for (const auto &member : object.members) {
        bool known = false;
        for (const std::string_view name : allowed) {
            if (member.first == name) {
                known = true;
                break;
            }
        }
        if (!known) {
            return false;
        }
    }
    return true;
}

// T035-A.1: read a REQUIRED integral number member in [minValue, maxValue].
// The raw literal is parsed with from_chars (locale-independent); a
// fractional value, an out-of-range value, or a non-finite overflow
// ("1e999" — JSON has no NaN/Infinity literals) is a refusal — never a
// clamp, never a coercion.
[[nodiscard]] bool readRequiredIntegralNumber(const JsonValue &object,
                                              std::string_view name,
                                              double minValue, double maxValue,
                                              double &out)
{
    const JsonValue *member = object.find(name);
    if (member == nullptr || member->kind != JsonValue::Kind::Number) {
        return false;
    }
    double value = 0.0;
    const char *first = member->text.data();
    const char *last = first + member->text.size();
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last) {
        return false;
    }
    if (!std::isfinite(value) || value != std::floor(value)
        || value < minValue || value > maxValue) {
        return false;
    }
    out = value;
    return true;
}

// T035-A.1: read an OPTIONAL finite number member; absence yields the
// frozen default. A present member must be a JSON number and must be finite.
[[nodiscard]] bool readOptionalFiniteNumber(const JsonValue &object,
                                            std::string_view name,
                                            double defaultValue, double &out)
{
    const JsonValue *member = object.find(name);
    if (member == nullptr) {
        out = defaultValue;
        return true;
    }
    if (member->kind != JsonValue::Kind::Number) {
        return false;
    }
    double value = 0.0;
    const char *first = member->text.data();
    const char *last = first + member->text.size();
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last) {
        return false;
    }
    if (!std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

// T035-A.1: read an OPTIONAL string member; absence yields the frozen
// default. A present member must be a JSON string (no coercion).
[[nodiscard]] bool readOptionalString(const JsonValue &object,
                                      std::string_view name,
                                      std::string_view defaultValue,
                                      std::string &out)
{
    const JsonValue *member = object.find(name);
    if (member == nullptr) {
        out = std::string(defaultValue);
        return true;
    }
    if (member->kind != JsonValue::Kind::String) {
        return false;
    }
    out = member->text;
    return true;
}

} // namespace

ProviderContentExtraction extractChatCompletionsResponseContent(
    std::string_view responseBody)
{
    ProviderContentExtraction extraction;

    JsonValue root;
    if (!parseJsonStrict(responseBody, root)) {
        return extraction; // ok = false: not parseable at all
    }
    if (root.kind != JsonValue::Kind::Object) {
        return extraction;
    }
    // Envelope-level parsing intentionally mirrors the accepted M6 client:
    // provider metadata (id / created / usage / model ...) is NOT our
    // contract, so it is tolerated here. STRICTNESS belongs to the extraction
    // PAYLOAD, which parseStrictCandidateProposals enforces below.
    const JsonValue *choices = root.find("choices");
    if (choices == nullptr || choices->kind != JsonValue::Kind::Array) {
        return extraction;
    }
    for (const JsonValue &choice : choices->items) {
        if (choice.kind != JsonValue::Kind::Object) {
            continue;
        }
        const JsonValue *message = choice.find("message");
        if (message == nullptr || message->kind != JsonValue::Kind::Object) {
            continue;
        }
        // Only message.content is consumed — reasoning_content is never
        // product state (same rule as the M6 diagnosis client).
        const JsonValue *content = message->find("content");
        if (content == nullptr || content->kind != JsonValue::Kind::String) {
            continue;
        }
        if (content->text.empty()) {
            continue;
        }
        extraction.ok = true;
        extraction.content = content->text;
        return extraction;
    }
    return extraction;
}

ProviderExtractionResult parseStrictCandidateProposals(
    std::string_view providerContent)
{
    JsonValue root;
    if (!parseJsonStrict(providerContent, root)) {
        return malformedFailure();
    }
    // Wrong top-level type: fail closed.
    if (root.kind != JsonValue::Kind::Object) {
        return schemaFailure();
    }
    // Missing required member / unexpected member.
    if (!hasOnlyMembers(root, {"proposals"})) {
        return schemaFailure();
    }
    const JsonValue *proposals = root.find("proposals");
    if (proposals == nullptr) {
        return schemaFailure();
    }
    if (proposals->kind != JsonValue::Kind::Array) {
        return schemaFailure();
    }

    ProviderExtractionResult result;
    result.ok = true;
    result.failure = ProviderExtractionFailure::None;

    // ATOMICITY (§13): build the whole set first; a single violation anywhere
    // discards EVERYTHING. There is no partial acceptance.
    std::vector<CandidateProposal> accepted;
    for (const JsonValue &element : proposals->items) {
        if (element.kind != JsonValue::Kind::Object) {
            return schemaFailure();
        }
        if (!hasOnlyMembers(element, {"target", "value", "evidence"})) {
            return schemaFailure();
        }

        std::string targetToken;
        std::string value;
        std::string evidence;
        if (!readRequiredNonEmptyString(element, "target", targetToken)
            || !readRequiredNonEmptyString(element, "value", value)
            || !readRequiredNonEmptyString(element, "evidence", evidence)) {
            return schemaFailure();
        }

        // An unknown target token cannot be mapped onto the frozen identity
        // list, so it is refused instead of guessed.
        ProfileFieldTarget target{ProfileFieldTarget::Manufacturer};
        if (!core::profileFieldTargetFromToken(targetToken, target)) {
            return schemaFailure();
        }

        CandidateProposal proposal;
        proposal.target = target;
        proposal.proposedValue = value;
        proposal.evidenceExcerpt = evidence;
        // The provider contract carries NO location hint in this slice, so the
        // neutral proposal keeps the frozen "not supplied" sentinel. Evidence
        // location is derived locally by SESSION M's validator (H6).
        proposal.locationHint = -1;
        accepted.push_back(std::move(proposal));
    }

    result.proposals = std::move(accepted);
    return result;
}

RegisterProviderExtractionResult parseStrictRegisterCandidateProposals(
    std::string_view providerContent)
{
    JsonValue root;
    if (!parseJsonStrict(providerContent, root)) {
        RegisterProviderExtractionResult failed;
        failed.ok = false;
        failed.failure = core::ProviderExtractionFailure::MalformedResponse;
        return failed;
    }
    // Wrong top-level type: fail closed.
    if (root.kind != JsonValue::Kind::Object) {
        return registerSchemaFailure();
    }
    // STRICT member set: "registerProposals" ONLY. A response that also
    // carries the profile-field "proposals" member (or any other member) is
    // a violation — the two contracts never mix and are never routed
    // implicitly (Human T035-A.1 §6).
    if (!hasOnlyMembers(root, {"registerProposals"})) {
        return registerSchemaFailure();
    }
    const JsonValue *proposals = root.find("registerProposals");
    if (proposals == nullptr || proposals->kind != JsonValue::Kind::Array) {
        return registerSchemaFailure();
    }

    RegisterProviderExtractionResult result;
    result.ok = true;
    result.failure = core::ProviderExtractionFailure::None;

    // ATOMICITY (§13, same as the profile-field parser): one violation
    // anywhere discards EVERYTHING. There is no partial acceptance and no
    // auto-correction anywhere in this parser.
    std::vector<core::RegisterCandidateProposal> accepted;
    for (const JsonValue &element : proposals->items) {
        if (element.kind != JsonValue::Kind::Object) {
            return registerSchemaFailure();
        }
        if (!hasOnlyMembers(
                element,
                {"functionCode", "address", "name", "dataType", "quantity",
                 "evidence", "description", "scale", "offset", "unit"})) {
            return registerSchemaFailure();
        }

        // functionCode: the frozen M10 register space 0x01..0x7F; address:
        // the PDU space 0..65535. Both must be integral JSON numbers —
        // string or fractional forms are type errors, never coerced.
        double functionCode = 0.0;
        double address = 0.0;
        double quantity = 0.0;
        if (!readRequiredIntegralNumber(element, "functionCode", 1.0, 127.0,
                                        functionCode)) {
            return registerSchemaFailure();
        }
        if (!readRequiredIntegralNumber(element, "address", 0.0, 65535.0,
                                        address)) {
            return registerSchemaFailure();
        }
        if (!readRequiredIntegralNumber(element, "quantity", 1.0, 65535.0,
                                        quantity)) {
            return registerSchemaFailure();
        }

        std::string name;
        std::string dataTypeToken;
        std::string evidence;
        if (!readRequiredNonEmptyString(element, "name", name)
            || !readRequiredNonEmptyString(element, "dataType", dataTypeToken)
            || !readRequiredNonEmptyString(element, "evidence", evidence)) {
            return registerSchemaFailure();
        }

        // An unknown dataType token cannot be mapped onto the frozen M11
        // matrix, so it is refused instead of guessed.
        core::RegisterDecodeType dataType{core::RegisterDecodeType::UInt16};
        if (!core::profileDataTypeFromToken(dataTypeToken, dataType)) {
            return registerSchemaFailure();
        }
        // quantity MUST equal the type's word count — the mismatch is a
        // violation, never a silent repair (Human T035-A.1 §5).
        const int requiredWords = core::registerDecodeTypeWordCount(dataType);
        if (requiredWords == 0
            || static_cast<int>(quantity) != requiredWords) {
            return registerSchemaFailure();
        }

        // Optional members (frozen defaults: description "", scale 1.0,
        // offset 0.0, unit "").
        std::string description;
        std::string unit;
        double scale = 1.0;
        double offset = 0.0;
        if (!readOptionalString(element, "description", "", description)
            || !readOptionalString(element, "unit", "", unit)
            || !readOptionalFiniteNumber(element, "scale", 1.0, scale)
            || !readOptionalFiniteNumber(element, "offset", 0.0, offset)) {
            return registerSchemaFailure();
        }

        // Optional members (frozen defaults: description "", scale 1.0,
        // offset 0.0, unit ""). The has* presence flags record HONESTLY
        // whether the provider actually stated each optional member — the
        // review surface must never present a default as an extraction
        // (Human T035-A.3.2.2 §4).
        core::RegisterCandidateProposal proposal;
        proposal.functionCode = static_cast<std::uint8_t>(functionCode);
        proposal.address = static_cast<std::uint16_t>(address);
        proposal.name = std::move(name);
        proposal.description = std::move(description);
        proposal.dataType = dataType;
        proposal.quantity = static_cast<int>(quantity);
        proposal.scale = scale;
        proposal.offset = offset;
        proposal.unit = std::move(unit);
        proposal.hasDescription = element.find("description") != nullptr;
        proposal.hasScale = element.find("scale") != nullptr;
        proposal.hasOffset = element.find("offset") != nullptr;
        proposal.hasUnit = element.find("unit") != nullptr;
        proposal.evidenceExcerpt = std::move(evidence);
        accepted.push_back(std::move(proposal));
    }

    result.proposals = std::move(accepted);
    return result;
}

// ---------------------------------------------------------------------------
// Adapter implementation.
// ---------------------------------------------------------------------------

namespace {

// The PROMPT SEMANTICS are frozen by H8; the exact wording is an adapter
// implementation detail (§11), which is why it lives here and not in a test's
// full-string assertion. The instructions state the semantic contract only:
// extract exclusively from the supplied text, never guess, always attach the
// exact supporting excerpt, and report nothing the source cannot support.
//
// CAND-F1 (T034-B2-HV2-D-CAND-F1): EVIDENCE ANCHORING. The local validator
// locates an excerpt by EXACT substring match against the canonical text with
// NO normalization whatsoever (no whitespace folding, no newline conversion,
// no full/half-width translation). A bounded live RCA showed a provider
// proposal being refused with `evidence_not_found` while the canonical text of
// the very same manual is stored with CRLF line endings, and an offline
// experiment reproduced a 0-match for the same two lines joined with LF. The
// ORIGINAL semantic contract is unchanged (use only the supplied text, never
// guess, omit what the text does not support) — this block only makes the
// evidence requirements EXPLICIT so the model can satisfy the validator. No
// JSON schema is repeated here: the user prompt owns the schema.
[[nodiscard]] std::string buildSystemInstruction()
{
    return "You extract structured facts from the supplied manual text. "
           "Use ONLY the supplied text. Do not guess or infer anything the "
           "text does not state. For every extracted fact, return the exact "
           "excerpt from the supplied text that supports it. Omit anything the "
           "text does not support. Never claim a fact is verified. "
           "Evidence rules — an excerpt is only usable when ALL of them hold:\n"
           "1. Copy it CHARACTER FOR CHARACTER, VERBATIM, exactly as the "
           "supplied text shows it. Never paraphrase, summarize, translate, "
           "correct, reorder or restyle it.\n"
           "2. It must be ONE CONTIGUOUS run of the text. Never join separate "
           "lines or fragments together, and never add, drop or change any "
           "character, digit, punctuation mark, space or character form "
           "(full-width characters stay full-width, half-width stay "
           "half-width).\n"
           "3. Prefer a SHORT, COMPLETE SINGLE-LINE snippet from the body "
           "text. Do not quote across a line break.\n"
           "4. It must occur EXACTLY ONCE in the WHOLE supplied text, not "
           "merely once on its page. Never use repeated page headers, page "
           "footers, copyright notices, document titles or other boilerplate "
           "as evidence — those are not unique and are rejected.\n"
           "5. For a manufacturer, prefer a snippet from the body text, or "
           "from an explicit label, that uniquely identifies the maker.\n"
           "6. If no snippet can satisfy these rules, return a LEGAL EMPTY "
           "result: an empty result list with no proposals at all. NEVER "
           "invent, approximate, reconstruct or pad an excerpt to fill the "
           "result.";
}

[[nodiscard]] std::string buildUserPrompt(const core::ExtractionRequest &request)
{
    std::string prompt;
    prompt += "Extract the requested field from the manual text below.\n";
    prompt += "Target field: ";
    prompt += request.targetFieldToken;
    prompt += "\nRespond with JSON of the form {\"proposals\":[{\"target\":\"";
    prompt += request.targetFieldToken;
    prompt += "\",\"value\":\"...\",\"evidence\":\"...\"}]}.\n";
    prompt += "--- MANUAL TEXT BEGIN ---\n";
    prompt += request.canonicalExtractedText;
    prompt += "\n--- MANUAL TEXT END ---\n";
    return prompt;
}

// Provider-specific output instruction (T034-B1 JSON OUTPUT round): decided
// by the PROFILE, never by the caller. DeepSeek's JSON Output contract
// requires the prompt to ask for JSON explicitly; the semantic requirements
// are: raw JSON only, no Markdown code fence, no prose around it, exactly the
// schema already shown in the user prompt (which mirrors the frozen parser
// contract — no invented fields).
[[nodiscard]] std::string buildJsonOnlyInstruction()
{
    return "Output ONLY the raw JSON object exactly as specified above. "
           "Do not wrap the JSON in Markdown code fences. Do not add any "
           "prose, explanation or any other text before or after the JSON. "
           "Follow exactly the schema shown above.";
}

// ---------------------------------------------------------------------------
// T035-A.2 — REGISTER-MAP prompt branch. Separate functions on purpose: the
// manufacturer prompt/system-instruction bytes stay EXACTLY what they were,
// and the register contract never reuses or re-words them.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string buildRegisterSystemInstruction()
{
    // Evidence rules 1-4 and 6 are the SAME frozen anchoring semantics as the
    // profile-field system instruction (CAND-F1); rule 5 is register-specific.
    return "You extract the device register map from the supplied manual "
           "text. Use ONLY the supplied text. Do not guess or infer anything "
           "the text does not state. For every register entry, return the "
           "exact excerpt from the supplied text that supports it. Omit "
           "anything the text does not support. Never claim a fact is "
           "verified. Evidence rules — an excerpt is only usable when ALL of "
           "them hold:\n"
           "1. Copy it CHARACTER FOR CHARACTER, VERBATIM, exactly as the "
           "supplied text shows it. Never paraphrase, summarize, translate, "
           "correct, reorder or restyle it.\n"
           "2. It must be ONE CONTIGUOUS run of the text. Never join separate "
           "lines or fragments together, and never add, drop or change any "
           "character, digit, punctuation mark, space or character form "
           "(full-width characters stay full-width, half-width stay "
           "half-width).\n"
           "3. Prefer a SHORT, COMPLETE SINGLE-LINE snippet from the register "
           "table row or the surrounding description. Do not quote across a "
           "line break.\n"
           "4. It must occur EXACTLY ONCE in the WHOLE supplied text, not "
           "merely once on its page. Never use repeated page headers, page "
           "footers, copyright notices, document titles or other boilerplate "
           "as evidence — those are not unique and are rejected.\n"
           "5. For a register entry, prefer the table row (or the sentence) "
           "that states the register address, its name and its data type — "
           "and scale/resolution/unit when the text gives them.\n"
           "6. If a register cannot satisfy these rules, leave it out and "
           "return a LEGAL EMPTY result: an empty registerProposals list. "
           "NEVER invent, approximate, reconstruct or pad an excerpt to fill "
           "the result.\n"
           "Name rules — the \"name\" member of a register proposal:\n"
           "1. \"name\" holds ONLY the concise register name; keep the "
           "manual's own wording exactly as written. Do not split, reword, "
           "shorten or clean it just because it contains separators, slashes "
           "or units.\n"
           "2. Descriptive, explanatory, or reference-style texts (for "
           "example \"same as above\" or \"description same as the first "
           "channel\", notes, unit remarks, range remarks) belong in "
           "\"description\", NEVER inside \"name\".\n"
           "3. If the name and such a remark sit on the same manual line, "
           "split them: the name part goes to \"name\", the remark goes to "
           "\"description\". When the split is not reliable, stay "
           "conservative: keep the line's wording intact and do not invent a "
           "replacement. An empty description is legal.\n"
           "4. Do not invent a name or a description the text does not "
           "state.";
}

[[nodiscard]] std::string buildRegisterUserPrompt(
    const core::ExtractionRequest &request)
{
    std::string prompt;
    prompt += "Extract the device register map from the manual text below.\n";
    prompt += "Target: register_map\n";
    prompt +=
        "Respond with JSON of the form {\"registerProposals\":[{"
        "\"functionCode\":<integer 1..127>,\"address\":<integer 0..65535>,"
        "\"name\":\"...\",\"dataType\":\"UInt16|Int16|UInt32|Int32|Float32\","
        "\"quantity\":<integer>,\"evidence\":\"...\"}]}.\n";
    prompt +=
        "Optional members you may include ONLY when the text states them: "
        "\"description\":\"...\", \"scale\":<number>, \"offset\":<number>, "
        "\"unit\":\"...\". Omit every member the text does not state.\n";
    prompt +=
        "quantity is the number of 16-bit registers the value spans: 1 for "
        "UInt16/Int16, 2 for UInt32/Int32/Float32. It must match dataType "
        "exactly; a wrong quantity is rejected, never corrected.\n";
    prompt +=
        "No confidence, score, reason or any other member is allowed; "
        "unknown members are rejected.\n";
    prompt += "--- MANUAL TEXT BEGIN ---\n";
    prompt += request.canonicalExtractedText;
    prompt += "\n--- MANUAL TEXT END ---\n";
    return prompt;
}

// ---------------------------------------------------------------------------
// T034-B1 live RCA — SANITIZED provider forensics (Human-authorized evidence
// path). Structural metadata ONLY: statuses, counts, JSON type names, lengths
// and enumeration words. NEVER logged: API keys, Authorization headers, the
// manual text, the prompt, or any raw provider/assistant content. The append
// file lives in the user's temp directory and carries no payload either.
// ---------------------------------------------------------------------------
[[nodiscard]] QString forensicsLogPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + QStringLiteral("/modbuslens_provider_forensics.log");
}

void writeForensics(const QString &block)
{
    // T034-B1: TEMPORARY diagnostic instrumentation, EXPLICITLY GATED — with
    // MODBUSLENS_PROVIDER_FORENSICS unset there is ZERO file output and ZERO
    // qWarning output. (Human decision pending: delete before the B1 commit,
    // or converge into a default-off controlled diagnostic.)
    if (qEnvironmentVariable("MODBUSLENS_PROVIDER_FORENSICS")
        != QLatin1String("1")) {
        return;
    }
    qWarning().noquote() << block;
    QFile file(forensicsLogPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(block.toUtf8());
        file.write("\n");
    }
}

// Structural analysis of the assistant content against the STRICT contract
// { "proposals": [ { "target", "value", "evidence" } ] }. Independent of the
// frozen parser: this only NAMES the first structural anomaly class (never a
// value) so a live RCA can distinguish malformed envelope from strict-schema
// rejection without weakening anything.
[[nodiscard]] QString describeContentStructure(std::string_view content)
{
    JsonValue root;
    if (!parseJsonStrict(content, root)) {
        return QStringLiteral("structured_json_parse=FAIL category=invalid_json");
    }
    QString report = QStringLiteral("structured_json_parse=ok");
    if (root.kind != JsonValue::Kind::Object) {
        report += QStringLiteral(" top_level=%1 category=wrong_top_level_type")
                      .arg(root.kind == JsonValue::Kind::Array
                               ? QStringLiteral("array")
                               : QStringLiteral("scalar"));
        return report;
    }
    report += QStringLiteral(" top_level=object");
    const JsonValue *proposals = root.find("proposals");
    if (proposals == nullptr) {
        report += QStringLiteral(" category=missing_required_field(proposals)");
        return report;
    }
    if (proposals->kind != JsonValue::Kind::Array) {
        report += QStringLiteral(" category=wrong_field_type(proposals)");
        return report;
    }
    report += QStringLiteral(" proposals_size=%1").arg(proposals->items.size());
    if (proposals->items.empty()) {
        report += QStringLiteral(" (empty proposals array is contract-legal)");
        return report;
    }
    for (std::size_t index = 0; index < proposals->items.size(); ++index) {
        const JsonValue &item = proposals->items[index];
        if (item.kind != JsonValue::Kind::Object) {
            report += QStringLiteral(" item[%1] category=wrong_field_type(item_not_object)")
                          .arg(index);
            return report;
        }
        QString itemReport;
        const JsonValue *target = item.find("target");
        const JsonValue *value = item.find("value");
        const JsonValue *evidence = item.find("evidence");
        if (target == nullptr || value == nullptr || evidence == nullptr) {
            itemReport += QStringLiteral(" missing=");
            if (target == nullptr) itemReport += QStringLiteral("target,");
            if (value == nullptr) itemReport += QStringLiteral("value,");
            if (evidence == nullptr) itemReport += QStringLiteral("evidence");
            itemReport.chop(1);
        }
        if ((target && target->kind != JsonValue::Kind::String)
            || (value && value->kind != JsonValue::Kind::String)
            || (evidence && evidence->kind != JsonValue::Kind::String)) {
            if (!itemReport.isEmpty()) itemReport += QLatin1Char(' ');
            itemReport += QStringLiteral("wrong_field_type=");
            if (target && target->kind != JsonValue::Kind::String) itemReport += QStringLiteral("target,");
            if (value && value->kind != JsonValue::Kind::String) itemReport += QStringLiteral("value,");
            if (evidence && evidence->kind != JsonValue::Kind::String) itemReport += QStringLiteral("evidence");
            itemReport.chop(1);
        }
        if (target && target->kind == JsonValue::Kind::String) {
            ProfileFieldTarget known{};
            if (!core::profileFieldTargetFromToken(target->text, known)) {
                if (!itemReport.isEmpty()) itemReport += QLatin1Char(' ');
                itemReport += QStringLiteral("unknown_target");
            }
        }
        if ((value && value->kind == JsonValue::Kind::String && value->text.empty())
            || (evidence && evidence->kind == JsonValue::Kind::String
                && evidence->text.empty())) {
            if (!itemReport.isEmpty()) itemReport += QLatin1Char(' ');
            itemReport += QStringLiteral("empty_evidence_or_value");
        }
        int extraKeys = 0;
        for (const auto &member : item.members) {
            if (member.first != "target" && member.first != "value"
                && member.first != "evidence") {
                ++extraKeys;
            }
        }
        if (extraKeys > 0) {
            if (!itemReport.isEmpty()) itemReport += QLatin1Char(' ');
            itemReport += QStringLiteral("unknown_field_count=%1").arg(extraKeys);
        }
        if (!itemReport.isEmpty()) {
            report += QStringLiteral(" item[%1] category:%2").arg(index).arg(itemReport);
            return report; // first anomaly names the class
        }
    }
    report += QStringLiteral(" items=structurally_conformant");
    return report;
}

void writeProviderForensics(const IExtractionTransport::Exchange &exchange,
                            bool envelopeStageReached, bool envelopeOk,
                            std::string_view extractedContent,
                            const core::ProviderExtractionResult *parserResult)
{
    QString block = QStringLiteral(
                        "[provider forensics] %1 (sanitized: statuses/counts/types/lengths "
                        "only — no keys, no headers, no manual text, no prompt, no raw "
                        "provider content)")
                        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    // The SESSION N seam does not carry the numeric HTTP status by design;
    // only the coarse transport/status booleans cross the boundary.
    block += QStringLiteral("\n  http_status=<not exposed by seam> transport_ok=%1 status_ok=%2")
                 .arg(exchange.transportOk ? 1 : 0)
                 .arg(exchange.statusOk ? 1 : 0);
    block += QStringLiteral("\n  response_body_len=%1")
                 .arg(static_cast<int>(exchange.responseBody.size()));
    if (!envelopeStageReached) {
        block += QStringLiteral("\n  failure_owner=transport (envelope never reached)");
        writeForensics(block);
        return;
    }
    JsonValue root;
    const bool envelopeJsonOk = parseJsonStrict(exchange.responseBody, root)
                                && root.kind == JsonValue::Kind::Object;
    block += QStringLiteral("\n  envelope_json_parse=%1").arg(envelopeJsonOk ? "ok" : "FAIL");
    int choicesCount = -1;
    QString finishReason = QStringLiteral("<absent>");
    QString contentType = QStringLiteral("<absent>");
    int contentLen = -1;
    bool reasoningExists = false;
    int reasoningLen = -1;
    if (envelopeJsonOk) {
        const JsonValue *choices = root.find("choices");
        if (choices != nullptr && choices->kind == JsonValue::Kind::Array) {
            choicesCount = static_cast<int>(choices->items.size());
        }
        const JsonValue *message = nullptr;
        if (choicesCount > 0) {
            const JsonValue &first = choices->items.front();
            if (first.kind == JsonValue::Kind::Object) {
                message = first.find("message");
                const JsonValue *finish = first.find("finish_reason");
                if (finish != nullptr && finish->kind == JsonValue::Kind::String) {
                    finishReason = QString::fromStdString(finish->text);
                }
            }
        }
        block += QStringLiteral("\n  choices_count=%1").arg(choicesCount);
        block += QStringLiteral("\n  message_exists=%1")
                     .arg(message != nullptr ? 1 : 0);
        if (message != nullptr && message->kind == JsonValue::Kind::Object) {
            const JsonValue *content = message->find("content");
            if (content != nullptr) {
                switch (content->kind) {
                case JsonValue::Kind::String: contentType = "string"; break;
                case JsonValue::Kind::Object: contentType = "object"; break;
                case JsonValue::Kind::Array: contentType = "array"; break;
                case JsonValue::Kind::Null: contentType = "null"; break;
                case JsonValue::Kind::Number: contentType = "number"; break;
                case JsonValue::Kind::Bool: contentType = "bool"; break;
                }
                if (content->kind == JsonValue::Kind::String) {
                    contentLen = static_cast<int>(content->text.size());
                }
            }
            const JsonValue *reasoning = message->find("reasoning_content");
            reasoningExists = reasoning != nullptr;
            if (reasoning != nullptr && reasoning->kind == JsonValue::Kind::String) {
                reasoningLen = static_cast<int>(reasoning->text.size());
            }
        }
    }
    block += QStringLiteral("\n  content_type=%1 content_len=%2").arg(contentType).arg(contentLen);
    block += QStringLiteral("\n  reasoning_content_exists=%1 reasoning_len=%2")
                 .arg(reasoningExists ? 1 : 0)
                 .arg(reasoningLen);
    block += QStringLiteral("\n  finish_reason=%1").arg(finishReason);
    block += QStringLiteral("\n  envelope_content_extraction=%1")
                 .arg(envelopeOk ? "ok" : "FAIL(failure_owner=envelope)");
    if (!envelopeOk) {
        block += QStringLiteral("\n  failure_owner=envelope (strict parser NOT reached)");
        writeForensics(block);
        return;
    }
    block += QStringLiteral("\n  content_outer_nonempty=%1")
                 .arg(extractedContent.empty() ? 0 : 1);
    block += "\n  " + describeContentStructure(extractedContent);
    if (parserResult != nullptr && !parserResult->ok) {
        block += QStringLiteral("\n  parser_result=%1")
                     .arg(QString::fromUtf8(
                         core::providerExtractionFailureToken(parserResult->failure)));
    } else if (parserResult != nullptr) {
        block += QStringLiteral("\n  parser_result=ok (local evidence validation follows)");
    }
    writeForensics(block);
}

} // namespace

CandidateProviderAdapter::CandidateProviderAdapter(
    IExtractionTransport &transport, CandidateAdapterConfig config)
    : transport_(transport), config_(std::move(config))
{
}

std::string CandidateProviderAdapter::buildWireBodyForTest(
    const core::ExtractionRequest &request) const
{
    // T034 B1: the wire body is built with QJsonDocument (authoritative JSON
    // escaping for RUNTIME model/prompt strings — quotes, backslashes,
    // newlines and unicode can no longer depend on hand-written escaping).
    // The shared OpenAI-compatible core is { model, messages, stream:false,
    // max_tokens }; provider extras come EXCLUSIVELY from the compiled
    // profile table (DeepSeek: thinking.type=disabled; ModelScope: none —
    // the verified wire semantics stay exactly what they were).
    // T035-A.2: the ONLY mode routing key is the request's target token —
    // register_map uses the register prompt/system-instruction pair; every
    // other token keeps the byte-for-byte manufacturer path.
    std::string userPromptText;
    std::string systemInstructionText;
    if (request.targetFieldToken == core::kRegisterMapTargetToken) {
        userPromptText = buildRegisterUserPrompt(request);
        systemInstructionText = buildRegisterSystemInstruction();
    } else {
        userPromptText = buildUserPrompt(request);
        systemInstructionText = buildSystemInstruction();
    }
    const AiProviderProfile &profile = aiProviderProfile(config_.kind);
    if (profile.jsonOnlyPromptInstruction) {
        userPromptText += "\n";
        userPromptText += buildJsonOnlyInstruction();
    }
    const QJsonObject systemMessage{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"),
         QString::fromStdString(systemInstructionText)}};
    const QJsonObject userMessage{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), QString::fromStdString(userPromptText)}};
    QJsonObject body{
        {QStringLiteral("model"), QString::fromStdString(config_.modelId)},
        {QStringLiteral("messages"), QJsonArray{systemMessage, userMessage}},
        {QStringLiteral("stream"), false},
        {QStringLiteral("max_tokens"),
         static_cast<qint64>(config_.maxOutputTokens)}};
    if (profile.sendThinkingDisabledExtra) {
        body.insert(QStringLiteral("thinking"),
                    QJsonObject{{QStringLiteral("type"),
                                 QStringLiteral("disabled")}});
    }
    if (profile.sendJsonOutputResponseFormat) {
        body.insert(QStringLiteral("response_format"),
                    QJsonObject{{QStringLiteral("type"),
                                 QStringLiteral("json_object")}});
    }
    return std::string(QJsonDocument(body).toJson(QJsonDocument::Compact));
}

core::ProviderExtractionResult CandidateProviderAdapter::extract(
    const core::ExtractionRequest &request)
{
    lastFailure_ = core::ProviderExtractionFailure::None;

    // Every failure path returns a result with an EMPTY proposal list, so a
    // caller can never mistake a failure for a partial success (§13).
    const std::string wireBody = buildWireBodyForTest(request);
    const IExtractionTransport::Exchange exchange = transport_.send(wireBody);

    if (!exchange.transportOk) {
        lastFailure_ = core::ProviderExtractionFailure::TransportError;
        writeProviderForensics(exchange, /*envelopeStageReached=*/false,
                               /*envelopeOk=*/false, /*extractedContent=*/{},
                               /*parserResult=*/nullptr);
        core::ProviderExtractionResult failed;
        failed.ok = false;
        failed.failure = lastFailure_;
        return failed;
    }
    if (!exchange.statusOk) {
        lastFailure_ = core::ProviderExtractionFailure::ProviderRejectedStatus;
        writeProviderForensics(exchange, /*envelopeStageReached=*/true,
                               /*envelopeOk=*/false, /*extractedContent=*/{},
                               /*parserResult=*/nullptr);
        core::ProviderExtractionResult failed;
        failed.ok = false;
        failed.failure = lastFailure_;
        return failed;
    }

    // The raw provider body is used HERE ONLY and never stored: it does not
    // leave this call scope (H4 / §14).
    const ProviderContentExtraction content =
        extractChatCompletionsResponseContent(exchange.responseBody);
    if (!content.ok) {
        lastFailure_ = core::ProviderExtractionFailure::MalformedResponse;
        writeProviderForensics(exchange, /*envelopeStageReached=*/true,
                               /*envelopeOk=*/false, /*extractedContent=*/{},
                               /*parserResult=*/nullptr);
        core::ProviderExtractionResult failed;
        failed.ok = false;
        failed.failure = lastFailure_;
        return failed;
    }

    core::ProviderExtractionResult result =
        parseStrictCandidateProposals(content.content);
    if (!result.ok) {
        lastFailure_ = result.failure;
    }
    writeProviderForensics(exchange, /*envelopeStageReached=*/true,
                           /*envelopeOk=*/true, content.content, &result);
    return result;
}

RegisterProviderExtractionResult CandidateProviderAdapter::extractRegister(
    const core::ExtractionRequest &request)
{
    lastFailure_ = core::ProviderExtractionFailure::None;

    // Fail-closed mode guard: a wrong-mode request is a caller bug and is
    // refused instead of being silently parsed against the wrong contract
    // (the prompt would not match the parser either way).
    if (request.targetFieldToken != core::kRegisterMapTargetToken) {
        lastFailure_ = core::ProviderExtractionFailure::SchemaViolation;
        return registerSchemaFailure();
    }

    // Every failure path returns a result with an EMPTY proposal list, so a
    // caller can never mistake a failure for a partial success (§13). The
    // transport/envelope steps mirror extract() exactly; ONLY the prompt
    // branch (inside buildWireBodyForTest) and the strict parser differ.
    const std::string wireBody = buildWireBodyForTest(request);
    const IExtractionTransport::Exchange exchange = transport_.send(wireBody);

    if (!exchange.transportOk) {
        lastFailure_ = core::ProviderExtractionFailure::TransportError;
        writeProviderForensics(exchange, /*envelopeStageReached=*/false,
                               /*envelopeOk=*/false, /*extractedContent=*/{},
                               /*parserResult=*/nullptr);
        RegisterProviderExtractionResult failed;
        failed.ok = false;
        failed.failure = lastFailure_;
        return failed;
    }
    if (!exchange.statusOk) {
        lastFailure_ = core::ProviderExtractionFailure::ProviderRejectedStatus;
        writeProviderForensics(exchange, /*envelopeStageReached=*/true,
                               /*envelopeOk=*/false, /*extractedContent=*/{},
                               /*parserResult=*/nullptr);
        RegisterProviderExtractionResult failed;
        failed.ok = false;
        failed.failure = lastFailure_;
        return failed;
    }

    // The raw provider body is used HERE ONLY and never stored: it does not
    // leave this call scope (H4 / §14).
    const ProviderContentExtraction content =
        extractChatCompletionsResponseContent(exchange.responseBody);
    if (!content.ok) {
        lastFailure_ = core::ProviderExtractionFailure::MalformedResponse;
        writeProviderForensics(exchange, /*envelopeStageReached=*/true,
                               /*envelopeOk=*/false, /*extractedContent=*/{},
                               /*parserResult=*/nullptr);
        RegisterProviderExtractionResult failed;
        failed.ok = false;
        failed.failure = lastFailure_;
        return failed;
    }

    RegisterProviderExtractionResult result =
        parseStrictRegisterCandidateProposals(content.content);
    if (!result.ok) {
        lastFailure_ = result.failure;
    }
    // Forensics boundary (honest): the structural descriptor models the
    // PROFILE-FIELD contract, so the register parser result is NOT passed —
    // transport / envelope / raw-content forensics still apply. Extending
    // the structural descriptor for the register contract belongs to the
    // orchestration slice (A.3).
    writeProviderForensics(exchange, /*envelopeStageReached=*/true,
                           /*envelopeOk=*/true, content.content,
                           /*parserResult=*/nullptr);
    return result;
}

std::vector<core::CandidateProposal> CandidateProviderAdapter::propose(
    const core::ManualDocument &document, std::string_view canonicalExtractedText)
{
    const core::ExtractionRequest request =
        core::buildC2FirstSliceExtractionRequest(document, canonicalExtractedText);
    core::ProviderExtractionResult result = extract(request);
    if (!result.ok) {
        return {}; // schema / transport failure => no proposals at all (H8)
    }
    return std::move(result.proposals);
}

} // namespace modbuslens::ui