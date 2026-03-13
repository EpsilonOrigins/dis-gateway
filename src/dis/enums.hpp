#pragma once
#include <cstdint>

namespace dis {

// IEEE 1278.1 Protocol Version
enum class ProtocolVersion : uint8_t {
    DIS_1_0    = 1,
    IEEE_1278  = 2,
    DIS_2_0    = 3,
    DIS_3_0    = 4,
    DIS_4_0    = 5,
    IEEE_1278_1_1995 = 6,   // "DIS 6"
    IEEE_1278_1A_1998 = 7,
    IEEE_1278_1_2012 = 9,   // "DIS 7" (note: version field = 7 in practice)
    OTHER      = 0,
};

// PDU Type (first two protocol families shown; others are pass-through)
enum class PduType : uint8_t {
    // Entity Information / Interaction family (1)
    ENTITY_STATE          = 1,
    FIRE                  = 2,
    DETONATION            = 3,
    COLLISION             = 4,
    // Service Request family (6)
    SERVICE_REQUEST       = 5,
    RESUPPLY_OFFER        = 6,
    RESUPPLY_RECEIVED     = 7,
    RESUPPLY_CANCEL       = 8,
    REPAIR_COMPLETE       = 9,
    REPAIR_RESPONSE       = 10,
    // Simulation Management family (5)
    CREATE_ENTITY         = 11,
    REMOVE_ENTITY         = 12,
    START_RESUME          = 13,
    STOP_FREEZE           = 14,
    ACKNOWLEDGE           = 15,
    ACTION_REQUEST        = 16,
    ACTION_RESPONSE       = 17,
    DATA_QUERY            = 18,
    SET_DATA              = 19,
    DATA                  = 20,
    EVENT_REPORT          = 21,
    COMMENT               = 22,
    ELECTROMAGNETIC_EMISSION = 23,
    DESIGNATOR            = 24,
    TRANSMITTER           = 25,
    SIGNAL                = 26,
    RECEIVER              = 27,
    // DIS 7 additions
    IFF                   = 28,
    UNDERWATER_ACOUSTIC   = 29,
    SUPPLEMENTAL_EMISSION = 30,
    INTERCOM_SIGNAL       = 31,
    INTERCOM_CONTROL      = 32,
    AGGREGATE_STATE       = 33,
    IS_GROUP_OF           = 34,
    TRANSFER_OWNERSHIP    = 35,
    IS_PART_OF            = 36,
    MINEFIELD_STATE       = 37,
    MINEFIELD_QUERY       = 38,
    MINEFIELD_DATA        = 39,
    MINEFIELD_RESPONSE_NAK = 40,
    ENVIRONMENTAL_PROCESS = 41,
    GRID_DATA             = 42,
    POINT_OBJECT_STATE    = 43,
    LINEAR_OBJECT_STATE   = 44,
    AREAL_OBJECT_STATE    = 45,
    TSPI                  = 46,
    APPEARANCE            = 47,
    ARTICULATED_PARTS     = 48,
    LE_FIRE               = 49,
    LE_DETONATION         = 50,
    CREATE_ENTITY_R       = 51,
    REMOVE_ENTITY_R       = 52,
    START_RESUME_R        = 53,
    STOP_FREEZE_R         = 54,
    ACKNOWLEDGE_R         = 55,
    ACTION_REQUEST_R      = 56,
    ACTION_RESPONSE_R     = 57,
    DATA_QUERY_R          = 58,
    SET_DATA_R            = 59,
    DATA_R                = 60,
    EVENT_REPORT_R        = 61,
    COMMENT_R             = 62,
    RECORD_R              = 63,
    SET_RECORD_R          = 64,
    RECORD_QUERY_R        = 65,
    COLLISION_ELASTIC     = 66,
    ENTITY_STATE_UPDATE   = 67,
    DIRECTED_ENERGY_FIRE  = 68,
    ENTITY_DAMAGE_STATUS  = 69,
    IO_ACTION             = 70,
    IO_REPORT             = 71,
    ATTRIBUTE             = 72,
    ANNOUNCE_OBJECT       = 73,
    DELETE_OBJECT         = 74,
    DESCRIBE_APPLICATION  = 75,
    DESCRIBE_EVENT        = 76,
    DESCRIBE_OBJECT       = 77,
    REQUEST_EVENT         = 78,
    REQUEST_OBJECT        = 79,
    APPLICATION_CONTROL   = 200,
    SEES                  = 201,
};

// Protocol Family
enum class ProtocolFamily : uint8_t {
    OTHER                       = 0,
    ENTITY_INFORMATION          = 1,
    WARFARE                     = 2,
    LOGISTICS                   = 3,
    RADIO_COMMUNICATION         = 4,
    SIMULATION_MANAGEMENT       = 5,
    DISTRIBUTED_EMISSION_REGEN  = 6,
    ENTITY_MANAGEMENT           = 7,
    MINEFIELD                   = 8,
    SYNTHETIC_ENVIRONMENT       = 9,
    SIMULATION_MANAGEMENT_R     = 10,
    LIVE_ENTITY                 = 11,
    NON_REAL_TIME               = 12,
    INFORMATION_OPS             = 13,
};

// Force ID
enum class ForceID : uint8_t {
    OTHER    = 0,
    FRIENDLY = 1,
    OPPOSING = 2,
    NEUTRAL  = 3,
    FRIENDLY_2 = 4,
    OPPOSING_2 = 5,
    NEUTRAL_2  = 6,
};

// Dead Reckoning Algorithm
enum class DeadReckoningAlgorithm : uint8_t {
    OTHER        = 0,
    STATIC       = 1,
    DRM_F_P_W    = 2,
    DRM_R_P_W    = 3,
    DRM_R_V_W    = 4,
    DRM_F_V_W    = 5,
    DRM_F_P_B    = 6,
    DRM_R_P_B    = 7,
    DRM_R_V_B    = 8,
    DRM_F_V_B    = 9,
};

} // namespace dis
