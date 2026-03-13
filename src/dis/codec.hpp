#pragma once
#include "pdus.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace dis {

// ---------------------------------------------------------------------------
// Decode a raw UDP datagram into an AnyPdu variant.
// Returns std::nullopt if the datagram is too short or malformed.
// ---------------------------------------------------------------------------
std::optional<AnyPdu> decode(const uint8_t* data, std::size_t len);

// ---------------------------------------------------------------------------
// Encode an AnyPdu variant back to a byte vector ready to transmit.
// The PDU length field in the header is recomputed automatically.
// ---------------------------------------------------------------------------
std::vector<uint8_t> encode(const AnyPdu& pdu);

} // namespace dis
