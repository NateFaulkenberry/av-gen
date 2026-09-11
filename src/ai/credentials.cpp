#include "ai/credentials.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

namespace avgen::ai {
namespace {

// One service name for the whole application, accounts inside it. That is what makes the items
// appear together in Keychain Access.app, which is where a person goes to rotate or revoke one.
constexpr const char* kService = "com.avgen.ai";

} // namespace

const char* credentialSourceName(CredentialSource source) {
    switch (source) {
    case CredentialSource::None: return "none";
    case CredentialSource::Keystore: return "keystore";
    case CredentialSource::Environment: return "environment";
    }
    return "none";
}

std::string CredentialStore::environmentVariableFor(std::string_view account) {
    std::string name = "AVGEN_AI_";
    for (const char c : account) {
        name += std::isalnum(static_cast<unsigned char>(c))
                    ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                    : '_';
    }
    name += "_KEY";
    return name;
}

std::optional<std::string> CredentialStore::resolve(std::string_view account) const {
    // Environment first: see the header. A headless or SSH session may not be able to read the
    // keystore at all, and an operator overriding a key for one run should not have to write it
    // into stored state to do so.
    const std::string variable = environmentVariableFor(account);
    if (const char* value = std::getenv(variable.c_str()); value != nullptr && *value != '\0') {
        return std::string(value);
    }
    return load(account);
}

CredentialStatus CredentialStore::status(std::string_view account) const {
    CredentialStatus out;
    const std::string variable = environmentVariableFor(account);
    if (const char* value = std::getenv(variable.c_str()); value != nullptr && *value != '\0') {
        out.configured = true;
        out.source = CredentialSource::Environment;
        // The variable's *name*, never its value.
        out.detail = variable + " is set and takes precedence";
        return out;
    }
    if (has(account)) {
        out.configured = true;
        out.source = CredentialSource::Keystore;
        out.detail = std::string("stored in the ") + std::string(backend());
        return out;
    }
    out.detail = writable() ? "not configured" : "not configured, and this build cannot store one";
    return out;
}

// ---- in-memory (tests) ---------------------------------------------------------------------------

Result<void> MemoryCredentialStore::store(std::string_view account, std::string_view secret) {
    const auto it = std::find_if(items_.begin(), items_.end(),
                                 [&](const auto& e) { return e.first == account; });
    if (it != items_.end()) {
        it->second = std::string(secret);
    } else {
        items_.emplace_back(std::string(account), std::string(secret));
    }
    return {};
}

std::optional<std::string> MemoryCredentialStore::load(std::string_view account) const {
    const auto it = std::find_if(items_.begin(), items_.end(),
                                 [&](const auto& e) { return e.first == account; });
    return it == items_.end() ? std::nullopt : std::optional<std::string>(it->second);
}

Result<void> MemoryCredentialStore::erase(std::string_view account) {
    std::erase_if(items_, [&](const auto& e) { return e.first == account; });
    return {};
}

bool MemoryCredentialStore::has(std::string_view account) const {
    return std::any_of(items_.begin(), items_.end(),
                       [&](const auto& e) { return e.first == account; });
}

// ---- macOS Keychain --------------------------------------------------------------------------------

#ifdef __APPLE__
namespace {

// Small RAII so an early return cannot leak a CF object. Manual CFRelease is correct here (no ARC:
// this is plain C++, because Security is a C API and pulling Objective-C in for it would buy
// nothing).
template <typename T>
class CfRef {
public:
    CfRef() = default;
    explicit CfRef(T ref) : ref_(ref) {}
    ~CfRef() {
        if (ref_ != nullptr) {
            CFRelease(ref_);
        }
    }
    CfRef(const CfRef&) = delete;
    CfRef& operator=(const CfRef&) = delete;
    CfRef(CfRef&& other) noexcept : ref_(other.ref_) { other.ref_ = nullptr; }
    CfRef& operator=(CfRef&& other) noexcept {
        if (this != &other) {
            if (ref_ != nullptr) {
                CFRelease(ref_);
            }
            ref_ = other.ref_;
            other.ref_ = nullptr;
        }
        return *this;
    }
    [[nodiscard]] T get() const { return ref_; }
    [[nodiscard]] T* out() { return &ref_; }
    explicit operator bool() const { return ref_ != nullptr; }

private:
    T ref_ = nullptr;
};

CfRef<CFStringRef> cfString(std::string_view text) {
    return CfRef<CFStringRef>(CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.data()),
        static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false));
}

std::string statusMessage(OSStatus status) {
    CfRef<CFStringRef> message(SecCopyErrorMessageString(status, nullptr));
    if (!message) {
        return fmt::format("OSStatus {}", static_cast<int>(status));
    }
    const CFIndex length = CFStringGetLength(message.get());
    const CFIndex capacity = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<std::size_t>(capacity), '\0');
    if (!CFStringGetCString(message.get(), out.data(), capacity, kCFStringEncodingUTF8)) {
        return fmt::format("OSStatus {}", static_cast<int>(status));
    }
    out.resize(std::strlen(out.c_str()));
    return fmt::format("{} ({})", out, static_cast<int>(status));
}

