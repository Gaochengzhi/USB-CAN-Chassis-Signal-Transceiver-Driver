def convert_timestamp(first_timestamp, current_timestamp):
    # 计算时间差（秒）
    time_diff = (int(current_timestamp) - int(first_timestamp)) / 1000
    # 格式化为6位小数
    return "{:.6f}".format(time_diff)

def convert_can_data(input_file, output_file):
    first_timestamp = None
    with open(input_file, 'r') as infile, open(output_file, 'w') as outfile:
        for line in infile:
            parts = line.strip().split(' ')
            if len(parts) >= 14:  # 确保行有足够的数据
                # 获取时间戳
                timestamp = parts[0]
                
                # 记录第一个时间戳
                if first_timestamp is None:
                    first_timestamp = timestamp
                
                data_bytes = " ".join(["{:02x}".format(int(x, base=16)) for x in parts[6:]])
                # 计算时间差
                time_diff = convert_timestamp(first_timestamp, timestamp)
                
                # 构建新的行格式
                # 替换Tx为Rx
                # direction = "Rx"
                
                # 构建数据字节部分
                
                # 组合新的行
                new_line = f"{time_diff} {parts[1]} {parts[2]} {parts[3]} {parts[4]} {parts[5]} {parts[6]} {data_bytes}"
                outfile.write(new_line + "\n")

# 使用示例
input_file = "senddebug.txt"    # 原始数据文件
output_file = "send.asc"        # 输出文件

try:
    convert_can_data(input_file, output_file)
    print("转换完成！")
except Exception as e:
    print(f"转换过程中出现错误: {str(e)}")

