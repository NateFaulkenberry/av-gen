#pragma once

// Credential storage (ADR-094, spec §6, addendum §9).
//
// ## The rule
//
// A provider credential lives in the operating system's keystore and **nowhere else**. Not in a
// project file, not in a scene, not in the settings JSON, not in a conversation log, not in a tool
// result, not in a log line, not in source control, and it is never displayed again after it is
// entered. The addendum is explicit that this must use the platform facility rather than
// hand-rolled encryption, and it is right: a bespoke cipher with a key next to the ciphertext is
// obfuscation described as security, which is worse than admitting there is none.
//
// Three structural properties enforce it:
//
//   - **`ProviderConfig` has no field for a secret.** There is no place to put one, so serialising
//     configuration cannot leak one, and `ProviderConfig::fromJson` rejects a file that carries a
//     key-shaped field rather than reading it.
//   - **`CredentialStore::load` is the only way a secret enters memory**, and callers move it
//     straight into a provider. It is never copied into a struct that is serialised.
//   - **`has()` exists so the UI never needs the value.** Settings shows "configured" or "not
//     configured" and a masked field; it never reads a stored key back to display it.
//
// ## What the keychain actually buys here -- stated accurately
//
// This was measured rather than assumed. With the file-based keychain and an unsigned binary,
// `/usr/bin/security find-generic-password -w` -- a program that is emphatically not in the item's
// trusted-application list -- printed the secret with no prompt at all. So the keychain here is
// **not** isolation from other local code running as the same user, and this file will not say it
// is. What it genuinely provides:
//
//   - the key is not in a dotfile, in shell history, in the project, or anywhere reachable by an
//     accidental `git add`;
//   - the user can see, rotate and delete it in Keychain Access.app, which is where people already
//     look for exactly this;
//   - we are not shipping a hand-rolled cipher with its key stored beside the ciphertext, which is
//     obfuscation labelled as security and is the thing §6 forbids.
//
// That is a real and worthwhile improvement over a config file, and it is the whole claim.
// Overstating it in a comment, an ADR or a help page would be worse than not using it.
//
// ## Why the environment is consulted first, and why the fallback is not a file
//
// `AVGEN_AI_<ID>_KEY` is read before the keystore. Three reasons: a headless render, a CI run and
// an SSH session have no interactive keychain (a keychain read over SSH fails with
// `errSecInteractionNotAllowed`, which looks like a hang to whoever is watching); an operator
// overriding a key for one run should not have to edit stored state; and the shell owns the
// variable's lifetime, so nothing we write persists. A dotfile would be the opposite of all three.
//
// Because the environment wins, `status()` reports *which* source is in effect, and Settings shows
// it. A UI that said "stored in keychain" while a stale environment variable was actually being
// used would be lying in the one place a person goes to find out why the wrong key is in use.
//
// ## macOS specifics
//
// `kSecClassGenericPassword` in the **file-based** keychain, keyed by service + account, written
// with add-then-update-on-duplicate. The data-protection keychain is explicitly opted out of
// (`kSecUseDataProtectionKeychain = false`) rather than merely not requested: it needs a
// `keychain-access-groups` entitlement, which needs a provisioning profile, which needs a bundle
// -- none of which a binary run out of `build/release` has, and a write to it returns
// `errSecMissingEntitlement`. Its *read* path returns "not found" instead, so probing reads alone
// would have suggested it worked.
//
// `kSecAttrAccessible` and `kSecAttrAccessGroup` are deliberately not set. On the file-based
// keychain they are accepted without error and then silently dropped -- reading the item's
// attributes back shows no accessibility at all -- so setting them would encode a guarantee that
// does not exist.

#include "core/error.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::ai {

// Where a loaded credential came from, for the UI to show honestly. The value never appears here.
enum class CredentialSource : std::uint8_t { None, Keystore, Environment };
[[nodiscard]] const char* credentialSourceName(CredentialSource source);

struct CredentialStatus {
    bool configured = false;
    CredentialSource source = CredentialSource::None;
    std::string detail; // "stored in the macOS keychain", "AVGEN_AI_OPENAI_KEY is set"
};

class CredentialStore {
public:
    virtual ~CredentialStore() = default;

    [[nodiscard]] virtual std::string_view backend() const = 0;
    // False when the platform has no keystore. Storing is then impossible; the environment
    // fallback still works, which is what keeps a headless run usable.
    [[nodiscard]] virtual bool writable() const = 0;

    [[nodiscard]] virtual Result<void> store(std::string_view account, std::string_view secret) = 0;
    // Empty optional means "nothing stored", which is an ordinary state, not an error.
    [[nodiscard]] virtual std::optional<std::string> load(std::string_view account) const = 0;
    [[nodiscard]] virtual Result<void> erase(std::string_view account) = 0;
    [[nodiscard]] virtual bool has(std::string_view account) const = 0;

    // `AVGEN_AI_<ACCOUNT>_KEY` first, then the keystore. Account punctuation becomes '_' and the
    // name is upper-cased, so "openai-compatible" reads AVGEN_AI_OPENAI_COMPATIBLE_KEY.
    [[nodiscard]] std::optional<std::string> resolve(std::string_view account) const;
    [[nodiscard]] CredentialStatus status(std::string_view account) const;
    [[nodiscard]] static std::string environmentVariableFor(std::string_view account);
};

// The platform store: macOS Keychain where available, otherwise one that cannot write (and says
// so) while the environment fallback still resolves.
[[nodiscard]] std::unique_ptr<CredentialStore> makeSystemCredentialStore();

// In-memory, for tests. Deliberately not persisted: a test must never write to a developer's real
// keychain, and a store that "just uses a temp file" is one bad path away from being shipped.
class MemoryCredentialStore final : public CredentialStore {
public:
    [[nodiscard]] std::string_view backend() const override { return "memory"; }
    [[nodiscard]] bool writable() const override { return true; }
    [[nodiscard]] Result<void> store(std::string_view account, std::string_view secret) override;
    [[nodiscard]] std::optional<std::string> load(std::string_view account) const override;
    [[nodiscard]] Result<void> erase(std::string_view account) override;
    [[nodiscard]] bool has(std::string_view account) const override;

private:
    std::vector<std::pair<std::string, std::string>> items_;
};

} // namespace avgen::ai
