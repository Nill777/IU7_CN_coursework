#include "http.hpp"
#include "logger.hpp"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sstream>
#include <algorithm>

static bool contains_dotdot(const std::string& p) {
    return p.find("..") != std::string::npos;
}

bool parse_request(Connection& c) {
    auto pos = c.in_buf.find("\r\n\r\n");
    if (pos == std::string::npos) return false;

    std::istringstream iss(c.in_buf.substr(0, pos));
    std::string request_line;
    if (!std::getline(iss, request_line)) return false;
    if (!request_line.empty() && request_line.back() == '\r')
        request_line.pop_back();

    std::istringstream rl(request_line);
    std::string http_version;
    rl >> c.method >> c.path >> http_version;
    if (c.method.empty() || c.path.empty()) return false;

    std::string lower_method = c.method;
    std::transform(lower_method.begin(), lower_method.end(),
                   lower_method.begin(), ::toupper);

    c.head_only = (lower_method == "HEAD");

    return true;
}

std::string build_status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        default:  return "Unknown";
    }
}

std::string get_mime_type(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot + 1);
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css")  return "text/css; charset=utf-8";
    if (ext == "js")   return "application/javascript";
    if (ext == "png")  return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif")  return "image/gif";
    if (ext == "txt")  return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static std::string build_headers(int status,
                                 size_t content_length,
                                 const std::string& content_type,
                                 bool keep_alive) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << build_status_text(status) << "\r\n";
    oss << "Content-Length: " << content_length << "\r\n";
    oss << "Content-Type: " << content_type << "\r\n";
    oss << "Connection: " << (keep_alive ? "keep-alive" : "close") << "\r\n";
    if (status == 405) {
        oss << "Allow: GET, HEAD\r\n";
    }
    oss << "\r\n";
    return oss.str();
}

static std::string build_simple_html(int status, const std::string& msg) {
    return "<html><body><h1>" + std::to_string(status) + " " +
           build_status_text(status) + "</h1><p>" + msg + "</p></body></html>";
}

bool prepare_response(Connection& c, const ServerConfig& cfg) {
    std::string method = c.method;
    std::transform(method.begin(), method.end(),
                   method.begin(), ::toupper);

    c.keep_alive = cfg.keep_alive_default;

    if (method != "GET" && method != "HEAD") {
        c.status_code = 405;
        std::string body = build_simple_html(405, "Method not allowed");
        c.out_buf = build_headers(405, body.size(), "text/html; charset=utf-8",
                                  c.keep_alive);
        if (!c.head_only) c.out_buf += body;
        c.state = ConnState::SENDING_HEADERS;
        return true;
    }

    std::string url_path = c.path;
    if (url_path.empty() || url_path[0] != '/') {
        url_path = "/";
    }
    if (url_path == "/") {
        url_path = "/index.html";
    }

    if (contains_dotdot(url_path)) {
        c.status_code = 403;
        std::string body = build_simple_html(403, "Forbidden");
        c.out_buf = build_headers(403, body.size(), "text/html; charset=utf-8",
                                  c.keep_alive);
        if (!c.head_only) c.out_buf += body;
        c.state = ConnState::SENDING_HEADERS;
        return true;
    }

    std::string fs_path = cfg.doc_root + url_path;

    struct stat st{};
    if (stat(fs_path.c_str(), &st) != 0) {
        c.status_code = 404;
        std::string body = build_simple_html(404, "Not found");
        c.out_buf = build_headers(404, body.size(), "text/html; charset=utf-8",
                                  c.keep_alive);
        if (!c.head_only) c.out_buf += body;
        c.state = ConnState::SENDING_HEADERS;
        return true;
    }

    if (!S_ISREG(st.st_mode)) {
        c.status_code = 403;
        std::string body = build_simple_html(403, "Forbidden");
        c.out_buf = build_headers(403, body.size(), "text/html; charset=utf-8",
                                  c.keep_alive);
        if (!c.head_only) c.out_buf += body;
        c.state = ConnState::SENDING_HEADERS;
        return true;
    }

    if ((size_t)st.st_size > cfg.max_file_size) {
        c.status_code = 403;
        std::string body = build_simple_html(403, "File too large");
        c.out_buf = build_headers(403, body.size(), "text/html; charset=utf-8",
                                  c.keep_alive);
        if (!c.head_only) c.out_buf += body;
        c.state = ConnState::SENDING_HEADERS;
        return true;
    }

    c.file_size = st.st_size;
    c.file_offset = 0;

    std::string mime = get_mime_type(fs_path);
    c.status_code = 200;

    size_t content_len = c.head_only ? 0 : static_cast<size_t>(c.file_size);
    c.out_buf = build_headers(200, content_len, mime, c.keep_alive);
    c.out_sent = 0;

    if (!c.head_only) {
        c.file_fd = ::open(fs_path.c_str(), O_RDONLY);
        if (c.file_fd < 0) {
            c.status_code = 404;
            std::string body = build_simple_html(404, "Not found");
            c.out_buf = build_headers(404, body.size(),
                                      "text/html; charset=utf-8",
                                      c.keep_alive);
            if (!c.head_only) c.out_buf += body;
            c.file_size = 0;
            c.file_offset = 0;
        }
    }

    c.state = ConnState::SENDING_HEADERS;
    return true;
}
