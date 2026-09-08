#pragma once

// Owning registry of parameters indexed by path.

#include "core/error.hpp"
#include "params/parameter.hpp"

#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace avgen::params {

class ParameterSet {
public:
    // Registers a parameter. Re-adding a path with the same type returns the existing parameter
    // (the new description is ignored). A different type at the same path is a programming error:
    // registerParameter throws std::logic_error and nothing is registered.
    template <typename T>
    Parameter<T>& add(ParamDesc<T> desc) {
        if (auto* existing = findAs<T>(desc.path)) {
            return *existing;
        }
        auto param = std::make_unique<Parameter<T>>(std::move(desc));
        auto& ref = *param;
        registerParameter(std::move(param));
        return ref;
    }

    [[nodiscard]] IParameter* find(std::string_view path);
    [[nodiscard]] const IParameter* find(std::string_view path) const;
    template <typename T>
    [[nodiscard]] Parameter<T>* findAs(std::string_view path) {
        return dynamic_cast<Parameter<T>*>(find(path));
    }

    [[nodiscard]] std::size_t size() const { return ordered_.size(); }
    [[nodiscard]] const std::vector<IParameter*>& ordered() const { return ordered_; }

    void resetFinals();       // final = base for every parameter (start of modulation pass)
    void resetAllToDefault();
    void clear();             // removes every parameter (scene switch); invalidates all pointers
    bool remove(std::string_view path); // removes one parameter; invalidates its pointer only

private:
    void registerParameter(std::unique_ptr<IParameter> param);

    std::vector<std::unique_ptr<IParameter>> storage_;
    std::vector<IParameter*> ordered_;
    std::unordered_map<std::string, IParameter*> index_;
};

} // namespace avgen::params
