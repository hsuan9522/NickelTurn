#include "nickelturn_log.h"

#include <NickelHook.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define NICKELTURN_LOG_DIRECTORY "/mnt/onboard/.adds/nickel-turn"
#define NICKELTURN_LOG_FILE NICKELTURN_LOG_DIRECTORY "/nickel-turn.log"

// 將可變參數格式化為固定大小訊息，供兩種 log 輸出共用。
static void format_message(char *message, size_t message_size,
                           const char *format, va_list arguments) {
    (void)vsnprintf(message, message_size, format, arguments);
}

// 寫完整行 log，處理短寫入與訊號中斷；呼叫端仍會立刻關閉 descriptor。
static void write_all(int fd, const char *line, size_t bytes) {
    size_t offset = 0;
    while (offset < bytes) {
        const ssize_t written = write(fd, line + offset, bytes - offset);
        if (written > 0) {
            offset += (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            return;
        }
    }
}

// 只寫入 Kobo 系統 log；可安全從 render hook 呼叫。
void nickelturn_log(const char *format, ...) {
    char message[768];
    va_list arguments;
    va_start(arguments, format);
    format_message(message, sizeof(message), format, arguments);
    va_end(arguments);
    nh_log("%s", message);
}

// 寫入系統 log 與持久檔；只供 init 或 Qt timer 等安全時機呼叫。
void nickelturn_event_log(const char *format, ...) {
    char message[768];
    va_list arguments;
    va_start(arguments, format);
    format_message(message, sizeof(message), format, arguments);
    va_end(arguments);
    nh_log("%s", message);

    // 呼叫端限制此函式只能在 init 與 Qt timer handler 使用；它立刻開啟、寫入、
    // 關閉檔案，不讓 descriptor 跨越 USB mass-storage 交接。
    if (mkdir(NICKELTURN_LOG_DIRECTORY, 0755) != 0 && errno != EEXIST)
        return;
    const int fd = open(NICKELTURN_LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return;

    struct timeval now;
    struct tm local_time;
    char timestamp[32];
    char line[832];
    int line_length;
    if (gettimeofday(&now, NULL) == 0 && localtime_r(&now.tv_sec, &local_time) != NULL &&
        strftime(timestamp, sizeof(timestamp), "%b %e %H:%M:%S", &local_time) != 0) {
        line_length = snprintf(line, sizeof(line), "[%s.%03ld] %s\n", timestamp,
                               (long)(now.tv_usec / 1000), message);
    } else {
        line_length = snprintf(line, sizeof(line), "%s\n", message);
    }
    if (line_length > 0) {
        size_t bytes = (size_t)line_length;
        if (bytes >= sizeof(line))
            bytes = sizeof(line) - 1;
        write_all(fd, line, bytes);
    }
    (void)close(fd);
}
