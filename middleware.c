// battery.c
#include "controlcan.h"
#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define msleep(ms) usleep((ms) * 1000)
#define min(a, b) (((a) < (b)) ? (a) : (b))

#define USBCAN_I 3  // USBCAN-I/I+ 3
#define USBCAN_II 4 // USBCAN-II/II+ 4
#define MAX_CHANNELS 2
#define RX_WAIT_TIME 100
#define RX_BUFF_SIZE 1000
#define ESP_COMMAND 0x1801B0A0
#define SPEED_COMMAND 0x1803B0A0
#define LIGHT_COMMAND 0x1805B0A0
#define REMOTE_COMMAND 0x1807B0A0
#define SOCKET_PATH "/tmp/can_socket"
#define SOCKET_READ_PATH "/tmp/can_read_socket"
// 档位值
uint8_t gear_value = 0;
// 用于缓存接收到的指令
pthread_mutex_t frame_mutex = PTHREAD_MUTEX_INITIALIZER;
// 定义控制帧的数据结构

typedef struct
{
    UINT id;
    int modified; // 是否使用修改的帧
    uint8_t data[8];
    uint8_t interval_ms;
    uint64_t next_send_time; // 下一次发送的绝对时间
    uint8_t temp_data[8];    // 新增：临时数据缓冲区
} ControlFrame;

#define FRAME_COUNT 4

ControlFrame control_frames[FRAME_COUNT] = {
    {ESP_COMMAND, .modified = 0,
     .data = {0x00, 0x00, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00},
     .interval_ms = 20},
    {SPEED_COMMAND, .modified = 0,
     .data = {0x80, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
     .interval_ms = 20},
    {LIGHT_COMMAND, .modified = 0,
     .data = {0x01, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00},
     .interval_ms = 50},
    {REMOTE_COMMAND, .modified = 0,
     .data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
     .interval_ms = 100}};
// 接收线程上下文
typedef struct
{
    int DevType; // 设备类型
    int DevIdx;  // 设备索引
    int index;   // 通道号
    int total;   // 接收总数
    int stop;    // 线程结束标
} RX_CTX;

int server_fd, client_fd;
void init_unix_domain_socket()
{
    struct sockaddr_un server_addr;
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket error");
        exit(1);
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);
    unlink(SOCKET_PATH); // 如果套接字文件已存在，先删除
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
        -1)
    {
        perror("bind error");
        exit(1);
    }
    if (listen(server_fd, 5) == -1)
    {
        perror("listen error");
        exit(1);
    }
    printf("Unix域套接字服务器已初始化。\n");
}

int server_read_fd; // 监听套接字
int client_read_fd; // 通信套接字
void init_unix_domain_socket_read()
{
    struct sockaddr_un server_addr;
    server_read_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_read_fd < 0)
    {
        perror("socket error");
        exit(1);
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SOCKET_READ_PATH, sizeof(server_addr.sun_path) - 1);
    unlink(SOCKET_READ_PATH); // 如果套接字文件已存在，先删除
    if (bind(server_read_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
        -1)
    {
        perror("bind error");
        exit(1);
    }
    if (listen(server_read_fd, 5) == -1)
    {
        perror("listen error");
        exit(1);
    }
    printf("Unix域套读取接字服务器已初始化。\n");
}

void construct_can_frame(VCI_CAN_OBJ *can, UINT id, const int *data_arr)
{
    memset(can, 0, sizeof(VCI_CAN_OBJ));
    can->ID = id;        // id
    can->SendType = 2;   // 发送方式 0-正常, 1-单次, 2-自发自收
    can->RemoteFlag = 0; // 0-数据帧 1-远程帧
    can->ExternFlag = 1; // 0-标准帧 1-扩展帧
    can->DataLen = 8;    // 数据长度 1~8
    for (int i = 0; i < can->DataLen; i++)
    {
        can->Data[i] = data_arr[i];
        printf("can->Data[i]: %d\n", can->Data[i]);
    }
}
/**
 * 更新帧的指定位 tmp_data
 */

