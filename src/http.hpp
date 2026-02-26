#ifndef HTTP_HPP
#define HTTP_HPP

#include <string>
#include "connection.hpp"
#include "config.hpp"

bool parse_request(Connection& c);
std::string build_status_text(int code);
std::string get_mime_type(const std::string& path);

bool prepare_response(Connection& c, const ServerConfig& cfg);

#endif
