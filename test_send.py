import socket
import os
import threading
import time

SOCKET_PATH = "/tmp/can_socket"

# 定义接收线程的函数
def receive_data(sock):
    try:
        while True:
            data = sock.recv(1024)  # 接收数据的缓冲区大小
            if not data:
                print("[INFO] 服务器断开连接或无数据可接收")
                break
            # print(f"[RECEIVED]\n{data.decode('utf-8')}")
    except Exception as e:
        print(f"[ERROR] 接收线程异常: {e}")
    finally:
        print("[INFO] 接收线程退出")

# 定义发送数据的函数
def send_data(sock):
    try:
        while True:
            command = input("[SEND] 输入要发送的指令 (格式: send id data 或 q 退出): ")
            if command.strip().lower() == "q":  # 输入 q 退出发送线程
                print("[INFO] 退出发送线程")
                break
            sock.sendall(command.encode('utf-8'))  # 向服务器发送数据
    except Exception as e:
        print(f"[ERROR] 发送线程异常: {e}")
    finally:
        print("[INFO] 发送线程退出")

def main():
    # 检查前一次退出是否没有清理套接字路径

    # 创建 Unix 域套接字
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client_sock:
        try:
            # 尝试连接服务器
            print(f"[INFO] 正在连接到服务器: {SOCKET_PATH}")
            client_sock.connect(SOCKET_PATH)
            print("[INFO] 已成功连接到服务器")

            # 使用多线程处理接收和发送
            recv_thread = threading.Thread(target=receive_data, args=(client_sock,))
            recv_thread.daemon = True  # 设置为守护进程，主线程结束时随之退出
            recv_thread.start()

            send_thread = threading.Thread(target=send_data, args=(client_sock,))
            send_thread.start()

            # 等待发送线程结束
            send_thread.join()

        except KeyboardInterrupt:
            print("\n[INFO] 用户中断程序，退出")
        except Exception as e:
            print(f"[ERROR] 主线程异常: {e}")
        finally:
            print("[INFO] 关闭客户端套接字")

if __name__ == "__main__":
    main()
