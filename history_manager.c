#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "history_manager.h"

// Create history directory if it doesn't exist
// This should be called at server startup
void create_history_dir(void){
    struct stat st = {0};
    if (stat(HISTORY_DIR, &st) == -1) {
        mkdir(".resa1", 0700);
        mkdir(HISTORY_DIR, 0700);
    }
}

// Get the file path for a user's history log
// The caller should provide a buffer and its size
void get_history_path(const char *username, char *path, size_t path_size){
    snprintf(path, path_size, "%s/%s.log", HISTORY_DIR, username);
}

// Add a message to a user's history log
// This should be called whenever a message is sent or received involving the user
void add_to_history(const char *username, const char *message){
    // Sanity check
    if (!username || strlen(username) == 0) return;

    // Ensure history directory exists
    create_history_dir();

    // Get the path to the user's history file
    char path[256];
    get_history_path(username, path, sizeof(path));

    FILE *f = fopen(path, "a");
    if (!f) {
        perror("add_to_history: fopen");
        return;
    }

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);

    fprintf(f, "[%s] %s\n", ts, message);
    fclose(f);
}

// Read a user's history log
// The caller is responsible for freeing the returned buffer
char *read_user_history(const char *username, size_t *total_size){
    create_history_dir();

    char path[256];
    get_history_path(username, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, sz, f);
    buf[n] = '\0';
    fclose(f);

    *total_size = n;
    return buf;
}

// Format a message for history logging
void format_message_for_history(char *buffer, size_t buffer_size,
                                const char *from, const char *to,
                                const char *message, int is_broadcast){
    if (is_broadcast || strcmp(to, "all") == 0)
        snprintf(buffer, buffer_size, "[%s] -> [all] : %s", from, message);
    else
        snprintf(buffer, buffer_size, "[%s] -> [%s] : %s", from, to, message);
}