#define _POSIX_C_SOURCE 200809L // for the compiler to get the correct prototypes for functions like getline

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"
#include "user_manager.h"
#include "history_manager.h"

#define ADMIN_LOGIN    "admin"
#define ADMIN_PASSWORD "adminpass"
#define MAX_CLIENTS    100
#define FILES_DIR      ".resa1/files"


//----------- Client Management -----------
typedef struct client_node {
    int         sockfd;
    char        nickname[NICK_LEN];
    char        login[LOGIN_LEN];
    int         authenticated; // 1 if authenticated, 0 otherwise
    struct      sockaddr_storage addr;
    socklen_t   addr_len;
    FILE        *upload_fp; // for file uploads
    char        upload_filename[INFOS_LEN + 32]; // to store the filename being uploaded
    time_t      connection_time;
    struct      client_node* next;
} client_node_t;

client_node_t* clients_head = NULL;
static volatile sig_atomic_t running = 1; // Flag to control server loop

// --------- Signal handler for graceful shutdown ---------
void handle_sigint(int sig) {
    (void)sig; // unused
    running = 0;
}

//------------------------- client functions --------------------

// Get client by socket fd
static client_node_t *get_client_by_fd(int fd) {
    for (client_node_t *c = clients_head; c; c = c->next)
        if (c->sockfd == fd) return c;
    return NULL;
}

// Get client by nickname
static client_node_t *get_client_by_nick(const char* nick) {
    for (client_node_t *c = clients_head; c; c = c->next)
        if (strcmp(c->nickname, nick) == 0) return c;
    return NULL;
}

// Check if a nickname is already taken by another client (excluding the one with exclude_fd)
static int is_nickname_taken(const char* nick, int exclude_fd) {
    for (client_node_t *c = clients_head; c; c = c->next)
        if (strcmp(c->nickname, nick) == 0 && c->sockfd != exclude_fd)
            return 1;
    return 0;
}

// Add a new client to the list
// calloc is used to zero-initialize the new client structure, ensuring all fields start with default values (e.g., authenticated = 0, nickname[0] = '\0')
static void add_client(int sockfd, struct sockaddr_storage addr, socklen_t addr_len) {
    client_node_t *new_client = calloc(1, sizeof(*new_client));
    if (!new_client) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    new_client->sockfd = sockfd;
    new_client->addr = addr;
    new_client->addr_len = addr_len;
    new_client->connection_time = time(NULL);
    new_client->nickname[0] = '\0';
    new_client->next = clients_head;
    clients_head = new_client;
}

// Remove a client from the list and free its resources
static void remove_client(int sockfd) {
    client_node_t **curr = &clients_head;
    while (*curr) {
        if ((*curr)->sockfd == sockfd) {
            client_node_t* tmp = *curr;
            *curr = (*curr)->next;
            free(tmp);
            return;
        }
        curr = &((*curr)->next);
    }
}

// Clean up all clients on server shutdown
static void cleanup_all_clients(void){
    client_node_t *c = clients_head;
    while (c) {
        close(c->sockfd);
        if (c->upload_fp) fclose(c->upload_fp);
        client_node_t *next = c->next;
        free(c);
        c = next;
    }
    clients_head = NULL;
}

//-------------------------  Helper Function --------------------

// Helper function to send a text reply to a client
static int reply_text(int fd, enum msg_type type, const char *text){
    struct message r = {0};
    r.pld_len = (int)strlen(text);
    r.type    = type;
    strncpy(r.nick_sender, "Server", NICK_LEN - 1);
    return send_msg(fd, &r, text);
}

// Ensure necessary directories exist at server startup
static void ensure_dirs(void){
    struct stat st = {0};
    if (stat(".resa1", &st) == -1)       mkdir(".resa1", 0755);
    if (stat(FILES_DIR, &st) == -1)      mkdir(FILES_DIR, 0755);
}

// List files available on the server and send the list to the client using opendir and readdir
static void list_files(int sockfd){
    char list[MSG_LEN] = {0};
    DIR *d = opendir(FILES_DIR);
    if (!d) {
        reply_text(sockfd, FILE_LIST, "No files available.\n");
        return;
    }

    // List files in the FILES_DIR directory
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;   /* skip . and .. */
        if (strlen(list) + strlen(ent->d_name) + 4 >= MSG_LEN) break;
        strcat(list, " - ");
        strcat(list, ent->d_name);
        strcat(list, "\n");
    }
    closedir(d);
    
    if (strlen(list) == 0)
        reply_text(sockfd, FILE_LIST, "No files available.\n");
    else
        reply_text(sockfd, FILE_LIST, list);
}

