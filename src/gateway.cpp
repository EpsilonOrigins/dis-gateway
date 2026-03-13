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
        sc.address           = s.at("address").get<std::string>();
        sc.port              = s.at("port").get<uint16_t>();
        sc.interface         = s.value("interface", "0.0.0.0");
        sc.receive_only_port = s.value("receive_only_port", static_cast<uint16_t>(0));
        sc.ttl               = s.value("ttl", 32);
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
    // Open sockets for side A
    if (!sock_a_.open(config_.side_a.address, config_.side_a.port,
                      config_.side_a.interface, config_.side_a.ttl)) {
        std::fprintf(stderr, "ERROR: Failed to open side A socket (%s:%u)\n",
                     config_.side_a.address.c_str(), config_.side_a.port);
        return;
    }
    std::printf("Side A: joined %s:%u on %s\n",
                config_.side_a.address.c_str(), config_.side_a.port,
                config_.side_a.interface.c_str());

    // Optional receive-only socket for side A
    if (config_.side_a.receive_only_port > 0) {
        if (!sock_a_recv_.open(config_.side_a.address, config_.side_a.receive_only_port,
                               config_.side_a.interface, config_.side_a.ttl)) {
            std::fprintf(stderr, "WARN: Failed to open side A recv-only socket on port %u\n",
                         config_.side_a.receive_only_port);
        } else {
            std::printf("Side A: recv-only port %u\n", config_.side_a.receive_only_port);
        }
    }

    // Open sockets for side B
    if (!sock_b_.open(config_.side_b.address, config_.side_b.port,
                      config_.side_b.interface, config_.side_b.ttl)) {
        std::fprintf(stderr, "ERROR: Failed to open side B socket (%s:%u)\n",
                     config_.side_b.address.c_str(), config_.side_b.port);
        return;
    }
    std::printf("Side B: joined %s:%u on %s\n",
                config_.side_b.address.c_str(), config_.side_b.port,
                config_.side_b.interface.c_str());

    // Optional receive-only socket for side B
    if (config_.side_b.receive_only_port > 0) {
        if (!sock_b_recv_.open(config_.side_b.address, config_.side_b.receive_only_port,
                               config_.side_b.interface, config_.side_b.ttl)) {
            std::fprintf(stderr, "WARN: Failed to open side B recv-only socket on port %u\n",
                         config_.side_b.receive_only_port);
        } else {
            std::printf("Side B: recv-only port %u\n", config_.side_b.receive_only_port);
        }
    }

    std::printf("Rules: %zu a->b, %zu b->a\n",
                config_.rules_a_to_b.size(), config_.rules_b_to_a.size());
    std::printf("Passthrough unknown PDU types: %s\n",
                config_.passthrough_unknown ? "yes" : "no");
    std::printf("Gateway running...\n");

    running_ = true;
    time_t last_stats = std::time(nullptr);

    // Build poll fd array
    std::vector<struct pollfd> pfds;
    auto add_pfd = [&](int fd) {
        if (fd >= 0) {
            struct pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLIN;
            pfds.push_back(pfd);
        }
    };

    // Indices into pfds
    size_t idx_a_main = pfds.size(); add_pfd(sock_a_.fd());
    size_t idx_a_recv = pfds.size(); add_pfd(sock_a_recv_.fd());
    size_t idx_b_main = pfds.size(); add_pfd(sock_b_.fd());
    size_t idx_b_recv = pfds.size(); add_pfd(sock_b_recv_.fd());

    uint8_t buf[MAX_PDU_SIZE];

    while (running_) {
        int ret = ::poll(pfds.data(), pfds.size(), 1000); // 1s timeout for stats
        if (ret < 0) {
            if (errno == EINTR) continue;
            std::perror("poll");
            break;
        }

        // Check each socket for data
        for (size_t i = 0; i < pfds.size(); ++i) {
            if (!(pfds[i].revents & POLLIN)) continue;

            // Determine which socket this is
            bool from_a = (i == idx_a_main || i == idx_a_recv);
            bool from_b = (i == idx_b_main || i == idx_b_recv);

            MulticastSocket* recv_sock = nullptr;
            if (i == idx_a_main)      recv_sock = &sock_a_;
            else if (i == idx_a_recv) recv_sock = &sock_a_recv_;
            else if (i == idx_b_main) recv_sock = &sock_b_;
            else if (i == idx_b_recv) recv_sock = &sock_b_recv_;

            if (!recv_sock || !recv_sock->is_open()) continue;

            int n = recv_sock->recv(buf, sizeof(buf));
            if (n <= 0) continue;

            if (from_a) {
                handle_pdu(buf, static_cast<size_t>(n),
                           config_.rules_a_to_b,
                           sock_b_, config_.side_b.port,
                           sock_a_, config_.side_a.port,
                           "A->B");
            } else if (from_b) {
                handle_pdu(buf, static_cast<size_t>(n),
                           config_.rules_b_to_a,
                           sock_a_, config_.side_a.port,
                           sock_b_, config_.side_b.port,
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
