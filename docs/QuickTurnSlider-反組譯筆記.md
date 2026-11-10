# QuickTurnSlider 反組譯筆記

本文記錄 NickelTurn 對 Kobo 閱讀進度滑桿的反組譯觀察，避免重複尋找同一批函式。

## ELF 與範圍

- 主要反組譯檔：`libnickel.so.1.0.0`，韌體 4.46.23852，Build ID `8515657b0cb4e08354dba85716b3b93e1984e2d5`。
- 對照檔：`pre-libnickel.so.1.0.0`，韌體 4.45.23697，Build ID `f043a6aed1c782880be5d6075a22619bd8e49dcb`。
- 下列函式位址只對應 4.46 ELF；不要直接套用到其他韌體。
- 4.45 ELF 的字串區可找到相同 QuickTurnSlider／QuickTurnEventFilter mangled 名稱，但本文沒有記錄 4.45 的動態符號位址或逐函式反組譯結果。

## 4.46 相關函式位址

位址取自 `arm-nickel-linux-gnueabihf-nm -D -S -C`：

| 函式 | 位址 | 大小 |
| --- | ---: | ---: |
| `QuickTurnSlider::paintEvent(QPaintEvent*)` | `0x00f8ff44` | 未記錄 |
| `QuickTurnSlider::QuickTurnSlider(QWidget*)` | `0x00f90018` | 未記錄 |
| `QuickTurnSlider::updateSliderAppearance()` | `0x00f901ec` | `0x24` |
| `QuickTurnSlider::setLeftToRightWritingDirection(bool)` | `0x00f90210` | `0x28` |
| `QuickTurnSlider::setProgress(int, int)` | `0x00f90238` | `0x26` |
| `QuickTurnEventFilter::QuickTurnEventFilter(QuickTurnDelegate*, QWidget*, QuickTurnSlider*, int, bool)` | `0x00d7113c` | `0x3ec` |
| `QuickTurnEventFilter::updateSlider()` | `0x00d70468` | `0x20` |
| `ReadingView::updateFooter()` | `0x00f9de80` | `0x98` |
| `ReadingView::updateReaderProgress()` | `0x00f9df18` | `0x28` |
| `ReadingView::setupReader(QString const&)` | `0x00fa1350` | `0x11e4` |
| `ReadingView::updateProgressSettings()` | `0x00fa40c0` | `0x88` |

## 已確認的行為

### 方向 setter 控制滑桿外觀

`QuickTurnSlider::setLeftToRightWritingDirection(bool)` 的反組譯顯示：

1. 將參數存入 slider 物件位移 `this + 0x26`。
2. 以同一個 bool 呼叫 `TouchSlider::setThickLineEnabled(bool)`。
3. 以相反 bool 呼叫 `TouchSlider::setInverseThickLineEnabled(bool)`。
4. 呼叫 `QuickTurnSlider::updateSliderAppearance()`；該函式會對 Qt style 執行 unpolish/polish。

方向 setter 本身會刷新 style。這些反組譯資料只證明 QuickTurnSlider 的方向和進度更新行為，並不代表它就是使用者所指的閱讀選單進度條。

因此這個 setter 會切換兩種 thick-line 樣式並刷新外觀，與 progress bar 的已讀／未讀視覺樣式有直接關係。

### 方向 bool 也影響 progress 值的映射

`QuickTurnSlider::setProgress(int current, int total)` 會設定最大值為 `total`，再依 `this + 0x26` 的方向 bool 選擇 slider position：

- bool 為 true：position 使用 `current`。
- bool 為 false：position 使用 `total - current`。

所以改方向 bool 不只會改外觀，也會影響下一次 `setProgress` 對 slider value 的映射。只呼叫方向 setter 不會重新計算已經顯示的 value；方向改變後還要透過 `QuickTurnEventFilter::updateSlider()` 重新呼叫 `setProgress`，否則滑桿可暫時保留反向映射的舊值。修改時要保留 Kobo 傳入的原生方向值，不宜只依 writing-mode 猜 true／false。

### Kobo 更新 progress 的呼叫鏈

`ReadingView::updateReaderProgress()` 會更新 footer，接著呼叫 `QuickTurnEventFilter::updateSlider()`。後者讀取閱讀進度，再呼叫 `QuickTurnSlider::setProgress(current, 100)`。`ReadingView::updateFooter()` 本身主要更新 footer 文字。

