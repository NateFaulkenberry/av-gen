// The portable half of the HTTP transport (ADR-094). Compiled where `http_darwin.mm` is not.
//
// This is not a stub in the sense of "unfinished". It is the honest answer for a platform whose
// system HTTP client has not been wired up: the client reports itself unavailable, the control
// plane says "AI is not available in this build", and every other part of AV Gen works exactly as
// it did. ADR-065's rule -- an optional failure must never become a total failure -- is the whole
// reason this file exists rather than a `#error`.

#include "ai/http.hpp"

namespace avgen::ai {
namespace {

class UnavailableHttpClient final : public HttpClient {
public:
    Result<HttpResponse> send(const HttpRequest&) override {
        return fail("this build has no HTTP transport, so no AI provider can be reached");
    }
    [[nodiscard]] bool available() const override { return false; }
    [[nodiscard]] std::string_view name() const override { return "unavailable"; }
};

} // namespace

std::unique_ptr<HttpClient> makeSystemHttpClient() {
    return std::make_unique<UnavailableHttpClient>();
}

} // namespace avgen::ai
