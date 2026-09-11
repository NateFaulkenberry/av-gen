// NSURLSession transport (ADR-094). Objective-C++ because NSURLSession has no C interface; the
// portable half is `http_portable.cpp`, chosen by CMake exactly as the MIDI, font and Syphon
// pairs are.
//
// Three things this file is careful about:
//
//   - **No header value is ever logged.** The request log line carries the method and the host,
//     and stops there. One `Authorization:` header in a log file is a leaked credential.
//   - **Cancellation actually cancels.** The wait is a semaphore polled in short slices against
//     the CancelToken, and a cancelled request calls `-cancel` on the task rather than letting it
//     finish into a result nobody will read.
//   - **Nothing here touches the main thread.** `NSURLSession`'s completion handler runs on its
//     own delegate queue; the caller is a JobSystem worker blocked on the semaphore. No run loop
//     is required, which is why this works from a plain worker thread at all.

#include "ai/http.hpp"

#include "core/log.hpp"

#import <Foundation/Foundation.h>

#include <chrono>
#include <string>

namespace avgen::ai {
namespace {

// Host and path only. Query strings are excluded deliberately: Gemini historically accepted the
// API key as `?key=`, and a log that printed the query would leak it for anyone still using that
// form.
std::string safeTarget(const std::string& url) {
    NSURL* parsed = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    if (parsed == nil) {
        return "<unparseable url>";
    }
    NSString* host = parsed.host != nil ? parsed.host : @"";
    NSString* path = parsed.path != nil ? parsed.path : @"";
    return std::string([[NSString stringWithFormat:@"%@%@", host, path] UTF8String]);
}

class DarwinHttpClient final : public HttpClient {
public:
    Result<HttpResponse> send(const HttpRequest& request) override {
        @autoreleasepool {
            NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:request.url.c_str()]];
            if (url == nil) {
                return fail("not a valid URL: {}", request.url);
            }
            NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:url];
            req.HTTPMethod = [NSString stringWithUTF8String:request.method.c_str()];
            req.timeoutInterval = request.timeoutSeconds;
            for (const auto& [name, value] : request.headers) {
                [req setValue:[NSString stringWithUTF8String:value.c_str()]
                    forHTTPHeaderField:[NSString stringWithUTF8String:name.c_str()]];
            }
            if (!request.body.empty()) {
                req.HTTPBody = [NSData dataWithBytes:request.body.data()
                                              length:static_cast<NSUInteger>(request.body.size())];
            }

            log::debug("ai/http: {} {}", request.method, safeTarget(request.url));

            __block HttpResponse response;
            __block std::string transportError;
            dispatch_semaphore_t done = dispatch_semaphore_create(0);

            NSURLSessionDataTask* task = [[NSURLSession sharedSession]
                dataTaskWithRequest:req
                  completionHandler:^(NSData* data, NSURLResponse* urlResponse, NSError* error) {
                    if (error != nil) {
                        transportError = std::string([[error localizedDescription] UTF8String]);
                    } else if ([urlResponse isKindOfClass:[NSHTTPURLResponse class]]) {
                        auto* http = static_cast<NSHTTPURLResponse*>(urlResponse);
                        response.status = static_cast<int>([http statusCode]);
                    }
                    if (data != nil && data.length > 0) {
                        response.body.assign(static_cast<const char*>(data.bytes),
                                             static_cast<std::size_t>(data.length));
                    }
                    dispatch_semaphore_signal(done);
                  }];
            [task resume];

            // Polled rather than waited on outright, so a Cancel in the panel reaches an in-flight
            // request within a frame or two instead of after the provider's timeout.
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::duration<double>(request.timeoutSeconds + 5.0);
            while (true) {
                const auto slice = dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC);
                if (dispatch_semaphore_wait(done, slice) == 0) {
                    break;
                }
                if (request.cancel.cancelled()) {
                    [task cancel];
                    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
                    return fail("request cancelled");
                }
                if (std::chrono::steady_clock::now() > deadline) {
                    [task cancel];
                    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
                    return fail("request to {} timed out after {:.0f}s", safeTarget(request.url),
                                request.timeoutSeconds);
                }
            }

            if (!transportError.empty()) {
                return fail("{}: {}", safeTarget(request.url), transportError);
            }
            if (response.status == 0) {
                return fail("{}: no HTTP response", safeTarget(request.url));
            }
            return response;
        }
    }

    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string_view name() const override { return "NSURLSession"; }
};

} // namespace

std::unique_ptr<HttpClient> makeSystemHttpClient() { return std::make_unique<DarwinHttpClient>(); }

} // namespace avgen::ai
