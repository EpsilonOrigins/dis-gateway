#include "field_accessor.hpp"

#include <unordered_map>
#include <functional>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// Header helpers (work on any PDU type via the common header)
// ---------------------------------------------------------------------------
const dis::PduHeader* hdr_of(const dis::AnyPdu& pdu) {
    return std::visit([](const auto& p) -> const dis::PduHeader* {
        return &p.header;
    }, pdu);
}

dis::PduHeader* hdr_of_mut(dis::AnyPdu& pdu) {
    return std::visit([](auto& p) -> dis::PduHeader* {
        return &p.header;
    }, pdu);
}

// ---------------------------------------------------------------------------
// Resolve header shorthand aliases
// ---------------------------------------------------------------------------
static const std::unordered_map<std::string, std::string> HEADER_ALIASES = {
    {"pdu_type",          "header.pdu_type"},
    {"exercise_id",       "header.exercise_id"},
    {"protocol_version",  "header.protocol_version"},
    {"protocol_family",   "header.protocol_family"},
    {"timestamp",         "header.timestamp"},
    {"pdu_status",        "header.pdu_status"},
};

const std::string& resolve(const std::string& path) {
    auto it = HEADER_ALIASES.find(path);
    return (it != HEADER_ALIASES.end()) ? it->second : path;
}

// ---------------------------------------------------------------------------
// EntityID get/set helper
// ---------------------------------------------------------------------------
std::optional<double> get_entity_id(const dis::EntityID& id, const std::string& sub) {
    if (sub == "site")        return id.site;
    if (sub == "application") return id.application;
    if (sub == "entity")      return id.entity;
    return std::nullopt;
}
bool set_entity_id(dis::EntityID& id, const std::string& sub, double v) {
    if (sub == "site")        { id.site        = static_cast<uint16_t>(v); return true; }
    if (sub == "application") { id.application = static_cast<uint16_t>(v); return true; }
    if (sub == "entity")      { id.entity      = static_cast<uint16_t>(v); return true; }
    return false;
}

// EventID
std::optional<double> get_event_id(const dis::EventID& id, const std::string& sub) {
    if (sub == "site")        return id.site;
    if (sub == "application") return id.application;
    if (sub == "event_num")   return id.event_num;
    return std::nullopt;
}
bool set_event_id(dis::EventID& id, const std::string& sub, double v) {
    if (sub == "site")        { id.site        = static_cast<uint16_t>(v); return true; }
    if (sub == "application") { id.application = static_cast<uint16_t>(v); return true; }
    if (sub == "event_num")   { id.event_num   = static_cast<uint16_t>(v); return true; }
    return false;
}

// EntityType
std::optional<double> get_entity_type(const dis::EntityType& t, const std::string& sub) {
    if (sub == "kind")        return t.kind;
    if (sub == "domain")      return t.domain;
    if (sub == "country")     return t.country;
    if (sub == "category")    return t.category;
    if (sub == "subcategory") return t.subcategory;
    if (sub == "specific")    return t.specific;
    if (sub == "extra")       return t.extra;
    return std::nullopt;
}
bool set_entity_type(dis::EntityType& t, const std::string& sub, double v) {
    if (sub == "kind")        { t.kind        = static_cast<uint8_t>(v);  return true; }
    if (sub == "domain")      { t.domain      = static_cast<uint8_t>(v);  return true; }
    if (sub == "country")     { t.country     = static_cast<uint16_t>(v); return true; }
    if (sub == "category")    { t.category    = static_cast<uint8_t>(v);  return true; }
    if (sub == "subcategory") { t.subcategory = static_cast<uint8_t>(v);  return true; }
    if (sub == "specific")    { t.specific    = static_cast<uint8_t>(v);  return true; }
    if (sub == "extra")       { t.extra       = static_cast<uint8_t>(v);  return true; }
    return false;
}

