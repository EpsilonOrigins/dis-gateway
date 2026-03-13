#pragma once
#include "dis/pdus.hpp"
#include <optional>
#include <string>

// ---------------------------------------------------------------------------
// FieldAccessor
//
// Maps dot-notation field paths (e.g. "entity_id.site", "location.x") to
// typed getters and setters on the AnyPdu variant.
//
// Numeric fields  → double  (integers are cast to/from double transparently)
// String fields   → std::string  (currently: marking.text)
//
// Returns std::nullopt / false when the field doesn't exist on the given
// PDU type (e.g. asking for "firing_entity_id.site" on an EntityStatePdu).
// ---------------------------------------------------------------------------
namespace FieldAccessor {

// Read a numeric field.  Header shorthand aliases accepted:
//   "pdu_type"        ≡ "header.pdu_type"
//   "exercise_id"     ≡ "header.exercise_id"
//   "protocol_version"≡ "header.protocol_version"
//   "timestamp"       ≡ "header.timestamp"
std::optional<double>      get    (const dis::AnyPdu& pdu, const std::string& path);
bool                       set    (dis::AnyPdu&       pdu, const std::string& path, double value);

// Read / write a string field (marking.text).
std::optional<std::string> get_str(const dis::AnyPdu& pdu, const std::string& path);
bool                       set_str(dis::AnyPdu&       pdu, const std::string& path,
                                   const std::string& value);

} // namespace FieldAccessor
