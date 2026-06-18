#pragma once

#include <string>

#include <arpa/inet.h>
#include <sys/socket.h>

namespace chaoxi::net::sockets {

///
/// Creates a non-blocking socket file descriptor,
/// abort if any error.
[[nodiscard]] int createNonblockingOrDie(sa_family_t family);

[[nodiscard]] ssize_t read(int sockfd, void* buf, size_t count);
[[nodiscard]] ssize_t write(int sockfd, const void* buf, size_t count);
int connect(int sockfd, const struct sockaddr* addr);
void listenOrDie(int sockfd);
void bindOrDie(int sockfd, const struct sockaddr* addr);
void fromIpPort(const char* ip, uint16_t port, struct sockaddr_in* addr);
void fromIpPort(const char* ip, uint16_t port, struct sockaddr_in6* addr);

[[nodiscard]] std::string toIpPort(const struct sockaddr* addr);
void toIpPort(char* buf, size_t size, const struct sockaddr* addr);
[[nodiscard]] std::string toIp(const struct sockaddr* addr);
void toIp(char* buf, size_t size, const struct sockaddr* addr);

[[nodiscard]] int getSocketError(int sockfd);

const struct sockaddr* sockaddr_cast(const struct sockaddr_in* addr);
const struct sockaddr* sockaddr_cast(const struct sockaddr_in6* addr);
struct sockaddr* sockaddr_cast(struct sockaddr_in6* addr);
const struct sockaddr_in* sockaddr_in_cast(const struct sockaddr* addr);
const struct sockaddr_in6* sockaddr_in6_cast(const struct sockaddr* addr);

[[nodiscard]] int accept(int sockfd, struct sockaddr_in6* addr);
void close(int sockfd);

struct sockaddr_in6 getLocalAddr(int sockfd);
struct sockaddr_in6 getPeerAddr(int sockfd);
[[nodiscard]] bool isSelfConnect(int sockfd);
}  // namespace chaoxi::net::sockets
