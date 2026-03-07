#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "msg_struct.h"

#define MSG_LEN 1024
#define SERV_PORT "8080"
#define SERV_ADDR "127.0.0.1"

// send_all and recv_all ensure that all bytes are sent/received ssize_t is used for byte counts and can represent negative values for error indication
static inline ssize_t send_all(int sockfd, const void *buff, size_t len){
    size_t total = 0;
    ssize_t n;

    while (total < len) {
        n = send(sockfd, (const char *)buff + total, len - total, 0);
        if (n < 0) {
            perror("send()");
            return -1;
        }
        total += n;
    }
    return (ssize_t)total;
}


static inline ssize_t recv_all(int sockfd, void *buff, size_t len){
    size_t total = 0;
    ssize_t n;

    while (total < len) {
        n = recv(sockfd, (char *)buff + total, len - total, 0);
        if (n == 0) return 0; 
        if (n < 0) {
            perror("recv()");
            return -1;
        }
        total += n;
    }
    return (ssize_t)total;
}

// Function to send struct and its payload
// Returns 0 on success, -1 on error
static inline int send_msg(int sockfd, struct message *msg, const char *payload){
    struct message net_msg = *msg;
    // Convert fields to network byte order
    net_msg.pld_len = htonl(msg->pld_len);
    net_msg.type = htonl(msg->type);

    if (send_all(sockfd, &net_msg, sizeof(struct message)) != sizeof(struct message))
        return -1;

    if (msg->pld_len > 0 && payload != NULL) {
        if (send_all(sockfd, payload, msg->pld_len) != (ssize_t)msg->pld_len)
            return -1;
    }
    return 0;
}


// Function to receive struct and its payload
// Returns: payload length (>=0) on success, -1 on error, -2 if connection closed.
static inline int recv_msg(int sockfd, struct message *msg, char *payload_buff, size_t buff_size){
    ssize_t n = recv_all(sockfd, msg, sizeof(struct message));
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
        ssize_t p = recv_all(sockfd, payload_buff, msg->pld_len);
        if (p == 0)  return -2;
        if (p != (ssize_t)msg->pld_len) return -1;
    }
    return msg->pld_len;
}
#endif // COMMON_H