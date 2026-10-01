#include "ui/candidate/ModelScopeExtractionTransport.h"

#include <utility>

namespace modbuslens::ui {

ModelScopeExtractionTransport::ModelScopeExtractionTransport(
    IModelScopeHttpClient &http, std::string endpoint, std::string bearerToken,
    int timeoutMs)
    : http_(http),
      endpoint_(std::move(endpoint)),
      bearerToken_(std::move(bearerToken)),
      timeoutMs_(timeoutMs)
{
}

IExtractionTransport::Exchange ModelScopeExtractionTransport::send(
    std::string_view requestBody)
{
    Exchange exchange;
    // Fail closed WITHOUT dispatching when there is nothing usable to send
    // with: an empty credential, endpoint or body must never become a request.
    if (bearerToken_.empty() || requestBody.empty() || endpoint_.empty()) {
        return exchange;
    }

    ModelScopeHttpRequest request;
    request.url = endpoint_;
    request.bearerToken = bearerToken_;
    request.contentType = kJsonContentType;
    request.body = std::string(requestBody);
    request.timeoutMs = timeoutMs_;

    ++dispatchCount_;
    const ModelScopeHttpResult result = http_.post(request);

    if (!result.transportOk) {
        return exchange; // transport-level failure: transportOk stays false
    }
    exchange.transportOk = true;
    exchange.responseBody = result.body;
    exchange.statusOk = result.status >= 200 && result.status < 300;
    return exchange;
}

} // namespace modbuslens::ui