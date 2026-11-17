#define _POSIX_C_SOURCE 200112L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *ip = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 5555;
    const char *message = argc > 3 ? argv[3] : "Привет от клиента";
    if (port < 1 || port > 65535) {
        fprintf(stderr, "Некорректный порт\n");
        return 1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "Некорректный IPv4-адрес\n"); close(fd); return 1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect"); close(fd); return 1;
    }
    size_t len = strlen(message);
    if (send(fd, message, len, 0) < 0) { perror("send"); close(fd); return 1; }
    shutdown(fd, SHUT_WR);
    char buffer[2048];
    ssize_t n;
    while ((n = recv(fd, buffer, sizeof buffer - 1, 0)) > 0) {
        buffer[n] = '\0';
        printf("Ответ сервера: %s\n", buffer);
    }
    if (n < 0) perror("recv");
    close(fd);
    return n < 0 ? 1 : 0;
}
