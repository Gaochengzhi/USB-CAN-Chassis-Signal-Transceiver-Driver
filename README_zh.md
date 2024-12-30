# 线控底盘信号收发驱动
## 介绍

线控底盘信号收发驱动是一个基于 USB-CAN 硬件实现的 CAN 信号传输与处理的驱动程序。它支持多通道通信、控制帧的数据分发及动态参数修改，通过 Unix 域套接字接口与其他应用程序通信。

该程序作为一个中间层，您可以编写上层应用代码来周期性地读取和发送 CAN 信号，同时更新心跳位和异或检测位。



## 快速开始

### 环境依赖：

硬件支持：USB CAN 卡 (测试使用 [ZLG USBCAN-II](https://www.zlg.cn/can/can/product/id/22.html) )

系统支持：
* UNIX 环境（测试使用64位模拟32位）
    ```bash
    sudo apt-get install build-essential
    sudo apt-get install gcc-multilib g++-multilib  # 支持 32 位编译
    ```
* [ZLG USBCAN-II 驱动](https://manual.zlg.cn/web/#/146)

## 安装
1. 编译,使用 Makefile
    ```bash
    make 
    ```
2. 运行
    执行生成的二进制文件：
    ```bash
    ./main
    ```
## 测试
1. 测试发送单帧信号（保持）
    ```bash
    python3 test_send.py
    ```

    指令格式：
    ```text
    send [帧ID] [开始位] [长度] [数据串]
    ```

    示例指令：
    ```text
    send 43878911(十进制) 0 8 10100111(二进制)
    ```
2. 测试整车信号（定制需要适配协议）
    读写信号
    ```c
    #define SOCKET_PATH "/tmp/can_socket"
    #define SOCKET_READ_PATH "/tmp/can_read_socket"
    ```

    GUI 测试界面
    ```bash
    python3 panel_control
    ```


## 定制化详细说明

### 概述
本中间件用于管理CAN总线通信，通过Unix域套接字实现命令的发送与接收。其主要功能包括控制帧的发送、接收数据的处理、心跳机制的维护以及异或校验的计算。本文档详细说明了中间件的工作模式以及如何根据需求定制相关参数。

### 架构
中间件主要由以下几个模块组成：

1. **初始化模块**：负责打开CAN设备，初始化CAN通道，并设置Unix域套接字服务器。
2. **发送模块**：通过定时发送控制帧到CAN总线，支持心跳更新和数据的动态修改。
3. **接收模块**：监听CAN总线上的数据，并将接收到的数据通过Unix域套接字传递给上层应用。
4. **命令处理模块**：接收来自客户端的命令，解析并修改控制帧的数据。

### 配置参数说明

| 参数名称           | 定义位置      | 默认值                   | 说明                 |
| ------------------ | ------------- | ------------------------ | -------------------- |
| RX\_WAIT\_TIME     | #define宏定义 | 100                      | 接收等待时间（毫秒） |
| SOCKET\_PATH       | #define宏定义 | "/tmp/can\_socket"       | 命令接收套接字路径   |
| SOCKET\_READ\_PATH | #define宏定义 | "/tmp/can\_read\_socket" | 数据发送套接字路径   |
| FRAME\_COUNT       | #define宏定义 | 4                        | 控制帧数量           |
| Baud               | main函数      | 0x1c01                   | CAN总线波特率配置    |
| DevType            | main函数      | USBCAN\_II               | 设备类型号           |
| DevIdx             | main函数      | 0                        | 设备索引号           |

### 定制化详细说明

以下是针对middleware的工作模式和定制参数的详细说明，重点关注心跳、周期、异或、发送和接收逻辑的关键部分。

#### 1. 心跳逻辑

心跳逻辑在`update_heartbeat_and_xor`函数中实现，用于更新特定字节以指示活动或状态。

```c
void update_heartbeat_and_xor(ControlFrame *frame)
{
    if (frame->id == ESP_COMMAND)
    {
        frame->data[1] = (frame->data[1] + 1) & 0xFF;
        frame->data[7] = calculate_xor(frame->data, 7);
        frame->temp_data[1] = (frame->temp_data[1] + 1) & 0xFF;
        frame->temp_data[7] = calculate_xor(frame->temp_data, 7);
    }
    // 其他ID的处理逻辑
}
```

**可定制参数**:
- 修改特定字节的更新逻辑，以适应不同的心跳机制。

#### 2. 周期管理

周期管理通过`ControlFrame`结构体中的`interval_ms`和`next_send_time`字段实现。

```c
typedef struct
{
    UINT id;
    int modified;
    uint8_t data[8];
    uint8_t interval_ms;
    uint64_t next_send_time;
    uint8_t temp_data[8];
} ControlFrame;
```

**可定制参数**:
- 调整`interval_ms`以更改每个帧的发送频率。

#### 3. XOR计算

XOR计算在`calculate_xor`函数中实现，用于数据完整性检查。

```c
uint8_t calculate_xor(uint8_t *data, int len)
{
    uint8_t xor_value = 0;
    for (int i = 0; i < len; i++)
    {
        xor_value ^= data[i];
    }
    return xor_value;
}
```

**可定制参数**:
- 修改或替换此函数以使用不同的错误检查机制。

#### 4. 发送逻辑

发送逻辑在`send_void_loop_frame_thread`函数中实现，基于间隔时间发送帧。

```c
void *send_void_loop_frame_thread(void *arg)
{
    while (server_running)
    {
        uint64_t current_time_ms = get_current_time_ms();
        pthread_mutex_lock(&frame_mutex);
        for (int i = 0; i < FRAME_COUNT; i++)
        {
            ControlFrame *frame = &control_frames[i];
            if (current_time_ms >= frame->next_send_time - 1)
            {
                update_heartbeat_and_xor(frame);
                // 发送CAN帧
                VCI_Transmit(ctx->DevType, ctx->DevIdx, 1, can_obj, 1);
                frame->next_send_time += frame->interval_ms;
            }
        }
        pthread_mutex_unlock(&frame_mutex);
        msleep(1);
    }
    return NULL;
}
```

**可定制参数**:
- 修改发送条件或添加外部输入条件以动态调整发送逻辑。

#### 5. 接收逻辑

接收逻辑在`rx_thread`函数中实现，处理接收到的CAN消息并发送到套接字。

```c
void *rx_thread(void *data)
{
    while (!ctx->stop && server_running)
    {
        int rcount = VCI_Receive(DevType, DevIdx, ctx->index, can, RX_BUFF_SIZE, RX_WAIT_TIME);
        for (int i = 0; i < rcount; i++)
        {
            // 处理接收到的消息并发送到套接字
            if (client_read_fd > 0)
            {
                int ret = write(client_read_fd, send_buffer, strlen(send_buffer));
                if (ret <= 0)
                {
                    // 处理错误
                }
            }
        }
        msleep(10);
    }
    pthread_exit(0);
}
```

**可定制参数**:
- 修改消息处理逻辑或添加过滤条件以适应特定需求。

#### 6. 命令处理

命令处理在`command_thread`和`process_command`函数中实现，处理来自套接字的命令并更新控制帧。

```c
void *command_thread(void *data)
{
    while (server_running)
    {
        // 接受客户端连接并读取命令
        process_command(buffer);
        msleep(1);
    }
    return NULL;
}
```

```c
void process_command(char *cmd)
{
    // 解析命令并更新控制帧
}
```