`ReadingView::setupReader()` 在 `0x00fa15f2` 呼叫 vtable slot `+0x130`，回傳值放入建構函式的整數參數；實機 log 顯示此值為 `120`，應是 slider zone 寬度，不能當成左右方向。最後的 bool 參數固定傳入 `1`，也不是方向值。`QuickTurnEventFilter` 建構函式會從 `ReadingView` 讀取有效方向，並以該值呼叫 slider 的方向 setter。NickelTurn 曾試過追蹤 QuickTurnSlider 並呼叫 `ReadingView::updateReaderProgress()`／`QuickTurnEventFilter::updateSlider()`，但那會操作另一個滑桿，且可能增加刷新；這段試驗程式已從執行期移除。反組譯資料仍保留在本文供日後參考。

### Reading menu 的 BreadcrumbSlider 樣式

`ReadingMenuController::scrubberMenu()` 會建立 `ReadingMenuScrubberController`；其 `loadView()` 建立 `ReadingMenuScrubberView`，並對 `scrubber()` 回傳的 `BreadcrumbSlider` 設定 `TouchSlider::setThickLineEnabled()` 與 `setInverseThickLineEnabled()`。兩組程式路徑分別寫入 `1/0` 與 `0/1`，但目前反組譯尚未證實這兩條路徑各自代表哪種書籍方向。

`TouchSlider` 兩個 setter 只寫入物件欄位 `this + 0x24` 和 `this + 0x25`，不會自行重設樣式。NickelTurn 追蹤 `BreadcrumbSlider` 建構實例，重排後依目前 `ReadingView::leftToRightWritingDirection()` 寫入同一組旗標，再對 widget style 執行 unpolish/polish 並呼叫 `update()`。它不會呼叫 `ReadingMenuScrubberController::updateSlider()`，因為該函式負責位置、章節進度與書籤標記更新。

實機驗證：使用者回報這版修正已成功，能在排版切換後更新進度列樣式。這支持「只改 TouchSlider 旗標並呼叫 update 不足，還需對 style unpolish/polish」的判斷。QuickTurnSlider 與 BreadcrumbSlider 是兩個不同元件；目前程式只改動 BreadcrumbSlider。

### 左右章節按鈕的動作

`ReadingMenuScrubberController::loadView()` 建立 view 後，將左右按鈕連到 `prevChapter()` 和 `nextChapter()`。這兩個槽分別呼叫 `ReadingView::goToPrevChapter()` 與 `goToNextChapter()`，方向映射在 view 建立時確定；只改 `BreadcrumbSlider` 的方向旗標不會改動這些連線。

4.46 的槽位址為 `prevChapter()` `0x00fc383c`、`nextChapter()` `0x00fc38cc`；4.45 分別為 `0x00fb7c08`、`0x00fb7c98`。兩版都匯出相同 mangled 名稱。直接 hook 這兩個槽未能修正實機按鈕行為，因此目前改在 `loadView()` 完成後，解除左右按鈕到 controller 的原有連線，再接到方向感知的路由。路由每次點擊時依當前 `ReadingView::leftToRightWritingDirection()` 呼叫原本的 `prevChapter()`／`nextChapter()`，保留 Kobo 的章節跳轉與事件流程。這個 signal 重綁方案尚待重新建置並在實機確認。

### 私有物件欄位觀察

4.46 的 `ReadingView::updateReaderProgress()` 從 `ReadingView + 0x118`／`+0x11c` 讀取 QuickTurnEventFilter 的 weak pointer 內容；QuickTurnEventFilter 內的 slider 欄位在 `+0x3c`。這些是私有 ABI 觀察值，不是穩定 API；目前 NickelTurn 不依賴這些欄位，也不呼叫該刷新路徑。

## 重跑反組譯

在專案根目錄使用 NickelTC Docker image：

```powershell
docker run --rm --platform linux/amd64 --volume "${PWD}:/work:ro" --workdir=/work --entrypoint=arm-nickel-linux-gnueabihf-nm ghcr.io/pgaskin/nickeltc:1.0 -D -S -C libnickel.so.1.0.0
```

反組譯單一函式時，將 entrypoint 換成 `arm-nickel-linux-gnueabihf-objdump`，使用 `-dC` 和該函式的起訖位址，例如：

```powershell
docker run --rm --platform linux/amd64 --volume "${PWD}:/work:ro" --workdir=/work --entrypoint=arm-nickel-linux-gnueabihf-objdump ghcr.io/pgaskin/nickeltc:1.0 -dC --start-address=0x00f90210 --stop-address=0x00f90238 libnickel.so.1.0.0
```

如果要查 4.45，將輸入檔改成 `pre-libnickel.so.1.0.0`，重新取得符號位址和指令；不可沿用上表的 4.46 位址。