// The lookup half of every operation: class, service, account, and an explicit opt-out of the
// data-protection keychain. Note what is *not* here -- no kSecAttrAccessible, no
// kSecAttrAccessGroup. Both are accepted and then silently dropped on the file-based keychain, so
// setting them would record a guarantee that is not kept.
CfRef<CFMutableDictionaryRef> baseQuery(std::string_view account) {
    CfRef<CFMutableDictionaryRef> query(CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    if (!query) {
        return query;
    }
    CFDictionarySetValue(query.get(), kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query.get(), kSecUseDataProtectionKeychain, kCFBooleanFalse);
    const CfRef<CFStringRef> service = cfString(kService);
    const CfRef<CFStringRef> accountRef = cfString(account);
    if (service) {
        CFDictionarySetValue(query.get(), kSecAttrService, service.get());
    }
    if (accountRef) {
        CFDictionarySetValue(query.get(), kSecAttrAccount, accountRef.get());
    }
    return query;
}

class KeychainCredentialStore final : public CredentialStore {
public:
    [[nodiscard]] std::string_view backend() const override { return "macOS keychain"; }
    [[nodiscard]] bool writable() const override { return true; }

    Result<void> store(std::string_view account, std::string_view secret) override {
        if (secret.empty()) {
            return erase(account);
        }
        CfRef<CFMutableDictionaryRef> query = baseQuery(account);
        if (!query) {
            return fail("could not build a keychain query");
        }
        CfRef<CFDataRef> data(CFDataCreate(kCFAllocatorDefault,
                                           reinterpret_cast<const UInt8*>(secret.data()),
                                           static_cast<CFIndex>(secret.size())));
        if (!data) {
            return fail("could not wrap the credential for the keychain");
        }
        CFDictionarySetValue(query.get(), kSecValueData, data.get());

        // Add first, update on duplicate. One round trip in the common case, no read of the
        // existing secret on the write path, and no window between "does it exist" and "write it".
        OSStatus status = SecItemAdd(query.get(), nullptr);
        if (status == errSecDuplicateItem) {
            CfRef<CFMutableDictionaryRef> lookup = baseQuery(account);
            CfRef<CFMutableDictionaryRef> update(
                CFDictionaryCreateMutable(kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks,
                                          &kCFTypeDictionaryValueCallBacks));
            if (!lookup || !update) {
                return fail("could not build a keychain update");
            }
            // Attributes only: a kSecClass or kSecReturn* key in here is rejected.
            CFDictionarySetValue(update.get(), kSecValueData, data.get());
            status = SecItemUpdate(lookup.get(), update.get());
        }
        if (status != errSecSuccess) {
            return fail("keychain: {}", statusMessage(status));
        }
        return {};
    }

    [[nodiscard]] std::optional<std::string> load(std::string_view account) const override {
        CfRef<CFMutableDictionaryRef> query = baseQuery(account);
        if (!query) {
            return std::nullopt;
        }
        // kSecReturnData alone yields a CFDataRef. Asking for attributes as well would yield a
        // CFDictionaryRef instead, and casting that to CFDataRef is a crash rather than an error.
        CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);
        CfRef<CFTypeRef> result;
        const OSStatus status = SecItemCopyMatching(query.get(), result.out());
        if (status != errSecSuccess || !result) {
            if (status != errSecItemNotFound) {
                // errSecInteractionNotAllowed is the SSH / headless case and is not a fault: the
                // caller falls back to the environment variable. Logged at debug so a puzzled user
                // can find it without it shouting at everyone else.
                log::debug("ai/credentials: keychain read for '{}': {}", account,
                           statusMessage(status));
            }
            return std::nullopt;
        }
        const auto data = static_cast<CFDataRef>(result.get());
        if (CFGetTypeID(data) != CFDataGetTypeID()) {
            return std::nullopt;
        }
        const auto* bytes = CFDataGetBytePtr(data);
        const CFIndex length = CFDataGetLength(data);
        if (bytes == nullptr || length <= 0) {
            return std::nullopt;
        }
        return std::string(reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(length));
    }

    Result<void> erase(std::string_view account) override {
        CfRef<CFMutableDictionaryRef> query = baseQuery(account);
        if (!query) {
            return fail("could not build a keychain query");
        }
        const OSStatus status = SecItemDelete(query.get());
        if (status != errSecSuccess && status != errSecItemNotFound) {
            return fail("keychain: {}", statusMessage(status));
        }
        return {};
    }

    [[nodiscard]] bool has(std::string_view account) const override {
        CfRef<CFMutableDictionaryRef> query = baseQuery(account);
        if (!query) {
            return false;
        }
        // No kSecReturnData: existence is the question, and reading the secret to answer it would
        // put a credential in memory for no reason.
        CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);
        return SecItemCopyMatching(query.get(), nullptr) == errSecSuccess;
    }
};

} // namespace

std::unique_ptr<CredentialStore> makeSystemCredentialStore() {
    return std::make_unique<KeychainCredentialStore>();
}

#else

namespace {

// No platform keystore. Storing fails and says so; `resolve()` still finds an environment
// variable, so a build on such a platform is configurable without inventing a file format for
// secrets.
class UnavailableCredentialStore final : public CredentialStore {
public:
    [[nodiscard]] std::string_view backend() const override { return "none"; }
    [[nodiscard]] bool writable() const override { return false; }
    Result<void> store(std::string_view account, std::string_view) override {
        return fail("this platform has no credential store; set {} instead",
                    environmentVariableFor(account));
    }
    [[nodiscard]] std::optional<std::string> load(std::string_view) const override {
        return std::nullopt;
    }
    Result<void> erase(std::string_view) override { return {}; }
    [[nodiscard]] bool has(std::string_view) const override { return false; }
};

} // namespace

std::unique_ptr<CredentialStore> makeSystemCredentialStore() {
    return std::make_unique<UnavailableCredentialStore>();
}

#endif

} // namespace avgen::ai
