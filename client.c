#define _POSIX_C_SOURCE 200809L // for the compiler to get the correct prototypes for functions like getline

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "common.h"

static char pending_filename[256];
static volatile sig_atomic_t running = 1;

static void sigint_handler(int sig){
    (void)sig;
    running = 0;
}

//------------- P2P File Transfer Functions -------------

// function to receive a file in P2P mode
static int receive_file_p2p(int port, const char *filename, const char *nick) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket p2p"); return -1; }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)
    };

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind p2p"); close(srv); return -1;
    }
    if (listen(srv, 1) < 0) {
        perror("listen p2p"); close(srv); return -1;
    }

    printf("[P2P] Waiting on port %d...\n", port);

    socklen_t alen = sizeof(addr);
    int peer = accept(srv, (struct sockaddr *)&addr, &alen);
    if (peer < 0) { perror("accept p2p"); close(srv); return -1; }

    /* Prepare download directory */
    struct stat st = {0};
    if (stat(".resa1", &st) == -1)            mkdir(".resa1", 0755);
    if (stat(".resa1/downloads", &st) == -1)  mkdir(".resa1/downloads", 0755);

    char udir[256];
    snprintf(udir, sizeof(udir), ".resa1/downloads/%s_files", nick);
    if (stat(udir, &st) == -1) mkdir(udir, 0755);

    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", udir, base);

    FILE *f = fopen(path, "wb");
    if (!f) { perror("fopen p2p"); close(peer); close(srv); return -1; }

    printf("[P2P] Receiving: %s\n", filename);

    struct message m;
    char buf[MSG_LEN];
    int total = 0;

    while (1) {
        memset(&m, 0, sizeof(m));
        memset(buf, 0, MSG_LEN);
        if (recv_msg_raw(peer, &m, buf, MSG_LEN) < 0) break;
        if (m.type != FILE_SEND) break;
        if (m.pld_len == 0) break;   /* end-of-file marker */
        fwrite(buf, 1, m.pld_len, f);
        total += m.pld_len;
    }

    fclose(f);
    printf("[P2P] Received '%s' (%d bytes) -> %s\n", base, total, path);
    close(peer);
    close(srv);
    return 0;
}

// Fonction pour envoyer un fichier en P2P
static int send_file_p2p(const char *addr_port, const char *filename, const char *nick) {
    char host[64];
    int port;
    if (sscanf(addr_port, "%63[^:]:%d", host, &port) != 2) {
        fprintf(stderr, "Bad address format: %s\n", addr_port);
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket p2p send"); return -1; }

    struct sockaddr_in sa = {.sin_family = AF_INET, .sin_port = htons(port)};
    if (inet_pton(AF_INET, host, &sa.sin_addr) <= 0) {
        fprintf(stderr, "Bad address: %s\n", host);
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("connect p2p");
        close(fd);
        return -1;
    }

    FILE *f = fopen(filename, "rb");
    if (!f) { perror("fopen send"); close(fd); return -1; }

    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;

    char buf[MSG_LEN];
    size_t n;
    int total = 0;

    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        struct message m = {.pld_len = (int)n, .type = FILE_SEND};
        strncpy(m.nick_sender, nick, NICK_LEN - 1);
        strncpy(m.infos, base, INFOS_LEN - 1);
        if (send_msg_raw(fd, &m, buf) < 0) break;
        total += n;
    }

    /* End-of-file marker */
    struct message eof = {.pld_len = 0, .type = FILE_SEND};
    strncpy(eof.nick_sender, nick, NICK_LEN - 1);
    strncpy(eof.infos, base, INFOS_LEN - 1);
    send_msg_raw(fd, &eof, NULL);

    printf("[P2P] Sent '%s' (%d bytes)\n", base, total);
    fclose(f);
    close(fd);
    return 0;
}

//------------------------- Nickname Extraction --------------------
static void try_extract_nick(const char *buff, const char *prefix, char *nickname){
    size_t plen = strlen(prefix);
    if (strncmp(buff, prefix, plen) == 0) {
        const char *start = buff + plen;
        size_t len = strcspn(start, "\n");
        if (len > 0 && len < NICK_LEN) {
            memcpy(nickname, start, len);
            nickname[len] = '\0';
        }
    }
}

