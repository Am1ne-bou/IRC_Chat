#ifndef MSG_STRUCT_H
#define MSG_STRUCT_H

#define NICK_LEN 128
#define INFOS_LEN 128
#define LOGIN_LEN 64
#define PWD_LEN 64

enum msg_type {
    NICKNAME_NEW,
    NICKNAME_LIST,
    NICKNAME_INFOS,
    ECHO_SEND,
    UNICAST_SEND,
    BROADCAST_SEND,
    MULTICAST_CREATE,
    MULTICAST_LIST,
    MULTICAST_JOIN,
    MULTICAST_SEND,
    MULTICAST_QUIT,
    FILE_REQUEST,
    FILE_ACCEPT,
    FILE_REJECT,
    FILE_SEND,
    FILE_ACK,
    LOGIN,
    LOGIN_OK,
    LOGIN_FAIL,
    HISTORY_SEND,
    FILE_LIST,
    FILE_UPLOAD,
    FILE_DOWNLOAD_REQUEST,
    FILE_DOWNLOAD,
    ADMIN_BAN,
    USER_BANNED
};

struct message {
    int pld_len;
    char nick_sender[NICK_LEN];
    enum msg_type type;
    char infos[INFOS_LEN];
    char login[LOGIN_LEN];
    char password[PWD_LEN];
};

#endif // MSG_STRUCT_H