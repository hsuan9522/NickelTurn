#include <NickelHook.h>
#include "nickelturn_log.h"

// smoke profile 的初始化：僅確認外掛可安全載入。
static int nickelturn_smoke_initialize(void) {
    nickelturn_event_log("NickelTurn smoke: plugin loaded; no hooks or user-storage state enabled");
    return 0;
}

// smoke profile 沒有建立額外檔案，因此卸載時不需清理。
static bool nickelturn_smoke_uninstall(void) { return true; }

static struct nh_info NickelTurnSmoke = {
    .name = "NickelTurn",
    .desc = "NickelTurn smoke load check",
    .uninstall_flag = "/mnt/onboard/.adds/nickelturn/uninstall",
    .failsafe_delay = 20,
};

static struct nh_hook NickelTurnSmokeHook[] = {{0}};
static struct nh_dlsym NickelTurnSmokeDlsym[] = {{0}};

// NickelTC 的 GCC 不接受 C++ 中的 NickelHook(...)，但接受此等價的 C descriptor。
// 此 smoke target 刻意不包含 dlsym 或 hook。
__attribute__((visibility("default"))) struct nh NickelHook = {
    .init = nickelturn_smoke_initialize,
    .info = &NickelTurnSmoke,
    .hook = NickelTurnSmokeHook,
    .dlsym = NickelTurnSmokeDlsym,
    .uninstall = nickelturn_smoke_uninstall,
};
