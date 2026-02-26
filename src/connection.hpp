#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include <string>
#include <cstdint>

enum class ConnState {
    READING_REQUEST,
    PREPARING_RESPONSE,
    SENDING_HEADERS,
    SENDING_BODY,
    CLOSING
};

struct Connection {
    int fd = -1;
    ConnState state = ConnState::READING_REQUEST;

    std::string in_buf;
    std::string out_buf;
    size_t      out_sent = 0;

    int file_fd = -1;
    off_t file_offset = 0;
    off_t file_size   = 0;

    bool keep_alive = false;
    bool head_only  = false;

    int status_code = 0;
    std::string method;
    std::string path;
};

struct ServerConfig;

void handle_read(Connection& conn,
                 const ServerConfig& cfg,
                 bool& want_close);

void handle_write(Connection& conn,
                  bool& want_close);

#endif
