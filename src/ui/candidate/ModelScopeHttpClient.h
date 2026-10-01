#pragma once

#include <QObject>

#include <string>

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C2 FOURTH SLICE — the thin HTTP seam under the production transport
// (T027 §76, SESSION P §9/§14).
//
// This exists for ONE reason: the production transport must be exercisable by
// deterministic offline tests WITHOUT making the production endpoint
// user-configurable. The endpoint stays a compiled constant inside the
// transport; only this thin layer is injected, so a test can capture the exact
// method target / Authorization / content type / body that production would
// have sent while performing zero network I/O.
// ---------------------------------------------------------------------------
struct ModelScopeHttpRequest {
    std::string url;
    std::string bearerToken;
    std::string contentType;
    std::string body;
    int timeoutMs{0};
};

struct ModelScopeHttpResult {
    int status{0};           // 0 = no HTTP status at all (transport-level)
    std::string body;
    bool transportOk{false}; // a reply was obtained (any status)
};

class IModelScopeHttpClient
{
public:
    virtual ~IModelScopeHttpClient() = default;

    [[nodiscard]] virtual ModelScopeHttpResult post(
        const ModelScopeHttpRequest &request) = 0;
};

// ---------------------------------------------------------------------------
// PRODUCTION implementation (Qt Network).
//
// Ownership mirrors the accepted M6 client: one QNetworkAccessManager per
// exchange owned locally, one QTimer as the SINGLE timeout owner (no
// setTransferTimeout — dual ownership was ISSUE-005), Authorization attached
// only to the configured endpoint, and a same-origin redirect policy so the
// bearer token can never be forwarded to an unrelated host. TLS peer
// verification stays ON (no bypass, no certificate-ignore code).
//
// The C2 provider seam (SESSION N `IExtractionTransport::send`) is synchronous
// by accepted design, so this implementation runs a bounded local event loop
// until the reply or the timeout fires. The bound is the accepted 90 s M6
// timeout. KNOWN LIMITATION: a synchronous seam means the calling thread waits
// for the exchange; making the chain asynchronous would be a contract change to
// the accepted SESSION N seam and is deliberately out of scope for this slice.
// ---------------------------------------------------------------------------
class QtModelScopeHttpClient : public QObject, public IModelScopeHttpClient
{
    Q_OBJECT

public:
    explicit QtModelScopeHttpClient(QObject *parent = nullptr);

    [[nodiscard]] ModelScopeHttpResult post(
        const ModelScopeHttpRequest &request) override;
};

} // namespace modbuslens::ui