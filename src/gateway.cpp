#include "gateway.h"
#include "pdu_codec.h"
#include <poll.h>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <algorithm>

namespace dis {

GatewayConfig parse_config(const nlohmann::json& j) {
    GatewayConfig cfg;

    auto parse_side = [](const nlohmann::json& s) -> SideConfig {
        SideConfig sc;
        sc.address   = s.at("address").get<std::string>();
        sc.interface = s.value("interface", "0.0.0.0");
        sc.ttl       = s.value("ttl", 32);

        sc.send_port    = s.at("send_port").get<uint16_t>();
        sc.receive_port = s.at("receive_port").get<uint16_t>();
        return sc;
    };

    cfg.side_a = parse_side(j.at("side_a"));
    cfg.side_b = parse_side(j.at("side_b"));

    cfg.passthrough_unknown = j.value("passthrough_unknown", true);
    cfg.stats_interval_sec  = j.value("stats_interval_sec", 30);
    cfg.log_level           = j.value("log_level", "info");

    if (j.contains("rules_a_to_b")) cfg.rules_a_to_b = parse_rules(j["rules_a_to_b"]);
    if (j.contains("rules_b_to_a")) cfg.rules_b_to_a = parse_rules(j["rules_b_to_a"]);

    return cfg;
}

Gateway::Gateway(GatewayConfig config)
    : config_(std::move(config)) {}

void Gateway::run() {
    // Open sockets for a side based on port configuration:
    //   same ports   → one socket on the shared port (send_sock handles both)
    //   different    → send_sock on send_port, recv_sock on receive_port
    auto open_side = [](const SideConfig& cfg, const char* label,
                        MulticastSocket& send_sock, MulticastSocket& recv_sock) -> bool {
        if (cfg.single_port()) {
            // Single-port: one socket for both send and receive
            if (!send_sock.open(cfg.address, cfg.send_port, cfg.interface, cfg.ttl)) {
                std::fprintf(stderr, "ERROR: Failed to open %s socket (%s:%u)\n",
                             label, cfg.address.c_str(), cfg.send_port);
                return false;
            }
            std::printf("%s: %s:%u (single port, iface %s)\n",
                        label, cfg.address.c_str(), cfg.send_port, cfg.interface.c_str());
        } else {
            // Dual-port: sender on send_port, receiver on receive_port
            if (!send_sock.open(cfg.address, cfg.send_port, cfg.interface, cfg.ttl)) {
                std::fprintf(stderr, "ERROR: Failed to open %s send socket (%s:%u)\n",
                             label, cfg.address.c_str(), cfg.send_port);
                return false;
            }
            if (!recv_sock.open(cfg.address, cfg.receive_port, cfg.interface, cfg.ttl)) {
                std::fprintf(stderr, "ERROR: Failed to open %s recv socket (%s:%u)\n",
                             label, cfg.address.c_str(), cfg.receive_port);
                return false;
            }
            std::printf("%s: %s send:%u recv:%u (dual port, iface %s)\n",
                        label, cfg.address.c_str(), cfg.send_port, cfg.receive_port,
                        cfg.interface.c_str());
        }
        return true;
    };

    if (!open_side(config_.side_a, "Side A", sock_a_send_, sock_a_recv_))
        return;
    if (!open_side(config_.side_b, "Side B", sock_b_send_, sock_b_recv_))
        return;

    std::printf("Rules: %zu a->b, %zu b->a\n",
                config_.rules_a_to_b.size(), config_.rules_b_to_a.size());
    std::printf("Passthrough unknown PDU types: %s\n",
                config_.passthrough_unknown ? "yes" : "no");
    std::printf("Gateway running...\n");

    running_ = true;
    time_t last_stats = std::time(nullptr);

    // Poll the receive fd for each side.
    // Single-port: send socket is also the receiver.
    // Dual-port: dedicated recv socket.
    struct pollfd pfds[2]{};
    pfds[0].fd = config_.side_a.single_port() ? sock_a_send_.fd() : sock_a_recv_.fd();
    pfds[0].events = POLLIN;
    pfds[1].fd = config_.side_b.single_port() ? sock_b_send_.fd() : sock_b_recv_.fd();
    pfds[1].events = POLLIN;

    uint8_t buf[MAX_PDU_SIZE];

    while (running_) {
        int ret = ::poll(pfds, 2, 1000); // 1s timeout for stats
        if (ret < 0) {
            if (errno == EINTR) continue;
            std::perror("poll");
            break;
        }

        // Side A received → forward to side B
        if (pfds[0].revents & POLLIN) {
            MulticastSocket& rsock = config_.side_a.single_port() ? sock_a_send_ : sock_a_recv_;
            int n = rsock.recv(buf, sizeof(buf));
            if (n > 0) {
                handle_pdu(buf, static_cast<size_t>(n),
                           config_.rules_a_to_b,
                           sock_b_send_, config_.side_b.send_port,
                           sock_a_send_, config_.side_a.send_port,
                           "A->B");
            }
        }

        // Side B received → forward to side A
        if (pfds[1].revents & POLLIN) {
            MulticastSocket& rsock = config_.side_b.single_port() ? sock_b_send_ : sock_b_recv_;
            int n = rsock.recv(buf, sizeof(buf));
            if (n > 0) {
                handle_pdu(buf, static_cast<size_t>(n),
                           config_.rules_b_to_a,
                           sock_a_send_, config_.side_a.send_port,
                           sock_b_send_, config_.side_b.send_port,
                           "B->A");
            }
        }

        // Periodic stats dump
        time_t now = std::time(nullptr);
        if (config_.stats_interval_sec > 0 &&
            (now - last_stats) >= config_.stats_interval_sec) {
            dump_stats();
            last_stats = now;
        }
    }

    std::printf("Gateway stopped.\n");
}

void Gateway::stop() {
    running_ = false;
}

void Gateway::handle_pdu(const uint8_t* buf, size_t len,
                         const std::vector<Rule>& rules,
                         MulticastSocket& forward_sock, uint16_t forward_port,
                         MulticastSocket& source_sock, uint16_t source_port,
                         const char* direction) {
    bool is_a_to_b = (direction[0] == 'A');

    // Validate minimum header
    if (len < PDU_HEADER_SIZE) {
        ++stats_.errors;
        return;
    }

    uint8_t pdu_type = buf[hdr::PDU_TYPE];

    // Check if this is a supported PDU type
    if (!is_supported_pdu(pdu_type)) {
        if (config_.passthrough_unknown) {
            forward_sock.send(buf, len, forward_port);
            ++stats_.passthrough;
        }
        return;
    }

    if (config_.log_level == "debug") {
        uint16_t pdu_len = read_u16(buf + hdr::LENGTH);
        std::printf("[%s] %s PDU, %zu bytes (header.length=%u)\n",
                    direction, pdu_type_name(pdu_type), len, pdu_len);
    }

    // Copy buffer for in-place modification
    uint8_t work[MAX_PDU_SIZE];
    size_t work_len = std::min(len, static_cast<size_t>(MAX_PDU_SIZE));
    std::memcpy(work, buf, work_len);

    // Apply rules
    RuleResult result = apply_rules(rules, work, work_len);

    // Handle synthetic response
    if (result.has_response && !result.response_pdu.empty()) {
        source_sock.send(result.response_pdu.data(), result.response_pdu.size(), source_port);
        if (is_a_to_b) ++stats_.a_to_b_responded;
        else            ++stats_.b_to_a_responded;
    }

    // Handle blocked
    if (result.blocked || !result.forward) {
        if (is_a_to_b) ++stats_.a_to_b_blocked;
        else            ++stats_.b_to_a_blocked;
        return;
    }

    // Forward (possibly modified)
    forward_sock.send(work, work_len, forward_port);

    if (result.modified) {
        if (is_a_to_b) ++stats_.a_to_b_modified;
        else            ++stats_.b_to_a_modified;
    }
    if (is_a_to_b) ++stats_.a_to_b_forwarded;
    else            ++stats_.b_to_a_forwarded;
}

void Gateway::dump_stats() const {
    std::printf("--- Gateway Stats ---\n");
    std::printf("  A->B: forwarded=%lu blocked=%lu modified=%lu responded=%lu\n",
                stats_.a_to_b_forwarded, stats_.a_to_b_blocked,
                stats_.a_to_b_modified, stats_.a_to_b_responded);
    std::printf("  B->A: forwarded=%lu blocked=%lu modified=%lu responded=%lu\n",
                stats_.b_to_a_forwarded, stats_.b_to_a_blocked,
                stats_.b_to_a_modified, stats_.b_to_a_responded);
    std::printf("  Passthrough: %lu  Errors: %lu\n",
                stats_.passthrough, stats_.errors);
    std::printf("---------------------\n");
}

} // namespace dis
