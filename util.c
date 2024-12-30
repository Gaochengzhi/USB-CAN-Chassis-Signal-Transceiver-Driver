#include "util.h"
#include <string.h>
#include <strings.h>
#include <sys/time.h>
uint8_t calculate_xor(uint8_t *data, int len) {
  uint8_t xor_value = 0;
  for (int i = 0; i < len; i++) {
    xor_value ^= data[i];
  }
  return xor_value;
}
/**
 * 解析命令并分割为 token 数组
 */
int parse_command(char *cmd, char **tokens, int max_tokens) {
  char *token = strtok(cmd, " ");
  for (int i = 0; i < max_tokens; i++) {
    tokens[i] = token;
    if (token == NULL)
      break;
    token = strtok(NULL, i == max_tokens - 2 ? "\n" : " ");
  }
  return tokens[max_tokens - 1] != NULL; // 确保有5个参数
}

uint64_t get_current_time_ms() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
}
// 初始化Unix域套接字服务器
