#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <array>

namespace dis {

// ---------------------------------------------------------------------------
// Primitive compound types
// ---------------------------------------------------------------------------

struct EntityID {
    uint16_t site        = 0;
    uint16_t application = 0;
    uint16_t entity      = 0;
};

struct EventID {
    uint16_t site        = 0;
    uint16_t application = 0;
    uint16_t event_num   = 0;
};

struct Vector3Float {
    float x = 0.f, y = 0.f, z = 0.f;
};

struct Vector3Double {
    double x = 0.0, y = 0.0, z = 0.0;
};

struct Orientation {   // Euler angles in radians
    float psi = 0.f;   // heading
    float theta = 0.f; // pitch
    float phi = 0.f;   // roll
};

// ---------------------------------------------------------------------------
// Entity type record (8 bytes)
// ---------------------------------------------------------------------------
struct EntityType {
    uint8_t  kind        = 0;
    uint8_t  domain      = 0;
    uint16_t country     = 0;
    uint8_t  category    = 0;
    uint8_t  subcategory = 0;
    uint8_t  specific    = 0;
    uint8_t  extra       = 0;
};

// ---------------------------------------------------------------------------
// Burst Descriptor (16 bytes)
// ---------------------------------------------------------------------------
struct BurstDescriptor {
    EntityType munition;
    uint16_t   warhead   = 0;
    uint16_t   fuse      = 0;
    uint16_t   quantity  = 0;
    uint16_t   rate      = 0;
};

// ---------------------------------------------------------------------------
// Dead Reckoning Parameters (40 bytes)
// ---------------------------------------------------------------------------
struct DeadReckoningParameters {
    uint8_t      algorithm          = 0;
    uint8_t      other_params[15]   = {};
    Vector3Float linear_acceleration;
    Vector3Float angular_velocity;
};

// ---------------------------------------------------------------------------
// Entity Marking (12 bytes)
// ---------------------------------------------------------------------------
struct EntityMarking {
    uint8_t character_set = 1; // 1 = ASCII
    char    marking[11]   = {};

    std::string text() const {
        // Null-terminated or 11-char padded
        std::size_t len = 0;
        while (len < 11 && marking[len] != '\0') ++len;
        return std::string(marking, len);
    }

    void set_text(const std::string& s) {
        std::size_t copy = std::min(s.size(), std::size_t{11});
        std::memcpy(marking, s.data(), copy);
        std::memset(marking + copy, 0, 11 - copy);
    }
};

// ---------------------------------------------------------------------------
// Articulation / Attached Part Parameter (16 bytes each)
// ---------------------------------------------------------------------------
struct ArticulationParameter {
    uint8_t  parameter_type_designator = 0; // 0=articulated, 1=attached
    uint8_t  change_indicator          = 0;
    uint16_t id_attached               = 0;
    uint32_t parameter_type            = 0;
    double   parameter_value           = 0.0; // 64-bit field
};

// ---------------------------------------------------------------------------
// PDU Header (12 bytes)
// ---------------------------------------------------------------------------
struct PduHeader {
    uint8_t  protocol_version = 6;
    uint8_t  exercise_id      = 0;
    uint8_t  pdu_type         = 0;
    uint8_t  protocol_family  = 0;
    uint32_t timestamp        = 0;
    uint16_t length           = 0;
    uint16_t pdu_status       = 0; // padding in DIS 6; PDU Status in DIS 7
};

static constexpr std::size_t PDU_HEADER_SIZE = 12;

} // namespace dis
