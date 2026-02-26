#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <cstdint>

struct ServerConfig {
    std::string host      = "0.0.0.0";
    uint16_t    port      = 8080;
    std::string doc_root  = "./www";
    std::string log_path  = "./server.log";
    int         workers   = 4;
    size_t      max_file_size = 128 * 1024 * 1024;
    bool        keep_alive_default = false;
};

#endif
