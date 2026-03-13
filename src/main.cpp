#include "gateway.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Signal handling
// ---------------------------------------------------------------------------
static Gateway* g_gateway = nullptr;

static void on_signal(int) {
    if (g_gateway) g_gateway->stop();
}

// ---------------------------------------------------------------------------
// Usage / argument parsing
// ---------------------------------------------------------------------------
static void usage(const char* prog) {
    std::cerr <<
        "Usage: " << prog << " [options]\n\n"
        "Options:\n"
        "  --listen-addr  ADDR      Bind address for ingress (default: 0.0.0.0)\n"
        "  --listen-port  PORT      UDP port to listen on (default: 3000)\n"
        "  --mcast-group  GROUP     Join multicast group (e.g. 239.1.2.3)\n"
        "  --mcast-iface  IFACE     Interface for multicast join (default: 0.0.0.0)\n"
        "  --forward-addr ADDR      Egress destination address (required)\n"
        "  --forward-port PORT      Egress destination port (default: 3000)\n"
        "  --fwd-mcast-iface IFACE  Interface for outbound multicast (default: 0.0.0.0)\n"
        "  --mcast-ttl    TTL       Multicast TTL (default: 1)\n"
        "  --rules        FILE      Rules file (default: rules.conf)\n"
        "  --drop-on-error          Drop PDU instead of passing through on rule error\n"
        "  --help\n\n"
        "Example (unicast):\n"
        "  dis-gateway --listen-port 3000 --forward-addr 10.0.0.2 --forward-port 3000 --rules rules.conf\n\n"
        "Example (multicast):\n"
        "  dis-gateway --mcast-group 239.1.2.3 --listen-port 3000 \\\n"
        "              --forward-addr 239.1.2.4 --forward-port 3000 --rules rules.conf\n";
}

static std::string next_arg(int& i, int argc, char** argv, const std::string& opt) {
    if (++i >= argc) {
        std::cerr << "Missing value for " << opt << "\n";
        std::exit(1);
    }
    return argv[i];
}

int main(int argc, char* argv[]) {
    GatewayConfig cfg;
    bool forward_set = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if      (arg == "--listen-addr")     cfg.listen_addr   = next_arg(i, argc, argv, arg);
        else if (arg == "--listen-port")     cfg.listen_port   = static_cast<uint16_t>(std::stoul(next_arg(i, argc, argv, arg)));
        else if (arg == "--mcast-group")     cfg.mcast_group   = next_arg(i, argc, argv, arg);
        else if (arg == "--mcast-iface")     cfg.mcast_iface   = next_arg(i, argc, argv, arg);
        else if (arg == "--forward-addr")  { cfg.forward_addr  = next_arg(i, argc, argv, arg); forward_set = true; }
        else if (arg == "--forward-port")    cfg.forward_port  = static_cast<uint16_t>(std::stoul(next_arg(i, argc, argv, arg)));
        else if (arg == "--fwd-mcast-iface") cfg.fwd_mcast_iface = next_arg(i, argc, argv, arg);
        else if (arg == "--mcast-ttl")       cfg.mcast_ttl     = static_cast<uint8_t>(std::stoul(next_arg(i, argc, argv, arg)));
        else if (arg == "--rules")           cfg.rules_file    = next_arg(i, argc, argv, arg);
        else if (arg == "--drop-on-error")   cfg.passthrough_on_error = false;
        else if (arg == "--help")          { usage(argv[0]); return 0; }
        else { std::cerr << "Unknown option: " << arg << "\n"; usage(argv[0]); return 1; }
    }

    if (!forward_set) {
        std::cerr << "Error: --forward-addr is required\n\n";
        usage(argv[0]);
        return 1;
    }

    // Install signal handlers
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    try {
        Gateway gw(cfg);
        g_gateway = &gw;
        gw.run();
        g_gateway = nullptr;
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
