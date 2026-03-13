#pragma once
#include "dis_types.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include <optional>

namespace dis {

// Field data type
enum class FieldType { U8, U16, U32, S32, F32, F64 };

// Describes where a named field lives in a PDU buffer
struct FieldDescriptor {
    size_t    offset;
    FieldType type;

    size_t size() const {
        switch (type) {
            case FieldType::U8:  return 1;
            case FieldType::U16: return 2;
            case FieldType::U32: return 4;
            case FieldType::S32: return 4;
            case FieldType::F32: return 4;
            case FieldType::F64: return 8;
        }
        return 0;
    }
};

// A value that can be read from / written to a field
using FieldValue = std::variant<uint8_t, uint16_t, uint32_t, int32_t, float, double>;

// Registry of field descriptors per PDU type
using FieldMap = std::unordered_map<std::string, FieldDescriptor>;

// Initialise the global registry (called once at startup)
void init_field_registry();

// Look up a field descriptor for a given PDU type and field path
const FieldDescriptor* lookup_field(uint8_t pdu_type, const std::string& field_path);

// Read a field value from a raw PDU buffer
std::optional<FieldValue> read_field(const uint8_t* buf, size_t len,
                                      uint8_t pdu_type, const std::string& field_path);

// Write a field value into a raw PDU buffer (in-place)
bool write_field(uint8_t* buf, size_t len,
                 uint8_t pdu_type, const std::string& field_path,
                 const FieldValue& value);

// Convert a numeric (double from JSON) to the appropriate FieldValue for a descriptor
FieldValue coerce_value(const FieldDescriptor& fd, double v);

// Convert FieldValue to double for comparison
double field_value_to_double(const FieldValue& v);

} // namespace dis
