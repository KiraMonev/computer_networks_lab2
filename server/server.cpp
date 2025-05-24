#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <chrono>
#include <thread>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iomanip>
#include <cstring>
#include <mutex>
#include "httplib.h"

const std::string MULTICAST_ADDR = "239.0.0.1";
const int MULTICAST_PORT = 5000;
const int SERVER_PORT = 6000;
const int DISCOVERY_INTERVAL = 20;
const int RESPONSE_TIMEOUT = 5;   

static std::set<std::string> previous_clients;     
static std::set<std::string> all_clients;         
static std::set<std::string> disconnected; 
static std::set<std::string> current_clients;
static std::mutex data_mutex;

std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string set_to_json_array(const std::set<std::string>& s) {
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    for (auto& ip : s) {
        if (!first) oss << ",";
        oss << "\"" << ip << "\"";
        first = false;
    }
    oss << "]";
    return oss.str();
}

void discovery_loop() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creating was failure" << std::endl;
        return;
    }

    std::cout << "Start work" << std::endl;

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERVER_PORT);
    bind(sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));

    sockaddr_in multicast_addr{};
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_ADDR.c_str());
    multicast_addr.sin_port = htons(MULTICAST_PORT);

    int request_id = 0;
    while (true) {
        std::cout << "Send DISCOVERY, request_id = " << request_id << std::endl;
        std::string message = "DISCOVERY," + std::to_string(request_id);
        sendto(sock, message.c_str(), message.size(), 0,
            reinterpret_cast<sockaddr*>(&multicast_addr), sizeof(multicast_addr));

        std::map<std::string, std::string> responses;
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(RESPONSE_TIMEOUT)) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            timeval timeout{ 0, 100000 }; // 0.1 sec
            int ret = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
            if (ret > 0 && FD_ISSET(sock, &readfds)) {
                char buf[1024];
                sockaddr_in client_addr{};
                socklen_t len = sizeof(client_addr);
                int n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                    reinterpret_cast<sockaddr*>(&client_addr), &len);
                if (n > 0) {
                    buf[n] = '\0';
                    std::string msg(buf);
                    auto tokens = split(msg, ',');
                    if (tokens.size() == 3 && tokens[0] == "RESPONSE" && tokens[1] == std::to_string(request_id)) {
                        char ipstr[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &client_addr.sin_addr, ipstr, sizeof(ipstr));
                        responses[ipstr] = tokens[2];
                        responses[ipstr] = tokens[2];
                        std::cout << "Get  response from IP = " << ipstr << ", time = " << tokens[2] << ", request_id = " << tokens[1] << std::endl;
                    }
                }
            }
        }

        std::set<std::string> new_current;
        for (auto& kv : responses) new_current.insert(kv.first);

        std::set<std::string> newly_connected;

        {
            std::lock_guard<std::mutex> lock(data_mutex);
            for (auto& ip : new_current) {
                if (previous_clients.find(ip) == previous_clients.end()) {
                    newly_connected.insert(ip);
                    disconnected.erase(ip);
                    std::cout << "Connected user: " << ip << std::endl;
                }
            }
            for (auto& ip : previous_clients) {
                if (new_current.find(ip) == new_current.end()) {
                    disconnected.insert(ip);
                    std::cout << "Disconnected user: " << ip << std::endl;
                }
            }
            for (auto& ip : new_current) all_clients.insert(ip);
            current_clients = new_current;
            previous_clients = new_current;
        }
        request_id++;
        std::this_thread::sleep_for(std::chrono::seconds(DISCOVERY_INTERVAL));
    }

    close(sock);
}

int main() {
    std::thread worker(discovery_loop);

    httplib::Server svr;

    svr.Get("/clients/active", [](const httplib::Request& req, httplib::Response& res) {
        std::string body;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            body = set_to_json_array(current_clients);
        }
        res.set_content(body, "application/json");
        });

    svr.Get("/clients/disconnected", [](const httplib::Request& req, httplib::Response& res) {
        std::string body;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            body = set_to_json_array(disconnected);
        }
        res.set_content(body, "application/json");
        });

    svr.Get("/clients/all", [](const httplib::Request& req, httplib::Response& res) {
        std::string body;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            body = set_to_json_array(all_clients);
        }
        res.set_content(body, "application/json");
        });

    std::cout << "HTTP server listening on 0.0.0.0:8080" << std::endl;
    svr.listen("0.0.0.0", 8080);

    worker.join();
    return 0;
}