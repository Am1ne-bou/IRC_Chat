#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <time.h>
#include "user_manager.h"

user_t users[MAX_USERS];
int user_count = 0;

static void get_random_salt(char *salt, size_t length) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    srand(time(NULL));
    for (size_t i = 0; i < length - 1; i++) {
        salt[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    salt[length - 1] = '\0';
}

// Hash a plaintext password into a 64-char hex string using SHA-256
// Output buffer must be at least HASH_LEN (65) bytes
static void hash_password(const char *salt, const char *password, char *output) {
    // Concatenate salt + password
    char combined[SALT_LEN + PWD_LEN];
    snprintf(combined, sizeof(combined), "%s%s", salt, password);

    // SHA-256
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char *)combined, strlen(combined), hash);

    // Convert to hex string
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(output + (i * 2), "%02x", hash[i]);
    output[64] = '\0';
}

// Load users from file
// Format per line: login:salt:hash
void load_users_from_file(void) {
    FILE *f = fopen(USERS_FILE, "r");
    if (!f) return;

    char line[LOGIN_LEN + SALT_LEN + HASH_LEN + 4];
    user_count = 0;

    while (fgets(line, sizeof(line), f) && user_count < MAX_USERS) {
        line[strcspn(line, "\n")] = '\0';

        // Parse login:salt:hash
        char *first_sep = strchr(line, ':');
        if (!first_sep) continue;
        *first_sep = '\0';

        char *second_sep = strchr(first_sep + 1, ':');
        if (!second_sep) continue;
        *second_sep = '\0';

        strncpy(users[user_count].login, line, LOGIN_LEN - 1);
        strncpy(users[user_count].salt, first_sep + 1, SALT_LEN - 1);
        strncpy(users[user_count].password, second_sep + 1, HASH_LEN - 1);
        user_count++;
    }
    fclose(f);
    printf("Loaded %d users from %s\n", user_count, USERS_FILE);
}

// Save users to the file
// This should be called whenever a new user is registered or when the server shuts down
// Format per line: login:salt:hash
void save_users_to_file(void) {
    FILE *f = fopen(USERS_FILE, "w");
    if (!f) {
        perror("save_users_to_file");
        return;
    }
    for (int i = 0; i < user_count; i++)
        fprintf(f, "%s:%s:%s\n", users[i].login, users[i].salt, users[i].password);
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

// Authenticate: hash salt + input password, compare to stored hash
int authenticate_user(const char *login, const char *password,
                      const char *admin_login, const char *admin_password) {
    // Admin check — hardcoded credentials
    if (strcmp(login, admin_login) == 0 && strcmp(password, admin_password) == 0)
        return 1;

    // Find user, hash input with their salt, compare
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].login, login) == 0) {
            char hashed[HASH_LEN];
            hash_password(users[i].salt, password, hashed);
            return strcmp(users[i].password, hashed) == 0;
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

    // Generate random salt for this user
    get_random_salt(users[user_count].salt, sizeof(users[user_count].salt));

    // Hash salt + password
    hash_password(users[user_count].salt, password, users[user_count].password);

    user_count++;
    save_users_to_file();
    printf("Registered new user: %s\n", login);
    return 1;
}