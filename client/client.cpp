#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iomanip>
#include <cstring>
#include <ifaddrs.h>

const std::string MULTICAST_ADDR = "239.0.0.1";
const int MULTICAST_PORT = 5000;

enum ExitCode {
    EXIT_SUCCESS_CODE = 0,
    EXIT_SOCKET_FAIL = 1,
    EXIT_SETSOCKOPT_FAIL = 2,
    EXIT_BIND_FAIL = 3,
    EXIT_MEMBERSHIP_FAIL = 4
};

std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}


std::string get_local_ip() {
    struct ifaddrs* ifaddr, * ifa;
    char buf[INET_ADDRSTRLEN];

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return "unknown";
    }

    std::string result = "unknown";
    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;

        if (std::string(ifa->ifa_name) == "lo")
            continue;

        auto* addr_in = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        inet_ntop(AF_INET, &addr_in->sin_addr, buf, sizeof(buf));
        result = buf;
        break;
    }

    freeifaddrs(ifaddr);
    return result;
}


int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return EXIT_SOCKET_FAIL;
    }

    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(sock);
        return EXIT_SETSOCKOPT_FAIL;
    }

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(MULTICAST_PORT);
    if (bind(sock, reinterpret_cast<struct sockaddr*>(&local_addr), sizeof(local_addr)) < 0) {
        perror("bind");
        close(sock);
        return EXIT_BIND_FAIL;
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_ADDR.c_str());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt IP_ADD_MEMBERSHIP");
        close(sock);
        return EXIT_MEMBERSHIP_FAIL;
    }

    while (true) {
        char buffer[1024];
        struct sockaddr_in sender_addr;
        socklen_t addr_len = sizeof(sender_addr);
        int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&sender_addr, &addr_len);
        if (len < 0) {
            perror("recvfrom");
            continue;
        }

        buffer[len] = '\0';
        std::string msg(buffer);
        auto tokens = split(msg, ',');
        if (tokens.size() == 2 && tokens[0] == "DISCOVERY") {
            std::string request_id = tokens[1];
            auto now = std::chrono::system_clock::now();
            auto time_t_now = std::chrono::system_clock::to_time_t(now);
            std::tm tm = *std::localtime(&time_t_now);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
            std::string time_str = oss.str();
            std::string response = "RESPONSE," + request_id + "," + time_str;
            std::cout << "Send response from IP = " << get_local_ip() << ", time = " << time_str << "" << ", request_id = " << request_id << std::endl;

            if (sendto(sock, response.c_str(), response.size(), 0, reinterpret_cast<struct sockaddr*>(&sender_addr), sizeof(sender_addr)) < 0) {
                perror("sendto");
            }

        }
    }

    close(sock);
    return 0;
}