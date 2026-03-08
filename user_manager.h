#ifndef USER_MANAGER_H
#define USER_MANAGER_H

#include "msg_struct.h"

#define USERS_FILE "users.txt"
#define MAX_USERS 1000
#define HASH_LEN 65 // 64 chars for SHA-256 + null terminator
#define SALT_LEN 17 // 16 chars + null terminator

typedef struct user {
    char login[LOGIN_LEN];
    char salt[SALT_LEN]; // Random salt for password hashing
    char password[HASH_LEN]; // Store hashed password
} user_t;

extern user_t users[MAX_USERS];
extern int user_count;

void load_users_from_file(void);
void save_users_to_file(void);
int  user_exists(const char *login);
int  authenticate_user(const char *login, const char *password,
                       const char *admin_login, const char *admin_password);
int  register_user(const char *login, const char *password);

#endif // USER_MANAGER_H


