import socket
import threading
import tkinter as tk
from tkinter import messagebox

SOCKET_PATH = "/tmp/can_socket"

# 全局变量，用于存储各个参数的值
params = {
    "steering_angle": 43,
    "acceleration": 0,
    "elec_brake": 0,
    "gear": 0,
    "emergency_braking": 0,
    # 其他参数...
}


class App:
    def __init__(self, root):
        self.root = root
        self.sock = None  # 初始时未连接
        self.connected = False
        self.root.title("控制界面")
        self.create_widgets()
        # 设置初始零点值
        self.steering_zero_point = 0
        self.acceleration_zero_point = 0
        # 启动定时发送线程
        self.root.after(3, self.periodic_send)

    def create_widgets(self):
        # 创建连接按钮
        self.connect_btn = tk.Button(
            self.root, text="连接服务器", command=self.connect_server
        )
        self.connect_btn.pack()

        # 创建steering_angle的滑块和校正按钮
        self.steering_angle_label = tk.Label(self.root, text="方向盘角度 (deg)")
        self.steering_angle_label.pack()
        self.steering_angle_scale = tk.Scale(
            self.root,
            from_=22,
            to=65,
            orient=tk.HORIZONTAL,
            length=360,
            resolution=1,
        )
        self.steering_angle_scale.set(43)  
        self.steering_angle_scale.pack()
        self.steering_zero_btn = tk.Button(
            self.root, text="矫正零点", command=self.reset_steering_angle
        )
        self.steering_zero_btn.pack()

        # 显示当前方向盘角度
        self.steering_value_label = tk.Label(self.root, text="当前角度: 0")
        self.steering_value_label.pack()

        # 创建acceleration的滑块和校正按钮
        self.acceleration_label = tk.Label(self.root, text="加速度 (m/s²)")
        self.acceleration_label.pack()
        self.acceleration_scale = tk.Scale(
            self.root,
            from_=-128,
            to=127,
            orient=tk.HORIZONTAL,
            length=300,
            resolution=1,
        )
        self.acceleration_scale.set(0)
        self.acceleration_scale.pack()
        self.acceleration_zero_btn = tk.Button(
            self.root, text="矫正零点", command=self.reset_acceleration
        )
        self.acceleration_zero_btn.pack()

        # 显示当前加速度
        self.acceleration_value_label = tk.Label(self.root, text="当前加速度: 0")
        self.acceleration_value_label.pack()

        # 创建elec_brake的按钮
        self.elec_brake_label = tk.Label(self.root, text="电制动")
        self.elec_brake_label.pack()
        self.elec_brake_var = tk.IntVar(value=0)
        self.elec_brake_frame = tk.Frame(self.root)
        self.elec_brake_frame.pack()
        elec_brake_options = {
            0: "No Control",
            1: "Parking",
            2: "Releasing",
            3: "Emergency braking",
        }
        for val, text in elec_brake_options.items():
            tk.Radiobutton(
                self.elec_brake_frame,
                text=text,
                variable=self.elec_brake_var,
                value=val,
            ).pack(side=tk.LEFT)

        # 创建gear的按钮
        self.gear_label = tk.Label(self.root, text="档位")
        self.gear_label.pack()
        self.gear_var = tk.IntVar(value=0)
        self.gear_frame = tk.Frame(self.root)
        self.gear_frame.pack()
        gear_options = {0: "N", 1: "D", 2: "R"}
        for val, text in gear_options.items():
            tk.Radiobutton(
                self.gear_frame, text=text, variable=self.gear_var, value=val
            ).pack(side=tk.LEFT)

        # 创建emergency_braking的按钮
        self.emergency_braking_label = tk.Label(self.root, text="紧急制动")
        self.emergency_braking_label.pack()
        self.emergency_braking_var = tk.IntVar(value=0)
        self.emergency_braking_frame = tk.Frame(self.root)
        self.emergency_braking_frame.pack()
        tk.Radiobutton(
            self.emergency_braking_frame,
            text="No",
            variable=self.emergency_braking_var,
            value=0,
        ).pack(side=tk.LEFT)
        tk.Radiobutton(
            self.emergency_braking_frame,
            text="Emergency Braking",
            variable=self.emergency_braking_var,
            value=1,
        ).pack(side=tk.LEFT)

        # 添加其他控件，例如各种灯的开关
        # 创建示例——大灯控制
        self.big_light_label = tk.Label(self.root, text="大灯控制")
        self.big_light_label.pack()
        self.big_light_var = tk.IntVar(value=0)
        self.big_light_frame = tk.Frame(self.root)
        self.big_light_frame.pack()
        big_light_options = {
            0: "no action",
            1: "open close light",
            2: "open far light",
            3: "close",
        }
        for val, text in big_light_options.items():
            tk.Radiobutton(
                self.big_light_frame, text=text, variable=self.big_light_var, value=val
            ).pack(side=tk.LEFT)

        # 您可以继续添加其他控件，按照消息定义进行

    def connect_server(self):
        if not self.connected:
            # 创建 Unix 域套接字
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                # 尝试连接服务器
                print(f"[INFO] 正在连接到服务器: {SOCKET_PATH}")
                self.sock.connect(SOCKET_PATH)
                print("[INFO] 已成功连接到服务器")
                self.connected = True
                self.connect_btn.config(text="断开连接")
                # 启动接收线程
                self.recv_thread = threading.Thread(target=self.receive_data)
                self.recv_thread.daemon = True
                self.recv_thread.start()
            except Exception as e:
                print(f"[ERROR] 无法连接到服务器: {e}")
                messagebox.showerror("连接错误", f"无法连接到服务器: {e}")
        else:
            # 断开连接
            self.sock.close()
            self.connected = False
            self.connect_btn.config(text="连接服务器")
            print("[INFO] 已断开连接")

    def reset_steering_angle(self):
        # 将当前值设定为新的零点
        self.steering_zero_point = self.steering_angle_scale.get()
        self.steering_angle_scale.set(0)
        self.update_steering_label()

    def reset_acceleration(self):
        # 将当前值设定为新的零点
        self.acceleration_zero_point = self.acceleration_scale.get()
        self.acceleration_scale.set(0)
        self.update_acceleration_label()

    def update_steering_label(self):
        # 更新显示的方向盘角度
        current_value = self.steering_zero_point + self.steering_angle_scale.get()
        self.steering_value_label.config(text=f"当前角度: {current_value}")

    def update_acceleration_label(self):
        # 更新显示的加速度
        current_value = self.acceleration_zero_point + self.acceleration_scale.get()
        self.acceleration_value_label.config(text=f"当前加速度: {current_value}")

    def periodic_send(self):
        # 每隔3ms发送一次数据
        # 更新显示的值
        self.update_steering_label()
        self.update_acceleration_label()

        if self.connected:
            # 获取当前参数值
            steering_angle = self.steering_zero_point + self.steering_angle_scale.get()
            acceleration = self.acceleration_zero_point + self.acceleration_scale.get()
            elec_brake = self.elec_brake_var.get()
            gear = self.gear_var.get()
            emergency_braking = self.emergency_braking_var.get()
            big_light = self.big_light_var.get()
            # 获取其他参数...

            # 构建并发送steering_angle的指令
            steering_angle_raw = int(
                steering_angle 
            )  # 使用factor和offset计算原始值
            steering_angle_data = format(steering_angle_raw, "b").zfill(
                8
            )  # 转为2位的hex字符串
            steering_angle_cmd = f"send 402763936 32 8 {steering_angle_data}\n"
            self.send_data(steering_angle_cmd)

            # 构建并发送402895008的指令
            acceleration_raw = int((acceleration - (-128)) / 1)
            acceleration_data = format(acceleration_raw, "b").zfill(8)
            # 组装其他字段
            elec_brake_data = elec_brake & 0b11  # 2位
            gear_data = gear & 0b1111  # 4位
            emergency_braking_data = emergency_braking & 0b1  # 1位

            # 构建位字段
            data_bits = ["0"] * 64  # 初始化64位的数据位
            # acceleration
            acc_bin = format(acceleration_raw, "b").zfill(8)
            for i in range(8):
                data_bits[0 + i] = acc_bin[i]
            # elec_brake
            elec_brake_bin = format(elec_brake_data, "02b").zfill(2)
            for i in range(2):
                data_bits[10 + i] = elec_brake_bin[i]
            # gear
            gear_bin = format(gear_data, "04b").zfill(4)
            for i in range(4):
                data_bits[12 + i] = gear_bin[i]
            # emergency_braking
            emergency_braking_bin = format(emergency_braking_data, "01b")
            data_bits[24] = emergency_braking_bin

            # 将位字段转为字节字符串
            data_bytes = []
            for i in range(0, 64, 8):
                byte_str = "".join(data_bits[i : i + 8])
                data_bytes.append(format(int(byte_str, 2), "02x"))

            data_field = "".join(data_bytes)
            # can_cmd = f"send 402895008 1 8 {data_field}"
            # self.send_data(can_cmd)

            # 构建并发送403026080的指令（包括各种灯的控制）
            # 类似的方法，按照定义的start_bit和length填充数据位
            # 示例：big_light控制
            big_light_data = big_light & 0b11  # 2位
            data_bits_403026080 = ["0"] * 64  # 初始化64位的数据位
            big_light_bin = format(big_light_data, "02b").zfill(2)
            for i in range(2):
                data_bits_403026080[16 + i] = big_light_bin[i]
            # 添加其他灯的控制...

            # 将位字段转为字节字符串
            data_bytes_403026080 = []
            for i in range(0, 64, 8):
                byte_str = "".join(data_bits_403026080[i : i + 8])
                data_bytes_403026080.append(format(int(byte_str, 2), "02x"))

            data_field_403026080 = "".join(data_bytes_403026080)
            # can_cmd_403026080 = f"send 403026080 1 8 {data_field_403026080}"
            # self.send_data(can_cmd_403026080)

        # 设定下一次调用
        self.root.after(10, self.periodic_send)

    def send_data(self, cmd):
        try:
            self.sock.sendall(cmd.encode("utf-8"))  # 向服务器发送数据
        except Exception as e:
            print(f"[ERROR] 发送数据异常: {e}")

    def receive_data(self):
        try:
            while self.connected:
                data = self.sock.recv(1024)  # 接收数据的缓冲区大小
                if not data:
                    print("[INFO] 服务器断开连接或无数据可接收")
                    break
                # 处理接收到的数据
                # print(f"[RECEIVED]\n{data.decode('utf-8')}")
        except Exception as e:
            print(f"[ERROR] 接收线程异常: {e}")
        finally:
            print("[INFO] 接收线程退出")


def main():
    # 创建主窗口
    root = tk.Tk()
    app = App(root)
    try:
        root.mainloop()
    except KeyboardInterrupt:
        print("\n[INFO] 用户中断程序，退出")
    except Exception as e:
        print(f"[ERROR] 主线程异常: {e}")


if __name__ == "__main__":
    main()
