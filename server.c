#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BACKLOG 32
#define BUFFER_SIZE 1024

typedef struct client {
    int fd;
    struct sockaddr_in addr;
    struct client *next;
} client_t;

static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t clients_empty = PTHREAD_COND_INITIALIZER;
static client_t *clients = NULL;
static unsigned int active_connections = 0;
static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int signo) {
    (void)signo;
    stop_requested = 1;
}

static void remove_client_locked(client_t *client) {
    client_t **cursor = &clients;
    while (*cursor != NULL) {
        if (*cursor == client) {
            *cursor = client->next;
            break;
        }
        cursor = &(*cursor)->next;
    }
    if (active_connections > 0)
        --active_connections;
    if (active_connections == 0)
        pthread_cond_broadcast(&clients_empty);
}

static void *client_worker(void *arg) {
    client_t *client = arg;
    char ip[INET_ADDRSTRLEN] = "?";
    (void)inet_ntop(AF_INET, &client->addr.sin_addr, ip, sizeof ip);
    printf("[+] Клиент %s:%u подключён\n", ip, ntohs(client->addr.sin_port));
    fflush(stdout);

    char buffer[BUFFER_SIZE];
    for (;;) {
        ssize_t received = recv(client->fd, buffer, sizeof buffer, 0);
        if (received == 0)
            break;
        if (received < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        ssize_t sent_total = 0;
        while (sent_total < received) {
            ssize_t sent = send(client->fd, buffer + sent_total,
                                (size_t)(received - sent_total), 0);
            if (sent < 0) {
                if (errno == EINTR)
                    continue;
                goto finished;
            }
            sent_total += sent;
        }
    }

finished:
    pthread_mutex_lock(&clients_mutex);
    close(client->fd);
    remove_client_locked(client);
    unsigned int remaining = active_connections;
    pthread_mutex_unlock(&clients_mutex);

    printf("[-] Клиент отключён; активных соединений: %u\n", remaining);
    fflush(stdout);
    free(client);
    return NULL;
}

static int parse_port(const char *s) {
    char *end = NULL;
    long value = strtol(s, &end, 10);
    if (s[0] == '\0' || *end != '\0' || value < 1 || value > 65535)
        return -1;
    return (int)value;
}

int main(int argc, char **argv) {
    int port = 5555;
    if (argc > 2 || (argc == 2 && (port = parse_port(argv[1])) < 0)) {
        fprintf(stderr, "Использование: %s [порт 1..65535]\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }
    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse) < 0) {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof server_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof server_addr) < 0) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Сервер слушает порт %d. Для остановки нажми Ctrl+C.\n", port);
    fflush(stdout);

    while (!stop_requested) {
        client_t *client = calloc(1, sizeof *client);
        if (client == NULL) {
            perror("calloc");
            break;
        }
        socklen_t addr_len = sizeof client->addr;
        int fd = accept(server_fd, (struct sockaddr *)&client->addr, &addr_len);
        if (fd < 0) {
            free(client);
            if (errno == EINTR && stop_requested)
                break;
            if (errno == EINTR)
                continue;
            if (stop_requested)
                break;
            perror("accept");
            continue;
        }
        client->fd = fd;

        pthread_mutex_lock(&clients_mutex);
        client->next = clients;
        clients = client;
        ++active_connections;
        pthread_mutex_unlock(&clients_mutex);

        pthread_t tid;
        int rc = pthread_create(&tid, NULL, client_worker, client);
        if (rc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            pthread_mutex_lock(&clients_mutex);
            close(client->fd);
            remove_client_locked(client);
            pthread_mutex_unlock(&clients_mutex);
            free(client);
            continue;
        }
        rc = pthread_detach(tid);
        if (rc != 0)
            fprintf(stderr, "pthread_detach: %s\n", strerror(rc));
    }

    close(server_fd);
    printf("Остановка: закрываем клиентские соединения...\n");
    pthread_mutex_lock(&clients_mutex);
    for (client_t *it = clients; it != NULL; it = it->next)
        (void)shutdown(it->fd, SHUT_RDWR);
    while (active_connections != 0)
        pthread_cond_wait(&clients_empty, &clients_mutex);
    pthread_mutex_unlock(&clients_mutex);

    pthread_cond_destroy(&clients_empty);
    pthread_mutex_destroy(&clients_mutex);
    puts("Все клиентские потоки завершены. Сервер остановлен.");
    return EXIT_SUCCESS;
}
