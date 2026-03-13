#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include <atomic>

class UdpReceiver;
class UdpSender;
class RuleEngine;

// ---------------------------------------------------------------------------
// Gateway configuration
// ---------------------------------------------------------------------------
struct GatewayConfig {
    // Ingress (listen) side
    std::string listen_addr   = "0.0.0.0";
    uint16_t    listen_port   = 3000;
    std::string mcast_group   = "";        // empty = unicast
    std::string mcast_iface   = "0.0.0.0";

    // Egress (forward) side
    std::string forward_addr  = "127.0.0.1";
    uint16_t    forward_port  = 3001;
    std::string fwd_mcast_iface = "0.0.0.0";
    uint8_t     mcast_ttl     = 1;

    // Rule engine
    std::string rules_file    = "rules.conf";
    bool        passthrough_on_error = true;

    // Operational
    int         recv_timeout_ms = 100;   // select() timeout per iteration
};

// ---------------------------------------------------------------------------
// Gateway – owns the sockets and rule engine, drives the main loop
// ---------------------------------------------------------------------------
class Gateway {
public:
    explicit Gateway(const GatewayConfig& cfg);
    ~Gateway();

    Gateway(const Gateway&) = delete;
    Gateway& operator=(const Gateway&) = delete;

    // Run until stop() is called (e.g. from a signal handler).
    void run();
    void stop();

    struct Stats {
        uint64_t received  = 0;
        uint64_t forwarded = 0;
        uint64_t dropped   = 0;   // returned nil by transform()
        uint64_t errors    = 0;   // decode failures or send errors
    };
    Stats stats() const { return stats_; }

private:
    GatewayConfig cfg_;
    std::unique_ptr<UdpReceiver> receiver_;
    std::unique_ptr<UdpSender>   sender_;
    std::unique_ptr<RuleEngine>  engine_;
    std::atomic<bool>            running_{false};
    Stats                        stats_;
};
