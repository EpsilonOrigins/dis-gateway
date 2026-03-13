#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <netinet/in.h>

// ---------------------------------------------------------------------------
// UdpReceiver – binds a UDP socket (unicast or multicast) for ingress.
// ---------------------------------------------------------------------------
class UdpReceiver {
public:
    struct Config {
        std::string bind_addr    = "0.0.0.0"; // local address to bind
        uint16_t    port         = 3000;
        std::string mcast_group  = "";         // empty = unicast
        std::string mcast_iface  = "0.0.0.0"; // interface for multicast join
        bool        reuse_addr   = true;
        std::size_t recv_buf_size = 65536;
    };

    explicit UdpReceiver(const Config& cfg);
    ~UdpReceiver();

    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;

    // Receive one datagram (blocks if timeout_ms == 0).
    // timeout_ms > 0: waits at most that many milliseconds; returns empty on timeout.
    std::optional<std::vector<uint8_t>> recv(int timeout_ms = 0);

    int fd() const { return fd_; }

private:
    int fd_ = -1;
    bool mcast_ = false;
    struct ip_mreq mreq_ = {};

    void join_multicast(const std::string& group, const std::string& iface);
    void leave_multicast();
};

// ---------------------------------------------------------------------------
// UdpSender – sends UDP datagrams to a fixed destination.
// ---------------------------------------------------------------------------
class UdpSender {
public:
    struct Config {
        std::string dest_addr;
        uint16_t    dest_port   = 3000;
        std::string bind_addr   = "0.0.0.0"; // source address (usually 0.0.0.0)
        std::string mcast_iface = "0.0.0.0"; // for multicast sends
        uint8_t     mcast_ttl   = 1;
        std::size_t send_buf_size = 65536;
    };

    explicit UdpSender(const Config& cfg);
    ~UdpSender();

    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;

    // Send a datagram.  Returns number of bytes sent or -1 on error.
    ssize_t send(const uint8_t* data, std::size_t len);
    ssize_t send(const std::vector<uint8_t>& buf) {
        return send(buf.data(), buf.size());
    }

private:
    int fd_ = -1;
    struct sockaddr_in dest_ = {};
};
