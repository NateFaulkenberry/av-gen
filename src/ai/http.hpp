#pragma once

// The HTTP transport for cloud and local providers (ADR-094).
//
// ## Why there is no HTTP library here
//
// ADR-008 pins every dependency and asks what each one buys. A general HTTP client (libcurl, cpr,
// Boost.Beast) would be a new pinned dependency with a TLS story, a certificate story and a
// build-time cost, bought to make a handful of JSON POSTs from a macOS application that already
// links six Apple frameworks for exactly this class of reason -- AVFoundation for video, CoreMIDI
// for MIDI, CoreText for fonts, Security for credentials.
//
// `NSURLSession` is the system's own client: TLS, proxies, the user's certificate trust settings,
// HTTP/2 and ATS, all maintained by the OS. The file pair below follows the same shape the
// repository already uses five times over -- `midi_coremidi.mm` / `midi_stub.cpp`,
// `font_coretext.mm` / `font_stub.cpp`, `syphon_share.mm` / `syphon_share_stub.cpp`. On a platform
// with no implementation the client reports itself unavailable and the AI panel says the provider
// cannot be reached, which is an ordinary state (ADR-065), not a build failure.
//
// ## Threading
//
// `send()` blocks the calling thread until the response arrives, the timeout expires or the
// `CancelToken` fires. It is called only from a `JobSystem` worker. It is never callable from the
// render, audio or UI thread for the same structural reason inference is not: nothing reachable
// from those threads holds one.
//
// ## Secrets
//
// Header values are never logged. The log line for a request carries the method, the host and the
// path, and nothing else -- an `Authorization` or `x-api-key` header printed once into a log file
// is a leaked credential for as long as that file exists (§34).

#include "ai/tool_context.hpp"
#include "core/error.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avgen::ai {

struct HttpRequest {
    std::string url;
    std::string method = "POST";
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    double timeoutSeconds = 180.0;
    CancelToken cancel;
};

struct HttpResponse {
    int status = 0;
    std::string body;
    [[nodiscard]] bool ok() const { return status >= 200 && status < 300; }
};

class HttpClient {
public:
    virtual ~HttpClient() = default;

    // Transport-level failure (no route, TLS refused, timeout, cancellation) is an `Error`. An
    // HTTP status is not: a 401 is a successful round trip carrying bad news, and the provider
    // adapter turns it into a message a person can act on.
    [[nodiscard]] virtual Result<HttpResponse> send(const HttpRequest& request) = 0;
    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string_view name() const = 0;
};

// The platform client, or an unavailable one where there is none.
[[nodiscard]] std::unique_ptr<HttpClient> makeSystemHttpClient();

// A client that answers from a table. Tests only: it is how the provider adapters are checked
// against recorded wire payloads without a network or a key (§52).
class ScriptedHttpClient final : public HttpClient {
public:
    struct Exchange {
        std::string urlContains; // matched as a substring, so a test need not spell out the host
        HttpResponse response;
    };

    void push(Exchange exchange) { exchanges_.push_back(std::move(exchange)); }
    [[nodiscard]] Result<HttpResponse> send(const HttpRequest& request) override;
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string_view name() const override { return "scripted"; }

    // What was sent, so a test can assert the request shape as well as the parse.
    [[nodiscard]] const std::vector<HttpRequest>& sent() const { return sent_; }

private:
    std::vector<Exchange> exchanges_;
    std::vector<HttpRequest> sent_;
    std::size_t cursor_ = 0;
};

} // namespace avgen::ai
