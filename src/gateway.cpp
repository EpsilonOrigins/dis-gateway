#include "gateway.h"
#include "pdu_codec.h"
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <algorithm>

namespace dis {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::string path_dirname(const std::string& path) {
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

static std::string path_basename(const std::string& path) {
    auto pos = path.find_last_of('/');
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// ---------------------------------------------------------------------------
// Config parsing
// ---------------------------------------------------------------------------

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

void parse_rules_into(const nlohmann::json& j, GatewayConfig& cfg) {
    cfg.passthrough_unknown = j.value("passthrough_unknown", cfg.passthrough_unknown);
    cfg.stats_interval_sec  = j.value("stats_interval_sec",  cfg.stats_interval_sec);
    cfg.log_level           = j.value("log_level",           cfg.log_level);

    if (j.contains("rules_a_to_b")) cfg.rules_a_to_b = parse_rules(j["rules_a_to_b"]);
    if (j.contains("rules_b_to_a")) cfg.rules_b_to_a = parse_rules(j["rules_b_to_a"]);
}

Gateway::Gateway(GatewayConfig config, std::string config_path)
    : config_(std::move(config)), config_path_(std::move(config_path)) {}

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

    // Set up inotify to watch the config file's parent directory.
    // Watching the directory (rather than the file directly) lets us detect
    // atomic-rename saves (e.g. vim, emacs) as well as plain writes.
    inotify_fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ < 0) {
        std::perror("inotify_init1");
        std::printf("Hot reload via inotify unavailable; use SIGUSR1 instead.\n");
    } else {
        std::string dir = path_dirname(config_path_);
        inotify_wd_ = ::inotify_add_watch(inotify_fd_, dir.c_str(),
                                          IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
        if (inotify_wd_ < 0) {
            std::perror("inotify_add_watch");
            ::close(inotify_fd_);
            inotify_fd_ = -1;
        } else {
            std::printf("Watching '%s' for config changes (SIGUSR1 also triggers reload).\n",
                        dir.c_str());
        }
    }

    std::printf("Gateway running...\n");

    running_ = true;
    time_t last_stats = std::time(nullptr);

    // Poll the receive fd for each side, plus inotify fd if available.
    // Single-port: send socket is also the receiver.
    // Dual-port: dedicated recv socket.
    struct pollfd pfds[3]{};
    pfds[0].fd = config_.side_a.single_port() ? sock_a_send_.fd() : sock_a_recv_.fd();
    pfds[0].events = POLLIN;
    pfds[1].fd = config_.side_b.single_port() ? sock_b_send_.fd() : sock_b_recv_.fd();
    pfds[1].events = POLLIN;
    int nfds = 2;
    if (inotify_fd_ >= 0) {
        pfds[2].fd     = inotify_fd_;
        pfds[2].events = POLLIN;
        nfds = 3;
    }

    const std::string config_basename = path_basename(config_path_);
    uint8_t buf[MAX_PDU_SIZE];

    while (running_) {
        int ret = ::poll(pfds, nfds, 1000); // 1s timeout for stats
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

        // inotify: check if our config file changed
        if (nfds == 3 && (pfds[2].revents & POLLIN)) {
            // Drain all pending inotify events
            alignas(inotify_event) char ibuf[4096];
            ssize_t nr = ::read(inotify_fd_, ibuf, sizeof(ibuf));
            while (nr > 0) {
                for (char* p = ibuf; p < ibuf + nr; ) {
                    auto* ev = reinterpret_cast<inotify_event*>(p);
                    if (ev->len > 0 && config_basename == ev->name) {
                        reload_pending_ = true;
                    }
                    p += sizeof(inotify_event) + ev->len;
                }
                nr = ::read(inotify_fd_, ibuf, sizeof(ibuf));
            }
        }

        // Process a pending reload (from inotify or SIGUSR1)
        if (reload_pending_.exchange(false)) {
            reload_rules();
        }

        // Periodic stats dump
        time_t now = std::time(nullptr);
        if (config_.stats_interval_sec > 0 &&
            (now - last_stats) >= config_.stats_interval_sec) {
            dump_stats();
            last_stats = now;
        }
    }

    // Clean up inotify
    if (inotify_fd_ >= 0) {
        ::close(inotify_fd_);
        inotify_fd_ = -1;
        inotify_wd_ = -1;
    }

    std::printf("Gateway stopped.\n");
}

void Gateway::stop() {
    running_ = false;
}

void Gateway::request_reload() {
    reload_pending_ = true;
}

void Gateway::reload_rules() {
    std::printf("Reloading rules from '%s'...\n", config_path_.c_str());

    nlohmann::json j;
    {
        std::ifstream ifs(config_path_);
        if (!ifs.is_open()) {
            std::fprintf(stderr, "WARN: Cannot open config for reload: %s\n",
                         config_path_.c_str());
            return;
        }
        try {
            ifs >> j;
        } catch (const nlohmann::json::parse_error& e) {
            std::fprintf(stderr, "WARN: JSON parse error during reload: %s\n", e.what());
            return;
        }
    }

    try {
        parse_rules_into(j, config_);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "WARN: Rule parse failed during reload: %s\n", e.what());
        return;
    }

    std::printf("Rules reloaded: %zu A->B, %zu B->A\n",
                config_.rules_a_to_b.size(), config_.rules_b_to_a.size());
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
