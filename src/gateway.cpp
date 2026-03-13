#include "gateway.hpp"
#include "net/udp_socket.hpp"
#include "rules/rule_engine.hpp"
#include "dis/codec.hpp"

#include <iostream>
#include <stdexcept>

Gateway::Gateway(const GatewayConfig& cfg) : cfg_(cfg) {
    // Set up receiver
    UdpReceiver::Config rx{};
    rx.bind_addr   = cfg_.listen_addr;
    rx.port        = cfg_.listen_port;
    rx.mcast_group = cfg_.mcast_group;
    rx.mcast_iface = cfg_.mcast_iface;
    receiver_ = std::make_unique<UdpReceiver>(rx);

    // Set up sender
    UdpSender::Config tx{};
    tx.dest_addr   = cfg_.forward_addr;
    tx.dest_port   = cfg_.forward_port;
    tx.mcast_iface = cfg_.fwd_mcast_iface;
    tx.mcast_ttl   = cfg_.mcast_ttl;
    sender_ = std::make_unique<UdpSender>(tx);

    // Set up rule engine
    RuleEngine::Options eng_opts{};
    eng_opts.passthrough_on_error = cfg_.passthrough_on_error;
    engine_ = std::make_unique<RuleEngine>(cfg_.rules_file, eng_opts);
    engine_->set_log_callback([](const std::string& msg) {
        std::cout << "[rules] " << msg << "\n";
    });

    std::cout << "[gateway] Listening on "
              << cfg_.listen_addr << ":" << cfg_.listen_port;
    if (!cfg_.mcast_group.empty())
        std::cout << " (multicast group " << cfg_.mcast_group << ")";
    std::cout << "\n";
    std::cout << "[gateway] Forwarding to "
              << cfg_.forward_addr << ":" << cfg_.forward_port << "\n";
    std::cout << "[gateway] Rules: " << cfg_.rules_file << "\n";
}

Gateway::~Gateway() = default;

void Gateway::stop() {
    running_.store(false, std::memory_order_relaxed);
}

void Gateway::run() {
    running_.store(true, std::memory_order_relaxed);

    while (running_.load(std::memory_order_relaxed)) {
        auto datagram = receiver_->recv(cfg_.recv_timeout_ms);
        if (!datagram) continue;  // timeout – check running_ flag

        ++stats_.received;

        // Decode
        auto pdu_opt = dis::decode(datagram->data(), datagram->size());
        if (!pdu_opt) {
            std::cerr << "[gateway] Decode failed for "
                      << datagram->size() << "-byte datagram\n";
            ++stats_.errors;
            continue;
        }

        // Apply rules
        auto result = engine_->transform(*pdu_opt);
        if (!result) {
            ++stats_.dropped;
            continue;  // transform() returned nil → drop
        }

        // Re-encode
        auto out = dis::encode(*result);

        // Forward
        ssize_t sent = sender_->send(out);
        if (sent < 0 || static_cast<std::size_t>(sent) != out.size()) {
            std::cerr << "[gateway] Send error (sent=" << sent
                      << " expected=" << out.size() << ")\n";
            ++stats_.errors;
        } else {
            ++stats_.forwarded;
        }
    }

    std::cout << "[gateway] Stopped. Received=" << stats_.received
              << " Forwarded=" << stats_.forwarded
              << " Dropped=" << stats_.dropped
              << " Errors=" << stats_.errors << "\n";
}
