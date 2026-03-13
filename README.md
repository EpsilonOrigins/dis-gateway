# dis-gateway

A small, stateless C++ gateway that ingests UDP DIS PDUs from one network side,
applies deterministic rewrite rules to correct field differences between two DIS
implementations, then re-encodes and forwards the corrected PDUs to the target side.

## Features

- Supports **DIS 6** (IEEE 1278.1-1995) and **DIS 7** (IEEE 1278.1-2012) in the
  same pipeline — the protocol version field is preserved and detected automatically.
- **Full structured decode/encode** for the three most common PDU types:
  - Entity State PDU (type 1)
  - Fire PDU (type 2)
  - Detonation PDU (type 3)
- **Pass-through** for all other PDU types; raw bytes are forwarded unchanged.
- **Rule DSL** for rewrite logic — every field of every PDU type is accessible
  by name. Rules are loaded from a plain-text `.conf` file at startup.
- Unicast and **multicast** (IGMPv2/v3) on both ingress and egress.
- **Zero external dependencies** — pure C++17 + POSIX sockets.

## Building

```bash
# Prerequisites: a C++17 compiler and CMake >= 3.16
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
# Binary: build/dis-gateway
```

## Quick start

```bash
# Forward DIS traffic from port 3000 to another host, applying rewrite rules:
./build/dis-gateway \
    --listen-port  3000 \
    --forward-addr 10.0.0.2 \
    --forward-port 3000 \
    --rules        config/example_rules.conf

# No-op passthrough (verify connectivity before adding rules):
./build/dis-gateway \
    --listen-port  3000 \
    --forward-addr 10.0.0.2 \
    --forward-port 3000 \
    --rules        config/passthrough_rules.conf

# Multicast ingress -> unicast egress:
./build/dis-gateway \
    --mcast-group  239.1.2.3 \
    --listen-port  3000 \
    --forward-addr 10.0.0.2 \
    --forward-port 3000 \
    --rules        config/example_rules.conf
```

## Command-line reference

| Option | Default | Description |
|---|---|---|
| `--listen-addr ADDR` | `0.0.0.0` | Local address to bind for ingress |
| `--listen-port PORT` | `3000` | UDP port to listen on |
| `--mcast-group GROUP` | *(none)* | Join multicast group (e.g. `239.1.2.3`) |
| `--mcast-iface IFACE` | `0.0.0.0` | Interface for multicast join |
| `--forward-addr ADDR` | **required** | Egress destination address |
| `--forward-port PORT` | `3000` | Egress destination port |
| `--fwd-mcast-iface IFACE` | `0.0.0.0` | Interface for outbound multicast |
| `--mcast-ttl TTL` | `1` | Multicast TTL |
| `--rules FILE` | `rules.conf` | Rewrite rules file |
| `--drop-on-error` | *(off)* | Drop PDU on rule evaluation error instead of passing through |

## Writing rewrite rules

Rules files are plain text. Lines starting with `#` are comments.

### Directives

| Directive | Syntax | Description |
|---|---|---|
| `remap` | `remap <field> <old>=<new> ...` | Replace specific values via lookup table |
| `offset` | `offset <field> <delta>` | Add a constant to a numeric field |
| `set` | `set <field> <value>` | Overwrite a numeric field with a constant |
| `set_str` | `set_str <field> <text>` | Overwrite a string field (`marking.text`) |
| `drop` | `drop` | Discard the PDU; no further rules run |
| `passthrough` | `passthrough` | Explicit no-op |
| `log` | `log <message>` | Print a message to stdout |

### Conditional blocks

```
if <field> [== | != | in] <value> [<value2> ...]
    <actions>
endif
```

Examples:
```
if pdu_type == 1          # single equality
if exercise_id != 99      # inequality
if pdu_type in 1 2 3      # membership
if pdu_type 1             # bare value = equality shorthand
```

### Common recipe

