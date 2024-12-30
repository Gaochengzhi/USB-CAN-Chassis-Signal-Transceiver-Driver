# USB CAN Chassis Signal Transceiver Driver


| English | [中文版](./README_zh.md)
## Introduction

The USB CAN Chassis Signal Transceiver Driver is a CAN signal transmission and processing driver based on USB-CAN hardware implementation. It supports multi-channel communication, data distribution of control frames, and dynamic parameter modification, communicating with other applications through Unix domain socket interfaces.

As a middleware layer, you can write upper-layer application code to periodically read and send CAN signals while updating the heartbeat bit and XOR check bit.

## Quick Start

### Environment Dependencies:

Hardware Support: USB CAN card (tested with [ZLG USBCAN-II](https://www.zlg.cn/can/can/product/id/22.html))

System Support:
* UNIX environment (tested with 64-bit emulating 32-bit)
    ```bash
    sudo apt-get install build-essential
    sudo apt-get install gcc-multilib g++-multilib  # Support 32-bit compilation
    ```
* [ZLG USBCAN-II Driver](https://manual.zlg.cn/web/#/146)

## Installation
1. Compile using Makefile
    ```bash
    make 
    ```
2. Run
    Execute the generated binary file:
    ```bash
    ./main
    ```

## Testing
1. Test sending a single frame signal (hold)
    ```bash
    python3 test_send.py
    ```

    Command format:
    ```text
    send [Frame ID] [Start bit] [Length] [Data string]
    ```

    Example command:
    ```text
    send 43878911(decimal) 0 8 10100111(binary)
    ```

2. Test vehicle signals (customization requires protocol adaptation)
    Read and write signals
    ```c
    #define SOCKET_PATH "/tmp/can_socket"
    #define SOCKET_READ_PATH "/tmp/can_read_socket"
    ```

    GUI test interface
    ```bash
    python3 panel_control
    ```

## Customization Details

### Overview
This middleware is used to manage CAN bus communication, implementing command sending and receiving through Unix domain sockets. Its main functions include sending control frames, processing received data, maintaining the heartbeat mechanism, and calculating XOR checks. This document details the working mode of the middleware and how to customize related parameters according to requirements.

### Architecture
The middleware mainly consists of the following modules:

1. **Initialization Module**: Responsible for opening the CAN device, initializing the CAN channel, and setting up the Unix domain socket server.
2. **Sending Module**: Periodically sends control frames to the CAN bus, supporting heartbeat updates and dynamic data modification.
3. **Receiving Module**: Listens to data on the CAN bus and passes the received data to the upper-layer application through Unix domain sockets.
4. **Command Processing Module**: Receives commands from the client, parses them, and modifies the data of the control frames.

### Configuration Parameter Description

| Parameter Name     | Definition Location | Default Value            | Description                 |
| ------------------ | ------------------- | ------------------------ | --------------------------- |
| RX\_WAIT\_TIME     | #define macro       | 100                      | Receive wait time (ms)      |
| SOCKET\_PATH       | #define macro       | "/tmp/can\_socket"       | Command receive socket path |
| SOCKET\_READ\_PATH | #define macro       | "/tmp/can\_read\_socket" | Data send socket path       |
| FRAME\_COUNT       | #define macro       | 4                        | Control frame count         |
| Baud               | main function       | 0x1c01                   | CAN bus baud rate config    |
| DevType            | main function       | USBCAN\_II               | Device type number          |
| DevIdx             | main function       | 0                        | Device index number         |

### Customization Details

The following is a detailed explanation of the middleware's working mode and customization parameters, focusing on the key parts of heartbeat, cycle, XOR, sending, and receiving logic.

#### 1. Heartbeat Logic

The heartbeat logic is implemented in the `update_heartbeat_and_xor` function, used to update specific bytes to indicate activity or status.

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
    // Handling logic for other IDs
}
```

**Customizable Parameters**:
- Modify the update logic of specific bytes to adapt to different heartbeat mechanisms.

#### 2. Cycle Management

Cycle management is implemented through the `interval_ms` and `next_send_time` fields in the `ControlFrame` structure.

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

**Customizable Parameters**:
- Adjust `interval_ms` to change the sending frequency of each frame.

#### 3. XOR Calculation

XOR calculation is implemented in the `calculate_xor` function, used for data integrity checks.

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

**Customizable Parameters**:
- Modify or replace this function to use different error-checking mechanisms.

#### 4. Sending Logic

The sending logic is implemented in the `send_void_loop_frame_thread` function, sending frames based on interval time.

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
                // Send CAN frame
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

**Customizable Parameters**:
- Modify sending conditions or add external input conditions to dynamically adjust the sending logic.

#### 5. Receiving Logic

The receiving logic is implemented in the `rx_thread` function, processing received CAN messages and sending them to the socket.

```c
void *rx_thread(void *data)
{
    while (!ctx->stop && server_running)
    {
        int rcount = VCI_Receive(DevType, DevIdx, ctx->index, can, RX_BUFF_SIZE, RX_WAIT_TIME);
        for (int i = 0; i < rcount; i++)
        {
            // Process received messages and send to socket
            if (client_read_fd > 0)
            {
                int ret = write(client_read_fd, send_buffer, strlen(send_buffer));
                if (ret <= 0)
                {
                    // Handle error
                }
            }
        }
        msleep(10);
    }
    pthread_exit(0);
}
```

**Customizable Parameters**:
- Modify message processing logic or add filtering conditions to adapt to specific requirements.

#### 6. Command Processing

Command processing is implemented in the `command_thread` and `process_command` functions, handling commands from the socket and updating control frames.

```c
void *command_thread(void *data)
{
    while (server_running)
    {
        // Accept client connection and read command
        process_command(buffer);
        msleep(1);
    }
    return NULL;
}
```

```c
void process_command(char *cmd)
{
    // Parse command and update control frame
}
```