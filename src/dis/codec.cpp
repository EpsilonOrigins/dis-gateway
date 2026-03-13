#include "codec.hpp"
#include "byte_buffer.hpp"
#include "enums.hpp"

namespace dis {

// ===========================================================================
// Internal helpers
// ===========================================================================
namespace {

// --- Read helpers -----------------------------------------------------------

PduHeader read_header(ReadBuffer& rb) {
    PduHeader h;
    h.protocol_version = rb.read_u8();
    h.exercise_id      = rb.read_u8();
    h.pdu_type         = rb.read_u8();
    h.protocol_family  = rb.read_u8();
    h.timestamp        = rb.read_u32();
    h.length           = rb.read_u16();
    h.pdu_status       = rb.read_u16();
    return h;
}

EntityID read_entity_id(ReadBuffer& rb) {
    EntityID id;
    id.site        = rb.read_u16();
    id.application = rb.read_u16();
    id.entity      = rb.read_u16();
    return id;
}

EventID read_event_id(ReadBuffer& rb) {
    EventID id;
    id.site        = rb.read_u16();
    id.application = rb.read_u16();
    id.event_num   = rb.read_u16();
    return id;
}

EntityType read_entity_type(ReadBuffer& rb) {
    EntityType t;
    t.kind        = rb.read_u8();
    t.domain      = rb.read_u8();
    t.country     = rb.read_u16();
    t.category    = rb.read_u8();
    t.subcategory = rb.read_u8();
    t.specific    = rb.read_u8();
    t.extra       = rb.read_u8();
    return t;
}

Vector3Float read_vec3f(ReadBuffer& rb) {
    Vector3Float v;
    v.x = rb.read_f32();
    v.y = rb.read_f32();
    v.z = rb.read_f32();
    return v;
}

Vector3Double read_vec3d(ReadBuffer& rb) {
    Vector3Double v;
    v.x = rb.read_f64();
    v.y = rb.read_f64();
    v.z = rb.read_f64();
    return v;
}

Orientation read_orientation(ReadBuffer& rb) {
    Orientation o;
    o.psi   = rb.read_f32();
    o.theta = rb.read_f32();
    o.phi   = rb.read_f32();
    return o;
}

BurstDescriptor read_burst_descriptor(ReadBuffer& rb) {
    BurstDescriptor bd;
    bd.munition = read_entity_type(rb);
    bd.warhead  = rb.read_u16();
    bd.fuse     = rb.read_u16();
    bd.quantity = rb.read_u16();
    bd.rate     = rb.read_u16();
    return bd;
}

DeadReckoningParameters read_dead_reckoning(ReadBuffer& rb) {
    DeadReckoningParameters dr;
    dr.algorithm = rb.read_u8();
    rb.read_bytes(dr.other_params, 15);
    dr.linear_acceleration = read_vec3f(rb);
    dr.angular_velocity    = read_vec3f(rb);
    return dr;  // 1 + 15 + 12 + 12 = 40 bytes
}

EntityMarking read_marking(ReadBuffer& rb) {
    EntityMarking m;
    m.character_set = rb.read_u8();
    rb.read_bytes(reinterpret_cast<uint8_t*>(m.marking), 11);
    return m;
}

ArticulationParameter read_articulation_param(ReadBuffer& rb) {
    ArticulationParameter ap;
    ap.parameter_type_designator = rb.read_u8();
    ap.change_indicator          = rb.read_u8();
    ap.id_attached               = rb.read_u16();
    ap.parameter_type            = rb.read_u32();
    // parameter_value is stored as a 64-bit field (treated as double in this impl)
    uint64_t raw = 0;
    uint8_t tmp[8]; rb.read_bytes(tmp, 8);
    std::memcpy(&raw, tmp, 8);
    // interpret as double
    double d; std::memcpy(&d, &raw, 8);
    ap.parameter_value = d;
    return ap;  // 1+1+2+4+8 = 16 bytes
}

// --- Write helpers ----------------------------------------------------------

void write_header(WriteBuffer& wb, const PduHeader& h) {
    wb.write_u8(h.protocol_version);
    wb.write_u8(h.exercise_id);
    wb.write_u8(h.pdu_type);
    wb.write_u8(h.protocol_family);
    wb.write_u32(h.timestamp);
    wb.write_u16(h.length);     // will be patched later
    wb.write_u16(h.pdu_status);
}

void write_entity_id(WriteBuffer& wb, const EntityID& id) {
    wb.write_u16(id.site);
    wb.write_u16(id.application);
    wb.write_u16(id.entity);
}

void write_event_id(WriteBuffer& wb, const EventID& id) {
    wb.write_u16(id.site);
    wb.write_u16(id.application);
    wb.write_u16(id.event_num);
}

void write_entity_type(WriteBuffer& wb, const EntityType& t) {
    wb.write_u8(t.kind);
    wb.write_u8(t.domain);
    wb.write_u16(t.country);
    wb.write_u8(t.category);
    wb.write_u8(t.subcategory);
    wb.write_u8(t.specific);
    wb.write_u8(t.extra);
}

void write_vec3f(WriteBuffer& wb, const Vector3Float& v) {
    wb.write_f32(v.x);
    wb.write_f32(v.y);
    wb.write_f32(v.z);
}

void write_vec3d(WriteBuffer& wb, const Vector3Double& v) {
    wb.write_f64(v.x);
    wb.write_f64(v.y);
    wb.write_f64(v.z);
}

void write_orientation(WriteBuffer& wb, const Orientation& o) {
    wb.write_f32(o.psi);
    wb.write_f32(o.theta);
    wb.write_f32(o.phi);
}

void write_burst_descriptor(WriteBuffer& wb, const BurstDescriptor& bd) {
    write_entity_type(wb, bd.munition);
    wb.write_u16(bd.warhead);
    wb.write_u16(bd.fuse);
    wb.write_u16(bd.quantity);
    wb.write_u16(bd.rate);
}

void write_dead_reckoning(WriteBuffer& wb, const DeadReckoningParameters& dr) {
    wb.write_u8(dr.algorithm);
    wb.write_bytes(dr.other_params, 15);
    write_vec3f(wb, dr.linear_acceleration);
    write_vec3f(wb, dr.angular_velocity);
}

void write_marking(WriteBuffer& wb, const EntityMarking& m) {
    wb.write_u8(m.character_set);
    wb.write_bytes(reinterpret_cast<const uint8_t*>(m.marking), 11);
}

void write_articulation_param(WriteBuffer& wb, const ArticulationParameter& ap) {
    wb.write_u8(ap.parameter_type_designator);
    wb.write_u8(ap.change_indicator);
    wb.write_u16(ap.id_attached);
    wb.write_u32(ap.parameter_type);
    uint8_t tmp[8];
    std::memcpy(tmp, &ap.parameter_value, 8);
    wb.write_bytes(tmp, 8);
}

// ---------------------------------------------------------------------------
// PDU decoders
// ---------------------------------------------------------------------------

EntityStatePdu decode_entity_state(const PduHeader& hdr, ReadBuffer& rb) {
    EntityStatePdu p;
    p.header = hdr;

    p.entity_id     = read_entity_id(rb);
    p.force_id      = rb.read_u8();
    uint8_t n_artic = rb.read_u8();
    p.entity_type   = read_entity_type(rb);
    p.alt_entity_type = read_entity_type(rb);
    p.velocity      = read_vec3f(rb);
    p.location      = read_vec3d(rb);
    p.orientation   = read_orientation(rb);
    p.appearance    = rb.read_u32();
    p.dead_reckoning = read_dead_reckoning(rb);
    p.marking       = read_marking(rb);
    p.capabilities  = rb.read_u32();

    p.articulation_params.reserve(n_artic);
    for (uint8_t i = 0; i < n_artic; ++i)
        p.articulation_params.push_back(read_articulation_param(rb));

    return p;
}

FirePdu decode_fire(const PduHeader& hdr, ReadBuffer& rb) {
    FirePdu p;
    p.header = hdr;

    p.firing_entity_id = read_entity_id(rb);
    p.target_entity_id = read_entity_id(rb);
    p.munition_id      = read_entity_id(rb);
    p.event_id         = read_event_id(rb);
    p.fire_mission_index = rb.read_u32();
    p.location         = read_vec3d(rb);
    p.burst_descriptor = read_burst_descriptor(rb);
    p.velocity         = read_vec3f(rb);
    p.range            = rb.read_f32();

    return p;
}

DetonationPdu decode_detonation(const PduHeader& hdr, ReadBuffer& rb) {
    DetonationPdu p;
    p.header = hdr;

    p.firing_entity_id = read_entity_id(rb);
    p.target_entity_id = read_entity_id(rb);
    p.munition_id      = read_entity_id(rb);
    p.event_id         = read_event_id(rb);
    p.velocity         = read_vec3f(rb);
    p.location         = read_vec3d(rb);
    p.burst_descriptor = read_burst_descriptor(rb);
    p.location_entity_coords = read_vec3f(rb);
    p.detonation_result = rb.read_u8();
    uint8_t n_artic    = rb.read_u8();
    rb.skip(2); // padding

    p.articulation_params.reserve(n_artic);
    for (uint8_t i = 0; i < n_artic; ++i)
        p.articulation_params.push_back(read_articulation_param(rb));

    return p;
}

// ---------------------------------------------------------------------------
// PDU encoders
// ---------------------------------------------------------------------------

std::vector<uint8_t> encode_entity_state(const EntityStatePdu& p) {
    WriteBuffer wb;
    write_header(wb, p.header);

    write_entity_id(wb, p.entity_id);
    wb.write_u8(p.force_id);
    wb.write_u8(static_cast<uint8_t>(p.articulation_params.size()));
    write_entity_type(wb, p.entity_type);
    write_entity_type(wb, p.alt_entity_type);
    write_vec3f(wb, p.velocity);
    write_vec3d(wb, p.location);
    write_orientation(wb, p.orientation);
    wb.write_u32(p.appearance);
    write_dead_reckoning(wb, p.dead_reckoning);
    write_marking(wb, p.marking);
    wb.write_u32(p.capabilities);

    for (const auto& ap : p.articulation_params)
        write_articulation_param(wb, ap);

    wb.patch_u16(10, static_cast<uint16_t>(wb.size()));
    return wb.data();
}

std::vector<uint8_t> encode_fire(const FirePdu& p) {
    WriteBuffer wb;
    write_header(wb, p.header);

    write_entity_id(wb, p.firing_entity_id);
    write_entity_id(wb, p.target_entity_id);
    write_entity_id(wb, p.munition_id);
    write_event_id(wb, p.event_id);
    wb.write_u32(p.fire_mission_index);
    write_vec3d(wb, p.location);
    write_burst_descriptor(wb, p.burst_descriptor);
    write_vec3f(wb, p.velocity);
    wb.write_f32(p.range);

    wb.patch_u16(10, static_cast<uint16_t>(wb.size()));
    return wb.data();
}

std::vector<uint8_t> encode_detonation(const DetonationPdu& p) {
    WriteBuffer wb;
    write_header(wb, p.header);

    write_entity_id(wb, p.firing_entity_id);
    write_entity_id(wb, p.target_entity_id);
    write_entity_id(wb, p.munition_id);
    write_event_id(wb, p.event_id);
    write_vec3f(wb, p.velocity);
    write_vec3d(wb, p.location);
    write_burst_descriptor(wb, p.burst_descriptor);
    write_vec3f(wb, p.location_entity_coords);
    wb.write_u8(p.detonation_result);
    wb.write_u8(static_cast<uint8_t>(p.articulation_params.size()));
    wb.write_zeros(2);

    for (const auto& ap : p.articulation_params)
        write_articulation_param(wb, ap);

    wb.patch_u16(10, static_cast<uint16_t>(wb.size()));
    return wb.data();
}

std::vector<uint8_t> encode_unknown(const UnknownPdu& p) {
    WriteBuffer wb;
    write_header(wb, p.header);
    wb.write_bytes(p.body.data(), p.body.size());
    // Preserve original length from header (body was stored verbatim)
    return wb.data();
}

} // anonymous namespace

// ===========================================================================
// Public API
// ===========================================================================

std::optional<AnyPdu> decode(const uint8_t* data, std::size_t len) {
    if (len < PDU_HEADER_SIZE) return std::nullopt;

    ReadBuffer rb(data, len);
    PduHeader hdr;
    try {
        hdr = read_header(rb);
    } catch (...) {
        return std::nullopt;
    }

    // Clamp payload to what the header says (ignore trailing UDP padding)
    std::size_t pdu_len = hdr.length;
    if (pdu_len < PDU_HEADER_SIZE || pdu_len > len) pdu_len = len;

    // Re-wrap buffer over the header-declared payload
    ReadBuffer body(data + PDU_HEADER_SIZE, pdu_len - PDU_HEADER_SIZE);

    try {
        switch (static_cast<PduType>(hdr.pdu_type)) {
            case PduType::ENTITY_STATE:
                return decode_entity_state(hdr, body);
            case PduType::FIRE:
                return decode_fire(hdr, body);
            case PduType::DETONATION:
                return decode_detonation(hdr, body);
            default: {
                UnknownPdu u;
                u.header = hdr;
                u.body.assign(data + PDU_HEADER_SIZE,
                              data + pdu_len);
                return u;
            }
        }
    } catch (...) {
        // Malformed body – fall back to raw pass-through
        UnknownPdu u;
        u.header = hdr;
        u.body.assign(data + PDU_HEADER_SIZE, data + pdu_len);
        return u;
    }
}

std::vector<uint8_t> encode(const AnyPdu& pdu) {
    return std::visit([](const auto& p) -> std::vector<uint8_t> {
        using T = std::decay_t<decltype(p)>;
        if      constexpr (std::is_same_v<T, EntityStatePdu>) return encode_entity_state(p);
        else if constexpr (std::is_same_v<T, FirePdu>)        return encode_fire(p);
        else if constexpr (std::is_same_v<T, DetonationPdu>)  return encode_detonation(p);
        else                                                   return encode_unknown(p);
    }, pdu);
}

} // namespace dis
