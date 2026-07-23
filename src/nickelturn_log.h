#ifndef NICKELTURN_LOG_H
#define NICKELTURN_LOG_H

// 寫入一行診斷訊息至 Nickel 系統 log。它不會開啟使用者儲存區的檔案，
// 因此可安全從 rendering hook 呼叫。
#ifdef __cplusplus
extern "C" {
#endif

void nickelturn_log(const char *format, ...);

// 也會附加寫入 /mnt/onboard/.adds/nickel-turn/nickel-turn.log。僅能從 init
// 或 Qt signal／timer handler 呼叫，絕不能從 reader hook 呼叫。刻意忽略 log 失敗。
void nickelturn_event_log(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif
