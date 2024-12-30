#!/bin/bash

LOGIN_LOG="/var/log/wtmp"  # 登录日志路径（不能直接读取，需用 last 命令解析）
CMD_LOG="/tmp/command-logs/skywell2/command.log"  # 用户命令的日志文件（假定是普通文本文件）

declare -A LAST_CMD_LINE  # 追踪每个用户的命令行号

monitor_logins_and_commands() {
    # 初始化上次检查登录日志的最后一条记录
    local last_login_record="$(last -F | head -n 1)"  # 获取最后一条登录记录

    # 使用 inotifywait 来监控文件修改
    while inotifywait -q -e modify "$LOGIN_LOG" "$CMD_LOG"; do
        # 检查登录日志
        local current_login_record="$(last -F | head -n 1)"  # 获取最新登录记录
        if [[ "$current_login_record" != "$last_login_record" ]]; then
            echo "New login detected: $current_login_record"
            last_login_record="$current_login_record"
        fi

        # 检查用户命令日志
        for user in "${!LAST_CMD_LINE[@]}"; do
            local new_cmd_lines=$(tail -n +$((LAST_CMD_LINE[$user] + 1)) "$CMD_LOG")
            if [[ -n "$new_cmd_lines" ]]; then
                echo "User $user commands:"
                echo "$new_cmd_lines"
                LAST_CMD_LINE[$user]=$(wc -l < "$CMD_LOG")
            fi
        done
    done
}

monitor_logins_and_commands