// -------------------------- P2P File Transfer Handling --------------------
// Handle incoming file transfer requests (FILE_REQUEST) and forward them to the intended recipient
static void handle_file_request(int sockfd, struct message *msg, const char *payload) {
    client_node_t *sender   = get_client_by_fd(sockfd);
    client_node_t *receiver = get_client_by_nick(msg->infos);
    if (!sender) return;

    if (!receiver) {
        reply_text(sockfd, FILE_REJECT, "[Server] User not found\n");
        return;
    }

    // Forward the file request to the intended recipient
    struct message fwd = *msg;
    strncpy(fwd.nick_sender, sender->nickname, NICK_LEN - 1);
    strncpy(fwd.infos, sender->nickname, INFOS_LEN - 1);
    send_msg(receiver->sockfd, &fwd, payload);

    printf("[Server] File request: %s -> %s (%s)\n",
           sender->nickname, msg->infos, payload);
}

// Handle file transfer responses (FILE_ACCEPT or FILE_REJECT) and forward them to the original sender of the request
void handle_file_response(int sockfd, struct message *msg, const char *payload) {
    client_node_t *responder = get_client_by_fd(sockfd);
    client_node_t *original  = get_client_by_nick(msg->infos);
    if (!responder) return;

    if (!original) {
        reply_text(sockfd, ECHO_SEND, "[Server] Original sender disconnected\n");
        return;
    }

    struct message fwd = *msg;
    strncpy(fwd.nick_sender, responder->nickname, NICK_LEN - 1);
    send_msg(original->sockfd, &fwd, payload);

    printf("[Server] File transfer %s: %s <-> %s\n",
           msg->type == FILE_ACCEPT ? "accepted" : "rejected",
           msg->infos, responder->nickname);
}

