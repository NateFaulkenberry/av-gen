#include "params/parameter_set.hpp"

#include "core/log.hpp"

#include <stdexcept>

namespace avgen::params {

IParameter* ParameterSet::find(std::string_view path) {
    const auto it = index_.find(std::string(path));
    return it == index_.end() ? nullptr : it->second;
}

const IParameter* ParameterSet::find(std::string_view path) const {
    const auto it = index_.find(std::string(path));
    return it == index_.end() ? nullptr : it->second;
}

void ParameterSet::resetFinals() {
    for (IParameter* p : ordered_) {
        p->resetFinal();
    }
}

void ParameterSet::resetAllToDefault() {
    for (IParameter* p : ordered_) {
        p->resetToDefault();
    }
}

// add() only reaches here when no parameter of the same type exists at this path. A parameter
// of a different type at the same path is a programming error: nothing is registered and the
// new object is destroyed with the exception.
void ParameterSet::registerParameter(std::unique_ptr<IParameter> param) {
    const std::string& path = param->path();
    if (index_.contains(path)) {
        log::error("parameter '{}' is already registered with a different type", path);
        throw std::logic_error("duplicate parameter path with different type: " + path);
    }
    IParameter* raw = param.get();
    storage_.push_back(std::move(param));
    ordered_.push_back(raw);
    index_.emplace(path, raw);
}

void ParameterSet::clear() {
    index_.clear();
    ordered_.clear();
    storage_.clear();
}

} // namespace avgen::params