// Burst descriptor (handles "burst_descriptor.<sub>" where sub may be
// "warhead", "fuse", "quantity", "rate", or "munition.<sub2>")
std::optional<double> get_burst_descriptor(const dis::BurstDescriptor& bd,
                                            const std::string& sub) {
    if (sub == "warhead")  return bd.warhead;
    if (sub == "fuse")     return bd.fuse;
    if (sub == "quantity") return bd.quantity;
    if (sub == "rate")     return bd.rate;
    // munition.<sub2>
    if (sub.substr(0, 8) == "munition" && sub.size() > 9 && sub[8] == '.')
        return get_entity_type(bd.munition, sub.substr(9));
    return std::nullopt;
}
bool set_burst_descriptor(dis::BurstDescriptor& bd, const std::string& sub, double v) {
    if (sub == "warhead")  { bd.warhead  = static_cast<uint16_t>(v); return true; }
    if (sub == "fuse")     { bd.fuse     = static_cast<uint16_t>(v); return true; }
    if (sub == "quantity") { bd.quantity = static_cast<uint16_t>(v); return true; }
    if (sub == "rate")     { bd.rate     = static_cast<uint16_t>(v); return true; }
    if (sub.substr(0, 8) == "munition" && sub.size() > 9 && sub[8] == '.')
        return set_entity_type(bd.munition, sub.substr(9), v);
    return false;
}

// Split "a.b.c" into ("a", "b.c") – first component vs rest
static std::pair<std::string, std::string> split1(const std::string& path) {
    auto dot = path.find('.');
    if (dot == std::string::npos) return {path, ""};
    return {path.substr(0, dot), path.substr(dot + 1)};
}

} // anonymous namespace

