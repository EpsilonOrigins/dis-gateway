#pragma once
#include "types.hpp"
#include <vector>
#include <variant>
#include <memory>

namespace dis {

// ---------------------------------------------------------------------------
// Entity State PDU  (PDU type 1)
// Fixed payload: 132 bytes after header
// ---------------------------------------------------------------------------
struct EntityStatePdu {
    PduHeader  header;

    EntityID   entity_id;
    uint8_t    force_id                = 0;
    // articulation_params count is derived from articulation_params.size()
    EntityType entity_type;
    EntityType alt_entity_type;
    Vector3Float velocity;
    Vector3Double location;
    Orientation  orientation;
    uint32_t   appearance             = 0;
    DeadReckoningParameters dead_reckoning;
    EntityMarking marking;
    uint32_t   capabilities           = 0;
    std::vector<ArticulationParameter> articulation_params;
};

// ---------------------------------------------------------------------------
// Fire PDU  (PDU type 2)
// Fixed payload: 84 bytes after header
// ---------------------------------------------------------------------------
struct FirePdu {
    PduHeader  header;

    EntityID   firing_entity_id;
    EntityID   target_entity_id;
    EntityID   munition_id;
    EventID    event_id;
    uint32_t   fire_mission_index     = 0;
    Vector3Double location;
    BurstDescriptor burst_descriptor;
    Vector3Float velocity;
    float      range                  = 0.f;
};

// ---------------------------------------------------------------------------
// Detonation PDU  (PDU type 3)
// Fixed payload: 92 bytes after header (excluding articulation params)
// ---------------------------------------------------------------------------
struct DetonationPdu {
    PduHeader  header;

    EntityID   firing_entity_id;
    EntityID   target_entity_id;
    EntityID   munition_id;
    EventID    event_id;
    Vector3Float velocity;
    Vector3Double location;
    BurstDescriptor burst_descriptor;
    Vector3Float location_entity_coords;
    uint8_t    detonation_result      = 0;
    // articulation count derived from articulation_params.size()
    // 2 bytes padding after count
    std::vector<ArticulationParameter> articulation_params;
};

// ---------------------------------------------------------------------------
// Unknown / pass-through PDU
// Carries the header plus raw body bytes for PDU types not decoded above.
// ---------------------------------------------------------------------------
struct UnknownPdu {
    PduHeader           header;
    std::vector<uint8_t> body; // bytes after the 12-byte header
};

// ---------------------------------------------------------------------------
// Tagged union over all supported PDU variants
// ---------------------------------------------------------------------------
using AnyPdu = std::variant<
    EntityStatePdu,
    FirePdu,
    DetonationPdu,
    UnknownPdu
>;

} // namespace dis
