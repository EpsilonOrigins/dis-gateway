#include "pdu_codec.h"
#include <cmath>

namespace dis {

// Global registry: pdu_type -> field map
static std::unordered_map<uint8_t, FieldMap> g_registry;

// Helper macros for building field maps concisely
#define F_U8(path, off)  {path, {off, FieldType::U8}}
#define F_U16(path, off) {path, {off, FieldType::U16}}
#define F_U32(path, off) {path, {off, FieldType::U32}}
#define F_S32(path, off) {path, {off, FieldType::S32}}
#define F_F32(path, off) {path, {off, FieldType::F32}}
#define F_F64(path, off) {path, {off, FieldType::F64}}

// Common header fields (present in every PDU at offset 0)
static void add_header_fields(FieldMap& m) {
    m.insert({F_U8 ("header.protocolVersion", 0)});
    m.insert({F_U8 ("header.exerciseId",      1)});
    m.insert({F_U8 ("header.pduType",         2)});
    m.insert({F_U8 ("header.protocolFamily",  3)});
    m.insert({F_U32("header.timestamp",       4)});
    m.insert({F_U16("header.length",          8)});
    m.insert({F_U16("header.padding",        10)});
}

// EntityID helper: 6 bytes = site(u16) + application(u16) + entity(u16)
static void add_entity_id(FieldMap& m, const std::string& prefix, size_t base) {
    m.insert({prefix + ".site",        {base + 0, FieldType::U16}});
    m.insert({prefix + ".application", {base + 2, FieldType::U16}});
    m.insert({prefix + ".entity",      {base + 4, FieldType::U16}});
}

// EntityType helper: 8 bytes
static void add_entity_type(FieldMap& m, const std::string& prefix, size_t base) {
    m.insert({prefix + ".kind",        {base + 0, FieldType::U8}});
    m.insert({prefix + ".domain",      {base + 1, FieldType::U8}});
    m.insert({prefix + ".country",     {base + 2, FieldType::U16}});
    m.insert({prefix + ".category",    {base + 4, FieldType::U8}});
    m.insert({prefix + ".subcategory", {base + 5, FieldType::U8}});
    m.insert({prefix + ".specific",    {base + 6, FieldType::U8}});
    m.insert({prefix + ".extra",       {base + 7, FieldType::U8}});
}

// Vector3Double helper: 24 bytes (x, y, z as f64)
static void add_vec3d(FieldMap& m, const std::string& prefix, size_t base) {
    m.insert({prefix + ".x", {base + 0,  FieldType::F64}});
    m.insert({prefix + ".y", {base + 8,  FieldType::F64}});
    m.insert({prefix + ".z", {base + 16, FieldType::F64}});
}

// Vector3Float helper: 12 bytes (x, y, z as f32)
static void add_vec3f(FieldMap& m, const std::string& prefix, size_t base) {
    m.insert({prefix + ".x", {base + 0, FieldType::F32}});
    m.insert({prefix + ".y", {base + 4, FieldType::F32}});
    m.insert({prefix + ".z", {base + 8, FieldType::F32}});
}

// EulerAngles helper: 12 bytes (psi, theta, phi as f32)
static void add_euler(FieldMap& m, const std::string& prefix, size_t base) {
    m.insert({prefix + ".psi",   {base + 0, FieldType::F32}});
    m.insert({prefix + ".theta", {base + 4, FieldType::F32}});
    m.insert({prefix + ".phi",   {base + 8, FieldType::F32}});
}

// ClockTime helper: 8 bytes (hour s32, timePastHour u32)
static void add_clock_time(FieldMap& m, const std::string& prefix, size_t base) {
    m.insert({prefix + ".hour",         {base + 0, FieldType::S32}});
    m.insert({prefix + ".timePastHour", {base + 4, FieldType::U32}});
}

// EventID helper: 6 bytes = site(u16) + application(u16) + eventNumber(u16)
static void add_event_id(FieldMap& m, const std::string& prefix, size_t base) {
    m.insert({prefix + ".site",        {base + 0, FieldType::U16}});
    m.insert({prefix + ".application", {base + 2, FieldType::U16}});
    m.insert({prefix + ".eventNumber", {base + 4, FieldType::U16}});
}

// ---- Build field maps for each supported PDU type ----

static FieldMap build_entity_state() {
    // IEEE 1278.1 Entity State PDU layout (DIS v6/v7):
    // Header:             0-11    (12 bytes)
    // EntityID:          12-17    (6 bytes)
    // ForceID:           18       (1 byte)
    // NumArticulation:   19       (1 byte)  -- u8 count of articulation params
    // EntityType:        20-27    (8 bytes)
    // AltEntityType:     28-35    (8 bytes)
    // LinearVelocity:    36-47    (12 bytes, Vector3Float)
    // Location:          48-71    (24 bytes, Vector3Double)
    // Orientation:       72-83    (12 bytes, EulerAngles)
    // Appearance:        84-87    (4 bytes, u32)
    // DeadReckoning:     88-127   (40 bytes)
    // Marking:          128-139   (12 bytes)
    // Capabilities:     140-143   (4 bytes, u32)
    // ArticulationParams: 144+    (variable)
    FieldMap m;
    add_header_fields(m);
    add_entity_id(m, "entityId", 12);
    m.insert({F_U8("forceId", 18)});
    m.insert({F_U8("numArticulationParams", 19)});
    add_entity_type(m, "entityType", 20);
    add_entity_type(m, "altEntityType", 28);
    add_vec3f(m, "velocity", 36);
    add_vec3d(m, "location", 48);
    add_euler(m, "orientation", 72);
    m.insert({F_U32("appearance", 84)});
    // Dead reckoning: algorithm byte at 88, then parameters
    m.insert({F_U8("deadReckoningAlgorithm", 88)});
    // Dead reckoning other params: 89-102 (14 bytes padding), linear accel 103-114, angular vel 115-126
    add_vec3f(m, "deadReckoningLinearAccel", 103);
    add_vec3f(m, "deadReckoningAngularVel", 115);
    // Marking: byte 128 = character set, bytes 129-139 = 11 chars
    m.insert({F_U8("markingCharacterSet", 128)});
    m.insert({F_U32("capabilities", 140)});
    return m;
}

static FieldMap build_fire() {
    // Fire PDU layout (DIS v6):
    // Header:              0-11   (12 bytes)
    // FiringEntityID:     12-17   (6 bytes)
    // TargetEntityID:     18-23   (6 bytes)
    // MunitionID:         24-29   (6 bytes)
    // EventID:            30-35   (6 bytes)
    // FireMissionIndex:   36-39   (u32)
    // Location:           40-63   (24 bytes, Vector3Double)
    // BurstDescriptor:    64-79   (16 bytes: EntityType(8) + warhead(u16) + fuse(u16) + quantity(u16) + rate(u16))
    // Velocity:           80-91   (12 bytes, Vector3Float)
    // Range:              92-95   (f32)
    FieldMap m;
    add_header_fields(m);
    add_entity_id(m, "firingEntityId", 12);
    add_entity_id(m, "targetEntityId", 18);
    add_entity_id(m, "munitionId", 24);
    add_event_id(m, "eventId", 30);
    m.insert({F_U32("fireMissionIndex", 36)});
    add_vec3d(m, "location", 40);
    add_entity_type(m, "burstDescriptor", 64);
    m.insert({F_U16("burstDescriptor.warhead",  72)});
    m.insert({F_U16("burstDescriptor.fuse",     74)});
    m.insert({F_U16("burstDescriptor.quantity",  76)});
    m.insert({F_U16("burstDescriptor.rate",      78)});
    add_vec3f(m, "velocity", 80);
    m.insert({F_F32("range", 92)});
    return m;
}

static FieldMap build_detonation() {
    // Detonation PDU layout (DIS v6):
    // Header:              0-11   (12 bytes)
    // FiringEntityID:     12-17   (6 bytes)
    // TargetEntityID:     18-23   (6 bytes)
    // MunitionID:         24-29   (6 bytes)
    // EventID:            30-35   (6 bytes)
    // Velocity:           36-47   (12 bytes, Vector3Float)
    // Location:           48-71   (24 bytes, Vector3Double, world coords)
    // BurstDescriptor:    72-87   (16 bytes)
    // EntityLocation:     88-99   (12 bytes, Vector3Float, entity-relative)
    // DetonationResult:  100      (u8)
    // NumArticulation:   101      (u8)
    // Padding:           102-103  (u16)
    // ArticulationParams: 104+    (variable)
    FieldMap m;
    add_header_fields(m);
    add_entity_id(m, "firingEntityId", 12);
    add_entity_id(m, "targetEntityId", 18);
    add_entity_id(m, "munitionId", 24);
    add_event_id(m, "eventId", 30);
    add_vec3f(m, "velocity", 36);
    add_vec3d(m, "location", 48);
    add_entity_type(m, "burstDescriptor", 72);
    m.insert({F_U16("burstDescriptor.warhead",  80)});
    m.insert({F_U16("burstDescriptor.fuse",     82)});
    m.insert({F_U16("burstDescriptor.quantity",  84)});
    m.insert({F_U16("burstDescriptor.rate",      86)});
    add_vec3f(m, "entityLocation", 88);
    m.insert({F_U8("detonationResult", 100)});
    m.insert({F_U8("numArticulationParams", 101)});
    m.insert({F_U16("padding", 102)});
    return m;
}

// Simulation Management PDUs share a common prefix after the header:
// OriginatingEntityID: 12-17
// ReceivingEntityID:   18-23
static void add_sim_mgmt_common(FieldMap& m) {
    add_header_fields(m);
    add_entity_id(m, "originatingEntityId", 12);
    add_entity_id(m, "receivingEntityId", 18);
}

static FieldMap build_start_resume() {
    // Start/Resume PDU (type 13):
    // Header:              0-11
    // OriginatingEntityID: 12-17
    // ReceivingEntityID:   18-23
    // RealWorldTime:       24-31  (ClockTime, 8 bytes)
    // SimulationTime:      32-39  (ClockTime, 8 bytes)
    // RequestID:           40-43  (u32)
    FieldMap m;
    add_sim_mgmt_common(m);
    add_clock_time(m, "realWorldTime", 24);
    add_clock_time(m, "simulationTime", 32);
    m.insert({F_U32("requestId", 40)});
    return m;
}

static FieldMap build_stop_freeze() {
    // Stop/Freeze PDU (type 14):
    // Header:              0-11
    // OriginatingEntityID: 12-17
    // ReceivingEntityID:   18-23
    // RealWorldTime:       24-31  (ClockTime, 8 bytes)
    // Reason:              32     (u8)
    // FrozenBehavior:      33     (u8)
    // Padding:             34-35  (u16)
    // RequestID:           36-39  (u32)
    FieldMap m;
    add_sim_mgmt_common(m);
    add_clock_time(m, "realWorldTime", 24);
    m.insert({F_U8("reason", 32)});
    m.insert({F_U8("frozenBehavior", 33)});
    m.insert({F_U16("padding", 34)});
    m.insert({F_U32("requestId", 36)});
    return m;
}

static FieldMap build_acknowledge() {
    // Acknowledge PDU (type 15):
    // Header:              0-11
    // OriginatingEntityID: 12-17
    // ReceivingEntityID:   18-23
    // AcknowledgeFlag:     24-25  (u16)
    // ResponseFlag:        26-27  (u16)
    // RequestID:           28-31  (u32)
    FieldMap m;
    add_sim_mgmt_common(m);
    m.insert({F_U16("acknowledgeFlag", 24)});
    m.insert({F_U16("responseFlag", 26)});
    m.insert({F_U32("requestId", 28)});
    return m;
}

static FieldMap build_data_query() {
    // Data Query PDU (type 18):
    // Header:              0-11
    // OriginatingEntityID: 12-17
    // ReceivingEntityID:   18-23
    // RequestID:           24-27  (u32)
    // TimeInterval:        28-31  (u32)
    // NumFixedDatums:      32-35  (u32)
    // NumVariableDatums:   36-39  (u32)
    // FixedDatumIDs:       40+    (each u32, count = NumFixedDatums)
    // VariableDatumIDs:    after fixed datums (each u32, count = NumVariableDatums)
    FieldMap m;
    add_sim_mgmt_common(m);
    m.insert({F_U32("requestId", 24)});
    m.insert({F_U32("timeInterval", 28)});
    m.insert({F_U32("numFixedDatums", 32)});
    m.insert({F_U32("numVariableDatums", 36)});
    return m;
}

static FieldMap build_set_data() {
    // Set Data PDU (type 19):
    // Header:              0-11
    // OriginatingEntityID: 12-17
    // ReceivingEntityID:   18-23
    // RequestID:           24-27  (u32)
    // Padding:             28-31  (u32)
    // NumFixedDatums:      32-35  (u32)
    // NumVariableDatums:   36-39  (u32)
    // FixedDatumRecords:   40+    (each 8 bytes: datumID u32 + value u32)
    // VariableDatumRecords: after fixed
    FieldMap m;
    add_sim_mgmt_common(m);
    m.insert({F_U32("requestId", 24)});
    m.insert({F_U32("padding", 28)});
    m.insert({F_U32("numFixedDatums", 32)});
    m.insert({F_U32("numVariableDatums", 36)});
    return m;
}

static FieldMap build_data() {
    // Data PDU (type 20): same layout as Set Data
    // Header:              0-11
    // OriginatingEntityID: 12-17
    // ReceivingEntityID:   18-23
    // RequestID:           24-27  (u32)
    // Padding:             28-31  (u32)
    // NumFixedDatums:      32-35  (u32)
    // NumVariableDatums:   36-39  (u32)
    // FixedDatumRecords:   40+
    // VariableDatumRecords: after fixed
    FieldMap m;
    add_sim_mgmt_common(m);
    m.insert({F_U32("requestId", 24)});
    m.insert({F_U32("padding", 28)});
    m.insert({F_U32("numFixedDatums", 32)});
    m.insert({F_U32("numVariableDatums", 36)});
    return m;
}

void init_field_registry() {
    g_registry[static_cast<uint8_t>(PduType::EntityState)] = build_entity_state();
    g_registry[static_cast<uint8_t>(PduType::Fire)]        = build_fire();
    g_registry[static_cast<uint8_t>(PduType::Detonation)]  = build_detonation();
    g_registry[static_cast<uint8_t>(PduType::StartResume)] = build_start_resume();
    g_registry[static_cast<uint8_t>(PduType::StopFreeze)]  = build_stop_freeze();
    g_registry[static_cast<uint8_t>(PduType::Acknowledge)] = build_acknowledge();
    g_registry[static_cast<uint8_t>(PduType::DataQuery)]   = build_data_query();
    g_registry[static_cast<uint8_t>(PduType::SetData)]     = build_set_data();
    g_registry[static_cast<uint8_t>(PduType::Data)]        = build_data();
}

const FieldDescriptor* lookup_field(uint8_t pdu_type, const std::string& field_path) {
    auto reg_it = g_registry.find(pdu_type);
    if (reg_it == g_registry.end()) return nullptr;
    auto& fm = reg_it->second;
    auto it = fm.find(field_path);
    if (it == fm.end()) return nullptr;
    return &it->second;
}

std::optional<FieldValue> read_field(const uint8_t* buf, size_t len,
                                      uint8_t pdu_type, const std::string& field_path) {
    const auto* fd = lookup_field(pdu_type, field_path);
    if (!fd) return std::nullopt;
    if (fd->offset + fd->size() > len) return std::nullopt;

    const uint8_t* p = buf + fd->offset;
    switch (fd->type) {
        case FieldType::U8:  return static_cast<uint8_t>(*p);
        case FieldType::U16: return read_u16(p);
        case FieldType::U32: return read_u32(p);
        case FieldType::S32: return read_s32(p);
        case FieldType::F32: return read_f32(p);
        case FieldType::F64: return read_f64(p);
    }
    return std::nullopt;
}

bool write_field(uint8_t* buf, size_t len,
                 uint8_t pdu_type, const std::string& field_path,
                 const FieldValue& value) {
    const auto* fd = lookup_field(pdu_type, field_path);
    if (!fd) return false;
    if (fd->offset + fd->size() > len) return false;

    uint8_t* p = buf + fd->offset;
    switch (fd->type) {
        case FieldType::U8:  *p = std::get<uint8_t>(value); break;
        case FieldType::U16: write_u16(p, std::get<uint16_t>(value)); break;
        case FieldType::U32: write_u32(p, std::get<uint32_t>(value)); break;
        case FieldType::S32: write_s32(p, std::get<int32_t>(value)); break;
        case FieldType::F32: write_f32(p, std::get<float>(value)); break;
        case FieldType::F64: write_f64(p, std::get<double>(value)); break;
    }
    return true;
}

FieldValue coerce_value(const FieldDescriptor& fd, double v) {
    switch (fd.type) {
        case FieldType::U8:  return static_cast<uint8_t>(v);
        case FieldType::U16: return static_cast<uint16_t>(v);
        case FieldType::U32: return static_cast<uint32_t>(v);
        case FieldType::S32: return static_cast<int32_t>(v);
        case FieldType::F32: return static_cast<float>(v);
        case FieldType::F64: return v;
    }
    return static_cast<uint8_t>(0);
}

double field_value_to_double(const FieldValue& v) {
    return std::visit([](auto&& arg) -> double { return static_cast<double>(arg); }, v);
}

} // namespace dis
