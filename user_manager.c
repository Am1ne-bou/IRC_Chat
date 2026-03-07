#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "user_manager.h"

user_t users[MAX_USERS];
int user_count = 0;

// Load users from file at startup
// This should be called once when the server starts
// It populates the users array and sets user_count
void load_users_from_file(void){
    FILE *f = fopen(USERS_FILE, "r");
    if (!f) return;

    char line[LOGIN_LEN + PWD_LEN + 4];
    user_count = 0;

    // Each line in the file should be in the format: login:password    //TODO: handle malformed lines and encrypted passwords
    while (fgets(line, sizeof(line), f) && user_count < MAX_USERS) {
        line[strcspn(line, "\n")] = '\0';
        char *sep = strchr(line, ':');
        if (!sep) continue;
        *sep = '\0';
        strncpy(users[user_count].login, line, LOGIN_LEN - 1);
        strncpy(users[user_count].password, sep + 1, PWD_LEN - 1);
        user_count++;
    }
    fclose(f);
    printf("Loaded %d users from %s\n", user_count, USERS_FILE);
}

// Save users to the file
// This should be called whenever a new user is registered or when the server shuts down
void save_users_to_file(void){
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

// Authenticate user with login and password
// Returns 1 if authentication is successful, 0 otherwise
int authenticate_user(const char *login, const char *password,
                      const char *admin_login, const char *admin_password){
    // Check admin credentials first
    if (strcmp(login, admin_login) == 0 && strcmp(password, admin_password) == 0)
        return 1;
    
    // Check regular users
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].login, login) == 0)
            return strcmp(users[i].password, password) == 0;
    }
    return 0;
}

// Register a new user and save to file
// Returns 1 on success, 0 on failure 
int register_user(const char *login, const char *password){
    // Check if we can add a new user
    if (user_count >= MAX_USERS) return 0;
    if (user_exists(login)) return 0;

    // Add new user to the array and save to file
    strncpy(users[user_count].login, login, LOGIN_LEN - 1);
    strncpy(users[user_count].password, password, PWD_LEN - 1);
    user_count++;
    save_users_to_file();
    printf("Registered new user: %s\n", login);
    return 1;
}