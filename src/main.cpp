#include "server.hpp"
#include "config.hpp"

#include <iostream>
#include <cstring>
#include <cstdlib>

int main(int argc, char* argv[]) {
    ServerConfig cfg;

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--port") && i + 1 < argc) {
            cfg.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (!std::strcmp(argv[i], "--root") && i + 1 < argc) {
            cfg.doc_root = argv[++i];
        } else if (!std::strcmp(argv[i], "--log") && i + 1 < argc) {
            cfg.log_path = argv[++i];
        } else if (!std::strcmp(argv[i], "--workers") && i + 1 < argc) {
            cfg.workers = std::atoi(argv[++i]);
        } else {
            std::cerr << "Usage: " << argv[0]
                      << " [--port N] [--root DIR] [--log FILE] [--workers N]\n";
            return 1;
        }
    }

    return run_server(cfg);
}
