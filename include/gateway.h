#pragma once
#include "multicast_socket.h"
#include "rule_engine.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <string>
#include <vector>
#include <cstdint>

namespace dis {

struct SideConfig {
    std::string address;           // multicast group address
    uint16_t    port = 3000;       // send/receive port
    std::string interface = "0.0.0.0";
    uint16_t    receive_only_port = 0;  // optional additional receive-only port (0 = disabled)
    int         ttl = 32;
};

struct GatewayConfig {
    SideConfig side_a;
    SideConfig side_b;
    bool       passthrough_unknown = true;
    int        stats_interval_sec  = 30;
    std::string log_level = "info";
    std::vector<Rule> rules_a_to_b;
    std::vector<Rule> rules_b_to_a;
};

struct Stats {
    uint64_t a_to_b_forwarded = 0;
    uint64_t a_to_b_blocked   = 0;
    uint64_t a_to_b_modified  = 0;
    uint64_t a_to_b_responded = 0;
    uint64_t b_to_a_forwarded = 0;
    uint64_t b_to_a_blocked   = 0;
    uint64_t b_to_a_modified  = 0;
    uint64_t b_to_a_responded = 0;
    uint64_t passthrough      = 0;
    uint64_t errors           = 0;
};

class Gateway {
public:
    explicit Gateway(GatewayConfig config);

    // Run the gateway event loop (blocks until stop() is called)
    void run();

    // Signal the gateway to stop
    void stop();

private:
    void handle_pdu(const uint8_t* buf, size_t len,
                    const std::vector<Rule>& rules,
                    MulticastSocket& forward_sock, uint16_t forward_port,
                    MulticastSocket& source_sock, uint16_t source_port,
                    const char* direction);

    void dump_stats() const;

    GatewayConfig config_;
    MulticastSocket sock_a_;
    MulticastSocket sock_a_recv_;  // optional receive-only socket for side A
    MulticastSocket sock_b_;
    MulticastSocket sock_b_recv_;  // optional receive-only socket for side B
    std::atomic<bool> running_{false};
    Stats stats_;
};

// Parse GatewayConfig from JSON
GatewayConfig parse_config(const nlohmann::json& j);

} // namespace dis
