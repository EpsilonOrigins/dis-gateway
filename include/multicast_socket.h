#pragma once
#include <string>
#include <cstdint>
#include <cstddef>

namespace dis {

class MulticastSocket {
public:
    MulticastSocket() = default;
    ~MulticastSocket();

    MulticastSocket(const MulticastSocket&) = delete;
    MulticastSocket& operator=(const MulticastSocket&) = delete;
    MulticastSocket(MulticastSocket&& o) noexcept;
    MulticastSocket& operator=(MulticastSocket&& o) noexcept;

    // Open, bind, and join the multicast group.
    // bind_port: local port to bind (0 to let OS choose for send-only)
    // mcast_group: multicast group address (e.g. "239.1.2.3")
    // iface_addr: local interface address (e.g. "0.0.0.0" for default)
    // ttl: multicast TTL
    bool open(const std::string& mcast_group, uint16_t bind_port,
              const std::string& iface_addr = "0.0.0.0",
              int ttl = 32);

    // Send data to the configured multicast group:port
    bool send(const uint8_t* data, size_t len, uint16_t dest_port) const;

    // Receive data (blocking until data or timeout)
    int recv(uint8_t* buf, size_t max_len) const;

    // Get raw file descriptor for poll/select
    int fd() const { return fd_; }

    // Check if socket is open
    bool is_open() const { return fd_ >= 0; }

    // Close the socket
    void close();

    const std::string& group() const { return mcast_group_; }
    uint16_t port() const { return bind_port_; }

private:
    int fd_ = -1;
    std::string mcast_group_;
    uint16_t bind_port_ = 0;
};

} // namespace dis
