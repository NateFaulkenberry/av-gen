#include "ai/http.hpp"

namespace avgen::ai {

Result<HttpResponse> ScriptedHttpClient::send(const HttpRequest& request) {
    sent_.push_back(request);
    // Matched in order rather than by lookup: an agent loop makes several calls to the same URL
    // and a test's point is usually the *sequence* of answers, not the routing.
    for (std::size_t i = cursor_; i < exchanges_.size(); ++i) {
        if (exchanges_[i].urlContains.empty() ||
            request.url.find(exchanges_[i].urlContains) != std::string::npos) {
            cursor_ = i + 1;
            return exchanges_[i].response;
        }
    }
    return fail("scripted http: no response left for {} {}", request.method, request.url);
}

} // namespace avgen::ai
