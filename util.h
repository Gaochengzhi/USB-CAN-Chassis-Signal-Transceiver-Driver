#ifndef UTIL_H
#define UTIL_H
#include <stdint.h>
#define SOCKET_PATH "/tmp/can_socket"
// 函数声明
int parse_command(char *cmd, char **tokens, int max_tokens);
uint8_t calculate_xor(uint8_t *data, int len);
uint64_t get_current_time_ms();
void init_unix_domain_socket();

#endif // UTIL_H