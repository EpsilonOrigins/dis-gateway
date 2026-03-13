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
    std::string address;               // multicast group address
    uint16_t    send_port = 3000;      // port used for sending
    uint16_t    receive_port = 3000;   // port used for receiving
    std::string interface = "0.0.0.0";
    int         ttl = 32;

    // True when send and receive use the same port (single socket mode)
    bool single_port() const { return send_port == receive_port; }
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
    Gateway(GatewayConfig config, std::string config_path);

    // Run the gateway event loop (blocks until stop() is called)
    void run();

    // Signal the gateway to stop
    void stop();

    // Request a rule reload on the next loop iteration (safe to call from signal handlers)
    void request_reload();

private:
    void handle_pdu(const uint8_t* buf, size_t len,
                    const std::vector<Rule>& rules,
                    MulticastSocket& forward_sock, uint16_t forward_port,
                    MulticastSocket& source_sock, uint16_t source_port,
                    const char* direction);

    void dump_stats() const;

    // Re-read rules (and non-network settings) from the config file in place.
    // Called from the main loop; sockets are not affected.
    void reload_rules();

    GatewayConfig config_;
    std::string   config_path_;     // path to config file, used for hot-reload
    MulticastSocket sock_a_send_;   // side A send socket (bound to send_port)
    MulticastSocket sock_a_recv_;   // side A recv socket (bound to receive_port; unused in single-port mode)
    MulticastSocket sock_b_send_;   // side B send socket (bound to send_port)
    MulticastSocket sock_b_recv_;   // side B recv socket (bound to receive_port; unused in single-port mode)
    std::atomic<bool> running_{false};
    std::atomic<bool> reload_pending_{false};
    int inotify_fd_ = -1;   // inotify instance fd (-1 if unavailable)
    int inotify_wd_ = -1;   // watch descriptor for config file's directory
    Stats stats_;
};

// Parse a full GatewayConfig from JSON
GatewayConfig parse_config(const nlohmann::json& j);

// Parse only the rule arrays and non-network settings from JSON into an existing config.
// Side network parameters (address, ports, interface, ttl) are left unchanged.
void parse_rules_into(const nlohmann::json& j, GatewayConfig& cfg);

} // namespace dis