int update_data_bits(ControlFrame *frame, int start, int len, const char *content_str)
{
    // 参数校验
    start += 1;
    if (frame == NULL || content_str == NULL || start < 0 || start > 64 || len < 1 || len > 64 ||
        (start + len - 1) > 64 || strlen(content_str) != len)
    {
        return 0; // 参数或输入内容无效，返回失败
    }

    for (int i = 0; i < len; i++)
    {
        if (content_str[i] != '0' && content_str[i] != '1')
        {
            return 0; // content_str 必须只包含 '0' 和 '1'
        }
    }

    // 将 frame->data 转为 64 位整数 (小端模式)
    unsigned long long data_value = 0;
    for (int i = 0; i < 8; i++)
    {
        data_value |= ((unsigned long long)frame->data[i] << (i * 8));
    }

    // 更新指定位区间的内容
    for (int i = 0; i < len; i++)
    {
        int bit_pos = start - 1 + i; // 将1基准的 start 转换为0基准
        if (content_str[len - 1 - i] == '1')
        {                                    // 注意 C 中字符串不允许反转直接操作
            data_value |= (1ULL << bit_pos); // 设置为 1
        }
        else if (content_str[len - 1 - i] == '0')
        {
            data_value &= ~(1ULL << bit_pos); // 设置为 0
        }
    }

    // 将更新后的 data_value 写回 frame->data 中 (小端模式)
    for (int i = 0; i < 8; i++)
    {
        frame->data[i] = (data_value >> (i * 8)) & 0xFF;
    }

    return 1; // 更新成功
}
/**
 * 查找匹配的控制帧，返回其索引（未找到返回 -1）
 */
int find_control_frame(UINT id)
{
    for (int i = 0; i < FRAME_COUNT; i++)
    {
        if (control_frames[i].id == id)
        {
            return i;
        }
    }
    return -1;
}
/**
 * 如果需要，则重新计算异或值
 */
void recalculate_xor(UINT id, uint8_t *data)
{
    if (id == ESP_COMMAND || id == SPEED_COMMAND)
    {
        data[7] = calculate_xor(data, 7);
    }
}
void process_command(char *cmd)
{
    // 提取并解析命令
    char *tokens[5]; // 静态分配存储分割后的5段命令
    if (!parse_command(cmd, tokens, 5))
    {
        printf("指令格式错误。\n");
        return;
    }
    // 校验前缀是否为 "send"
    if (strcmp(tokens[0], "send") != 0)
    {
        printf("未知指令。\n");
        return;
    }
    // 提取命令参数
    UINT id = strtoul(tokens[1], NULL, 0);
    int start = atoi(tokens[2]);
    int len = atoi(tokens[3]);
    char *content_str = tokens[4];
    int content_len = strlen(content_str);
    // 校验数据有效性
    if (content_len != len)
    {
        printf("数据长度与声明长度不匹配。\n");
        return;
    }
    // 进入线程锁保护
    pthread_mutex_lock(&frame_mutex);
    // 查找并更新对应帧
    int frame_index = find_control_frame(id);
    if (frame_index == -1)
    {
        pthread_mutex_unlock(&frame_mutex);
        printf("未找到对应控制帧。\n");
        return;
    }
    if (!update_data_bits(&control_frames[frame_index], start, len,
                          content_str))
    {
        pthread_mutex_unlock(&frame_mutex);
        printf("数据超出范围。\n");
        return;
    }
    // 如果需要重新计算异或值
    recalculate_xor(id, control_frames[frame_index].temp_data);
    // 标记帧已修改
    control_frames[frame_index].modified = 1;
    // 输出调试信息
    //   printf("id: %X, data: ", id);
    //   for (int j = 0; j < 8; j++) {
    //     printf("%X ", control_frames[frame_index].data[j]);
    //   }
    //   printf("\n");

    pthread_mutex_unlock(&frame_mutex);
}

volatile int server_running = 1; // 使用信号量或其他方式控制退出

