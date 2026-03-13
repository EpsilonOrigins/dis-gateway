#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <arpa/inet.h>

namespace dis {

// --- Byte-order helpers ---------------------------------------------------

inline uint16_t read_u16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return ntohs(v); }
inline uint32_t read_u32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return ntohl(v); }
inline int32_t  read_s32(const uint8_t* p) { return static_cast<int32_t>(read_u32(p)); }
inline float    read_f32(const uint8_t* p) { uint32_t v = read_u32(p); float f; std::memcpy(&f, &v, 4); return f; }
inline double   read_f64(const uint8_t* p) {
    uint8_t swapped[8];
    for (int i = 0; i < 8; ++i) swapped[i] = p[7 - i];
    double d; std::memcpy(&d, swapped, 8); return d;
}

inline void write_u16(uint8_t* p, uint16_t v) { v = htons(v);  std::memcpy(p, &v, 2); }
inline void write_u32(uint8_t* p, uint32_t v) { v = htonl(v);  std::memcpy(p, &v, 4); }
inline void write_s32(uint8_t* p, int32_t v)  { write_u32(p, static_cast<uint32_t>(v)); }
inline void write_f32(uint8_t* p, float f)    { uint32_t v; std::memcpy(&v, &f, 4); write_u32(p, v); }
inline void write_f64(uint8_t* p, double d) {
    uint8_t raw[8]; std::memcpy(raw, &d, 8);
    for (int i = 0; i < 8; ++i) p[i] = raw[7 - i];
}

// --- PDU Type IDs (IEEE 1278.1) ------------------------------------------

enum class PduType : uint8_t {
    EntityState  =  1,
    Fire         =  2,
    Detonation   =  3,
    StartResume  = 13,
    StopFreeze   = 14,
    Acknowledge  = 15,
    DataQuery    = 18,
    SetData      = 19,
    Data         = 20,
    Other        = 0   // sentinel for unsupported
};

inline const char* pdu_type_name(uint8_t t) {
    switch (static_cast<PduType>(t)) {
        case PduType::EntityState: return "EntityState";
        case PduType::Fire:        return "Fire";
        case PduType::Detonation:  return "Detonation";
        case PduType::StartResume: return "StartResume";
        case PduType::StopFreeze:  return "StopFreeze";
        case PduType::Acknowledge: return "Acknowledge";
        case PduType::DataQuery:   return "DataQuery";
        case PduType::SetData:     return "SetData";
        case PduType::Data:        return "Data";
        default:                   return "Unknown";
    }
}

inline PduType pdu_type_from_name(const std::string& name) {
    if (name == "EntityState")  return PduType::EntityState;
    if (name == "Fire")         return PduType::Fire;
    if (name == "Detonation")   return PduType::Detonation;
    if (name == "StartResume")  return PduType::StartResume;
    if (name == "StopFreeze")   return PduType::StopFreeze;
    if (name == "Acknowledge")  return PduType::Acknowledge;
    if (name == "DataQuery")    return PduType::DataQuery;
    if (name == "SetData")      return PduType::SetData;
    if (name == "Data")         return PduType::Data;
    return PduType::Other;
}

inline bool is_supported_pdu(uint8_t t) {
    switch (static_cast<PduType>(t)) {
        case PduType::EntityState:
        case PduType::Fire:
        case PduType::Detonation:
        case PduType::StartResume:
        case PduType::StopFreeze:
        case PduType::Acknowledge:
        case PduType::DataQuery:
        case PduType::SetData:
        case PduType::Data:
            return true;
        default:
            return false;
    }
}

// --- Protocol Family IDs -------------------------------------------------

enum class ProtocolFamily : uint8_t {
    EntityInformation     = 1,
    Warfare               = 2,
    SimulationManagement  = 5,
};

// --- PDU Header (12 bytes, network byte order on the wire) ---------------

static constexpr size_t PDU_HEADER_SIZE = 12;
static constexpr size_t MAX_PDU_SIZE = 8192;

// Offsets within the 12-byte header
namespace hdr {
    static constexpr size_t PROTOCOL_VERSION = 0;  // u8
    static constexpr size_t EXERCISE_ID      = 1;  // u8
    static constexpr size_t PDU_TYPE         = 2;  // u8
    static constexpr size_t PROTOCOL_FAMILY  = 3;  // u8
    static constexpr size_t TIMESTAMP        = 4;  // u32
    static constexpr size_t LENGTH           = 8;  // u16
    static constexpr size_t PADDING          = 10; // u16
}

} // namespace dis
