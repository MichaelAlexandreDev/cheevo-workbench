#include "core_options.hpp"

#include <string_view>
#include <utility>

namespace cw {

void CoreOptions::clear() {
    options_.clear();
    updated_ = false;
}

void CoreOptions::set_variables(const retro_variable* variables) {
    clear();
    for (; variables && variables->key; ++variables) {
        Option option;
        option.key = variables->key;
        const std::string_view text = variables->value ? variables->value : "";

        // "Description; value1|value2|value3"
        const auto separator = text.find("; ");
        if (separator == std::string_view::npos) {
            option.description = text;
        } else {
            option.description = text.substr(0, separator);
            std::string_view rest = text.substr(separator + 2);
            while (!rest.empty()) {
                const auto bar = rest.find('|');
                option.values.emplace_back(rest.substr(0, bar));
                rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);
            }
        }
        if (!option.values.empty()) {
            option.value = option.values.front();
        }
        options_.push_back(std::move(option));
    }
    updated_ = true;
}

template <typename Definition> void CoreOptions::set_definitions(const Definition* definitions) {
    clear();
    for (; definitions && definitions->key; ++definitions) {
        Option option;
        option.key = definitions->key;
        option.description = definitions->desc ? definitions->desc : "";
        for (const retro_core_option_value& v : definitions->values) {
            if (!v.value) {
                break; // the list ends with a { NULL, NULL } entry
            }
            option.values.emplace_back(v.value);
        }
        // An absent or unknown default means "the first value".
        option.value = definitions->default_value ? definitions->default_value : "";
        if (option.value.empty() && !option.values.empty()) {
            option.value = option.values.front();
        }
        options_.push_back(std::move(option));
    }
    updated_ = true;
}

void CoreOptions::set_definitions_v1(const retro_core_option_definition* definitions) {
    set_definitions(definitions);
}

void CoreOptions::set_definitions_v2(const retro_core_option_v2_definition* definitions) {
    set_definitions(definitions);
}

const char* CoreOptions::get(const char* key) const {
    for (const Option& option : options_) {
        if (option.key == key) {
            return option.value.c_str();
        }
    }
    return nullptr;
}

bool CoreOptions::take_updated() {
    const bool was_updated = updated_;
    updated_ = false;
    return was_updated;
}

} // namespace cw
