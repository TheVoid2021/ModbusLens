#pragma once

#include <string>
#include <string_view>

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C2 SECOND SLICE — provider transport SEAM (T027 §68, §15).
//
// The smallest possible injection point: one synchronous request/response
// exchange with an opaque string body. It is intentionally NOT a generalized
// HTTP framework — it exists so the adapter can be exercised by a
// deterministic fake, with no live network, no token and no cost.
//
// There is deliberately no URL / header / status type here: a provider-
// specific transport shape would be a contract leak. The CONCRETE ModelScope
// implementation (which knows the endpoint, the Authorization header and how
// to map a status) is a separate, production-only type; the automated tests
// use the fake below and link no network type at all.
// ---------------------------------------------------------------------------
class IExtractionTransport
{
public:
    virtual ~IExtractionTransport() = default;

    struct Exchange {
        std::string responseBody;
        bool transportOk{false};  // the exchange itself completed
        bool statusOk{false};     // the provider reported a success status
    };

    // Sends the request body and returns the exchange outcome. Implementations
    // own their own transport concerns entirely; nothing about them leaks
    // upward. A failure here must leave the caller's state untouched.
    [[nodiscard]] virtual Exchange send(std::string_view requestBody) = 0;
};

} // namespace modbuslens::ui