void *command_thread(void *data)
{
    RX_CTX *ctx = (RX_CTX *)data;

    while (server_running)
    {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd =
            accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0)
        {
            perror("accept error");
            if (!server_running)
                break; // 如果服务关闭，退出循环
            continue;
        }
        printf("客户端已连接。\n");
        char buffer[256];
        while (server_running)
        {
            ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
            if (n <= 0)
            {
                if (n < 0)
                    perror("read error");
                else
                    printf("客户端断开连接。\n");
                close(client_fd);
                break;
            }
            process_command(buffer); // 调用处理指令的逻辑
            msleep(1);
        }
    }

    return NULL;
}

void update_heartbeat_and_xor(ControlFrame *frame)
{
    if (frame->id == ESP_COMMAND)
    {
        frame->data[1] = (frame->data[1] + 1) & 0xFF;
        frame->data[7] = calculate_xor(frame->data, 7);
        frame->temp_data[1] = (frame->temp_data[1] + 1) & 0xFF;
        frame->temp_data[7] = calculate_xor(frame->temp_data, 7);
    }
    else if (frame->id == SPEED_COMMAND)
    {
        frame->data[2] = (frame->data[2] + 1) & 0xFF;
        frame->data[7] = calculate_xor(frame->data, 7);
        frame->temp_data[2] = (frame->temp_data[2] + 1) & 0xFF;
        frame->temp_data[7] = calculate_xor(frame->temp_data, 7);
    }
    else if (frame->id == LIGHT_COMMAND)
    {
        frame->data[7] = (frame->data[7] + 1) & 0xFF;
        frame->temp_data[7] = (frame->temp_data[7] + 1) & 0xFF;
    }
    else if (frame->id == REMOTE_COMMAND)
    {
        frame->data[7] = (frame->data[7] + 1) & 0xFF;
        frame->temp_data[7] = (frame->temp_data[7] + 1) & 0xFF;
    }
}

/**
 * 主发送循环线程
 */
