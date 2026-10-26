#pragma once

#include <libretro.h>

#include <string>
#include <vector>

namespace cw {

// Core options ("settings" a libretro core exposes, e.g. renderer or BIOS region).
//
// Cores announce their options through one of several environment calls, depending on which
// API version the front-end reports; all of them boil down to key + allowed values + default.
// This store keeps the current value of each option and hands it back on GET_VARIABLE.
class CoreOptions {
  public:
    struct Option {
        std::string key;
        std::string description;
        std::vector<std::string> values;
        std::string value; // current value; starts as the default
    };

    void clear();

    // RETRO_ENVIRONMENT_SET_VARIABLES (legacy): "Description; value1|value2|...",
    // the first value is the default.
    void set_variables(const retro_variable* variables);
    // RETRO_ENVIRONMENT_SET_CORE_OPTIONS (v1) and the "us" half of SET_CORE_OPTIONS_INTL.
    void set_definitions_v1(const retro_core_option_definition* definitions);
    // RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2 and the "us" half of SET_CORE_OPTIONS_V2_INTL.
    void set_definitions_v2(const retro_core_option_v2_definition* definitions);

    // Current value for GET_VARIABLE, or nullptr if the core never declared the key.
    // The pointer stays valid until that option changes or the options are cleared/redeclared.
    const char* get(const char* key) const;

    // Sets an option to one of the values the core declared. Returns false (and changes
    // nothing) for an unknown key or value. A real change is reported by take_updated().
    bool set(const std::string& key, const std::string& value);

    // GET_VARIABLE_UPDATE: true once after any value changed.
    bool take_updated();

    const std::vector<Option>& options() const { return options_; }

  private:
    // v1 and v2 definitions share these fields, so one template handles both.
    template <typename Definition> void set_definitions(const Definition* definitions);

    std::vector<Option> options_;
    bool updated_ = false;
};

} // namespace cw
