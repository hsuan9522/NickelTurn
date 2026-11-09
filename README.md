# NickelTurn

NickelTurn 是一項**實驗性功能**，建議安裝前先備份閱讀器，並自行承擔使用風險。
本專案讓 Kobo 閱讀器使用者在閱讀時切換直排與橫排，目前僅支援 `KEPUB` 格式。安裝後，
NickelMenu 的閱讀器選單會出現 **直橫排互換** 按鈕。

目前版本主要以 Clara BW 實機測試。先前曾在 Libra Colour 上測試，但後續版本未必有在該機種
重新驗證，因此無法保證相容。

本專案採用 [MIT License](LICENSE) 授權。

> [!警告]
>
> 絕大部分程式碼由 AI 生成。本功能仍屬實驗性功能，且必須使用相符的韌體版本。

`libnickel.so.1.0.0` 位於韌體中的 `usr/local/Kobo/libnickel.so.1.0.0`。

## 移除方式

在 `.adds/nickel-turn` 資料夾中新增名為 `uninstall` 的檔案，重新開機即會卸載相關檔案。

## 建置模式

舊的概念驗證階段已從公開建置介面移除。目前支援以下兩種建置模式：

| 模式 | 建置參數 | 用途 |
| --- | --- | --- |
| `full` | 預設值 | 提供 KEPUB 直橫排切換、JSON 快取、滑動翻頁方向調整，以及延遲還原原生書籤。 |
| `smoke` | `NT_BUILD=smoke` | 僅檢查外掛載入與 failsafe，不會變更書籍排版或狀態。 |

## 韌體版本設定

完整建置目前支援兩個韌體版本。每個版本的 Build ID 與 Nickel 位移值分別存放在
`src/firmware_profiles/`：

| Kobo 韌體 | Build ID | 建置參數 |
| --- | --- | --- |
| `4.45.23697` | `f043a6aed1c782880be5d6075a22619bd8e49dcb` | `NT_FIRMWARE=4.45.23697` |
| `4.46.23852` | `8515657b0cb4e08354dba85716b3b93e1984e2d5` | `NT_FIRMWARE=4.46.23852` |

`config/firmware.mk` 列出 CI 與 Release 要建置的所有韌體版本，也指定本機建置的預設版本。
Makefile 會依照 `NT_FIRMWARE` 引入相符的版本設定檔；GitHub Actions 則會為清單中的每個版本
各建置一份安裝包。新增韌體時，請加入已驗證的版本設定檔，並將版本號加入
`config/firmware.mk` 的 `NT_SUPPORTED_FIRMWARES`。

在 Linux、macOS 或 WSL 的此目錄執行以下指令，建置一般安裝包：

```sh
docker run --platform linux/amd64 --volume="$PWD:/work" --user="$(id -u):$(id -g)" \
  --workdir=/work --env=HOME --entrypoint=make --rm \
  ghcr.io/pgaskin/nickeltc:1.0 clean all koboroot NT_BUILD=full NT_FIRMWARE=4.46.23852
```


目前這會使用與 `config/firmware.mk` 預設值相同的韌體版本 `4.46.23852`。若要指定其他支援版本，
可設定環境變數。


## 使用方式

NickelTurn 的 NickelMenu 設定檔已包在 `KoboRoot.tgz` 中，安裝位置為 `.adds/nm/nickelturn`；
使用前也必須先安裝 NickelMenu。NickelTurn 啟動時不會重新建立或覆寫這個檔案，因此手動修改或刪除
後，變更會在重新啟動後保留。重新安裝或升級套件時，則會再次安裝套件內附的預設設定檔。開啟一本
KEPUB，進入 NickelMenu 的閱讀器選單，再選擇 **直橫排互換**。

選擇的排版模式會儲存在：

```text
/mnt/onboard/.adds/nickel-turn/book-modes.json
```

JSON 使用 Kobo 原始的 `ContentID` 值作為索引，不會正規化 `file://` 前綴或路徑。

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

## Log 紀錄

所有診斷訊息都可以透過以下指令查看：

```sh
logread | grep NickelTurn | tail -n 120
```

重要的初始化與選單操作事件也會寫入以下檔案：

```text
/mnt/onboard/.adds/nickel-turn/nickel-turn.log
```

## NickelHook 安全機制

- NickelHook 的 20 秒 failsafe 保持啟用。若啟動失敗，程式庫會被標記為 `.failsafe`，避免下次開機再次載入。
- 初始化讀取 JSON 時只開啟一次，解析到有容量上限的記憶體 POD 快取（檔案最多 1 MiB、最多 8192 筆）後立即關閉檔案。
- JSON 只會由 NickelMenu signal 的 Qt timer handler 寫入，流程為 write → `fsync` → close → rename；
  不會讓使用者儲存區的檔案描述符持續開啟。
- CSS 與 reader hook 不會進行檔案 I/O；只在目前 KEPUB 的呼叫流程中執行有限工作，並使用快取的模式資料。
- 程式碼使用 Qt／C API，不使用 C++ 標準函式庫。

其餘依韌體版本而異的 reader 指標，會在初始化時透過必要 symbol 檢查與精確位移值進行保護。
只要任一檢查失敗，初始化就會回傳錯誤，讓 NickelHook 還原原始 hooks。
