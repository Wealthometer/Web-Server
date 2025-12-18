#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <thread>
#include <chrono>
#include <functional>
#include <cstring>
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <arpa/inet.h>
#endif

class HTTPServer {
private:
    int server_fd;
    int port;
    bool running;

    // Route handlers
    std::map<std::string, std::function<std::string(const std::map<std::string, std::string>&)>> routes;

    // Helper function to initialize network
    void initNetwork() {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
#endif
    }

    // Helper function to cleanup network
    void cleanupNetwork() {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    // Close socket
    void closeSocket(int sock) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }

    // Parse HTTP request
    struct HTTPRequest {
        std::string method;
        std::string path;
        std::string version;
        std::map<std::string, std::string> headers;
        std::string body;
    };

    HTTPRequest parseRequest(const std::string& request) {
        HTTPRequest req;
        std::istringstream stream(request);
        std::string line;

        // Parse request line
        if (std::getline(stream, line)) {
            std::istringstream lineStream(line);
            lineStream >> req.method >> req.path >> req.version;
        }

        // Parse headers
        while (std::getline(stream, line) && line != "\r") {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string value = line.substr(colon + 2); // +2 to skip ": "
                // Remove trailing \r
                if (!value.empty() && value.back() == '\r') {
                    value.pop_back();
                }
                req.headers[key] = value;
            }
        }

        // Parse body (if any)
        std::ostringstream bodyStream;
        while (std::getline(stream, line)) {
            bodyStream << line;
            if (!stream.eof()) {
                bodyStream << '\n';
            }
        }
        req.body = bodyStream.str();

        return req;
    }

    // Create HTTP response
    std::string createResponse(int status_code, const std::string& status_msg,
                              const std::string& content, const std::string& content_type = "text/html") {
        std::ostringstream response;
        response << "HTTP/1.1 " << status_code << " " << status_msg << "\r\n";
        response << "Content-Type: " << content_type << "\r\n";
        response << "Content-Length: " << content.length() << "\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << content;

        return response.str();
    }

    // Handle client connection
    void handleClient(int client_socket) {
        const int BUFFER_SIZE = 4096;
        char buffer[BUFFER_SIZE];

        // Receive request
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::string request_str(buffer);

            // Parse request
            HTTPRequest request = parseRequest(request_str);

            // Default response
            std::string response_content;
            int status_code = 404;
            std::string status_msg = "Not Found";
            std::string content_type = "text/html";

            // Check if route exists
            if (routes.find(request.path) != routes.end()) {
                response_content = routes[request.path](request.headers);
                status_code = 200;
                status_msg = "OK";
            } else {
                // Default 404 response
                response_content = "<!DOCTYPE html><html><head><title>404 Not Found</title></head>"
                                   "<body><h1>404 Not Found</h1><p>The requested URL " +
                                   request.path + " was not found on this server.</p></body></html>";
            }

            // Create and send response
            std::string response = createResponse(status_code, status_msg, response_content, content_type);
            send(client_socket, response.c_str(), response.length(), 0);
        }

        closeSocket(client_socket);
    }

public:
    HTTPServer(int port = 8080) : port(port), running(false) {
        initNetwork();
    }

    ~HTTPServer() {
        stop();
        cleanupNetwork();
    }

    // Add route handler
    void addRoute(const std::string& path,
                  std::function<std::string(const std::map<std::string, std::string>&)> handler) {
        routes[path] = handler;
    }

    // Start server
    void start() {
        // Create socket
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            std::cerr << "Socket creation failed" << std::endl;
            return;
        }

        // Set socket options to reuse address
        int opt = 1;
#ifdef _WIN32
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        // Bind socket
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            std::cerr << "Bind failed" << std::endl;
            closeSocket(server_fd);
            return;
        }

        // Listen for connections
        if (listen(server_fd, 10) < 0) {
            std::cerr << "Listen failed" << std::endl;
            closeSocket(server_fd);
            return;
        }

        std::cout << "Server started on port " << port << std::endl;
        std::cout << "Access at: http://localhost:" << port << std::endl;

        running = true;

        // Main server loop
        while (running) {
            struct sockaddr_in client_addr;
#ifdef _WIN32
            int addrlen = sizeof(client_addr);
#else
            socklen_t addrlen = sizeof(client_addr);
#endif

            int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);

            if (client_socket < 0) {
                std::cerr << "Accept failed" << std::endl;
                continue;
            }

            // Handle client in a new thread
            std::thread client_thread(&HTTPServer::handleClient, this, client_socket);
            client_thread.detach();
        }

        closeSocket(server_fd);
    }

    // Stop server
    void stop() {
        running = false;
    }
};

// Example usage
int main() {
    HTTPServer server(8080);

    // Add routes
    server.addRoute("/", [](const std::map<std::string, std::string>& headers) -> std::string {
        return "<!DOCTYPE html>"
               "<html>"
               "<head><title>My C++ Server</title>"
               "<style>"
               "body { font-family: Arial, sans-serif; margin: 40px; background: #f0f0f0; }"
               ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }"
               "h1 { color: #333; }"
               ".nav a { margin-right: 15px; text-decoration: none; color: #0066cc; }"
               "</style></head>"
               "<body>"
               "<div class='container'>"
               "<h1>Welcome to C++ HTTP Server!</h1>"
               "<p>This is a simple web server built with C++.</p>"
               "<div class='nav'>"
               "<a href='/'>Home</a>"
               "<a href='/about'>About</a>"
               "<a href='/api/data'>API Data</a>"
               "</div>"
               "</div>"
               "</body></html>";
    });

    server.addRoute("/about", [](const std::map<std::string, std::string>& headers) -> std::string {
        return "<!DOCTYPE html>"
               "<html>"
               "<head><title>About</title></head>"
               "<body>"
               "<h1>About This Server</h1>"
               "<p>This is a lightweight HTTP server written in C++.</p>"
               "<p>Features:</p>"
               "<ul>"
               "<li>Multi-threaded request handling</li>"
               "<li>Basic routing</li>"
               "<li>Cross-platform (Windows/Linux)</li>"
               "</ul>"
               "<a href='/'>Back to Home</a>"
               "</body></html>";
    });

    server.addRoute("/api/data", [](const std::map<std::string, std::string>& headers) -> std::string {
        // Return JSON response
        return "{"
               "\"status\": \"success\","
               "\"data\": {"
               "\"message\": \"Hello from C++ Server!\","
               "\"timestamp\": \"" + std::to_string(std::time(nullptr)) + "\","
               "\"version\": \"1.0\""
               "}"
               "}";
    });

    // Start server
    server.start();

    return 0;
}
