#pragma once

// Parameters as signals (ADR-1064, brief §5 "effects modulating effects"): a `publish` source puts the FINAL value
// of chosen parameters -- after their routes and the timeline -- on the bus, so one effect's intensity can drive
// another's:
//
//   {"kind": "publish", "name": "fx", "settings": {"parameters": ["fx/heroGlow/gain", "post/bloom/intensity"]}}
//
// publishes `fx.heroGlow.gain` and `post.bloom.intensity` (the path with '/' as '.'). A vector parameter publishes
// its first component under the name and the others as `<name>.1`, `<name>.2`, ...
//
// Like the macro bridge (`macros/<k>` -> `macro.<k>`), it reads the value the parameter had when the rack updates:
// the previous frame's final (one frame, 17 ms at 60 fps). Not pure in time, for the same reason as the macro: what
// it reads is the output of routes. Two renders of the same range agree; a scrub reaches it through the routes that
// feed the parameter.

#include "signals/source.hpp"

#include <string>
#include <vector>

namespace avgen::signals {

class PublishSource final : public Source {
public:
    explicit PublishSource(std::string name = "publish");
    [[nodiscard]] std::string kind() const override { return "publish"; }
    void attach(SignalBus& bus, params::ParameterSet& params) override;
    void detach(params::ParameterSet& params) override;
    void update(SignalBus& bus, const SourceContext& context) override;
    void reset() override {}
    [[nodiscard]] nlohmann::json settingsToJson() const override;
    Result<void> settingsFromJson(const nlohmann::json& j) override;
    [[nodiscard]] std::vector<std::string> outputs() const override;

    // `fx/heroGlow/gain` -> `fx.heroGlow.gain`.
    [[nodiscard]] static std::string signalName(const std::string& path);

private:
    struct Entry {
        std::string path;
        std::vector<SignalId> ids; // one per component, declared at attach
    };
    std::vector<Entry> entries_;
    params::ParameterSet* params_ = nullptr; // looked up by path each frame: effect parameters come and go
};

} // namespace avgen::signals
