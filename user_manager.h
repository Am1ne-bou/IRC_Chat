#ifndef USER_MANAGER_H
#define USER_MANAGER_H

#include "msg_struct.h"

#define USERS_FILE "users.txt"
#define MAX_USERS 1000
#define BCRYPT_HASH_LEN 61 // 60 chars for bcrypt hash + null terminator
#define BCRYPT_COST 12 // bcrypt cost factor 

typedef struct user {
    char login[LOGIN_LEN];
    char password[BCRYPT_HASH_LEN]; // Store bcrypt hashed password
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