void *send_void_loop_frame_thread(void *arg)
{
    RX_CTX *ctx = (RX_CTX *)arg;
    VCI_CAN_OBJ can[RX_BUFF_SIZE]; // 接收结构体
    uint64_t current_time_ms = get_current_time_ms();
    pthread_mutex_lock(&frame_mutex);
    for (int i = 0; i < FRAME_COUNT; i++)
    {
        control_frames[i].next_send_time =
            current_time_ms + control_frames[i].interval_ms;
    }
    pthread_mutex_unlock(&frame_mutex);
    while (server_running)
    {
        uint64_t current_time_ms = get_current_time_ms();
        pthread_mutex_lock(&frame_mutex);
        // 生成临时帧（控制帧的拷贝，可被临时修改）

        for (int i = 0; i < FRAME_COUNT; i++)
        {
            ControlFrame *frame = &control_frames[i];
            if (current_time_ms >= frame->next_send_time - 1)
            {
                // 更新心跳值
                update_heartbeat_and_xor(frame);

                // 发送控制帧
                VCI_CAN_OBJ can_obj[1];
                memset(&can_obj, 0, sizeof(can_obj));
                can_obj[0].ID = frame->id;
                can_obj[0].ExternFlag = 1;
                can_obj[0].SendType = 2;
                can_obj[0].RemoteFlag = 0;
                can_obj[0].DataLen = 8;
                if (frame->modified)
                {
                    memcpy(can_obj[0].Data, frame->temp_data, 8);
                    frame->modified = 0; // 重置标志位
                    memcpy(frame->temp_data, frame->data, 8);
                }
                else
                {
                    memcpy(can_obj[0].Data, frame->data, 8);
                }
                int sig = VCI_Transmit(ctx->DevType, ctx->DevIdx, 1, can_obj, 1);
                frame->next_send_time += frame->interval_ms;
                if (frame->next_send_time < current_time_ms)
                {
                    frame->next_send_time = current_time_ms + frame->interval_ms;
                }
            }
        }
        pthread_mutex_unlock(&frame_mutex);
        msleep(1); // 休眠1ms，避免占用过多CPU
    }
    return NULL;
}
void *rx_thread(void *data)
{
    RX_CTX *ctx = (RX_CTX *)data;
    int DevType = ctx->DevType;
    int DevIdx = ctx->DevIdx;
    int chn_idx = ctx->index;

    VCI_CAN_OBJ can[RX_BUFF_SIZE]; // 接收结构体
    int cnt = 0;                   // 接收数量
    int count = 0;                 // 缓冲区报文数量

    while (!ctx->stop && server_running)
    {
        memset(can, 0, sizeof(can));
        count =
            VCI_GetReceiveNum(DevType, DevIdx, ctx->index); // 获取缓冲区报文数量
        if (count > 0)
        {
            int rcount = VCI_Receive(DevType, DevIdx, ctx->index, can, RX_BUFF_SIZE,
                                     RX_WAIT_TIME); // 读报文
            for (int i = 0; i < rcount; i++)
            {
                // 将数据发送给64位程序
                time_t now = time(NULL);
                can[i].TimeStamp = (int)now;
                char send_buffer[256];
                snprintf(send_buffer, sizeof(send_buffer),
                         "time:%d id:0x%x data:", can[i].TimeStamp,
                         can[i].ID & 0x1fffffff);
                for (int j = 0; j < can[i].DataLen; j++)
                {
                    char byte_str[4];
                    snprintf(byte_str, sizeof(byte_str), "%02X", can[i].Data[j]);
                    strncat(send_buffer, byte_str,
                            sizeof(send_buffer) - strlen(send_buffer) - 1);
                }
                strncat(send_buffer, "\n",
                        sizeof(send_buffer) - strlen(send_buffer) - 1);
                for (int i = 0; i < sizeof(send_buffer); i++)
                {
                    // printf("%c", send_buffer[i]); // 使用 %c 格式化输出单个字符
                }

                if (client_read_fd > 0)
                {
                    int ret = write(client_read_fd, send_buffer, strlen(send_buffer));
                    if (ret <= 0)
                    {
                        perror("write error");
                        close(client_read_fd);
                        client_read_fd = -1;
                    }
                }
            }
        }
        msleep(10);
    }
    pthread_exit(0);
}