// -------------------------- Message Handling --------------------
int handle_client_message(int sockfd) {
    struct message msg;
    char buff[MSG_LEN];
    memset(&msg, 0, sizeof(msg));
    memset(buff, 0, MSG_LEN);

    int recv_result = recv_msg(sockfd, &msg, buff, MSG_LEN);
    if (recv_result == -2) {
        printf("[Server] Client disconnected\n");
        return 0;
    } else if (recv_result < 0) {
        perror("recv_msg failed");
        return 0;
    }

    client_node_t* client = get_client_by_fd(sockfd);
    if (!client) return 0;

    // Require authentication for all message types except LOGIN   
    if (msg.type != LOGIN && !client->authenticated) {
        reply_text(sockfd, ECHO_SEND,
                   "[Server] Please authenticate first with /login <user> <pass>\n");
        return 1;
    }

    // login
    if (msg.type == LOGIN) {
        char* login = msg.login;
        char* password = msg.password;

        // check empty login or password
        if (strlen(login) == 0 || strlen(password) == 0) {
            reply_text(sockfd, LOGIN_FAIL, "[Server] Invalid login or password\n");
            return 1;
        }

        // check if user exists
        if (!user_exists(login)) {
            // New user
            if (register_user(login, password)) {
                strncpy(client->login, login, LOGIN_LEN - 1);
                client->authenticated = 1;
                
                char m[MSG_LEN];
                snprintf(m, sizeof(m), "[Server] Registration OK! Welcome %s\n", login);
                reply_text(sockfd, LOGIN_OK, m);
            } else {
                reply_text(sockfd, LOGIN_FAIL, "[Server] Registration failed\n");
            }
        } else {
            // user exists
            if (authenticate_user(login, password,ADMIN_LOGIN, ADMIN_PASSWORD)) {
                strncpy(client->login, login, LOGIN_LEN - 1);
                client->authenticated = 1;
                
                char m[MSG_LEN];
                snprintf(m, sizeof(m), "[Server] Login OK! Welcome back %s\n", login);
                reply_text(sockfd, LOGIN_OK, m);

                // send history
                size_t history_size;
                char* history = read_user_history(login, &history_size);
                if (history) {
                    struct message reply = {.pld_len = (int)history_size, .type = HISTORY_SEND};
                    strncpy(reply.nick_sender, "Server", NICK_LEN);
                    if (send_msg(sockfd, &reply, history) < 0) {
                        perror("send_msg");
                        free(history);
                        return 0;
                    }
                    free(history);
                }

            } else {
                reply_text(sockfd, LOGIN_FAIL,
                           "[Server] Authentication failed: wrong password\n");
            }
        }
        // save to history
        char history_entry[MSG_LEN + 64];
        snprintf(history_entry, sizeof(history_entry), "[Server] : User %s logged in\n", client->login);
        add_to_history(client->login, history_entry);
        return 1;
    }

    // nickname new
    if (msg.type == NICKNAME_NEW) {
        char *nick = msg.infos;
        int valid = (strlen(nick) > 0 && strlen(nick) < NICK_LEN);
        for (size_t i = 0; valid && i < strlen(nick); i++)
            if (!isalnum(nick[i])) valid = 0;

        if (!valid) {
            reply_text(sockfd, ECHO_SEND,
                       "[Server] Invalid nickname (alphanumeric only)\n");
            return 1;
        }
        if (is_nickname_taken(nick, sockfd)) {
            char m[MSG_LEN];
            snprintf(m, sizeof(m), "[Server] Nickname '%s' is already taken\n", nick);
            reply_text(sockfd, ECHO_SEND, m);
            return 1;
        }

        strncpy(client->nickname, nick, NICK_LEN - 1);
        char m[MSG_LEN];
        snprintf(m, sizeof(m), "[Server] : Welcome on the chat %s\n", nick);
        reply_text(sockfd, ECHO_SEND, m);

        add_to_history(client->login, m);
        return 1;
    }

    /* Require nickname for all commands below */
    if (strlen(client->nickname) == 0) {
        reply_text(sockfd, ECHO_SEND,
                   "[Server] Set your nickname first with /nick <pseudo>\n");
        return 1;
    }

    // list connected users
    if (msg.type == NICKNAME_LIST) {
        char list[MSG_LEN] = "[Server] Connected users:\n";
        for (client_node_t *c = clients_head; c; c = c->next) {
            if (strlen(c->nickname) > 0) {
                strcat(list, " - ");
                strcat(list, c->nickname);
                strcat(list, "\n");
            }
        }
        reply_text(sockfd, ECHO_SEND, list);
        //log
        char he[MSG_LEN + 64];
        snprintf(he, sizeof(he), "[%s] used /who", client->nickname);
        add_to_history(client->login, he);
        return 1;
    }

    // get infos about a user
    if (msg.type == NICKNAME_INFOS) {
        client_node_t *t = get_client_by_nick(msg.infos);
        if (!t) {
            char m[MSG_LEN];
            snprintf(m, sizeof(m), "[Server] User %s not found\n", msg.infos);
            reply_text(sockfd, ECHO_SEND, m);
            return 1;
        }

        char ip[INET6_ADDRSTRLEN];
        int port;
        if (t->addr.ss_family == AF_INET) {
            struct sockaddr_in *s4 = (struct sockaddr_in *)&t->addr;
            inet_ntop(AF_INET, &s4->sin_addr, ip, sizeof(ip));
            port = ntohs(s4->sin_port);
        } else {
            struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)&t->addr;
            inet_ntop(AF_INET6, &s6->sin6_addr, ip, sizeof(ip));
            port = ntohs(s6->sin6_port);
        }

        char date[64];
        strftime(date, sizeof(date), "%Y/%m/%d@%H:%M", localtime(&t->connection_time));

        char m[MSG_LEN];
        snprintf(m, sizeof(m),
                 "[Server] %s connected since %s with IP %s port %d\n",
                 t->nickname, date, ip, port);
        reply_text(sockfd, ECHO_SEND, m);

        char he[MSG_LEN + 64];
        format_message_for_history(he, sizeof(he), "Server", client->nickname, m, 0);
        add_to_history(client->login, he);
        return 1;
    }

    // Admin ban user
    if (msg.type == ADMIN_BAN) {
        if (strcmp(client->login, ADMIN_LOGIN) != 0) {
            reply_text(sockfd, ECHO_SEND, "[Server] Only admin can use /ban\n");
            return 1;
        }

        client_node_t *target = get_client_by_nick(msg.infos);
        if (!target) {
            char m[MSG_LEN];
            snprintf(m, sizeof(m), "[Server] User %s not found\n", msg.infos);
            reply_text(sockfd, ECHO_SEND, m);
            // log
            char he[MSG_LEN + 64];
            snprintf(he, sizeof(he), "[admin] banned %s", msg.infos);
            add_to_history(client->login, he);
            return 1;
        }

        reply_text(target->sockfd, USER_BANNED,
                   "[Server] You have been banned by the administrator.\n");
        close(target->sockfd);
        remove_client(target->sockfd);

        char m[MSG_LEN];
        snprintf(m, sizeof(m), "[Server] User %s has been banned\n", msg.infos);
        reply_text(sockfd, ECHO_SEND, m);
        return 1;
    }

    // Unicast message to another user
    if (msg.type == UNICAST_SEND) {
        /* Parse "<nick> <message>" from infos */
        char target_nick[NICK_LEN] = {0};
        int i = 0;
        while (msg.infos[i] && msg.infos[i] != ' ' && i < NICK_LEN - 1) {
            target_nick[i] = msg.infos[i];
            i++;
        }

        if (msg.infos[i] == '\0') {
            reply_text(sockfd, ECHO_SEND, "[Server] Usage: /msg <user> <message>\n");
            return 1;
        }

        char *priv_msg = msg.infos + i + 1;
        client_node_t *t = get_client_by_nick(target_nick);
        if (!t) {
            char m[MSG_LEN];
            snprintf(m, sizeof(m), "[Server] User %s does not exist\n", target_nick);
            reply_text(sockfd, ECHO_SEND, m);
            return 1;
        }

        char full[MSG_LEN + 256];
        snprintf(full, sizeof(full), "[%s] : %s\n", client->nickname, priv_msg);
        reply_text(t->sockfd, ECHO_SEND, full);

        char he[MSG_LEN + 64];
        format_message_for_history(he, sizeof(he),
                                   client->nickname, t->nickname, priv_msg, 0);
        add_to_history(client->login, he);
        add_to_history(t->login, he);
        return 1;
    }

    // Handle broadcast messages
    if (msg.type == BROADCAST_SEND) {
        char fmt[MSG_LEN + 256];
        snprintf(fmt, sizeof(fmt), "[%s] : %s", client->nickname, buff);

        for (client_node_t *c = clients_head; c; c = c->next) {
            if (c->sockfd != sockfd && strlen(c->nickname) > 0)
                reply_text(c->sockfd, ECHO_SEND, fmt);
        }
        reply_text(sockfd, ECHO_SEND, "Message broadcasted\n");

        char he[MSG_LEN + 64];
        format_message_for_history(he, sizeof(he), client->nickname, "all", buff, 1);
        for (client_node_t *c = clients_head; c; c = c->next)
            if (strlen(c->login) > 0)
                add_to_history(c->login, he);
        return 1;
    }

    // P2P file transfer request and response handling
    if (msg.type == FILE_REQUEST) {
        handle_file_request(sockfd, &msg, buff);
        //log
        char he[MSG_LEN*2];
        snprintf(he, sizeof(he), "[%s] wants to send file '%s' to '%s'", client->nickname, buff, msg.infos);
        add_to_history(client->login, he);
        return 1;
    }
    if (msg.type == FILE_ACCEPT || msg.type == FILE_REJECT) {
        handle_file_response(sockfd, &msg, buff);
        //log
        char he[MSG_LEN + 64];
        snprintf(he, sizeof(he), "[%s] %s file transfer with '%s'", client->nickname,
                 msg.type == FILE_ACCEPT ? "accepted" : "rejected", msg.infos);
        add_to_history(client->login, he);
        return 1;
    }

    // File send
    if (msg.type == FILE_SEND) {
        ensure_dirs();

        if (msg.pld_len > 0 && client->upload_fp == NULL) {
            snprintf(client->upload_filename, sizeof(client->upload_filename),
                     "%s/%s", FILES_DIR, msg.infos);
            client->upload_fp = fopen(client->upload_filename, "wb");
            if (!client->upload_fp) {
                perror("fopen upload");
                return 1;
            }
        }

        if (client->upload_fp && msg.pld_len > 0)
            fwrite(buff, 1, msg.pld_len, client->upload_fp);

        if (msg.pld_len == 0 && client->upload_fp) {
            fclose(client->upload_fp);
            printf("[Server] Received file '%s' from %s\n",
                   client->upload_filename, client->nickname);
            client->upload_fp = NULL;
            client->upload_filename[0] = '\0';
            reply_text(sockfd, FILE_ACK, "[Server] File received successfully\n");

            char he[MSG_LEN + 64];
            char m[MSG_LEN];
            snprintf(m, sizeof(m), "Uploaded file '%s'", msg.infos);
            format_message_for_history(he, sizeof(he),
                                       client->nickname, "Server", m, 0);
            add_to_history(client->login, he);
        }
        return 1;
    }

    // List files on the server
    if (msg.type == FILE_LIST) {
        list_files(sockfd);
        // log
        char he[MSG_LEN + 64];
        snprintf(he, sizeof(he), "[%s] listed files", client->nickname);
        add_to_history(client->login, he);
        return 1;
    }

    // File download request
    if (msg.type == FILE_DOWNLOAD_REQUEST) {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", FILES_DIR, msg.infos);

        FILE *f = fopen(path, "rb");
        if (!f) {
            char m[MSG_LEN];
            snprintf(m, sizeof(m), "[Server] File '%s' not found\n", msg.infos);
            reply_text(sockfd, ECHO_SEND, m);
            return 1;
        }

        char chunk[MSG_LEN];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
            struct message r = {0};
            r.pld_len = (int)n;
            r.type    = FILE_DOWNLOAD;
            strncpy(r.nick_sender, "Server", NICK_LEN - 1);
            if (send_msg(sockfd, &r, chunk) < 0) break;
        }

        /* End-of-file marker */
        struct message eof = {0};
        eof.type = FILE_DOWNLOAD;
        strncpy(eof.nick_sender, "Server", NICK_LEN - 1);
        send_msg(sockfd, &eof, "");

        fclose(f);
        printf("[Server] Sent file '%s' to %s\n", msg.infos, client->nickname);
        // log
        char he[MSG_LEN + 64];
        snprintf(he, sizeof(he), "[%s] downloaded file '%s'", client->nickname, msg.infos);
        add_to_history(client->login, he);
        return 1;
    }

    // echo message standard
    if (msg.type == ECHO_SEND) {
        /* Handle /quit */
        if (strcmp(buff, "/quit") == 0) {
            printf("Client %d (%s) disconnected\n", sockfd, client->nickname);
            return 0;
        }

        printf("[Server] %s says: %s\n", client->nickname, buff);
        struct message r = msg;
        send_msg(sockfd, &r, buff);

        char he[MSG_LEN + 64];
        format_message_for_history(he, sizeof(he),
                                   client->nickname, "Server (echo)", buff, 0);
        add_to_history(client->login, he);
        return 1;
    }

    /* Unknown message type */
    reply_text(sockfd, ECHO_SEND, "[Server] Unknown command\n");
    return 1;
}