//-------------------------- Client Loop --------------------
static void client_loop(SSL *ssl) {
    struct message msg;
    char buff[MSG_LEN];
    char nickname[NICK_LEN] = "";

    int sockfd = SSL_get_fd(ssl);  

    struct pollfd fds[2] = {
        {.fd = STDIN_FILENO, .events = POLLIN},
        {.fd = sockfd,       .events = POLLIN}
    };

    printf("> ");
    fflush(stdout);

    while (running) {
        int ret = poll(fds, 2, 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        // Check for user input
        if (fds[0].revents & POLLIN) {
            memset(buff, 0, MSG_LEN);
            if (!fgets(buff, MSG_LEN, stdin)) break;
            buff[strcspn(buff, "\n")] = '\0';
            memset(&msg, 0, sizeof(msg));
            
            if (strncmp(buff, "/nick ", 6) == 0) { // Set nickname command
                msg.type = NICKNAME_NEW;
                strncpy(msg.infos, buff + 6, INFOS_LEN - 1);

            } else if (strncmp(buff, "/login ", 7) == 0) { // Login command
                char login[LOGIN_LEN] = {0}, pw[PWD_LEN] = {0};
                if (sscanf(buff + 7, "%63s %63s", login, pw) != 2) {
                    printf("Usage: /login <user> <password>\n> ");
                    fflush(stdout);
                    continue;
                }
                msg.type = LOGIN;
                strncpy(msg.login, login, LOGIN_LEN - 1);
                strncpy(msg.password, pw, PWD_LEN - 1);

            } else if (strcmp(buff, "/who") == 0) { // List users command
                msg.type = NICKNAME_LIST; 

            } else if (strncmp(buff, "/whois ", 7) == 0) { // User info command
                msg.type = NICKNAME_INFOS;
                strncpy(msg.infos, buff + 7, INFOS_LEN - 1);

            } else if (strncmp(buff, "/msgall ", 8) == 0) { // Broadcast message command
                if (strlen(nickname) == 0) {
                    printf("Set your nickname first with /nick\n> ");
                    fflush(stdout);
                    continue;
                }
                msg.type    = BROADCAST_SEND;
                msg.pld_len = strlen(buff + 8);
                strncpy(msg.nick_sender, nickname, NICK_LEN - 1);
                if (send_msg(ssl, &msg, buff + 8) < 0) break;
                printf("> ");
                fflush(stdout);
                continue;

            } else if (strncmp(buff, "/msg ", 5) == 0) { // Private message command
                msg.type = UNICAST_SEND;
                strncpy(msg.infos, buff + 5, INFOS_LEN - 1);

            } else if (strncmp(buff, "/ban ", 5) == 0) { // Admin ban command
                if (strlen(nickname) == 0) {
                    printf("Login first\n> ");
                    fflush(stdout);
                    continue;
                }
                msg.type = ADMIN_BAN;
                strncpy(msg.nick_sender, nickname, NICK_LEN - 1);
                strncpy(msg.infos, buff + 5, INFOS_LEN - 1);

            } else if (strncmp(buff, "/sendfile ", 10) == 0) { // P2P file send command
                if (strlen(nickname) == 0) {
                    printf("Set your nickname first\n> ");
                    fflush(stdout);
                    continue;
                }

                char *path = buff + 10;
                FILE *fp = fopen(path, "rb");
                if (!fp) { perror("fopen"); printf("> "); fflush(stdout); continue; }

                const char *base = strrchr(path, '/'); // extract filename from path
                base = base ? base + 1 : path;

                char fbuf[MSG_LEN];
                size_t n;
                // Send file in chunks
                while ((n = fread(fbuf, 1, sizeof(fbuf), fp)) > 0) {
                    struct message fm = {0};
                    fm.type    = FILE_SEND;
                    fm.pld_len = (int)n;
                    strncpy(fm.nick_sender, nickname, NICK_LEN - 1);
                    strncpy(fm.infos, base, INFOS_LEN - 1);
                    if (send_msg(ssl, &fm, fbuf) < 0) break;
                }
                // Send end-of-file marker
                struct message eof = {0};
                eof.type = FILE_SEND;
                strncpy(eof.nick_sender, nickname, NICK_LEN - 1);
                strncpy(eof.infos, base, INFOS_LEN - 1);
                send_msg(ssl, &eof, NULL);
                fclose(fp);
                printf("File '%s' sent to server\n> ", base);
                fflush(stdout);
                continue;

            } else if (strcmp(buff, "/listfiles") == 0) { // List files on server command
                msg.type = FILE_LIST;

            } else if (strncmp(buff, "/getfile ", 9) == 0) { // Download file from server command
                if (strlen(nickname) == 0) {
                    printf("Set your nickname first\n> ");
                    fflush(stdout);
                    continue;
                }
                char *fname = buff + 9;
                // Sanitize filename to prevent directory traversal
                for (char *p = fname; *p; p++)
                    if (*p == '/' || *p == '\\') *p = '_';

                struct stat st = {0};
                if (stat(".resa1", &st) == -1)            mkdir(".resa1", 0755);
                if (stat(".resa1/downloads", &st) == -1)  mkdir(".resa1/downloads", 0755);

                // Create user-specific download directory
                char udir[256];
                snprintf(udir, sizeof(udir), ".resa1/downloads/%s_files", nickname);
                if (stat(udir, &st) == -1) mkdir(udir, 0755);

                // Construct the file path for saving the downloaded file
                char fpath[512];
                snprintf(fpath, sizeof(fpath), "%s/%s", udir, fname);

                // Send file download request to server
                struct message req = {0};
                req.type = FILE_DOWNLOAD_REQUEST;
                strncpy(req.infos, fname, INFOS_LEN - 1);
                if (send_msg(ssl, &req, NULL) < 0) break;

                // Wait for server response and handle file download
                struct message rm;
                char rbuf[MSG_LEN];
                int r = recv_msg(ssl, &rm, rbuf, MSG_LEN);
                if (r < 0) { printf("> "); fflush(stdout); continue; }

                // If server sent an error (not FILE_DOWNLOAD), print it and skip
                if (rm.type != FILE_DOWNLOAD) {
                    printf("\r                              \r%s> ", rbuf);
                    fflush(stdout);
                    continue;
                }
                // Wait for the file to be sent by the server and save it
                FILE *f = fopen(fpath, "wb");
                if (!f) { perror("fopen"); printf("> "); fflush(stdout); continue; }

                // Receive file in chunks until end-of-file marker
                struct message m;
                char chunk[MSG_LEN];
                while (1) {
                    int r = recv_msg(ssl, &m, chunk, MSG_LEN);
                    if (r < 0) break;
                    if (r == 0 ||m.type != FILE_DOWNLOAD) break;
                    fwrite(chunk, 1, r, f);
                }
                fclose(f);
                printf("File '%s' downloaded\n> ", fname);
                fflush(stdout);
                continue;

            } else if (strncmp(buff, "/send ", 6) == 0) { // P2P file send command with server coordination
                if (strlen(nickname) == 0) {
                    printf("Set your nickname first\n> ");
                    fflush(stdout);
                    continue;
                }
                // Extract target user and filename
                char target[NICK_LEN] = {0}, fname[256] = {0};
                if (sscanf(buff + 6, "%127s %255s", target, fname) != 2) {
                    printf("Usage: /send <user> <file>\n> ");
                    fflush(stdout);
                    continue;
                }
                FILE *test = fopen(fname, "rb");
                if (!test) {
                    printf("File not found: %s\n> ", fname);
                    fflush(stdout);
                    continue;
                }
                fclose(test);

                // Sanitize filename to prevent directory traversal
                strncpy(pending_filename, fname, sizeof(pending_filename) - 1);
                pending_filename[sizeof(pending_filename) - 1] = '\0';

                // Send file transfer request to server to coordinate with recipient
                struct message req = {.type = FILE_REQUEST};
                strncpy(req.nick_sender, nickname, NICK_LEN - 1);
                strncpy(req.infos, target, INFOS_LEN - 1);
                req.pld_len = strlen(fname) + 1;
                if (send_msg(ssl, &req, fname) < 0) break;

                printf("Transfer request sent to %s for '%s'\n> ", target, fname);
                fflush(stdout);
                continue;

            } else {
                // Regular message (not a command)
                msg.type    = ECHO_SEND;
                msg.pld_len = strlen(buff);
                if (strlen(nickname) > 0)
                    strncpy(msg.nick_sender, nickname, NICK_LEN - 1);
                if (send_msg(ssl, &msg, buff) < 0) break;
                printf("> ");
                fflush(stdout);
                continue;
            }

            // For commands that don't require immediate user input, send the message to the server
            if (strlen(nickname) > 0)
                strncpy(msg.nick_sender, nickname, NICK_LEN - 1);
            if (send_msg(ssl, &msg, NULL) < 0) break;
            printf("> ");
            fflush(stdout);
        }

        // Check for messages from server
        if (fds[1].revents & POLLIN) {

            /*TLS fix poll sees tls as readable but recv_msg would block, 
            so we need to call recv_msg in a loop until we get a message or an error
            */

            int flags = fcntl(sockfd, F_GETFL, 0);
            fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
            char peek;
            int pr = SSL_peek(ssl, &peek, 1);
            fcntl(sockfd, F_SETFL, flags); // restore original flags
            if (pr <= 0) {
                int err = SSL_get_error(ssl, pr);
                if (err == SSL_ERROR_WANT_READ)  continue;
                else if (err == SSL_ERROR_ZERO_RETURN) {
                    printf("[Client] Server disconnected\n");
                    break;
                }
                else {
                    fprintf(stderr, "SSL_peek error: %d\n", err);
                    printf("[Client] Server disconnected\n");
                    break;
                }
            }


            memset(&msg, 0, sizeof(msg));
            memset(buff, 0, MSG_LEN);

            int ret2 = recv_msg(ssl, &msg, buff, MSG_LEN);
            if (ret2 <= 0) {
                printf("[Client] Server disconnected\n");
                break;
            }

            printf("\r                              \r");// Clear the current input line

            switch (msg.type) {
            case HISTORY_SEND: // Received chat history from server
                printf("\n---- Chat History ----\n%s----------------------\n", buff);
                break;

            case FILE_REQUEST: { // Received a file transfer request from another user
                printf("\n%s wants to send you file: %s\nAccept? (y/n): ",
                       msg.nick_sender, buff);
                fflush(stdout);

                char resp[10];
                if (fgets(resp, sizeof(resp), stdin)) {
                    resp[strcspn(resp, "\n")] = '\0';
                    if (resp[0] == 'y' || resp[0] == 'Y') {
                        int p2p_port = 8081 + rand() % 1000;
                        char ap[64];
                        snprintf(ap, sizeof(ap), "127.0.0.1:%d", p2p_port);

                        struct message acc = {.type = FILE_ACCEPT};
                        strncpy(acc.infos, msg.nick_sender, INFOS_LEN - 1);
                        acc.pld_len = strlen(ap) + 1;
                        send_msg(ssl, &acc, ap);
                        // Start receiving the file in a separately to avoid blocking the main loop
                        receive_file_p2p(p2p_port, buff, nickname);
                    } else {
                        struct message rej = {.type = FILE_REJECT};
                        strncpy(rej.infos, msg.nick_sender, INFOS_LEN - 1);
                        char em[256];
                        snprintf(em, sizeof(em), "Rejected by %s", nickname);
                        rej.pld_len = strlen(em) + 1;
                        send_msg(ssl, &rej, em);
                        printf("Transfer rejected\n");
                    }
                }
                break;
            }

            case FILE_ACCEPT: // Received acceptance for a file transfer request we sent
                printf("\n%s accepted transfer. Connecting to %s...\n",
                       msg.nick_sender, buff);
                if (strlen(pending_filename) > 0) {
                    // Start sending the file in a separately to avoid blocking the main loop
                    send_file_p2p(buff, pending_filename, nickname);
                    pending_filename[0] = '\0';
                } else {
                    printf("Error: no pending file\n");
                }
                break;

            case FILE_REJECT: // Received rejection for a file transfer request we sent
                printf("\n%s rejected the transfer\n", msg.nick_sender);
                pending_filename[0] = '\0';
                break;

            case USER_BANNED: // Received notification that we have been banned by the server
                printf("%s\n", buff);
                close(sockfd);
                exit(EXIT_SUCCESS);

            case LOGIN_OK: // Received successful login response from server
                // Extract nickname from welcome message
                try_extract_nick(buff, "[Server] Login OK! Welcome back ", nickname);
                try_extract_nick(buff, "[Server] Registration OK! Welcome ", nickname);
                printf("%s", buff);
                break;

            case LOGIN_FAIL: // Received failed login response from server
                memset(nickname, 0, NICK_LEN);
                printf("%s", buff);
                break;

            default: // For other message types, just print the payload
                printf("%s", buff);
                // Try to extract nickname from server messages that include it
                try_extract_nick(buff, "[Server] : Welcome on the chat ", nickname);
                break;
            }

            printf("> ");
            fflush(stdout);
        }
    }
}

//------------------------- Connection Function -------------------------
static int do_connect(const char *host, const char *port){
    struct addrinfo hints = {0}, *res, *rp;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        perror("getaddrinfo");
        exit(EXIT_FAILURE);
    }

    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        fprintf(stderr, "Could not connect to %s:%s\n", host, port);
        exit(EXIT_FAILURE);
    }
    return fd;
}

//------------------------- Main Function -------------------------
int main(int argc, char *argv[]){
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    signal(SIGINT, sigint_handler);

    int fd = do_connect(argv[1], argv[2]);

    //TLS setup
    SSL_library_init();
    SSL_load_error_strings();
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);

    // Perform TLS handshake
    if (SSL_connect(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return EXIT_FAILURE;
    }
    printf("Connected to %s:%s\n", argv[1], argv[2]);

    client_loop(ssl);

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(fd);
    return 0;
}