void stop_command(RX_CTX rx_ctx[2], pthread_t rx_threads[2], int DevType, int DevIdx, pthread_t cmd_thread, pthread_t send_thread)
{

    control_frames[2].data[0] = 0x00;
    control_frames[2].data[4] = 0x00;
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        rx_ctx[i].stop = 1;                // 标记线程需要停止
        pthread_join(rx_threads[i], NULL); // 等待线程退出
    }
    // 复位通道
    msleep(80);         // 等待线程退出
    server_running = 0; // 通知线程退出
    msleep(80);         // 等待线程退出

    pthread_cancel(cmd_thread);
    pthread_join(cmd_thread, NULL);
    printf("cmd thread join success!\n");
    pthread_cancel(send_thread);
    pthread_join(send_thread, NULL);
    printf("send thread join success!\n");
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        if (!VCI_ResetCAN(DevType, DevIdx, i))
            printf("ResetCAN(%d) fail\n", i);
        else
            printf("ResetCAN(%d) success!\n", i);
    }

    // 关闭套接字
    close(server_fd);
    if (client_fd > 0)
        close(client_fd);
    unlink(SOCKET_PATH);
    close(server_read_fd);
    if (client_read_fd > 0)
    {
        close(client_read_fd);
    }
    unlink(SOCKET_READ_PATH);

    // 关闭设备
    if (!VCI_CloseDevice(DevType, DevIdx))
        printf("关闭设备失败\n");
    else
        printf("关闭设备成功\n");
}
void *write_socket_server_thread(void *arg)
{

    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);
    while (server_running)
    {
        int tmp_fd = accept(server_read_fd, (struct sockaddr *)&client_addr, &client_len);
        if (tmp_fd < 0)
        {
            if (!server_running)
                break;
            perror("accept error on server_read_fd");
            continue;
        }
        printf("Rx 数据通道客户端已连接。\n");

        // 如果你只允许一个客户端，则可以将其保存到 client_read_fd
        // 如果你要支持多个，可以用别的方式管理
        client_read_fd = tmp_fd;
        // 简单阻塞在这里等待对端断开，如果想一边 accept 新连接，一边给旧连接发数据，就要更复杂的管理
        // 这里仅演示一种最简单的模式
        // 在这个例子里，只要连上，rx_thread 就可以使用 client_read_fd 写数据
        // 直到对端断开连接
        char dummy[16];
        while (server_running)
        {
            ssize_t n = read(client_read_fd, dummy, sizeof(dummy));
            if (n <= 0)
            {
                printf("Rx 数据通道客户端断开。\n");
                close(client_read_fd);
                client_read_fd = -1;
                break;
            }
            // 如果还想处理对端发来的东西，就在此解析 dummy
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    // 波特率，十六进制数字，可以由“zcanpro 波特率计算器”计算得出
    int Baud = 0x1c01;       // 波特率 0x1400-1M(75%), 0x1c00-500k(87.5%),
                             // 0x1c01-250k(87.5%), 0x1c03-125k(87.5%)
    int DevType = USBCAN_II; // 设备类型号
    int DevIdx = 0;          // 设备索引号

    RX_CTX rx_ctx[MAX_CHANNELS];        // 接收线程上下文
    pthread_t rx_threads[MAX_CHANNELS]; // 接收线程

    // 打开设备
    if (!VCI_OpenDevice(DevType, DevIdx, 0))
    {
        printf("打开设备失败\n");
        return 0;
    }
    printf("打开设备成功\n");

    // 初始化，启动通道
    for (int i = 0; i < MAX_CHANNELS; i++)
    {
        VCI_INIT_CONFIG config;
        config.AccCode = 0;
        config.AccMask = 0xffffffff;
        config.Reserved = 0;
        config.Filter = 1;
        config.Timing0 = Baud & 0xff; // 0x00
        config.Timing1 = Baud >> 8;   // 0x1c
        config.Mode = 0;

        if (!VCI_InitCAN(DevType, DevIdx, i, &config))
        {
            printf("InitCAN(%d)失败\n", i);
            return 0;
        }
        printf("InitCAN(%d)成功\n", i);

        if (!VCI_StartCAN(DevType, DevIdx, i))
        {
            printf("StartCAN(%d)失败\n", i);
            return 0;
        }
        printf("StartCAN(%d)成功\n", i);

        rx_ctx[i].DevType = DevType;
        rx_ctx[i].DevIdx = DevIdx;
        rx_ctx[i].index = i;
        rx_ctx[i].total = 0;
        rx_ctx[i].stop = 0;
        pthread_create(&rx_threads[i], NULL, rx_thread, &rx_ctx[i]); // 创建接收线程
    }

    // 初始化Unix域套接字服务器
    init_unix_domain_socket();
    init_unix_domain_socket_read();

    // 创建命令处理线程
    pthread_t cmd_thread;
    pthread_create(&cmd_thread, NULL, command_thread,
                   &rx_ctx[0]); // 将通道信息传递给命令线程
    pthread_t send_thread;
    pthread_create(&send_thread, NULL, send_void_loop_frame_thread, &rx_ctx[0]);

    pthread_t write_srv_thread;
    pthread_create(&write_srv_thread, NULL, write_socket_server_thread, NULL);

    // 阻塞等待，按下回车键退出
    char input;
    input = getchar();
    if (input == 'q' || input == '\n')
    {
        printf("检测到退出指令，正在停止并复位设备...\n");
        // 停止线程
        stop_command(rx_ctx, rx_threads, DevType, DevIdx, cmd_thread, send_thread);
        return 0;
    }
    stop_command(rx_ctx, rx_threads, DevType, DevIdx, cmd_thread, send_thread);
    return 0;
}
