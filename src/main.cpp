#include "gateway.h"
#include "pdu_codec.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <csignal>
#include <cstdio>
#include <cstring>

static dis::Gateway* g_gateway = nullptr;

static void signal_handler(int /*sig*/) {
    if (g_gateway) g_gateway->stop();
}

static void print_usage(const char* prog) {
    std::printf("Usage: %s <config.json> [--dry-run]\n", prog);
    std::printf("\nDIS Translation Gateway\n");
    std::printf("  Ingests UDP DIS PDUs from two multicast groups,\n");
    std::printf("  applies configurable rewrite rules, and forwards\n");
    std::printf("  corrected PDUs to the other side.\n\n");
    std::printf("Options:\n");
    std::printf("  --dry-run   Load config and validate, then exit\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    bool dry_run = false;
    const char* config_path = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dry-run") == 0) {
            dry_run = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            config_path = argv[i];
        }
    }

    if (!config_path) {
        std::fprintf(stderr, "ERROR: No config file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    // Load configuration
    nlohmann::json j;
    {
        std::ifstream ifs(config_path);
        if (!ifs.is_open()) {
            std::fprintf(stderr, "ERROR: Cannot open config file: %s\n", config_path);
            return 1;
        }
        try {
            ifs >> j;
        } catch (const nlohmann::json::parse_error& e) {
            std::fprintf(stderr, "ERROR: JSON parse error: %s\n", e.what());
            return 1;
        }
    }

    dis::GatewayConfig config;
    try {
        config = dis::parse_config(j);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR: Config validation failed: %s\n", e.what());
        return 1;
    }

    std::printf("DIS Translation Gateway\n");
    auto print_side = [](const char* label, const dis::SideConfig& sc) {
        if (sc.single_port()) {
            std::printf("  %s: %s:%u (single port, iface %s)\n",
                        label, sc.address.c_str(), sc.send_port, sc.interface.c_str());
        } else {
            std::printf("  %s: %s send:%u recv:%u (dual port, iface %s)\n",
                        label, sc.address.c_str(), sc.send_port, sc.receive_port,
                        sc.interface.c_str());
        }
    };
    print_side("Side A", config.side_a);
    print_side("Side B", config.side_b);

    std::printf("  Rules: %zu A->B, %zu B->A\n",
                config.rules_a_to_b.size(), config.rules_b_to_a.size());

    if (dry_run) {
        std::printf("Dry run: config is valid. Exiting.\n");
        return 0;
    }

    // Initialise PDU field registry
    dis::init_field_registry();

    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Run gateway
    dis::Gateway gw(std::move(config));
    g_gateway = &gw;
    gw.run();
    g_gateway = nullptr;

    return 0;
}
