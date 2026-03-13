#include "multicast_socket.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <cstdio>

namespace dis {

MulticastSocket::~MulticastSocket() {
    close();
}

MulticastSocket::MulticastSocket(MulticastSocket&& o) noexcept
    : fd_(o.fd_), mcast_group_(std::move(o.mcast_group_)), bind_port_(o.bind_port_) {
    o.fd_ = -1;
}

MulticastSocket& MulticastSocket::operator=(MulticastSocket&& o) noexcept {
    if (this != &o) {
        close();
        fd_ = o.fd_;
        mcast_group_ = std::move(o.mcast_group_);
        bind_port_ = o.bind_port_;
        o.fd_ = -1;
    }
    return *this;
}

bool MulticastSocket::open(const std::string& mcast_group, uint16_t bind_port,
                           const std::string& iface_addr, int ttl) {
    close();

    fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd_ < 0) {
        std::perror("socket");
        return false;
    }

    mcast_group_ = mcast_group;
    bind_port_ = bind_port;

    // Allow multiple sockets on the same port
    int reuse = 1;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::perror("SO_REUSEADDR");
    }
#ifdef SO_REUSEPORT
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        std::perror("SO_REUSEPORT");
    }
#endif

    // Bind to the port
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(bind_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        close();
        return false;
    }

    // Join multicast group
    struct ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = ::inet_addr(mcast_group.c_str());
    mreq.imr_interface.s_addr = ::inet_addr(iface_addr.c_str());

    if (::setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        std::perror("IP_ADD_MEMBERSHIP");
        close();
        return false;
    }

    // Set multicast TTL
    uint8_t ttl_val = static_cast<uint8_t>(ttl);
    if (::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl_val, sizeof(ttl_val)) < 0) {
        std::perror("IP_MULTICAST_TTL");
    }

    // Set outgoing interface
    struct in_addr iface{};
    iface.s_addr = ::inet_addr(iface_addr.c_str());
    if (::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface)) < 0) {
        std::perror("IP_MULTICAST_IF");
    }

    // Disable multicast loopback (we don't want to receive our own sends)
    uint8_t loop = 0;
    if (::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0) {
        std::perror("IP_MULTICAST_LOOP");
    }

    return true;
}

bool MulticastSocket::send(const uint8_t* data, size_t len, uint16_t dest_port) const {
    if (fd_ < 0) return false;

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(dest_port);
    dest.sin_addr.s_addr = ::inet_addr(mcast_group_.c_str());

    ssize_t sent = ::sendto(fd_, data, len, 0,
                            reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
    return sent == static_cast<ssize_t>(len);
}

int MulticastSocket::recv(uint8_t* buf, size_t max_len) const {
    if (fd_ < 0) return -1;

    struct sockaddr_in sender{};
    socklen_t slen = sizeof(sender);
    ssize_t n = ::recvfrom(fd_, buf, max_len, 0,
                           reinterpret_cast<struct sockaddr*>(&sender), &slen);
    return static_cast<int>(n);
}

void MulticastSocket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace dis
