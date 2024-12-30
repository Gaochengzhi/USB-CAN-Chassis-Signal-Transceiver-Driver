import yaml
import json
import struct
import datetime
from datetime import datetime, timezone

def parse_data_from_lookup(data_bits, lookup):
    """
    根据 lookup.yaml 的定义解析数据
    Args:
        data_bits: 数据的比特流，存储为字符串，如"00010000 01111111 01111111 10011100..."。
        lookup: 针对特定ID的解析规则，包含start_bit、length、factor等定义。
    Returns:
        解析后的字典，包含解析出的每个字段的名称以及对应的值。
    """
    result = {}
    # 将Data的比特位拆解成连续的字符串并转为小端模式
    bitstream = "".join(data_bits.split())  # 去掉空格形成连续的比特流
    byte_count = len(bitstream) // 8  # 一共多少字节

    # 小端模式重组字节流，8位一组提取并倒序排列
    reordered_bits = ""
    # for i in range(byte_count):
    #     reordered_bits = bitstream[i * 8:(i + 1) * 8] + reordered_bits
    for i in range(byte_count):
        reordered_bits += bitstream[i * 8:(i + 1) * 8]

    # 遍历lookup定义，解析各字段
    for field in lookup:
        start_bit = field['start_bit']
        length = field['length']
        factor = field.get('factor', 1)
        offset = field.get('offset', 0)
        field_type = field.get('type', 'numeric')
        name = field['name']

        # 提取目标比特段
        raw_bits = reordered_bits[start_bit:start_bit + length]
        value = int(raw_bits, 2)  # 把二进制转换为整数

        # 根据字段类型解析
        if field_type == 'enum':  # 枚举类型
            try:
                value = field['dic'].get(value, "Unknown")
            except:
                print("wrong")
                value = 0
        elif field_type == 'numeric':  # 数字类型
            value = value * factor + offset
            # value = (value - offset)/ factor

        # 保存结果
        result[name] = value

    return result


def parse_record(file_name, lookup_file, output_file):
    """
    主函数：解析record.txt并生成JSON
    Args:
        file_name: record.txt文件名
        lookup_file: lookup.yaml文件名
        output_file: 输出的JSON文件名
    """
    # 加载 lookup.yaml 文件
    with open(lookup_file, 'r') as f:
        lookup_data = yaml.safe_load(f)

    results = []

    # 逐行解析 record.txt 文件
    with open(file_name, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # 解析行数据
            try:
                parts = line.split()
                timestamp = parts[0][1:-1]  # 去掉中括号的时间戳
                frame_type = parts[4]  # 标准帧 or 扩展帧
                frame_id = parts[3]  # 提取ID
                data = " ".join(parts[6:])  # 提取Data位

                frame_id_upper = int(frame_id, 16) # 转为大写匹配YAML中定义
                # lookup_data_lower = {str(key).lower(): value for key, value in lookup_data.items()}
                

                # 检查ID是否在lookup.yaml中
                if frame_id_upper in lookup_data:
                    lookup = lookup_data[frame_id_upper]  # 根据ID找到对应的解析规则
                    parsed_data = parse_data_from_lookup(data, lookup)  # 解析具体字段
                    
                    timestamp_str = datetime.fromtimestamp(int(timestamp), tz=timezone.utc).isoformat()
                    # 构造解析后的结果
                    result = {
                        "timestamp":timestamp,
                        "id": frame_id,
                        "type": frame_type,
                        "data": parsed_data
                    }
                    results.append(result)
                else:
                    # 如果ID没有在lookup文件里，跳过
                    print(f"{hex(frame_id_upper)}")

                    # print(f"ID {hex(frame_id_upper)} 不在 lookup.yaml 文件中，跳过该条记录。")
            except:
                print("ERROW",line)
    # 输出结果到JSON文件
    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(results, f, indent=4, ensure_ascii=False)

    print(f"解析完成，结果已保存到 {output_file} 文件中。")


if __name__ == '__main__':
    # 解析record.txt，并保存为JSON
   parse_record('record.txt', 'lookup.yaml', 'output.json')
