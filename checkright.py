import re

# 定义帧的周期（毫秒）
TIME_INTERVALS = {
    "1801B0A0": 20,
    "1803B0A0": 20,
    "1805B0A0": 50,
    "1807B0A0": 100
}


def calculate_xor(data_bytes):
    """
    计算数据的8位异或值。
    """
    xor_result = 0
    for byte in data_bytes[:-1]:  # 排除最后的异或字节
        xor_result ^= int(byte, 16)
    return xor_result


def parse_line(line):
    """
    解析一行日志记录，提取时间戳、ID和数据（以字节作为字符串列表返回）。
    """
    match = re.match(r"time: (\d+) id: ([0-9A-F]+), data: ((?:[0-9A-F]+ )+)", line)
    if match:
        timestamp = int(match.group(1))
        frame_id = match.group(2)
        data = match.group(3).strip().split(" ")
        return timestamp, frame_id, data
    return None, None, None


def check_frames(filename):
    """
    检查帧的发送周期、心跳位和异或解码。并输出每帧的时间间隔与心跳值。
    """
    last_timestamps = {}  # 用于记录每个ID的上一次时间戳
    last_heartbeats = {}  # 用于记录每个ID的上一次心跳值
    errors = []  # 用于记录所有发现的错误

    print(f"{'ID':<12} {'Time Diff':<12} {'Heartbeat':<12} {'Expected HB':<12} {'Status':<12}")

    with open(filename, "r") as file:
        for line in file:
            timestamp, frame_id, data = parse_line(line)
            if not frame_id:
                continue

            # 初始化相关变量
            time_diff = "-"
            heartbeat = "-"
            expected_heartbeat = "-"
            status = "OK"

            # 检查时间间隔
            if frame_id in last_timestamps:
                time_diff = timestamp - last_timestamps[frame_id]
                expected_interval = TIME_INTERVALS.get(frame_id, None)
                if expected_interval and abs(time_diff - expected_interval) > 1:
                    status = "ERR"
                    errors.append(f"周期错误: {frame_id} 在 {timestamp}ms，实际间隔 {time_diff}ms，期待间隔 {expected_interval}ms")

            last_timestamps[frame_id] = timestamp  # 更新时间戳

            # 检查心跳位
            if frame_id in ["1801B0A0", "1803B0A0", "1805B0A0", "1807B0A0"]:
                if frame_id == "1801B0A0":
                    heartbeat = int(data[1], 16)
                elif frame_id == "1803B0A0":
                    heartbeat = int(data[2], 16)
                elif frame_id == "1805B0A0":
                    heartbeat = int(data[7], 16)
                elif frame_id == "1807B0A0":
                    heartbeat = int(data[7], 16)

                # 计算心跳递增关系
                expected_heartbeat = "-"
                if frame_id in last_heartbeats:
                    expected_heartbeat = (last_heartbeats[frame_id] + 1) % 256  # 心跳溢出处理
                    if heartbeat != expected_heartbeat:
                        status = "ERR"
                        errors.append(
                            f"心跳错误: {frame_id} 在 {timestamp}ms, 当前值 {heartbeat}, 期待值 {expected_heartbeat}")

                last_heartbeats[frame_id] = heartbeat  # 更新心跳值

            # 检查异或值
            if frame_id in ["1801B0A0", "1803B0A0"]:
                calculated_xor = calculate_xor(data)  # 计算异或值
                xor_value = int(data[-1], 16)
                if calculated_xor != xor_value:
                    status = "ERR"
                    errors.append(f"异或错误: {frame_id} 在 {timestamp}ms，计算值 {calculated_xor:02X}，数据值 {xor_value:02X}")

            # 打印当前帧信息
            print(f"{frame_id:<12} {time_diff:<12} {heartbeat:<12} {expected_heartbeat:<12} {status:<12}")

    return errors


if __name__ == "__main__":
    # 替换为你的文件名
    filename = "right.txt"
    errors = check_frames(filename)

    print("\n结果总结：")
    if errors:
        print("发现以下错误：")
        for error in errors:
            print(error)
    else:
        print("所有帧验证通过！")