```conf
# Remap entity site IDs across all PDU types
remap entity_id.site         1=2  2=1
remap firing_entity_id.site  1=2  2=1
remap target_entity_id.site  1=2  2=1
remap munition_id.site       1=2  2=1

# Fix entity type country code for Entity State PDUs
if pdu_type == 1
    set entity_type.country 225
endif

# Apply an ECEF coordinate offset
if pdu_type == 1
    offset location.x 1000.0
    offset location.y 0.0
    offset location.z 0.0
endif

# Swap Friendly/Opposing force IDs
if pdu_type == 1
    remap force_id  1=2  2=1
endif

# Rename entity marking
if pdu_type == 1
    set_str marking.text ALPHA01
endif

# Drop a maintenance exercise
if exercise_id == 99
    drop
endif
```

### Field path reference

**Header fields** (shorthand aliases accepted):

| Shorthand | Full path | Type |
|---|---|---|
| `pdu_type` | `header.pdu_type` | uint8 |
| `exercise_id` | `header.exercise_id` | uint8 |
| `protocol_version` | `header.protocol_version` | uint8 |
| `timestamp` | `header.timestamp` | uint32 |
| `pdu_status` | `header.pdu_status` | uint16 |

**Entity State PDU** (pdu_type 1):

`entity_id.site/application/entity`, `force_id`, `appearance`, `capabilities`,
`entity_type.kind/domain/country/category/subcategory/specific/extra`,
`alt_entity_type.<same>`,
`velocity.x/y/z` (m/s float),
`location.x/y/z` (ECEF metres double),
`orientation.psi/theta/phi` (radians float),
`dead_reckoning.algorithm`,
`dead_reckoning.linear_acceleration.x/y/z`,
`dead_reckoning.angular_velocity.x/y/z`,
`marking.character_set`, `marking.text` (string)

**Fire PDU** (pdu_type 2):

`firing_entity_id.site/application/entity`,
`target_entity_id.site/application/entity`,
`munition_id.site/application/entity`,
`event_id.site/application/event_num`,
`fire_mission_index`, `range`,
`location.x/y/z`, `velocity.x/y/z`,
`burst_descriptor.warhead/fuse/quantity/rate`,
`burst_descriptor.munition.kind/domain/country/category/subcategory/specific/extra`

**Detonation PDU** (pdu_type 3):

Same firing/target/munition/event fields as Fire, plus:
`location.x/y/z`, `velocity.x/y/z`, `burst_descriptor.<same>`,
`location_entity_coords.x/y/z`, `detonation_result`

## Architecture

```
UDP datagram
    |
    v
[UdpReceiver]  (bind + optional multicast join)
    |
    v (raw bytes)
[dis::decode()]  (big-endian PDU parser)
    |
    v (AnyPdu variant: EntityStatePdu | FirePdu | DetonationPdu | UnknownPdu)
[RuleEngine::transform()]
    |  - evaluates conditional blocks top-to-bottom
    |  - applies: remap / offset / set / set_str / drop / log
    |  - returns modified PDU or nullopt (drop)
    v
[dis::encode()]  (re-serialise; PDU length field recomputed)
    |
    v (raw bytes)
[UdpSender]  (unicast or multicast forward)
```

## Project structure

```
dis-gateway/
├── CMakeLists.txt
├── config/
│   ├── example_rules.conf     # annotated rules with common rewrite patterns
│   └── passthrough_rules.conf # no-op passthrough
└── src/
    ├── main.cpp               # CLI argument parsing, signal handling
    ├── gateway.hpp/cpp        # main loop: receive -> transform -> forward
    ├── dis/
    │   ├── byte_buffer.hpp    # big-endian read/write buffer
    │   ├── types.hpp          # EntityID, Vector3, EntityType, PduHeader, ...
    │   ├── enums.hpp          # PduType, ForceID, ProtocolFamily, ...
    │   ├── pdus.hpp           # EntityStatePdu, FirePdu, DetonationPdu, UnknownPdu
    │   ├── codec.hpp
    │   └── codec.cpp          # decode() / encode()
    ├── net/
    │   ├── udp_socket.hpp
    │   └── udp_socket.cpp     # UdpReceiver (unicast+multicast), UdpSender
    └── rules/
        ├── field_accessor.hpp/cpp   # dot-notation field get/set on AnyPdu
        ├── rule_engine.hpp
        └── rule_engine.cpp          # DSL parser + evaluator
```
