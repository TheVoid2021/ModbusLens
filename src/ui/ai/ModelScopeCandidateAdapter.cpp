#include "ui/ai/ModelScopeCandidateAdapter.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace modbuslens::ui {
namespace {

using core::CandidateProposal;
using core::ProfileFieldTarget;
using core::ProviderExtractionFailure;
using core::ProviderExtractionResult;

// ---------------------------------------------------------------------------
// Minimal strict JSON reader.
//
// Deliberately hand-written and dependency-free: this translation unit must
// stay Qt-free AND network-free, so it cannot borrow QJsonDocument. The reader
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

} // namespace

ProviderContentExtraction extractModelScopeResponseContent(
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

// ---------------------------------------------------------------------------
// Adapter implementation.
// ---------------------------------------------------------------------------

namespace {

// JSON string escaping for the serialized provider body. Only the characters
// JSON requires are escaped; the payload bytes are otherwise passed through.
[[nodiscard]] std::string escapeJsonString(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20u) {
                static const char *hex = "0123456789abcdef";
                const auto uc = static_cast<unsigned>(static_cast<unsigned char>(c));
                out += "\\u00";
                out.push_back(hex[(uc >> 4) & 0xFu]);
                out.push_back(hex[uc & 0xFu]);
            } else {
                out.push_back(c);
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

// The PROMPT SEMANTICS are frozen by H8; the exact wording is an adapter
// implementation detail (§11), which is why it lives here and not in a test's
// full-string assertion. The instructions state the semantic contract only:
// extract exclusively from the supplied text, never guess, always attach the
// exact supporting excerpt, and report nothing the source cannot support.
[[nodiscard]] std::string buildSystemInstruction()
{
    return "You extract structured facts from the supplied manual text. "
           "Use ONLY the supplied text. Do not guess or infer anything the "
           "text does not state. For every extracted fact, return the exact "
           "excerpt from the supplied text that supports it. Omit anything the "
           "text does not support. Never claim a fact is verified.";
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

} // namespace

ModelScopeCandidateAdapter::ModelScopeCandidateAdapter(
    IExtractionTransport &transport, ModelScopeAdapterConfig config)
    : transport_(transport), config_(std::move(config))
{
}

std::string ModelScopeCandidateAdapter::buildWireBodyForTest(
    const core::ExtractionRequest &request) const
{
    // The provider wire shape mirrors the accepted M6 Chat Completions body:
    // { model, messages:[{role:system,content},{role:user,content}],
    //   stream:false, max_tokens }. It is constructed internally so that no
    // provider type leaks into the provider-neutral contract.
    std::string body = "{";
    body += "\"model\":";
    body += escapeJsonString(config_.modelId);
    body += ",\"messages\":[";
    body += "{\"role\":\"system\",\"content\":";
    body += escapeJsonString(buildSystemInstruction());
    body += "},";
    body += "{\"role\":\"user\",\"content\":";
    body += escapeJsonString(buildUserPrompt(request));
    body += "}],";
    body += "\"stream\":false,";
    body += "\"max_tokens\":";
    body += std::to_string(config_.maxOutputTokens);
    body += "}";
    return body;
}

core::ProviderExtractionResult ModelScopeCandidateAdapter::extract(
    const core::ExtractionRequest &request)
{
    lastFailure_ = core::ProviderExtractionFailure::None;

    // Every failure path returns a result with an EMPTY proposal list, so a
    // caller can never mistake a failure for a partial success (§13).
    const std::string wireBody = buildWireBodyForTest(request);
    const IExtractionTransport::Exchange exchange = transport_.send(wireBody);

    if (!exchange.transportOk) {
        lastFailure_ = core::ProviderExtractionFailure::TransportError;
        core::ProviderExtractionResult failed;
        failed.ok = false;
        failed.failure = lastFailure_;
        return failed;
    }
    if (!exchange.statusOk) {
        lastFailure_ = core::ProviderExtractionFailure::ProviderRejectedStatus;
        core::ProviderExtractionResult failed;
        failed.ok = false;
        failed.failure = lastFailure_;
        return failed;
    }

    // The raw provider body is used HERE ONLY and never stored: it does not
    // leave this call scope (H4 / §14).
    const ProviderContentExtraction content =
        extractModelScopeResponseContent(exchange.responseBody);
    if (!content.ok) {
        lastFailure_ = core::ProviderExtractionFailure::MalformedResponse;
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
    return result;
}

std::vector<core::CandidateProposal> ModelScopeCandidateAdapter::propose(
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