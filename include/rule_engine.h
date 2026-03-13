#pragma once
#include "pdu_codec.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>

namespace dis {

// Rule action types
enum class RuleAction { Block, Respond, Override, Replace };

// A single condition: field == value
struct Condition {
    std::string field;
    double      equals;
};

// A rewrite/filter rule
struct Rule {
    RuleAction  action;
    uint8_t     pdu_type;       // PduType enum value (0 = match all supported)

    // Conditions (all must match for rule to fire)
    std::vector<Condition> conditions;

    // Replace action
    std::string replace_field;  // field path to rewrite
    double      match_value;    // only replace if current value == this (NaN = always)
    double      new_value;      // value to write

    // Override action
    std::vector<uint8_t> override_payload; // full PDU bytes

    // Respond action
    std::vector<uint8_t> response_payload; // full PDU bytes to send back
    bool                 also_forward;     // forward original after responding?
};

// Result of applying rules to a PDU
struct RuleResult {
    bool blocked       = false;
    bool modified      = false;
    bool has_response  = false;
    bool forward       = true;   // should original (possibly modified) be forwarded?
    std::vector<uint8_t> response_pdu; // synthetic response to send back to source
};

// Parse rules from a JSON array
std::vector<Rule> parse_rules(const nlohmann::json& arr);

// Apply a rule chain to a PDU buffer (may modify buf in place)
// Returns the result indicating what to do
RuleResult apply_rules(const std::vector<Rule>& rules,
                       uint8_t* buf, size_t len);

} // namespace dis
