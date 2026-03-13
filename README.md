# dis-gateway

A DIS (Distributed Interactive Simulation) translation gateway that bridges two separate DIS networks. It ingests UDP-based PDUs (Protocol Data Units) from two multicast groups, applies configurable rewrite/filter rules, and forwards modified PDUs to the opposite network.

## Features

- **Bidirectional translation** between two DIS multicast networks (Side A and Side B)
- **Rule-based PDU processing**: block, respond, override, and field-replace actions
- **Field remapping**: rewrite entity IDs, exercise IDs, and other PDU fields
- **Synthetic responses**: intercept queries and reply with custom payloads
- **Dual-port mode**: separate send and receive ports per side
- **Dry-run mode**: validate configuration without opening any sockets
- **Live statistics**: periodic counters for forwarded, blocked, and modified PDUs

## Supported PDU Types

| Type ID | Name |
|---------|------|
| 1 | EntityState |
| 2 | Fire |
| 3 | Detonation |
| 13 | StartResume |
| 14 | StopFreeze |
| 15 | Acknowledge |
| 18 | DataQuery |
| 19 | SetData |
| 20 | Data |

## Requirements

- Linux (POSIX multicast socket APIs)
- CMake 3.14+
- C++17 compiler (GCC or Clang)

## Build

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

The executable is placed at `build/dis-gateway`.

## Usage

```bash
# Run with a config file
./dis-gateway config.json

# Validate config without starting (dry-run)
./dis-gateway config.json --dry-run

# Help
./dis-gateway --help
```

Send `SIGINT` (Ctrl+C) or `SIGTERM` to trigger a graceful shutdown.

## Configuration

All runtime behaviour is driven by a JSON config file. See [config.json](config.json) for a full example.

```json
{
  "side_a": {
    "address":      "239.1.2.3",
    "send_port":    3000,
    "receive_port": 3000,
    "interface":    "0.0.0.0",
    "ttl":          32
  },
  "side_b": {
    "address":      "239.1.2.4",
    "send_port":    3000,
    "receive_port": 3000,
    "interface":    "0.0.0.0",
    "ttl":          32
  },
  "passthrough_unknown": true,
  "stats_interval_sec":  30,
  "log_level":           "info",
  "rules_a_to_b": [],
  "rules_b_to_a": []
}
```

**Dual-port mode** is activated automatically when `send_port` and `receive_port` differ; a separate socket is opened for each direction.

### Rule Actions

| Action | Description |
|--------|-------------|
| `block` | Drop the PDU; do not forward it |
| `respond` | Send a synthetic PDU back to the source |
| `override` | Replace the entire PDU payload with provided hex data |
| `replace` | Rewrite a specific field value when conditions match |

### Conditions and Field Paths

Rules can be conditional on field values. Field paths follow a dot-notation that mirrors the PDU structure:

```
header.exerciseId
entityId.site
entityId.application
entityId.entity
position.x
```

### Example Rules

**Block all Fire PDUs from a specific site:**
```json
{
  "action": "block",
  "pdu_type": 2,
  "conditions": [{ "field": "entityId.site", "equals": 5 }]
}
```

**Remap entity site IDs (Side A site 1 → Side B site 42):**
```json
{
  "action": "replace",
  "pdu_type": 1,
  "conditions": [{ "field": "entityId.site", "equals": 1 }],
  "field": "entityId.site",
  "value": 42
}
```

**Intercept a DataQuery and send a synthetic response:**
```json
{
  "action": "respond",
  "pdu_type": 18,
  "response_hex": "07014200...",
  "also_forward": false
}
```

## Project Structure

```
dis-gateway/
├── CMakeLists.txt          # Build configuration
├── config.json             # Example runtime configuration
├── src/
│   ├── main.cpp            # Entry point, config loading, signal handling
│   ├── gateway.cpp         # Event loop and multicast polling
│   ├── pdu_codec.cpp       # PDU field registry and byte-level codecs
│   ├── rule_engine.cpp     # Rule parsing and application logic
│   └── multicast_socket.cpp# Multicast socket abstraction
├── include/
│   ├── gateway.h
│   ├── rule_engine.h
│   ├── pdu_codec.h
│   ├── dis_types.h         # DIS protocol constants and byte-order helpers
│   └── multicast_socket.h
└── third_party/
    └── nlohmann/json.hpp   # JSON parsing (header-only)
```

## License

See repository root for license information.
