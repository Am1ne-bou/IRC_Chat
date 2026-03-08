#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <crypt.h>
#include <time.h>
#include "user_manager.h"

user_t users[MAX_USERS];
int user_count = 0;

// Generate a bcrypt salt string: "$2b$10$" + 22 random base64 chars
static void generate_bcrypt_salt(char *salt, size_t salt_size) {
    const char b64[] = "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    char random_part[23]; // 22 chars + null
    FILE *f = fopen("/dev/urandom", "rb"); // Try to get randomness from the system
    if (f) {
        unsigned char raw[22]; 
        fread(raw, 1, sizeof(raw), f);
        fclose(f);
        for (int i = 0; i < 22; i++)
            random_part[i] = b64[raw[i] % 64];
    } else { // Fallback to rand() if /dev/urandom is not available 
        srand(time(NULL));
        for (int i = 0; i < 22; i++)
            random_part[i] = b64[rand() % 64];
    }
    random_part[22] = '\0';

    // $2b$ = bcrypt version, cost from BCRYPT_COST, then the random salt
    snprintf(salt, salt_size, "$2b$%02d$%s", BCRYPT_COST, random_part);
}

// Load users from file
// Format per line: login:bcrypt_hash
void load_users_from_file(void) {
    FILE *f = fopen(USERS_FILE, "r");
    if (!f) return;

    char line[LOGIN_LEN + BCRYPT_HASH_LEN + 4];
    user_count = 0;

    while (fgets(line, sizeof(line), f) && user_count < MAX_USERS) {
        line[strcspn(line, "\n")] = '\0';
        char *sep = strchr(line, ':');
        if (!sep) continue;
        *sep = '\0';
        strncpy(users[user_count].login, line, LOGIN_LEN - 1);
        strncpy(users[user_count].password, sep + 1, BCRYPT_HASH_LEN - 1);
        user_count++;
    }
    fclose(f);
    printf("Loaded %d users from %s\n", user_count, USERS_FILE);
}

// Save users to the file
// This should be called whenever a new user is registered or when the server shuts down
// Format per line: login:bcrypt_hash
void save_users_to_file(void) {
    FILE *f = fopen(USERS_FILE, "w");
    if (!f) {
        perror("save_users_to_file");
        return;
    }
    for (int i = 0; i < user_count; i++)
        fprintf(f, "%s:%s\n", users[i].login, users[i].password);
    fclose(f);
}

// Check if a user exists by login
int user_exists(const char *login){
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].login, login) == 0)
            return 1;
    }
    return 0;
}

// Authenticate: crypt() will read the salt from the stored hash and apply it to the provided password
// Returns 1 if authentication is successful, 0 otherwise
int authenticate_user(const char *login, const char *password,
                      const char *admin_login, const char *admin_password) {
    if (strcmp(login, admin_login) == 0 && strcmp(password, admin_password) == 0)
        return 1;

    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].login, login) == 0) {
            // crypt() reads the salt from users[i].password automatically
            char *result = crypt(password, users[i].password);
            if (!result) return 0;
            return strcmp(result, users[i].password) == 0;
        }
    }
    return 0;
}

// Register: generate salt, hash salt + password, store both
// Returns 1 on success, 0 on failure (user exists or max users reached)
int register_user(const char *login, const char *password) {
    if (user_count >= MAX_USERS) return 0;
    if (user_exists(login)) return 0;

    strncpy(users[user_count].login, login, LOGIN_LEN - 1);

    // Generate bcrypt salt and hash the password
    char salt[30];
    generate_bcrypt_salt(salt, sizeof(salt));

    // crypt() will combine the password with the salt and return the full bcrypt hash
    char *hashed = crypt(password, salt);
    if (!hashed) return 0;

    strncpy(users[user_count].password, hashed, BCRYPT_HASH_LEN - 1);

    user_count++;
    save_users_to_file();
    printf("Registered new user: %s\n", login);
    return 1;
}