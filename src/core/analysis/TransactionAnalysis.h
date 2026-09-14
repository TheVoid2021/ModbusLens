#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/protocol/ModbusRtuFrame.h"

namespace modbuslens::core {

enum class TransactionStatus {
    Pending,       // no response yet, elapsed < timeoutThreshold
    Success,       // legal, matching normal response
    Exception,     // legal, matching Modbus exception response
    CrcError,      // wire arrived but decode failed with CrcMismatch
    Timeout,       // no response and elapsed >= timeoutThreshold
    ProtocolError, // data arrived but cannot be a valid match for this request
    // T015 Gate C / Part C orthogonal semantics (ADR-003): the RESPONSE
    // expectation / observed outcome of a broadcast-capable request — no
    // response was observed and protocol semantics do not expect one.
    // Request semantic validity is an ORTHOGONAL dimension carried by the
    // request-issues collection, never folded into this status. It proves
    // NOTHING about device write success or device health.
    ExpectedNoResponse,
};

// T014: orthogonal deterministic diagnostic detail. A TransactionIssue is
// the REASON the analyzer knew at classification time — a directly observed
// protocol/transaction fact, never a physical root-cause claim (M8.1 §5/§27
// discipline). It deliberately does NOT widen the six-status outcome axis.
enum class TransactionIssueCode {
    ResponseFrameTooShort,      // response wire below the minimum RTU frame
    ResponseAddressMismatch,    // response.address != request.address
    MalformedExceptionResponse, // 0x83-shaped reply failed exception decoding
    MalformedNormalResponse,    // 0x03 reply failed normal-response decoding
    QuantityMismatch,           // response value count != request quantity
    UnexpectedResponseFunction, // any other response function code
    UnknownProtocolError,       // deterministic defensive/fallback branch only
    // ---- T015 additive (FC06 / broadcast / Function 0x10) ----
    WriteSingleRegisterEchoMismatch, // FC06 echo fields differ from request
    UnexpectedResponseForBroadcast,  // any bytes replied to a broadcast
    WriteMultipleRegistersEchoMismatch, // 0x10 reply shaped right, fields differ
};

// Sparse context payload: each optional is populated ONLY when its code
// requires it (per-code invariants, locked by transaction-analysis tests).
struct TransactionIssue {
    TransactionIssueCode code{};

    std::optional<std::uint8_t> expectedAddress;
    std::optional<std::uint8_t> actualAddress;

    std::optional<std::uint8_t> actualFunctionCode;

    std::optional<std::uint16_t> expectedQuantity;
    std::optional<std::uint16_t> actualQuantity;

    // ---- T015 FC06 echo mismatch payload (register semantics: the address
    // columns above are Modbus DEVICE addresses, never register addresses).
    std::optional<std::uint16_t> expectedRegisterAddress;
    std::optional<std::uint16_t> actualRegisterAddress;
    std::optional<std::uint16_t> expectedRegisterValue;
    std::optional<std::uint16_t> actualRegisterValue;

    bool operator==(const TransactionIssue&) const = default;
};

// Stable machine serialization token for adapters/tools (e.g.
// "response_address_mismatch"). Deliberately NOT human UI prose — that
// mapping belongs to the Qt adapter layer.
std::string_view transactionIssueName(TransactionIssueCode code);

// ---------------------------------------------------------------------------
// T015 Gate B: request-side orthogonal deterministic detail. Produced ONLY
// by the Passive Core analyzer (never by T007 — its trusted-request contract
// stays intact; never re-derived by Replay/Diagnosis/Prompt/Agent/UI).
// A request issue records "what the captured request violated" as an
// observed fact — never a root-cause guess (no WrongDeviceConfiguration /
// BadPLCProgram / OperatorError codes, ever).
// ---------------------------------------------------------------------------

enum class TransactionRequestIssueCode {
    InvalidRequestQuantity,   // quantity outside the function's legal domain
    InvalidRequestLength,     // request data length not valid for the function
    InvalidRequestByteCount,  // declared byteCount != 2*quantity (valid qty)
    InvalidBroadcastFunction, // address==0 with a non-broadcast function
};

struct TransactionRequestIssue {
    TransactionRequestIssueCode code{};

    // InvalidRequestQuantity payload (observed + the exact legal domain;
    // T015 Part C: min is recorded for both FC03 (1..125) and Function16
    // (1..123) so a quantity=0 cannot be misread as "only over the max").
    std::optional<std::uint16_t> observedQuantity;
    std::optional<std::uint16_t> minAllowedQuantity;
    std::optional<std::uint16_t> maxAllowedQuantity;

    // InvalidRequestByteCount payload (byteCount is a wire 1-byte field;
    // expected = 2*quantity <= 246, so uint8 is the exact wire width).
    std::optional<std::uint8_t> observedByteCount;
    std::optional<std::uint8_t> expectedByteCount;

    // InvalidRequestLength payload. Length unit (stable definition):
    // Function16 request frame.data bytes; expected = 5 + declaredByteCount,
    // absent when not reliably computable. NOT uint8_t on purpose: the
    // expression can reach 260 (quantity=124 => byteCount=248 => data=253,
    // which itself crosses the RTU frame boundary). uint16_t is protocol-
    // sufficient and serialization-stable (size_t NOT chosen: build-width
    // dependent serialization).
    std::optional<std::uint16_t> observedLength;
    std::optional<std::uint16_t> expectedLength;

    bool operator==(const TransactionRequestIssue&) const = default;
};

// Stable machine serialization token (adapter/tool serialization only).
std::string_view transactionRequestIssueName(TransactionRequestIssueCode code);

// A transaction is a request plus its response/failure observation — never a
// single frame. NoResponse is deliberately decoupled from T006's
// DroppedResponse: the analyzer speaks the abstract observation language.
struct NoResponse {
    bool operator==(const NoResponse&) const = default;
};

using ResponseObservation = std::variant<ModbusRtuFrame, RtuDecodeError, NoResponse>;

struct TransactionAnalysis {
    TransactionStatus status{};
    std::chrono::milliseconds elapsed{0};
    std::optional<std::uint8_t> exceptionCode;   // set only for Exception
    // T014 (append-last for aggregate source compatibility). Production
    // invariant: status == ProtocolError => issue.has_value(); all other
    // statuses leave it nullopt. Downstream consumers stay defensive:
    // a ProtocolError WITHOUT an issue must be rendered by omitting the
    // detail — never by guessing a reason.
    std::optional<TransactionIssue> issue;

    bool operator==(const TransactionAnalysis&) const = default;
};

// Analyze one Function 0x03 transaction. Pure function: elapsed and the
// timeout threshold are caller-provided facts — no clock, no waiting. The
// request contract is "an already-validated Function 0x03 request".
TransactionAnalysis analyzeFunction03Transaction(
    const ModbusRtuFrame& request,
    const ResponseObservation& observation,
    std::chrono::milliseconds elapsed,
    std::chrono::milliseconds timeoutThreshold);

} // namespace modbuslens::core