//------------------------- Server Setup --------------------
static int handle_bind(const char *port){
    struct addrinfo hints = {0}, *res, *rp;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    if (getaddrinfo(NULL, port, &hints, &res) != 0) {
        perror("getaddrinfo");
        exit(EXIT_FAILURE);
    }

    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        fprintf(stderr, "Could not bind to port %s\n", port);
        exit(EXIT_FAILURE);
    }
    return fd;
}

//------------------------- Main Server Loop --------------------
int main(int argc, char *argv[]){
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_sigint);
    load_users_from_file();
    ensure_dirs();

    int sfd = handle_bind(argv[1]);
    if (listen(sfd, SOMAXCONN) != 0) {
        perror("listen");
        return EXIT_FAILURE;
    }
    printf("[Server] Listening on port %s\n", argv[1]);

    struct pollfd fds[MAX_CLIENTS] = {0};
    fds[0].fd     = sfd;
    fds[0].events = POLLIN;
    int nfds = 1;

    while (running) {
        int ret = poll(fds, nfds, 1000);  /* 1s timeout for signal check */
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (!(fds[i].revents & POLLIN)) continue;

            if (fds[i].fd == sfd) {
                /* New connection */
                struct sockaddr_storage addr;
                socklen_t len = sizeof(addr);
                int newfd = accept(sfd, (struct sockaddr *)&addr, &len);
                if (newfd < 0) { perror("accept"); continue; }

                printf("[Server] New connection: fd %d\n", newfd);
                add_client(newfd, addr, len);

                if (nfds < MAX_CLIENTS) {
                    fds[nfds].fd     = newfd;
                    fds[nfds].events = POLLIN;
                    nfds++;
                } else {
                    fprintf(stderr, "[Server] Max clients reached\n");
                    close(newfd);
                    remove_client(newfd);
                }
            } else {
                int cfd = fds[i].fd;
                if (handle_client_message(cfd) == 0) {
                    printf("[Server] Disconnected: fd %d\n", cfd);
                    close(cfd);
                    remove_client(cfd);
                    fds[i] = fds[nfds - 1];
                    nfds--;
                    i--;
                }
            }
        }
    }

    printf("\n[Server] Shutting down...\n");
    cleanup_all_clients();
    close(sfd);
    return 0;
}