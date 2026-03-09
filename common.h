#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "msg_struct.h"

#define MSG_LEN 1024
#define SERV_PORT "8080"
#define SERV_ADDR "127.0.0.1"

//SSL in place of raw sockets for secure communication

// send_all and recv_all ensure that all bytes are sent/received ssize_t is used for byte counts and can represent negative values for error indication
// Over TLS, we need to use SSL_write and SSL_read instead of send and recv, and handle their return values properly
static inline ssize_t send_all(SSL *ssl, const void *buff, size_t len){
    size_t total = 0;
    ssize_t n;

    while (total < len) {
        n = SSL_write(ssl, (const char *)buff + total, len - total);
        if (n <= 0) {
            ERR_print_errors_fp(stderr);
            return -1;
        }
        total += n;
    }
    return (ssize_t)total;
}


static inline ssize_t recv_all(SSL *ssl, void *buff, size_t len){
    size_t total = 0;
    ssize_t n;

    while (total < len) {
        n = SSL_read(ssl, (char *)buff + total, len - total);
        if (n == 0) return 0; 
        if (n < 0) {
            ERR_print_errors_fp(stderr);
            return -1;
        }
        total += n;
    }
    return (ssize_t)total;
}

// Function to send struct and its payload
// Returns 0 on success, -1 on error
static inline int send_msg(SSL *ssl, struct message *msg, const char *payload){
    struct message net_msg = *msg;
    // Convert fields to network byte order
    net_msg.pld_len = htonl(msg->pld_len);
    net_msg.type = htonl(msg->type);

    if (send_all(ssl, &net_msg, sizeof(struct message)) != sizeof(struct message))
        return -1;

    if (msg->pld_len > 0 && payload != NULL) {
        if (send_all(ssl, payload, msg->pld_len) != (ssize_t)msg->pld_len)
            return -1;
    }
    return 0;
}


// Function to receive struct and its payload
// Returns: payload length (>=0) on success, -1 on error, -2 if connection closed.
static inline int recv_msg(SSL *ssl, struct message *msg, char *payload_buff, size_t buff_size){
    ssize_t n = recv_all(ssl, msg, sizeof(struct message));
    if (n == 0)  return -2;
    if (n != sizeof(struct message)) return -1;
    // Convert fields from network byte order
    msg->pld_len = ntohl(msg->pld_len);
    msg->type    = ntohl(msg->type);

    if (msg->pld_len > 0) {
        if (buff_size < (size_t)msg->pld_len) {
            fprintf(stderr, "Buffer too small: need %d, have %zu\n",
                    msg->pld_len, buff_size);
            return -1;
        }
        ssize_t p = recv_all(ssl, payload_buff, msg->pld_len);
        if (p == 0)  return -2;
        if (p != (ssize_t)msg->pld_len) return -1;
    }
    return msg->pld_len;
}


/* Raw socket versions for non-SSL communication for the P2P parts0
Raw (non-TLS) versions for P2P file transfers in client.c, 
since we want to avoid the overhead of SSL for direct client-to-client transfers. 
These functions are not used for server communication.
*/
static inline ssize_t send_all_raw(int fd, const void *buff, size_t len){
    size_t total = 0;
    ssize_t n;
    while (total < len) {
        n = send(fd, (const char *)buff + total, len - total, 0);
        if (n < 0) { perror("send()"); return -1; }
        total += n;
    }
    return (ssize_t)total;
}

static inline ssize_t recv_all_raw(int fd, void *buff, size_t len){
    size_t total = 0;
    ssize_t n;
    while (total < len) {
        n = recv(fd, (char *)buff + total, len - total, 0);
        if (n == 0) return 0;
        if (n < 0) { perror("recv()"); return -1; }
        total += n;
    }
    return (ssize_t)total;
}

static inline int send_msg_raw(int fd, struct message *msg, const char *payload){
    struct message net_msg = *msg;
    net_msg.pld_len = htonl(msg->pld_len);
    net_msg.type = htonl(msg->type);
    if (send_all_raw(fd, &net_msg, sizeof(struct message)) != sizeof(struct message))
        return -1;
    if (msg->pld_len > 0 && payload != NULL) {
        if (send_all_raw(fd, payload, msg->pld_len) != (ssize_t)msg->pld_len)
            return -1;
    }
    return 0;
}

static inline int recv_msg_raw(int fd, struct message *msg, char *payload_buff, size_t buff_size){
    ssize_t n = recv_all_raw(fd, msg, sizeof(struct message));
    if (n == 0)  return -2;
    if (n != sizeof(struct message)) return -1;
    msg->pld_len = ntohl(msg->pld_len);
    msg->type    = ntohl(msg->type);
    if (msg->pld_len > 0) {
        if (buff_size < (size_t)msg->pld_len) return -1;
        ssize_t p = recv_all_raw(fd, payload_buff, msg->pld_len);
        if (p == 0)  return -2;
        if (p != (ssize_t)msg->pld_len) return -1;
    }
    return msg->pld_len;
}
#endif // COMMON_H