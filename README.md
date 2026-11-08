# NickelTurn

NickelTurn 是一個**實驗性功能**，建議安裝前先備份閱讀器，且風險自負。
這個功能是提供給 Kobo 閱讀器使用者，在閱讀時輕鬆的轉換直橫排模式。目前只適用於 `KEPUB` 格式的書籍。此功能會搭配 NickelMenu 在閱讀模式的設定列中生成一個**直橫排互換**按鈕。

主要的實機測試為 Clara BW，後續更新也僅會使用此機種測試。
另外，之前也有使用 Libra Colour 測試過，除非有什麼太重大的更新，否則應該都是可以通用的。


Licensed under the [MIT License](LICENSE).

> [!警告]

> 絕大部分的程式碼皆由 AI 生成。為實驗性功能，且需安裝對應的韌體版本。


`libnickel.so.1.0.0` 在 `usr/local/Kobo/libnickel.so.1.0.0` 裡可以找到。

## 移除方式

在 .adds/nickelturn 資料夾中新增一個名為 uninstall 的檔案即可。


## Build profiles

The old proof-of-concept sequence has been retired from the public build
interface. There are only two supported profiles:

| Profile | Build argument | Purpose |
| --- | --- | --- |
| `full` | default | KEPUB toggle, JSON cache, swipe direction, and delayed native bookmark restore. |
| `smoke` | `NT_BUILD=smoke` | Load/failsafe check only. It changes no book layout or state. |

Build the normal package from this directory:

```sh
docker run --platform linux/amd64 --volume="$PWD:/work" --user="$(id -u):$(id -g)" \
  --workdir=/work --env=HOME --entrypoint=make --rm \
  ghcr.io/pgaskin/nickeltc:1.0 clean all koboroot NT_BUILD=full
```

The output is `KoboRoot.tgz`. Copy it to `.kobo/` on the Kobo, safely eject,
and let the device finish its update. Do not install while a previous Kobo
update is still in progress.

## Use

NickelTurn writes a small NickelMenu configuration file at
`.adds/nm/nickelturn`; this requires NickelMenu to be installed. Open a KEPUB,
open NickelMenu's Reader menu, and choose **直橫排互換**.

The selected mode is stored in:

```text
/mnt/onboard/.adds/nickel-turn/book-modes.json
```

The JSON uses exact Kobo `ContentID` values; it intentionally does not
normalise `file://` prefixes or paths.

```json
{
  "books": {
    "file:///mnt/onboard/Books/example.kepub.epub": {
      "mode": "vertical-rl",
      "updatedAt": 1784590000
    }
  }
}
```

## Logs

All diagnostic messages are available through:

```sh
logread | grep NickelTurn | tail -n 120
```

Important initialization and menu-action events are also written to:

```text
/mnt/onboard/.adds/nickel-turn/nickel-turn.log
```

The persistent log is deliberately not written from CSS/render hooks. Those
hooks only write to `logread`; this avoids user-storage I/O during reading or
USB mass-storage sessions.



## NickelHook safety notes

- NickelHook's 20-second failsafe remains enabled. A startup failure leaves
  the library as `.failsafe`, preventing it from loading on the next boot.
- JSON is opened only once during init, parsed into a bounded in-memory POD
  cache (maximum 1 MiB / 8192 rows), and immediately closed.
- JSON writes happen only from the NickelMenu signal's Qt timer handler. They
  use write → `fsync` → close → rename; no user-storage descriptor is kept.
- CSS and reader hooks do no file I/O. Their work is bounded to active KEPUB
  calls and uses the cached modes.
- The source uses Qt/C APIs and avoids the C++ standard library.

The remaining firmware-specific reader pointers are protected by required
symbol checks and exact offsets at initialization. If any check fails, init
returns an error so NickelHook can restore the original hooks.
