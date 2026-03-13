#include "rule_engine.h"
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace dis {

// Hex string → bytes
static std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        uint8_t hi = 0, lo = 0;
        auto nibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + c - 'a';
            if (c >= 'A' && c <= 'F') return 10 + c - 'A';
            return 0;
        };
        hi = nibble(hex[i]);
        lo = nibble(hex[i + 1]);
        out.push_back((hi << 4) | lo);
    }
    return out;
}

std::vector<Rule> parse_rules(const nlohmann::json& arr) {
    std::vector<Rule> rules;
    if (!arr.is_array()) return rules;

    for (auto& j : arr) {
        Rule r{};

        // Action
        std::string action_str = j.at("action").get<std::string>();
        if      (action_str == "block")   r.action = RuleAction::Block;
        else if (action_str == "respond") r.action = RuleAction::Respond;
        else if (action_str == "override")r.action = RuleAction::Override;
        else if (action_str == "replace") r.action = RuleAction::Replace;
        else throw std::runtime_error("Unknown rule action: " + action_str);

        // PDU type (optional, 0 = match all)
        if (j.contains("pdu_type")) {
            auto pt = pdu_type_from_name(j["pdu_type"].get<std::string>());
            r.pdu_type = static_cast<uint8_t>(pt);
        }

        // Conditions
        if (j.contains("conditions")) {
            for (auto& cj : j["conditions"]) {
                Condition c;
                c.field  = cj.at("field").get<std::string>();
                c.equals = cj.at("equals").get<double>();
                r.conditions.push_back(c);
            }
        }

        // Replace-specific
        if (r.action == RuleAction::Replace) {
            r.replace_field = j.at("field").get<std::string>();
            r.new_value     = j.at("value").get<double>();
            r.match_value   = j.contains("match") ? j["match"].get<double>() : std::nan("");
        }

        // Override-specific
        if (r.action == RuleAction::Override) {
            if (j.contains("payload_hex")) {
                r.override_payload = hex_to_bytes(j["payload_hex"].get<std::string>());
            }
        }

        // Respond-specific
        if (r.action == RuleAction::Respond) {
            if (j.contains("response_hex")) {
                r.response_payload = hex_to_bytes(j["response_hex"].get<std::string>());
            }
            r.also_forward = j.value("also_forward", false);
        }

        rules.push_back(std::move(r));
    }
    return rules;
}

// Check if all conditions match for a rule against the PDU buffer
static bool conditions_match(const Rule& rule, const uint8_t* buf, size_t len) {
    // PDU type check
    if (rule.pdu_type != 0) {
        if (len < PDU_HEADER_SIZE) return false;
        uint8_t pdu_type = buf[hdr::PDU_TYPE];
        if (pdu_type != rule.pdu_type) return false;
    }

    // Field conditions
    for (auto& cond : rule.conditions) {
        uint8_t pt = (len >= PDU_HEADER_SIZE) ? buf[hdr::PDU_TYPE] : 0;
        auto val = read_field(buf, len, pt, cond.field);
        if (!val.has_value()) return false;
        double dval = field_value_to_double(*val);
        if (std::abs(dval - cond.equals) > 0.5) return false;  // integer comparison with tolerance
    }
    return true;
}

RuleResult apply_rules(const std::vector<Rule>& rules,
                       uint8_t* buf, size_t len) {
    RuleResult result;
    uint8_t pdu_type = (len >= PDU_HEADER_SIZE) ? buf[hdr::PDU_TYPE] : 0;

    // Phase 1: Block rules
    for (auto& rule : rules) {
        if (rule.action != RuleAction::Block) continue;
        if (conditions_match(rule, buf, len)) {
            result.blocked = true;
            result.forward = false;
            return result;
        }
    }

    // Phase 2: Respond rules
    for (auto& rule : rules) {
        if (rule.action != RuleAction::Respond) continue;
        if (conditions_match(rule, buf, len)) {
            result.has_response = true;
            result.response_pdu = rule.response_payload;
            result.forward = rule.also_forward;
            if (!result.forward) return result;
            break; // only first respond rule fires
        }
    }

    // Phase 3: Override rules (first match wins)
    for (auto& rule : rules) {
        if (rule.action != RuleAction::Override) continue;
        if (conditions_match(rule, buf, len)) {
            // Replace entire buffer content
            if (!rule.override_payload.empty()) {
                size_t copy_len = std::min(rule.override_payload.size(), static_cast<size_t>(MAX_PDU_SIZE));
                std::memcpy(buf, rule.override_payload.data(), copy_len);
                result.modified = true;
            }
            return result; // override replaces everything, no further replace rules
        }
    }

    // Phase 4: Replace rules (all matching rules are applied / stacked)
    for (auto& rule : rules) {
        if (rule.action != RuleAction::Replace) continue;
        if (conditions_match(rule, buf, len)) {
            const auto* fd = lookup_field(pdu_type, rule.replace_field);
            if (!fd) continue;

            // Check match condition (if specified)
            if (!std::isnan(rule.match_value)) {
                auto cur = read_field(buf, len, pdu_type, rule.replace_field);
                if (!cur.has_value()) continue;
                double cur_d = field_value_to_double(*cur);
                if (std::abs(cur_d - rule.match_value) > 0.5) continue;
            }

            FieldValue new_val = coerce_value(*fd, rule.new_value);
            if (write_field(buf, len, pdu_type, rule.replace_field, new_val)) {
                result.modified = true;
            }
        }
    }

    return result;
}

} // namespace dis
