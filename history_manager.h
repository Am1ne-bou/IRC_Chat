#ifndef HISTORY_MANAGER_H
#define HISTORY_MANAGER_H

#include <stddef.h>

#define HISTORY_DIR ".resa1/history"

void  create_history_dir(void);
void  get_history_path(const char *username, char *path, size_t path_size);
void  add_to_history(const char *username, const char *message);
char *read_user_history(const char *username, size_t *total_size);
void  format_message_for_history(char *buffer, size_t buffer_size,
                                 const char *from, const char *to,
                                 const char *message, int is_broadcast);

#endif // HISTORY_MANAGER_H