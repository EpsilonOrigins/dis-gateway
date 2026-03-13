#include "udp_socket.hpp"

#include <cstring>
#include <stdexcept>
#include <system_error>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static int make_udp_socket() {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        throw std::system_error(errno, std::generic_category(), "socket()");
    return fd;
}

static void set_reuse_addr(int fd) {
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#ifdef SO_REUSEPORT
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif
}

static void set_recv_buf(int fd, std::size_t sz) {
    int s = static_cast<int>(sz);
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &s, sizeof(s));
}

static void set_send_buf(int fd, std::size_t sz) {
    int s = static_cast<int>(sz);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &s, sizeof(s));
}

static in_addr_t parse_addr(const std::string& s) {
    in_addr a{};
    if (::inet_aton(s.c_str(), &a) == 0)
        throw std::runtime_error("Invalid IP address: " + s);
    return a.s_addr;
}

// ---------------------------------------------------------------------------
// UdpReceiver
// ---------------------------------------------------------------------------
UdpReceiver::UdpReceiver(const Config& cfg) {
    fd_ = make_udp_socket();

    if (cfg.reuse_addr) set_reuse_addr(fd_);
    set_recv_buf(fd_, cfg.recv_buf_size);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(cfg.port);
    addr.sin_addr.s_addr = parse_addr(cfg.bind_addr);

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd_);
        throw std::system_error(errno, std::generic_category(),
                                "bind(" + cfg.bind_addr + ":" +
                                std::to_string(cfg.port) + ")");
    }

    if (!cfg.mcast_group.empty())
        join_multicast(cfg.mcast_group, cfg.mcast_iface);
}

UdpReceiver::~UdpReceiver() {
    if (mcast_) leave_multicast();
    if (fd_ >= 0) ::close(fd_);
}

void UdpReceiver::join_multicast(const std::string& group,
                                 const std::string& iface) {
    mreq_.imr_multiaddr.s_addr = parse_addr(group);
    mreq_.imr_interface.s_addr = parse_addr(iface);
    if (::setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                     &mreq_, sizeof(mreq_)) < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "IP_ADD_MEMBERSHIP(" + group + ")");
    }
    mcast_ = true;
}

void UdpReceiver::leave_multicast() {
    ::setsockopt(fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq_, sizeof(mreq_));
}

std::optional<std::vector<uint8_t>> UdpReceiver::recv(int timeout_ms) {
    if (timeout_ms > 0) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_, &fds);
        struct timeval tv{};
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        int rc = ::select(fd_ + 1, &fds, nullptr, nullptr, &tv);
        if (rc <= 0) return std::nullopt; // timeout or error
    }

    // Maximum DIS PDU size – 64 KiB is more than enough
    std::vector<uint8_t> buf(65535);
    ssize_t n = ::recv(fd_, buf.data(), buf.size(), 0);
    if (n < 0) return std::nullopt;
    buf.resize(static_cast<std::size_t>(n));
    return buf;
}

// ---------------------------------------------------------------------------
// UdpSender
// ---------------------------------------------------------------------------
UdpSender::UdpSender(const Config& cfg) {
    fd_ = make_udp_socket();
    set_send_buf(fd_, cfg.send_buf_size);

    // Bind source address (needed when specifying a multicast interface)
    sockaddr_in src{};
    src.sin_family      = AF_INET;
    src.sin_port        = 0;
    src.sin_addr.s_addr = parse_addr(cfg.bind_addr);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&src), sizeof(src)) < 0) {
        ::close(fd_);
        throw std::system_error(errno, std::generic_category(),
                                "sender bind(" + cfg.bind_addr + ")");
    }

    // Set multicast TTL / interface if applicable
    in_addr_t dest_raw = parse_addr(cfg.dest_addr);
    bool is_mcast = (ntohl(dest_raw) >> 28) == 0xE; // 224.x.x.x – 239.x.x.x
    if (is_mcast) {
        int ttl = cfg.mcast_ttl;
        ::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
        in_addr iface{};
        iface.s_addr = parse_addr(cfg.mcast_iface);
        ::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface));
    }

    std::memset(&dest_, 0, sizeof(dest_));
    dest_.sin_family      = AF_INET;
    dest_.sin_port        = htons(cfg.dest_port);
    dest_.sin_addr.s_addr = dest_raw;
}

UdpSender::~UdpSender() {
    if (fd_ >= 0) ::close(fd_);
}

ssize_t UdpSender::send(const uint8_t* data, std::size_t len) {
    return ::sendto(fd_, data, len, 0,
                    reinterpret_cast<const sockaddr*>(&dest_),
                    sizeof(dest_));
}