// ===========================================================================
// Public API
// ===========================================================================
namespace FieldAccessor {

std::optional<double> get(const dis::AnyPdu& pdu, const std::string& raw_path) {
    const std::string& path = resolve(raw_path);
    auto [top, rest] = split1(path);

    // --- Header fields ------------------------------------------------------
    if (top == "header") {
        const auto* h = hdr_of(pdu);
        if (!h) return std::nullopt;
        if (rest == "pdu_type")          return h->pdu_type;
        if (rest == "exercise_id")       return h->exercise_id;
        if (rest == "protocol_version")  return h->protocol_version;
        if (rest == "protocol_family")   return h->protocol_family;
        if (rest == "timestamp")         return h->timestamp;
        if (rest == "pdu_status")        return h->pdu_status;
        if (rest == "length")            return h->length;
        return std::nullopt;
    }

    return std::visit([&](const auto& p) -> std::optional<double> {
        using T = std::decay_t<decltype(p)>;

        // --- EntityStatePdu -------------------------------------------------
        if constexpr (std::is_same_v<T, dis::EntityStatePdu>) {
            if (top == "entity_id")          return get_entity_id(p.entity_id, rest);
            if (top == "force_id")           return p.force_id;
            if (top == "entity_type")        return get_entity_type(p.entity_type, rest);
            if (top == "alt_entity_type")    return get_entity_type(p.alt_entity_type, rest);
            if (top == "velocity") {
                if (rest == "x") return p.velocity.x;
                if (rest == "y") return p.velocity.y;
                if (rest == "z") return p.velocity.z;
            }
            if (top == "location") {
                if (rest == "x") return p.location.x;
                if (rest == "y") return p.location.y;
                if (rest == "z") return p.location.z;
            }
            if (top == "orientation") {
                if (rest == "psi")   return p.orientation.psi;
                if (rest == "theta") return p.orientation.theta;
                if (rest == "phi")   return p.orientation.phi;
            }
            if (top == "appearance")         return p.appearance;
            if (top == "capabilities")       return p.capabilities;
            if (top == "dead_reckoning") {
                if (rest == "algorithm") return p.dead_reckoning.algorithm;
                if (rest == "linear_acceleration.x") return p.dead_reckoning.linear_acceleration.x;
                if (rest == "linear_acceleration.y") return p.dead_reckoning.linear_acceleration.y;
                if (rest == "linear_acceleration.z") return p.dead_reckoning.linear_acceleration.z;
                if (rest == "angular_velocity.x")    return p.dead_reckoning.angular_velocity.x;
                if (rest == "angular_velocity.y")    return p.dead_reckoning.angular_velocity.y;
                if (rest == "angular_velocity.z")    return p.dead_reckoning.angular_velocity.z;
            }
            if (top == "marking") {
                if (rest == "character_set") return p.marking.character_set;
            }
        }

        // --- FirePdu --------------------------------------------------------
        if constexpr (std::is_same_v<T, dis::FirePdu>) {
            if (top == "firing_entity_id")   return get_entity_id(p.firing_entity_id, rest);
            if (top == "target_entity_id")   return get_entity_id(p.target_entity_id, rest);
            if (top == "munition_id")        return get_entity_id(p.munition_id, rest);
            if (top == "event_id")           return get_event_id(p.event_id, rest);
            if (top == "fire_mission_index") return static_cast<double>(p.fire_mission_index);
            if (top == "location") {
                if (rest == "x") return p.location.x;
                if (rest == "y") return p.location.y;
                if (rest == "z") return p.location.z;
            }
            if (top == "burst_descriptor")   return get_burst_descriptor(p.burst_descriptor, rest);
            if (top == "velocity") {
                if (rest == "x") return p.velocity.x;
                if (rest == "y") return p.velocity.y;
                if (rest == "z") return p.velocity.z;
            }
            if (top == "range")              return p.range;
        }

        // --- DetonationPdu --------------------------------------------------
        if constexpr (std::is_same_v<T, dis::DetonationPdu>) {
            if (top == "firing_entity_id")   return get_entity_id(p.firing_entity_id, rest);
            if (top == "target_entity_id")   return get_entity_id(p.target_entity_id, rest);
            if (top == "munition_id")        return get_entity_id(p.munition_id, rest);
            if (top == "event_id")           return get_event_id(p.event_id, rest);
            if (top == "velocity") {
                if (rest == "x") return p.velocity.x;
                if (rest == "y") return p.velocity.y;
                if (rest == "z") return p.velocity.z;
            }
            if (top == "location") {
                if (rest == "x") return p.location.x;
                if (rest == "y") return p.location.y;
                if (rest == "z") return p.location.z;
            }
            if (top == "burst_descriptor")   return get_burst_descriptor(p.burst_descriptor, rest);
            if (top == "location_entity_coords") {
                if (rest == "x") return p.location_entity_coords.x;
                if (rest == "y") return p.location_entity_coords.y;
                if (rest == "z") return p.location_entity_coords.z;
            }
            if (top == "detonation_result")  return p.detonation_result;
        }

        return std::nullopt;
    }, pdu);
}

// ---------------------------------------------------------------------------
bool set(dis::AnyPdu& pdu, const std::string& raw_path, double v) {
    const std::string& path = resolve(raw_path);
    auto [top, rest] = split1(path);

    if (top == "header") {
        auto* h = hdr_of_mut(pdu);
        if (!h) return false;
        if (rest == "pdu_type")         { h->pdu_type         = static_cast<uint8_t>(v);  return true; }
        if (rest == "exercise_id")      { h->exercise_id      = static_cast<uint8_t>(v);  return true; }
        if (rest == "protocol_version") { h->protocol_version = static_cast<uint8_t>(v);  return true; }
        if (rest == "protocol_family")  { h->protocol_family  = static_cast<uint8_t>(v);  return true; }
        if (rest == "timestamp")        { h->timestamp        = static_cast<uint32_t>(v); return true; }
        if (rest == "pdu_status")       { h->pdu_status       = static_cast<uint16_t>(v); return true; }
        return false;
    }

    return std::visit([&](auto& p) -> bool {
        using T = std::decay_t<decltype(p)>;

        if constexpr (std::is_same_v<T, dis::EntityStatePdu>) {
            if (top == "entity_id")       return set_entity_id(p.entity_id, rest, v);
            if (top == "force_id")        { p.force_id    = static_cast<uint8_t>(v); return true; }
            if (top == "entity_type")     return set_entity_type(p.entity_type, rest, v);
            if (top == "alt_entity_type") return set_entity_type(p.alt_entity_type, rest, v);
            if (top == "velocity") {
                if (rest == "x") { p.velocity.x = static_cast<float>(v); return true; }
                if (rest == "y") { p.velocity.y = static_cast<float>(v); return true; }
                if (rest == "z") { p.velocity.z = static_cast<float>(v); return true; }
            }
            if (top == "location") {
                if (rest == "x") { p.location.x = v; return true; }
                if (rest == "y") { p.location.y = v; return true; }
                if (rest == "z") { p.location.z = v; return true; }
            }
            if (top == "orientation") {
                if (rest == "psi")   { p.orientation.psi   = static_cast<float>(v); return true; }
                if (rest == "theta") { p.orientation.theta = static_cast<float>(v); return true; }
                if (rest == "phi")   { p.orientation.phi   = static_cast<float>(v); return true; }
            }
            if (top == "appearance")    { p.appearance    = static_cast<uint32_t>(v); return true; }
            if (top == "capabilities")  { p.capabilities  = static_cast<uint32_t>(v); return true; }
            if (top == "dead_reckoning") {
                if (rest == "algorithm") { p.dead_reckoning.algorithm = static_cast<uint8_t>(v); return true; }
                if (rest == "linear_acceleration.x") { p.dead_reckoning.linear_acceleration.x = static_cast<float>(v); return true; }
                if (rest == "linear_acceleration.y") { p.dead_reckoning.linear_acceleration.y = static_cast<float>(v); return true; }
                if (rest == "linear_acceleration.z") { p.dead_reckoning.linear_acceleration.z = static_cast<float>(v); return true; }
                if (rest == "angular_velocity.x")    { p.dead_reckoning.angular_velocity.x = static_cast<float>(v); return true; }
                if (rest == "angular_velocity.y")    { p.dead_reckoning.angular_velocity.y = static_cast<float>(v); return true; }
                if (rest == "angular_velocity.z")    { p.dead_reckoning.angular_velocity.z = static_cast<float>(v); return true; }
            }
            if (top == "marking" && rest == "character_set") {
                p.marking.character_set = static_cast<uint8_t>(v); return true;
            }
        }

        if constexpr (std::is_same_v<T, dis::FirePdu>) {
            if (top == "firing_entity_id")   return set_entity_id(p.firing_entity_id, rest, v);
            if (top == "target_entity_id")   return set_entity_id(p.target_entity_id, rest, v);
            if (top == "munition_id")        return set_entity_id(p.munition_id, rest, v);
            if (top == "event_id")           return set_event_id(p.event_id, rest, v);
            if (top == "fire_mission_index") { p.fire_mission_index = static_cast<uint32_t>(v); return true; }
            if (top == "location") {
                if (rest == "x") { p.location.x = v; return true; }
                if (rest == "y") { p.location.y = v; return true; }
                if (rest == "z") { p.location.z = v; return true; }
            }
            if (top == "burst_descriptor")   return set_burst_descriptor(p.burst_descriptor, rest, v);
            if (top == "velocity") {
                if (rest == "x") { p.velocity.x = static_cast<float>(v); return true; }
                if (rest == "y") { p.velocity.y = static_cast<float>(v); return true; }
                if (rest == "z") { p.velocity.z = static_cast<float>(v); return true; }
            }
            if (top == "range") { p.range = static_cast<float>(v); return true; }
        }

        if constexpr (std::is_same_v<T, dis::DetonationPdu>) {
            if (top == "firing_entity_id")   return set_entity_id(p.firing_entity_id, rest, v);
            if (top == "target_entity_id")   return set_entity_id(p.target_entity_id, rest, v);
            if (top == "munition_id")        return set_entity_id(p.munition_id, rest, v);
            if (top == "event_id")           return set_event_id(p.event_id, rest, v);
            if (top == "velocity") {
                if (rest == "x") { p.velocity.x = static_cast<float>(v); return true; }
                if (rest == "y") { p.velocity.y = static_cast<float>(v); return true; }
                if (rest == "z") { p.velocity.z = static_cast<float>(v); return true; }
            }
            if (top == "location") {
                if (rest == "x") { p.location.x = v; return true; }
                if (rest == "y") { p.location.y = v; return true; }
                if (rest == "z") { p.location.z = v; return true; }
            }
            if (top == "burst_descriptor")   return set_burst_descriptor(p.burst_descriptor, rest, v);
            if (top == "location_entity_coords") {
                if (rest == "x") { p.location_entity_coords.x = static_cast<float>(v); return true; }
                if (rest == "y") { p.location_entity_coords.y = static_cast<float>(v); return true; }
                if (rest == "z") { p.location_entity_coords.z = static_cast<float>(v); return true; }
            }
            if (top == "detonation_result") { p.detonation_result = static_cast<uint8_t>(v); return true; }
        }

        return false;
    }, pdu);
}

// ---------------------------------------------------------------------------
// String accessors (marking.text only for now)
// ---------------------------------------------------------------------------
std::optional<std::string> get_str(const dis::AnyPdu& pdu, const std::string& path) {
    if (path != "marking.text") return std::nullopt;
    return std::visit([](const auto& p) -> std::optional<std::string> {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, dis::EntityStatePdu>)
            return p.marking.text();
        return std::nullopt;
    }, pdu);
}

bool set_str(dis::AnyPdu& pdu, const std::string& path, const std::string& value) {
    if (path != "marking.text") return false;
    return std::visit([&](auto& p) -> bool {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, dis::EntityStatePdu>) {
            p.marking.set_text(value);
            return true;
        }
        return false;
    }, pdu);
}

} // namespace FieldAccessor
