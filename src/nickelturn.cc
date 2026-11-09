#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <ctime>
#include <unistd.h>

#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QPointF>
#include <QtCore/QRect>
#include <QtCore/QRectF>
#include <QtCore/QSize>
#include <QtCore/QStringList>
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtGui/QBrush>
#include <QtGui/QIcon>
#include <QtGui/QMouseEvent>
#include <QtGui/QPalette>
#include <QtGui/QPainter>
#include <QtGui/QPen>
#include <QtGui/QPixmap>
#include <QtGui/QTouchEvent>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QWidget>

#include <NickelHook.h>
#include "nickelturn_log.h"

// 此處不可引入或連結 QtWebKit。NickelTC 沒有 WebKit 開發用的 pkg-config
// 資訊；但 WebkitView::addCssToHtml 以值傳遞 QString，因此仍需要 QtCore。

namespace {

#ifndef NT_FIRMWARE_PROFILE_HEADER
#error "The Makefile must select NT_FIRMWARE_PROFILE_HEADER"
#endif
#include NT_FIRMWARE_PROFILE_HEADER

// ！！下列型別描述 libnickel 的私有 C++/ARM ABI。新版函式即使同名且 offset 可更新，
// 參數、回傳值、this 指標或 Qt 型別傳遞方式變動時，也必須在此及呼叫點一起更新。
typedef void (*WebkitAddCssToHtmlFn)(void *, QString);
typedef void (*KepubUpdateHtmlCssFn)(void *);
typedef void (*KepubReloadChapterFn)(void *);
typedef void (*KepubSetWritingDirectionFn)(void *, int);
typedef void *(*WebkitWebViewFn)(const void *);
typedef void (*CustomSetPageProgressionDirectionFn)(void *, const QByteArray &);
// Kobo 會在 bookmarkForCurrentLocation 的隱藏結果指標中建構此 16-byte 物件；
// NickelTurn 不解析其內部資料。
struct BookmarkStorage { uint32_t words[4]; };
typedef void (*BookmarkForCurrentLocationFn)(BookmarkStorage *, void *);
typedef void (*BookmarkStorageDestructorFn)(void *);
typedef void (*KepubGoToBookmarkAnchorFn)(void *);
typedef void *(*BookmarkSharedDataAssignFn)(void *, const void *);
// WritingDirection 在 ARM ABI 中以整數 enum 傳遞及回傳。
typedef void (*ContentGetBookIdFn)(QString *, const void *);
typedef void (*WebkitSetWritingDirectionFn)(void *, int);

// 初始化完成後固定不變的 Kobo 原生函式入口，集中管理避免每次切換重複轉型。
struct NativeKepubApi {
    KepubUpdateHtmlCssFn update_html_css;
    KepubReloadChapterFn reload_chapter;
    KepubSetWritingDirectionFn set_writing_direction;
    WebkitWebViewFn web_view;
    CustomSetPageProgressionDirectionFn set_page_progression;
    BookmarkForCurrentLocationFn bookmark_for_current_location;
    BookmarkStorageDestructorFn destroy_bookmark_shared_data;
    BookmarkStorageDestructorFn destroy_scobject;
    KepubGoToBookmarkAnchorFn go_to_bookmark_anchor;
    BookmarkSharedDataAssignFn assign_bookmark_shared_data;
};
static NativeKepubApi g_native_kepub_api = {};
const char kNickelTurnNickelMenuConfig[] = "/mnt/onboard/.adds/nm/nickelturn";
const char kNickelTurnToggleScript[] = "/mnt/onboard/.adds/nickel-turn/nickelturn-toggle";
const char kNickelTurnReadyFlag[] = "/mnt/onboard/.adds/nickel-turn/ready";
static QTimer *g_nickelturn_timer = nullptr;
static QPointer<QWidget> g_reader_toolbar_toggle_button;
static QPointer<QWidget> g_reader_toolbar_native_anchor;
static bool g_reader_toolbar_button_diagnostic_logged = false;
static bool g_reader_toolbar_button_setup_log_pending = false;
static bool g_reader_toolbar_button_setup_log_written = false;
static bool g_reader_toolbar_button_uses_stats_anchor = false;
static bool g_reader_toolbar_input_log_pending = false;
static bool g_reader_toolbar_input_log_written = false;
static bool g_reader_toolbar_input_sequence_ended = false;
static unsigned int g_reader_toolbar_input_event_mask = 0;
static unsigned int g_reader_toolbar_input_ticks_remaining = 0;
static char g_reader_toolbar_input_first_target[96] = {};
static char g_reader_toolbar_input_last_target[96] = {};
static bool g_pending_toolbar_toggle_request = false;
static unsigned int g_pending_toolbar_toggle_ticks = 0;
static QString g_pending_toolbar_toggle_content_id;
static QString g_pending_native_anchor_content_id;
static void *g_pending_native_anchor_webkit_view = nullptr;
static unsigned int g_pending_native_anchor_generation = 0;
static unsigned int g_pending_native_anchor_ticks = 0;
static unsigned int g_pending_horizontal_generation = 0;
static unsigned int g_pending_horizontal_ticks = 0;

// 只觀察落在 NickelTurn 按鈕矩形內的標準輸入事件，不攔截或改寫事件。
class ReaderToolbarInputDiagnosticFilter : public QObject {
public:
    explicit ReaderToolbarInputDiagnosticFilter(QObject *parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (g_reader_toolbar_input_log_written || g_reader_toolbar_toggle_button.isNull() || event == nullptr)
            return false;

        unsigned int event_bit = 0;
        bool ends_sequence = false;
        bool inside_button = false;
        const QRect button_rect(g_reader_toolbar_toggle_button->mapToGlobal(QPoint(0, 0)),
                                g_reader_toolbar_toggle_button->size());
        if (event->type() == QEvent::MouseButtonPress) {
            event_bit = 1u << 0;
            inside_button = button_rect.contains(static_cast<QMouseEvent *>(event)->globalPos());
        } else if (event->type() == QEvent::MouseButtonRelease) {
            event_bit = 1u << 1;
            ends_sequence = true;
            inside_button = button_rect.contains(static_cast<QMouseEvent *>(event)->globalPos());
        } else if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate ||
                   event->type() == QEvent::TouchEnd) {
            event_bit = event->type() == QEvent::TouchBegin ? (1u << 2) :
                        event->type() == QEvent::TouchUpdate ? (1u << 3) : (1u << 4);
            ends_sequence = event->type() == QEvent::TouchEnd;
            QTouchEvent *touch_event = static_cast<QTouchEvent *>(event);
            const QList<QTouchEvent::TouchPoint> points = touch_event->touchPoints();
            for (const QTouchEvent::TouchPoint &point : points) {
                if (button_rect.contains(point.screenPos().toPoint())) {
                    inside_button = true;
                    break;
                }
            }
        }

        if (event_bit == 0 || !inside_button)
            return false;
        if (!g_reader_toolbar_input_log_pending) {
            g_reader_toolbar_input_log_pending = true;
            g_reader_toolbar_input_event_mask = 0;
            g_reader_toolbar_input_first_target[0] = '\0';
        }
        g_reader_toolbar_input_event_mask |= event_bit;
        const char *class_name = watched != nullptr && watched->metaObject() != nullptr
                                     ? watched->metaObject()->className() : "unknown";
        const QByteArray object_name = watched != nullptr ? watched->objectName().toUtf8() : QByteArray();
        const QByteArray target = object_name.isEmpty()
                                      ? QByteArray(class_name)
                                      : QByteArray(class_name) + "/" + object_name;
        if (g_reader_toolbar_input_first_target[0] == '\0')
            qstrncpy(g_reader_toolbar_input_first_target, target.constData(), sizeof(g_reader_toolbar_input_first_target));
        qstrncpy(g_reader_toolbar_input_last_target, target.constData(), sizeof(g_reader_toolbar_input_last_target));
        g_reader_toolbar_input_sequence_ended = ends_sequence;
        g_reader_toolbar_input_ticks_remaining = 4u;
        return false;
    }
};

// 一次性 CSS handoff 診斷只保留在記憶體；由 Qt timer 安全地寫入持久 log。
enum CssHandoffDiagnosticResult {
    CssHandoffDiagnosticNone,
    CssHandoffDiagnosticAppended,
    CssHandoffDiagnosticNoActiveContent,
    CssHandoffDiagnosticNoOverride,
};
struct CssHandoffDiagnostic {
    unsigned int ticks_remaining;
    CssHandoffDiagnosticResult result;
    char previous_mode[16];
    char mode[16];
    char content_id[512];
};
static CssHandoffDiagnostic g_css_handoff_diagnostic = {};
static pthread_mutex_t g_css_handoff_diagnostic_mutex = PTHREAD_MUTEX_INITIALIZER;

static uintptr_t g_kepub_add_css_start = 0;
static bool g_firmware_matches = false;
static uintptr_t g_kepub_render_shortcover_start = 0;
static uintptr_t g_kepub_set_writing_direction_start = 0;
static QString g_last_rendered_content_id;
static pthread_mutex_t g_rendered_content_id_mutex = PTHREAD_MUTEX_INITIALIZER;

enum SourceMode {
    SourceModeUnknown,
    SourceModeHorizontal,
    SourceModeVerticalRL,
};

static QString g_active_content_id;
static SourceMode g_source_mode = SourceModeUnknown;
static void *g_active_webkit_view = nullptr;
// 每次換書或確認來源方向時遞增；延後工作以此排除過期的閱讀狀態。
static unsigned int g_direction_generation = 0;
static pthread_mutex_t g_source_mode_mutex = PTHREAD_MUTEX_INITIALIZER;

// 將同一把鎖保護的閱讀狀態一次複製出來，避免各呼叫端重複且不一致地讀取全域值。
struct ActiveReaderState {
    QString content_id;
    SourceMode source_mode;
    void *webkit_view;
    unsigned int direction_generation;
};

// 取得目前閱讀器狀態的一致快照；回傳後不再持有 mutex。
ActiveReaderState snapshotActiveReaderState() {
    ActiveReaderState state;
    (void)pthread_mutex_lock(&g_source_mode_mutex);
    state.content_id = g_active_content_id;
    state.source_mode = g_source_mode;
    state.webkit_view = g_active_webkit_view;
    state.direction_generation = g_direction_generation;
    (void)pthread_mutex_unlock(&g_source_mode_mutex);
    return state;
}

const char kModeJsonPath[] = "/mnt/onboard/.adds/nickel-turn/book-modes.json";
// 容納大型個人書庫，同時保留啟動時記憶體與使用者儲存區 I/O 的安全上限。
const qint64 kMaxModeJsonBytes = 1024 * 1024;
const unsigned int kMaxModeJsonEntries = 8192;

// 此狀態必須維持純 POD；不可使用全域 Qt container，因為其 C++ 建構子會在
// shared object 載入時執行，早於 NickelHook 建立 failsafe 的時機。
struct ModeJsonEntry {
    char *content_id;
    char *mode;
    qint64 updated_at;
};
static ModeJsonEntry *g_mode_json_entries = nullptr;
static unsigned int g_mode_json_entry_count = 0;
static bool g_mode_json_cache_available = false;
// 只有成功解析既有 JSON，或確認檔案原本不存在時，才允許覆寫此檔案。
static bool g_mode_json_write_allowed = false;
static pthread_mutex_t g_mode_json_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

const char kVerticalOverrideCss[] =
    "html, body {"
    "writing-mode: vertical-rl !important;"
    "-epub-writing-mode: vertical-rl !important;"
    "-webkit-writing-mode: vertical-rl !important;"
    "text-orientation: mixed !important;"
    "-webkit-text-orientation: mixed !important;"
    "}"
    // EPUB 原始樣式常用實體 margin-bottom；在 vertical-rl 中它不再是相鄰段落
    // 欄位的間距，所以補上保守的 block-end 間距，而不改動 EPUB 本身。
    "p { margin: 0 1em !important; }";
const char kHorizontalOverrideCss[] =
    "html, body {"
    "writing-mode: horizontal-tb !important;"
    "-epub-writing-mode: horizontal-tb !important;"
    "-webkit-writing-mode: horizontal-tb !important;"
    "text-orientation: mixed !important;"
    "-webkit-text-orientation: mixed !important;"
    "}";

// 釋放模式快取陣列及其中每本書的 ContentID／模式字串。
void freeModeJsonEntries(ModeJsonEntry *entries, unsigned int count) {
    if (entries == nullptr)
        return;
    for (unsigned int i = 0; i < count; ++i) {
        free(entries[i].content_id);
        free(entries[i].mode);
    }
    free(entries);
}

// 深複製既有模式快取，讓 JSON 寫入完成前不會改動目前正在使用的快取。
ModeJsonEntry *copyModeJsonEntries(const ModeJsonEntry *entries, unsigned int count) {
    ModeJsonEntry *copy = static_cast<ModeJsonEntry *>(calloc(count, sizeof(ModeJsonEntry)));
    if (copy == nullptr && count != 0)
        return nullptr;
    for (unsigned int i = 0; i < count; ++i) {
        copy[i].content_id = strdup(entries[i].content_id);
        copy[i].mode = strdup(entries[i].mode);
        copy[i].updated_at = entries[i].updated_at;
        if (copy[i].content_id == nullptr || copy[i].mode == nullptr) {
            freeModeJsonEntries(copy, count);
            return nullptr;
        }
    }
    return copy;
}

// 僅在初始化時讀取有限大小的 JSON，驗證後轉為 POD 記憶體快取；閱讀 hook 不會開檔。
void parseModeJsonAtInit() {
    QFile file(QString::fromLatin1(kModeJsonPath));
    if (!file.exists()) {
        (void)pthread_mutex_lock(&g_mode_json_cache_mutex);
        g_mode_json_write_allowed = true;
        (void)pthread_mutex_unlock(&g_mode_json_cache_mutex);
        return;
    }
    const qint64 size = file.size();
    if (size < 0) {
        nickelturn_event_log("NickelTurn mode JSON size query failed; no file opened");
        return;
    }
    if (size > kMaxModeJsonBytes) {
        nickelturn_event_log("NickelTurn mode JSON exceeds size limit bytes=%ld limit=%ld; no file opened",
                             (long)size, (long)kMaxModeJsonBytes);
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        const QByteArray error_utf8 = file.errorString().toUtf8();
        nickelturn_event_log("NickelTurn mode JSON open failed error=%s", error_utf8.constData());
        return;
    }
    const QByteArray bytes = file.read(kMaxModeJsonBytes + 1);
    const QString read_error = file.error() == QFile::NoError ? QString() : file.errorString();
    file.close();
    if (!read_error.isEmpty() || bytes.size() > kMaxModeJsonBytes) {
        const QByteArray read_error_utf8 = read_error.toUtf8();
        nickelturn_event_log("NickelTurn mode JSON read failed bytes=%d limit=%ld error=%s",
                             bytes.size(), (long)kMaxModeJsonBytes,
                             read_error.isEmpty() ? "size limit exceeded" : read_error_utf8.constData());
        return;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        const QByteArray error_utf8 = parse_error.errorString().toUtf8();
        nickelturn_event_log("NickelTurn mode JSON parse failed offset=%lld error=%s",
                             (long long)parse_error.offset, error_utf8.constData());
        return;
    }
    const QJsonObject root = document.object();
    const QJsonValue books_value = root.value(QLatin1String("books"));
    if (!books_value.isObject()) {
        nickelturn_event_log("NickelTurn mode JSON requires a books object");
        return;
    }
    const QJsonObject books = books_value.toObject();
    if (books.size() > (int)kMaxModeJsonEntries) {
        nickelturn_event_log("NickelTurn mode cache unavailable: too many entries");
        return;
    }
    ModeJsonEntry *loaded_entries = static_cast<ModeJsonEntry *>(calloc(books.size(), sizeof(ModeJsonEntry)));
    if (loaded_entries == nullptr && !books.isEmpty()) {
        nickelturn_event_log("NickelTurn mode cache unavailable: allocation failed");
        return;
    }
    unsigned int valid_rows = 0;
    for (QJsonObject::const_iterator it = books.constBegin(); it != books.constEnd(); ++it) {
        const QString mode = it.value().toObject().value(QLatin1String("mode")).toString();
        if (!it.key().isEmpty() && (mode == QLatin1String("horizontal-tb") ||
                                    mode == QLatin1String("vertical-rl"))) {
            const QByteArray content_id_utf8 = it.key().toUtf8();
            const QByteArray mode_utf8 = mode.toUtf8();
            loaded_entries[valid_rows].content_id = strdup(content_id_utf8.constData());
            loaded_entries[valid_rows].mode = strdup(mode_utf8.constData());
            loaded_entries[valid_rows].updated_at =
                static_cast<qint64>(it.value().toObject().value(QLatin1String("updatedAt")).toDouble(0));
            if (loaded_entries[valid_rows].content_id == nullptr || loaded_entries[valid_rows].mode == nullptr) {
                freeModeJsonEntries(loaded_entries, valid_rows + 1);
                nickelturn_event_log("NickelTurn mode JSON entry allocation failed");
                return;
            }
            ++valid_rows;
        }
    }
    (void)pthread_mutex_lock(&g_mode_json_cache_mutex);
    freeModeJsonEntries(g_mode_json_entries, g_mode_json_entry_count);
    g_mode_json_entries = loaded_entries;
    g_mode_json_entry_count = valid_rows;
    g_mode_json_cache_available = true;
    g_mode_json_write_allowed = true;
    (void)pthread_mutex_unlock(&g_mode_json_cache_mutex);
    nickelturn_event_log("NickelTurn: mode JSON cached bytes=%d valid rows=%u",
                         bytes.size(), valid_rows);
}

// 在已載入的記憶體快取中，以完全相同的 ContentID 查詢指定模式。
bool lookupCachedModeJson(const QString &content_id, char *mode, size_t mode_size) {
    if (mode_size == 0)
        return false;
    mode[0] = '\0';
    const QByteArray content_id_utf8 = content_id.toUtf8();
    (void)pthread_mutex_lock(&g_mode_json_cache_mutex);
    if (g_mode_json_cache_available) {
        for (unsigned int i = 0; i < g_mode_json_entry_count; ++i) {
            if (strcmp(content_id_utf8.constData(), g_mode_json_entries[i].content_id) == 0) {
                (void)snprintf(mode, mode_size, "%s", g_mode_json_entries[i].mode);
                break;
            }
        }
    }
    (void)pthread_mutex_unlock(&g_mode_json_cache_mutex);
    return mode[0] != '\0';
}

// 將所有資料寫入檔案，處理短寫入與訊號中斷。
bool writeAll(int fd, const char *data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const ssize_t written = write(fd, data + offset, size - offset);
        if (written > 0) {
            offset += (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            if (written == 0)
                errno = EIO;
            return false;
        }
    }
    return true;
}

// 將目前書籍模式安全寫回 JSON；先完成新的快取配置，再以原子方式替換檔案與快取。
bool persistCachedModeJson(const QString &content_id, const char *mode) {
    const QByteArray content_id_utf8 = content_id.toUtf8();
    const qint64 now = static_cast<qint64>(time(nullptr));
    (void)pthread_mutex_lock(&g_mode_json_cache_mutex);
    if (!g_mode_json_write_allowed) {
        (void)pthread_mutex_unlock(&g_mode_json_cache_mutex);
        nickelturn_event_log("NickelTurn mode JSON was invalid or unreadable; refusing to overwrite it");
        return false;
    }
    unsigned int next_count = g_mode_json_entry_count;
    bool existing_entry = false;
    for (unsigned int i = 0; i < g_mode_json_entry_count; ++i) {
        if (strcmp(g_mode_json_entries[i].content_id, content_id_utf8.constData()) == 0) {
            existing_entry = true;
            break;
        }
    }
    if (!existing_entry) {
        if (next_count >= kMaxModeJsonEntries) {
            (void)pthread_mutex_unlock(&g_mode_json_cache_mutex);
            nickelturn_event_log("NickelTurn mode JSON has too many entries; layout unchanged");
            return false;
        }
        ++next_count;
    }
    ModeJsonEntry *next_entries = copyModeJsonEntries(g_mode_json_entries, g_mode_json_entry_count);
    if (next_entries == nullptr && g_mode_json_entry_count != 0) {
        (void)pthread_mutex_unlock(&g_mode_json_cache_mutex);
        nickelturn_event_log("NickelTurn mode JSON cache allocation failed; layout unchanged");
        return false;
    }
    if (!existing_entry) {
        ModeJsonEntry *grown_entries = static_cast<ModeJsonEntry *>(
            realloc(next_entries, next_count * sizeof(ModeJsonEntry)));
        if (grown_entries == nullptr) {
            freeModeJsonEntries(next_entries, g_mode_json_entry_count);
            (void)pthread_mutex_unlock(&g_mode_json_cache_mutex);
            nickelturn_event_log("NickelTurn mode JSON cache allocation failed; layout unchanged");
            return false;
        }
        next_entries = grown_entries;
        memset(&next_entries[next_count - 1], 0, sizeof(ModeJsonEntry));
        next_entries[next_count - 1].content_id = strdup(content_id_utf8.constData());
        next_entries[next_count - 1].mode = strdup(mode);
        if (next_entries[next_count - 1].content_id == nullptr || next_entries[next_count - 1].mode == nullptr) {
            freeModeJsonEntries(next_entries, next_count);
            (void)pthread_mutex_unlock(&g_mode_json_cache_mutex);
            nickelturn_event_log("NickelTurn mode JSON cache allocation failed; layout unchanged");
            return false;
        }
        next_entries[next_count - 1].updated_at = now;
    } else {
        for (unsigned int i = 0; i < next_count; ++i) {
            if (strcmp(next_entries[i].content_id, content_id_utf8.constData()) == 0) {
                char *next_mode = strdup(mode);
                if (next_mode == nullptr) {
                    freeModeJsonEntries(next_entries, next_count);
                    (void)pthread_mutex_unlock(&g_mode_json_cache_mutex);
                    nickelturn_event_log("NickelTurn mode JSON cache allocation failed; layout unchanged");
                    return false;
                }
                free(next_entries[i].mode);
                next_entries[i].mode = next_mode;
                next_entries[i].updated_at = now;
                break;
            }
        }
    }
    (void)pthread_mutex_unlock(&g_mode_json_cache_mutex);

    QJsonObject books;
    for (unsigned int i = 0; i < next_count; ++i) {
        QJsonObject row;
        row.insert(QLatin1String("mode"), QString::fromUtf8(next_entries[i].mode));
        row.insert(QLatin1String("updatedAt"), next_entries[i].updated_at);
        books.insert(QString::fromUtf8(next_entries[i].content_id), row);
    }
    QJsonObject root;
    root.insert(QLatin1String("books"), books);
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);

    char temporary[256];
    const int n = snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", kModeJsonPath, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(temporary)) {
        freeModeJsonEntries(next_entries, next_count);
        nickelturn_event_log("NickelTurn mode JSON temporary path is too long ContentID=%s",
                             content_id_utf8.constData());
        return false;
    }
    const int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        const int error = errno;
        freeModeJsonEntries(next_entries, next_count);
        nickelturn_event_log("NickelTurn mode JSON temporary file open failed error=%s ContentID=%s",
                             strerror(error), content_id_utf8.constData());
        return false;
    }
    const char *failure_stage = nullptr;
    int failure_errno = 0;
    bool ok = writeAll(fd, bytes.constData(), (size_t)bytes.size());
    if (!ok) {
        failure_stage = "write";
        failure_errno = errno == 0 ? EIO : errno;
    }
    if (ok && fsync(fd) != 0) {
        ok = false;
        failure_stage = "fsync";
        failure_errno = errno;
    }
    // 每個 descriptor 只關閉一次；rename 失敗後不可重試 close()，因為該
    // descriptor 編號可能已被系統重新使用。
    if (close(fd) != 0) {
        ok = false;
        if (failure_stage == nullptr) {
            failure_stage = "close";
            failure_errno = errno;
        }
    }
    if (ok && rename(temporary, kModeJsonPath) != 0) {
        ok = false;
        failure_stage = "rename";
        failure_errno = errno;
    }
    if (!ok) {
        (void)unlink(temporary);
        freeModeJsonEntries(next_entries, next_count);
        nickelturn_event_log("NickelTurn mode JSON %s failed error=%s ContentID=%s",
                             failure_stage == nullptr ? "persist" : failure_stage,
                             strerror(failure_errno), content_id_utf8.constData());
        return false;
    }

    (void)pthread_mutex_lock(&g_mode_json_cache_mutex);
    freeModeJsonEntries(g_mode_json_entries, g_mode_json_entry_count);
    g_mode_json_entries = next_entries;
    g_mode_json_entry_count = next_count;
    g_mode_json_cache_available = true;
    (void)pthread_mutex_unlock(&g_mode_json_cache_mutex);
    return true;
}

// 取得目前閱讀中書籍的 ContentID，並查詢是否有已儲存的模式覆寫。
bool getActiveModeJsonOverride(QString *content_id, char *mode, size_t mode_size) {
    *content_id = snapshotActiveReaderState().content_id;
    return !content_id->isEmpty() && lookupCachedModeJson(*content_id, mode, mode_size);
}


// 判斷 CSS hook 是否由 KEPUB 的 CSS 建構函式呼叫，避免影響其他 WebKit 頁面。
bool wasCalledByKepubCssBuilder(uintptr_t return_address) {
    return_address &= ~uintptr_t(1);
    return g_firmware_matches && return_address >= g_kepub_add_css_start &&
           return_address < g_kepub_add_css_start + kKepubAddCssToHtmlSize;
}


// 判斷 Content::getBookId 是否由 KEPUB reader 開書流程呼叫。
bool wasCalledByKepubRenderShortcover(uintptr_t return_address) {
    return_address &= ~uintptr_t(1);
    return g_firmware_matches && return_address >= g_kepub_render_shortcover_start &&
           return_address < g_kepub_render_shortcover_start + kKepubRenderShortcoverSize;
}

// 判斷 WebkitView 方向設定是否來自 KEPUB reader，而非其他畫面。
bool wasCalledByKepubSetWritingDirection(uintptr_t return_address) {
    return_address &= ~uintptr_t(1);
    return g_firmware_matches && return_address >= g_kepub_set_writing_direction_start &&
           return_address < g_kepub_set_writing_direction_start + kKepubSetWritingDirectionSize;
}

// 從 KEPUB 原始 CSS 偵測直排；部分商店書的 WritingDirection raw 值仍為橫排。
void observeVerticalSourceModeFromCss(const QString &css) {
    if (!css.contains(QLatin1String("vertical-rl"), Qt::CaseInsensitive))
        return;
    (void)pthread_mutex_lock(&g_source_mode_mutex);
    if (!g_active_content_id.isEmpty() && g_source_mode != SourceModeVerticalRL) {
        g_source_mode = SourceModeVerticalRL;
        g_pending_horizontal_ticks = 0u;
        ++g_direction_generation;
    }
    (void)pthread_mutex_unlock(&g_source_mode_mutex);
}

// 將內部排版列舉轉為 JSON 與 CSS 使用的模式字串。
const char *sourceModeName(SourceMode mode) {
    switch (mode) {
    case SourceModeHorizontal:
        return "horizontal-tb";
    case SourceModeVerticalRL:
        return "vertical-rl";
    default:
        return "unknown";
    }
}

// 排程延後確認橫排；直排書啟動時可能先短暫回報橫排。
void scheduleHorizontalSourceMode() {
    // 呼叫時仍持有 g_source_mode_mutex，且位於 Nickel GUI thread；交給既有 Qt
    // timer 延後判定，不要以 pthread 直接碰觸 reader／Qt 狀態。
    g_pending_horizontal_generation = g_direction_generation;
    g_pending_horizontal_ticks = 3u;
}

// 驗證 symbol 位於目前 libnickel 的預期 offset，並在需要時回傳已移除 Thumb bit 的位址。
bool validateLibnickelOffset(void *symbol, void *libnickel_base, uintptr_t offset,
                             const char *symbol_name, uintptr_t *normalized_address) {
    Dl_info info;
    if (dladdr(symbol, &info) == 0 || info.dli_fbase != libnickel_base) {
        nickelturn_event_log("NickelTurn: cannot determine %s libnickel base address", symbol_name);
        return false;
    }
    const uintptr_t actual = reinterpret_cast<uintptr_t>(symbol) & ~uintptr_t(1);
    const uintptr_t expected = reinterpret_cast<uintptr_t>(libnickel_base) + offset;
    if (actual != expected) {
        nickelturn_event_log("NickelTurn: unsupported %s offset %p (expected %p)", symbol_name,
                             reinterpret_cast<void *>(actual), reinterpret_cast<void *>(expected));
        return false;
    }
    if (normalized_address != nullptr)
        *normalized_address = actual;
    return true;
}

// 一筆 firmware guard：symbol、預期 offset、log 名稱，以及可選的已正規化位址輸出。
struct FirmwareOffsetGuard {
    void *symbol;
    uintptr_t offset;
    const char *symbol_name;
    uintptr_t *normalized_address;
};

// 確認 NickelHook 已解析所有本外掛會使用的 symbol，避免後續解參考空指標。
bool areRequiredSymbolsAvailable(void *const *symbols, const char *const *names, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (symbols[i] == nullptr) {
            nickelturn_event_log("NickelTurn: required symbol unavailable name=%s", names[i]);
            return false;
        }
    }
    return true;
}

// 確認所有切換流程所需的原生函式入口都已在 firmware guard 後完成設定。
bool isNativeKepubApiReady() {
#define CHECK_NATIVE_API(field, label) \
    do { \
        if (g_native_kepub_api.field == nullptr) { \
            nickelturn_event_log("NickelTurn: native API entrypoint unavailable name=%s", label); \
            return false; \
        } \
    } while (0)
    CHECK_NATIVE_API(update_html_css, "updateHtmlCss");
    CHECK_NATIVE_API(reload_chapter, "reloadChapter");
    CHECK_NATIVE_API(set_writing_direction, "setWritingDirection");
    CHECK_NATIVE_API(web_view, "webView");
    CHECK_NATIVE_API(set_page_progression, "setPageProgressionDirection");
    CHECK_NATIVE_API(bookmark_for_current_location, "bookmarkForCurrentLocation");
    CHECK_NATIVE_API(destroy_bookmark_shared_data, "BookmarkSharedData destructor");
    CHECK_NATIVE_API(destroy_scobject, "ScObject destructor");
    CHECK_NATIVE_API(go_to_bookmark_anchor, "goToBookmarkAnchor");
    CHECK_NATIVE_API(assign_bookmark_shared_data, "BookmarkSharedData assignment");
#undef CHECK_NATIVE_API
    return true;
}

// 在一次 toggle 前開始觀測下一次 KEPUB CSS handoff，最多等待兩秒。
void beginCssHandoffDiagnostic(const char *previous_mode) {
    (void)pthread_mutex_lock(&g_css_handoff_diagnostic_mutex);
    g_css_handoff_diagnostic.ticks_remaining = 8u;
    g_css_handoff_diagnostic.result = CssHandoffDiagnosticNone;
    (void)snprintf(g_css_handoff_diagnostic.previous_mode,
                   sizeof(g_css_handoff_diagnostic.previous_mode), "%s",
                   previous_mode == nullptr ? "" : previous_mode);
    g_css_handoff_diagnostic.mode[0] = '\0';
    g_css_handoff_diagnostic.content_id[0] = '\0';
    (void)pthread_mutex_unlock(&g_css_handoff_diagnostic_mutex);
}

// CSS hook 僅記下第一個觀測結果；不可在這裡寫入使用者儲存區。
void recordCssHandoffDiagnostic(CssHandoffDiagnosticResult result, const QString &content_id,
                                const char *mode) {
    (void)pthread_mutex_lock(&g_css_handoff_diagnostic_mutex);
    if (g_css_handoff_diagnostic.ticks_remaining != 0u &&
        g_css_handoff_diagnostic.result == CssHandoffDiagnosticNone) {
        const QByteArray content_id_utf8 = content_id.toUtf8();
        g_css_handoff_diagnostic.result = result;
        (void)snprintf(g_css_handoff_diagnostic.mode, sizeof(g_css_handoff_diagnostic.mode),
                       "%s", mode == nullptr ? "" : mode);
        (void)snprintf(g_css_handoff_diagnostic.content_id,
                       sizeof(g_css_handoff_diagnostic.content_id), "%s",
                       content_id_utf8.constData());
    }
    (void)pthread_mutex_unlock(&g_css_handoff_diagnostic_mutex);
}

// 在 Qt timer 中輸出一次性 CSS 診斷結果，避免 render hook 進行持久 I/O。
void processCssHandoffDiagnostic() {
    CssHandoffDiagnostic diagnostic = {};
    bool ready_to_log = false;
    (void)pthread_mutex_lock(&g_css_handoff_diagnostic_mutex);
    if (g_css_handoff_diagnostic.ticks_remaining != 0u) {
        if (g_css_handoff_diagnostic.result != CssHandoffDiagnosticNone ||
            --g_css_handoff_diagnostic.ticks_remaining == 0u) {
            diagnostic = g_css_handoff_diagnostic;
            g_css_handoff_diagnostic.ticks_remaining = 0u;
            ready_to_log = true;
        }
    }
    (void)pthread_mutex_unlock(&g_css_handoff_diagnostic_mutex);
    if (!ready_to_log)
        return;
    switch (diagnostic.result) {
    case CssHandoffDiagnosticAppended:
        // 成功切換已有一筆摘要；只把異常 handoff 寫入持久 log。
        break;
    case CssHandoffDiagnosticNoActiveContent:
        nickelturn_event_log("NickelTurn: toggle CSS handoff had no active ContentID");
        break;
    case CssHandoffDiagnosticNoOverride:
        nickelturn_event_log("NickelTurn: toggle CSS handoff had no JSON override ContentID=%s",
                             diagnostic.content_id);
        break;
    default:
        nickelturn_event_log("NickelTurn: toggle CSS handoff was not observed within 2 seconds");
        break;
    }
}

// 釋放 bookmarkForCurrentLocation 建構出的暫存 Bookmark，確保兩個析構步驟不會漏掉其一。
void destroyCapturedBookmark(BookmarkStorage *bookmark) {
    g_native_kepub_api.destroy_bookmark_shared_data(
        reinterpret_cast<char *>(bookmark) + sizeof(uint32_t));
    g_native_kepub_api.destroy_scobject(bookmark);
}

} // 匿名 namespace 結束

extern "C" {
void *nickelturn_original_webkit_add_css_to_html = nullptr;
void *nickelturn_kepub_add_css_to_html = nullptr;
void *nickelturn_update_html_css = nullptr;
void *nickelturn_reload_chapter = nullptr;
void *nickelturn_webkit_web_view = nullptr;
void *nickelturn_custom_set_page_progression_direction = nullptr;
void *nickelturn_original_reading_view_left_to_right_page_progress_direction = nullptr;
void *nickelturn_bookmark_for_current_location = nullptr;
void *nickelturn_bookmark_shared_data_destructor = nullptr;
void *nickelturn_scobject_destructor = nullptr;
void *nickelturn_kepub_go_to_bookmark_anchor = nullptr;
void *nickelturn_bookmark_shared_data_assign = nullptr;
void *nickelturn_original_content_get_book_id = nullptr;
void *nickelturn_content_get_book_id = nullptr;
void *nickelturn_kepub_render_shortcover = nullptr;
void *nickelturn_original_webkit_set_writing_direction = nullptr;
void *nickelturn_kepub_set_writing_direction = nullptr;
void *nickelturn_webkit_set_writing_direction = nullptr;
void *nickelturn_original_reading_menu_view_constructor = nullptr;
void *nickelturn_reading_menu_view_font_button = nullptr;
void *nickelturn_reading_menu_view_stats_button = nullptr;
void *nickelturn_touch_label_constructor = nullptr;
void *nickelturn_touch_label_set_deselected_pixmap = nullptr;
void *nickelturn_touch_label_set_selected_pixmap = nullptr;
}

// 清除舊版 NickelMenu 整合留下的設定與 script。
extern "C" bool nickelturn_uninstall(void) {
    bool ok = true;
    nickelturn_event_log("NickelTurn: uninstall cleanup started");
    if (unlink(kNickelTurnNickelMenuConfig) != 0 && errno != ENOENT) {
        nickelturn_event_log("NickelTurn: failed to remove NickelMenu config error=%s", strerror(errno));
        ok = false;
    }
    if (unlink(kNickelTurnToggleScript) != 0 && errno != ENOENT) {
        nickelturn_event_log("NickelTurn: failed to remove toggle script error=%s", strerror(errno));
        ok = false;
    }
    if (unlink(kNickelTurnReadyFlag) != 0 && errno != ENOENT) {
        nickelturn_event_log("NickelTurn: failed to remove ready marker error=%s", strerror(errno));
        ok = false;
    }
    nickelturn_event_log("NickelTurn: uninstall cleanup %s", ok ? "completed" : "incomplete");
    return ok;
}


// Hook Content::getBookId，僅在 KEPUB 開書流程中記錄目前書籍識別碼。
extern "C" void NickelTurnContentGetBookId(QString *result, const void *content) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    ContentGetBookIdFn original =
        reinterpret_cast<ContentGetBookIdFn>(nickelturn_original_content_get_book_id);
    if (original == nullptr) {
        nickelturn_log("NickelTurn: original Content::getBookId pointer missing");
        return;
    }

    original(result, content);
    if (!wasCalledByKepubRenderShortcover(caller) || result->isEmpty())
        return;

    bool new_content_id = false;
    (void)pthread_mutex_lock(&g_rendered_content_id_mutex);
    if (*result != g_last_rendered_content_id) {
        g_last_rendered_content_id = *result;
        new_content_id = true;
    }
    (void)pthread_mutex_unlock(&g_rendered_content_id_mutex);

    if (new_content_id) {
        (void)pthread_mutex_lock(&g_source_mode_mutex);
        g_active_content_id = *result;
        g_source_mode = SourceModeUnknown;
        g_active_webkit_view = nullptr;
        ++g_direction_generation;
        (void)pthread_mutex_unlock(&g_source_mode_mutex);
    }
}

typedef bool (*ReadingViewLeftToRightPageProgressDirectionFn)(const void *);

// Hook 閱讀器翻頁方向；有覆寫模式時讓手勢方向符合直排或橫排。
extern "C" bool NickelTurnReadingViewLeftToRightPageProgressDirection(const void *reading_view) {
    ReadingViewLeftToRightPageProgressDirectionFn original =
        reinterpret_cast<ReadingViewLeftToRightPageProgressDirectionFn>(
            nickelturn_original_reading_view_left_to_right_page_progress_direction);
    if (original == nullptr)
        return true;

    const ActiveReaderState state = snapshotActiveReaderState();

    char override_mode[16];
    if (!state.content_id.isEmpty() &&
        lookupCachedModeJson(state.content_id, override_mode, sizeof(override_mode))) {
        // ReadingView::processSwipe 以此布林值選擇上一／下一頁；firmware 原生
        // 的直排 KEPUB 行為是 RTL。
        const bool left_to_right = strcmp(override_mode, "vertical-rl") != 0;
        return left_to_right;
    }
    return original(reading_view);
}


// Hook Kobo 的方向設定，保存目前 WebkitView 並辨識原始書籍排版方向。
extern "C" void NickelTurnWebkitSetWritingDirection(void *webkit_view, int direction) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    WebkitSetWritingDirectionFn original =
        reinterpret_cast<WebkitSetWritingDirectionFn>(nickelturn_original_webkit_set_writing_direction);
    if (original == nullptr) {
        nickelturn_log("NickelTurn: original WebkitView::setWritingDirection pointer missing");
        return;
    }

    original(webkit_view, direction);
    if (!wasCalledByKepubSetWritingDirection(caller))
        return;

    (void)pthread_mutex_lock(&g_source_mode_mutex);
    if (!g_active_content_id.isEmpty()) {
        g_active_webkit_view = webkit_view;
        if (direction == 3) {
            // 首個已確認的值是書籍原始排版方向。附加覆寫 CSS 後，WebKit 可能再送出
            // 有效排版的 setter 值；它不可覆蓋 Toggle state machine 稍後需要的原始值。
            if (g_source_mode == SourceModeUnknown) {
                ++g_direction_generation;
                g_source_mode = SourceModeVerticalRL;
            }
        } else if (direction == 0 && g_source_mode == SourceModeUnknown) {
            ++g_direction_generation;
            scheduleHorizontalSourceMode();
        }
    }
    (void)pthread_mutex_unlock(&g_source_mode_mutex);
}

// 建立 NickelTurn 自己使用的目錄；既有目錄與檔案都不會被刪除。
static bool ensureNickelTurnDirectories() {
    if (mkdir("/mnt/onboard/.adds", 0755) != 0 && errno != EEXIST) {
        nickelturn_event_log("NickelTurn: cannot create .adds directory error=%s", strerror(errno));
        return false;
    }
    if (mkdir("/mnt/onboard/.adds/nickel-turn", 0755) != 0 && errno != EEXIST) {
        nickelturn_event_log("NickelTurn: cannot create nickel-turn directory error=%s", strerror(errno));
        return false;
    }
    return true;
}

static void removeLegacyNickelMenuArtifact(const char *path, const char *name) {
    if (unlink(path) == 0) {
        nickelturn_event_log("NickelTurn: removed legacy NickelMenu %s", name);
    } else if (errno != ENOENT) {
        nickelturn_event_log("NickelTurn: cannot remove legacy NickelMenu %s error=%s",
                             name, strerror(errno));
    }
}

// 在延後時間到達時，將尚未收到方向 setter 的書籍確定為原始橫排。
static void processPendingHorizontalSourceMode() {
    if (g_pending_horizontal_ticks == 0u || --g_pending_horizontal_ticks != 0u)
        return;
    (void)pthread_mutex_lock(&g_source_mode_mutex);
    if (g_pending_horizontal_generation == g_direction_generation &&
        !g_active_content_id.isEmpty() && g_source_mode == SourceModeUnknown) {
        g_source_mode = SourceModeHorizontal;
    }
    (void)pthread_mutex_unlock(&g_source_mode_mutex);
}

// 在新章節 DOM 建立後，以 Kobo 原生 Bookmark API 回到切換前的位置。
static void processPendingNativeAnchorRestore() {
    if (g_pending_native_anchor_ticks == 0u || --g_pending_native_anchor_ticks != 0u)
        return;
    const ActiveReaderState state = snapshotActiveReaderState();
    const bool still_active =
        state.content_id == g_pending_native_anchor_content_id &&
        state.webkit_view == g_pending_native_anchor_webkit_view &&
        state.direction_generation == g_pending_native_anchor_generation;
    void *anchor_webkit_view = g_pending_native_anchor_webkit_view;
    const QString anchor_content_id = g_pending_native_anchor_content_id;
    g_pending_native_anchor_content_id.clear();
    g_pending_native_anchor_webkit_view = nullptr;
    if (still_active) {
        if (g_native_kepub_api.go_to_bookmark_anchor != nullptr) {
            g_native_kepub_api.go_to_bookmark_anchor(anchor_webkit_view);
            nickelturn_event_log("NickelTurn: bookmark restore invoked after reflow");
        } else {
            nickelturn_event_log("NickelTurn: delayed native anchor restore unavailable; entrypoint is null ContentID=%s",
                                 anchor_content_id.toUtf8().constData());
        }
    } else {
        nickelturn_event_log("NickelTurn: delayed native anchor restore canceled; reader state changed ContentID=%s",
                             anchor_content_id.toUtf8().constData());
    }
}

// 還原後等兩個 timer tick，讀取實際目前位置並與重排前的 Bookmark 比對。
// 處理一次使用者切換：保存模式、更新 Kobo 原生方向與翻頁方向，再重新載入章節。
static void processNickelTurnToggleRequest() {
    // 前一次 reloadChapter 尚在等待原生錨點復原時，不允許覆蓋其暫存 Bookmark。
    if (g_pending_native_anchor_ticks != 0u) {
        nickelturn_event_log("NickelTurn: toggle ignored; previous layout change is still restoring its anchor");
        return;
    }
    const ActiveReaderState state = snapshotActiveReaderState();
    if (state.content_id.isEmpty() || state.source_mode == SourceModeUnknown ||
        state.webkit_view == nullptr) {
        nickelturn_event_log("NickelTurn: toggle ignored; active KEPUB layout is not ready ContentID=%s source-mode=%s webkit-view-present=%d",
                             state.content_id.toUtf8().constData(),
                             sourceModeName(state.source_mode),
                             state.webkit_view == nullptr ? 0 : 1);
        return;
    }
    const QByteArray content_id_utf8 = state.content_id.toUtf8();
    char current_mode[16];
    const bool has_override = lookupCachedModeJson(state.content_id, current_mode, sizeof(current_mode));
    if (!has_override)
        (void)snprintf(current_mode, sizeof(current_mode), "%s", sourceModeName(state.source_mode));
    const char *target_mode = strcmp(current_mode, "vertical-rl") == 0 ? "horizontal-tb" : "vertical-rl";
    if (!isNativeKepubApiReady()) {
        nickelturn_event_log("NickelTurn native layout entrypoint is missing; layout unchanged ContentID=%s",
                             content_id_utf8.constData());
        return;
    }
    void *custom_web_view = g_native_kepub_api.web_view(state.webkit_view);
    if (custom_web_view == nullptr) {
        nickelturn_event_log("NickelTurn: active CustomWebView is unavailable; layout unchanged ContentID=%s",
                             content_id_utf8.constData());
        return;
    }
    BookmarkStorage captured_bookmark;
    g_native_kepub_api.bookmark_for_current_location(&captured_bookmark, state.webkit_view);
    if (!persistCachedModeJson(state.content_id, target_mode)) {
        destroyCapturedBookmark(&captured_bookmark);
        nickelturn_event_log("NickelTurn JSON update failed; layout unchanged ContentID=%s",
                             content_id_utf8.constData());
        return;
    }
    const bool vertical = strcmp(target_mode, "vertical-rl") == 0;
    beginCssHandoffDiagnostic(current_mode);
    g_native_kepub_api.set_writing_direction(state.webkit_view, vertical ? 3 : 0);
    g_native_kepub_api.set_page_progression(custom_web_view, QByteArray(vertical ? "rtl" : "ltr"));
    g_native_kepub_api.update_html_css(state.webkit_view);
    // reloadChapter 會走 Kobo 正常的重新載入與 repaint 流程；其後的新 DOM 才能正確
    // 使用原生 Bookmark anchor，這個呼叫不能延後到 updateHtmlCss 的 queued work 之後。
    g_native_kepub_api.reload_chapter(state.webkit_view);
    // goToBookmarkAnchor 讀取 reader +0x1c0 的 Bookmark；只複製其 QSharedDataPointer。
    void *reader_bookmark_data = reinterpret_cast<char *>(state.webkit_view) +
                                 kReaderBookmarkOffset + sizeof(uint32_t);
    const void *captured_bookmark_data =
        reinterpret_cast<const char *>(&captured_bookmark) + sizeof(uint32_t);
    g_native_kepub_api.assign_bookmark_shared_data(reader_bookmark_data, captured_bookmark_data);
    destroyCapturedBookmark(&captured_bookmark);
    g_pending_native_anchor_content_id = state.content_id;
    g_pending_native_anchor_webkit_view = state.webkit_view;
    g_pending_native_anchor_generation = state.direction_generation;
    // reloadChapter 回傳時 replacement DOM 尚未完成；四個 tick 留出完整一秒等待。
    g_pending_native_anchor_ticks = 4u;
    nickelturn_event_log("NickelTurn: source-mode=%s from=%s to=%s ContentID=%s; preference saved and native reflow requested",
                         sourceModeName(state.source_mode), current_mode, target_mode,
                         content_id_utf8.constData());
}

// 工具列點擊若剛好早於方向偵測完成，暫存請求讓 Qt timer 在狀態就緒後送出。
static void handleReaderToolbarToggleClick() {
    const ActiveReaderState state = snapshotActiveReaderState();
    if (state.content_id.isEmpty()) {
        nickelturn_event_log("NickelTurn: toolbar toggle clicked without an active KEPUB");
        return;
    }
    if (g_pending_native_anchor_ticks != 0u) {
        nickelturn_event_log("NickelTurn: toolbar toggle ignored; bookmark restore is pending");
        return;
    }
    if (g_pending_toolbar_toggle_request) {
        nickelturn_event_log("NickelTurn: toolbar toggle click ignored; a request is already waiting");
        return;
    }
    // 延到 Qt timer 執行，避免在 Kobo 正顯示閱讀工具列的 clicked signal 內重入原生 reader。
    g_pending_toolbar_toggle_request = true;
    g_pending_toolbar_toggle_ticks = 12u;
    g_pending_toolbar_toggle_content_id = state.content_id;
    nickelturn_event_log("NickelTurn: toolbar toggle queued on Qt timer ContentID=%s source-mode=%s webkit-view-present=%d",
                         state.content_id.toUtf8().constData(), sourceModeName(state.source_mode),
                         state.webkit_view == nullptr ? 0 : 1);
}

static void processPendingReaderToolbarToggleClick() {
    if (!g_pending_toolbar_toggle_request)
        return;
    const ActiveReaderState state = snapshotActiveReaderState();
    if (state.content_id != g_pending_toolbar_toggle_content_id) {
        g_pending_toolbar_toggle_request = false;
        g_pending_toolbar_toggle_ticks = 0u;
        g_pending_toolbar_toggle_content_id.clear();
        nickelturn_event_log("NickelTurn: pending toolbar toggle canceled; active book changed");
        return;
    }
    if (state.source_mode != SourceModeUnknown && state.webkit_view != nullptr &&
        g_pending_native_anchor_ticks == 0u) {
        g_pending_toolbar_toggle_request = false;
        g_pending_toolbar_toggle_ticks = 0u;
        g_pending_toolbar_toggle_content_id.clear();
        nickelturn_event_log("NickelTurn: delayed toolbar toggle dispatched ContentID=%s",
                             state.content_id.toUtf8().constData());
        processNickelTurnToggleRequest();
        return;
    }
    if (g_pending_toolbar_toggle_ticks > 0u)
        --g_pending_toolbar_toggle_ticks;
    if (g_pending_toolbar_toggle_ticks == 0u) {
        g_pending_toolbar_toggle_request = false;
        g_pending_toolbar_toggle_content_id.clear();
        nickelturn_event_log("NickelTurn: toolbar toggle request expired; layout still not ready ContentID=%s source-mode=%s webkit-view-present=%d",
                             state.content_id.toUtf8().constData(), sourceModeName(state.source_mode),
                             state.webkit_view == nullptr ? 0 : 1);
    }
}

// 單色頁面圖示：左側以直線表示直排，右側以橫線表示橫排。
static QIcon createWritingDirectionIcon(const QWidget *widget) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(widget->palette().color(QPalette::ButtonText));
    pen.setWidthF(1.8);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    // 原生工具列圖示幾乎填滿 TouchLabel 的 pixmap；放大線稿本身，避免同尺寸 pixmap 裡的留白讓圖示看起來偏小。
    painter.translate(16.0, 16.0);
    painter.scale(1.25, 1.25);
    painter.translate(-16.0, -16.0);
    painter.drawRoundedRect(QRectF(4.5, 3.5, 23.0, 25.0), 2.0, 2.0);
    painter.drawLine(QPointF(16.0, 7.5), QPointF(16.0, 24.5));
    painter.drawLine(QPointF(9.0, 8.5), QPointF(9.0, 23.5));
    painter.drawLine(QPointF(12.5, 8.5), QPointF(12.5, 23.5));
    painter.drawLine(QPointF(19.0, 9.5), QPointF(24.0, 9.5));
    painter.drawLine(QPointF(19.0, 14.0), QPointF(24.0, 14.0));
    painter.drawLine(QPointF(19.0, 18.5), QPointF(24.0, 18.5));
    painter.drawLine(QPointF(19.0, 23.0), QPointF(24.0, 23.0));
    painter.end();
    return QIcon(pixmap);
}

struct ReaderMenuButtonAnchor {
    QBoxLayout *layout;
    QWidget *widget;
};

// fontButton()/statsButton() 回傳實際按鈕，不是按鈕內的 icon；往父層找出 layout 中的直接項目。
static ReaderMenuButtonAnchor findReaderMenuButtonAnchor(QWidget *button) {
    QWidget *item_widget = button;
    while (item_widget != nullptr) {
        QWidget *parent = item_widget->parentWidget();
        if (parent == nullptr)
            break;
        QBoxLayout *layout = qobject_cast<QBoxLayout *>(parent->layout());
        if (layout != nullptr && layout->indexOf(item_widget) >= 0)
            return {layout, item_widget};
        item_widget = parent;
    }
    return {nullptr, nullptr};
}

// 將按鈕插在閱讀工具列的原生控制項旁；先用字體按鈕，沒有時改用統計按鈕。
static void addReaderToolbarToggleButton(void *reading_menu_view) {
    if (!g_firmware_matches || reading_menu_view == nullptr)
        return;
    QWidget *view = reinterpret_cast<QWidget *>(reading_menu_view);
    QWidget *existing = view->findChild<QWidget *>(
        QString::fromLatin1("nickelTurnToggleButton"));
    if (existing != nullptr) {
        g_reader_toolbar_toggle_button = existing;
        return;
    }

    typedef QWidget *(*ReadingMenuButtonGetter)(const void *);
    ReadingMenuButtonGetter font_button_getter = reinterpret_cast<ReadingMenuButtonGetter>(
        nickelturn_reading_menu_view_font_button);
    ReadingMenuButtonGetter stats_button_getter = reinterpret_cast<ReadingMenuButtonGetter>(
        nickelturn_reading_menu_view_stats_button);
    QWidget *anchor_button = font_button_getter == nullptr
                                 ? nullptr
                                 : font_button_getter(reading_menu_view);
    bool uses_stats_anchor = false;
    if (anchor_button == nullptr && stats_button_getter != nullptr) {
        anchor_button = stats_button_getter(reading_menu_view);
        uses_stats_anchor = anchor_button != nullptr;
    }

    const ReaderMenuButtonAnchor anchor = findReaderMenuButtonAnchor(anchor_button);
    if (anchor.layout == nullptr || anchor.widget == nullptr) {
        if (!g_reader_toolbar_button_diagnostic_logged) {
            g_reader_toolbar_button_diagnostic_logged = true;
            nickelturn_event_log("NickelTurn: no ReadingMenuView font/stats button layout anchor");
        }
        return;
    }
    g_reader_toolbar_native_anchor = anchor_button;

    typedef void (*TouchLabelConstructorFn)(void *, QWidget *, uint32_t);
    TouchLabelConstructorFn touch_label_constructor = reinterpret_cast<TouchLabelConstructorFn>(
        nickelturn_touch_label_constructor);
    if (touch_label_constructor == nullptr) {
        nickelturn_event_log("NickelTurn: TouchLabel constructor unavailable; reader toolbar toggle not inserted");
        return;
    }
    // TouchLabel 是 libnickel 私有類別，沒有可用標頭；依 NickelMenu 的作法配置原生物件儲存區後呼叫其韌體建構函式。
    void *touch_label_storage = calloc(1, 256u);
    if (touch_label_storage == nullptr) {
        nickelturn_event_log("NickelTurn: cannot allocate TouchLabel storage; reader toolbar toggle not inserted");
        return;
    }
    QWidget *button_parent = anchor.widget->parentWidget();
    touch_label_constructor(touch_label_storage, button_parent, 0u);
    QWidget *button = reinterpret_cast<QWidget *>(touch_label_storage);
    button->setObjectName(QString::fromLatin1("nickelTurnToggleButton"));
    QAbstractButton *native_button = qobject_cast<QAbstractButton *>(anchor_button);
    QLabel *native_label = qobject_cast<QLabel *>(anchor_button);
    QSize native_icon_size = native_button == nullptr ? QSize() : native_button->iconSize();
    if (native_label != nullptr) {
        const QPixmap *native_pixmap = native_label->pixmap();
        if (native_pixmap != nullptr && !native_pixmap->isNull())
            native_icon_size = native_pixmap->size();
    }
    QSize native_button_size = anchor_button->sizeHint();
    if (native_icon_size.width() <= 0 || native_icon_size.height() <= 0)
        native_icon_size = QSize(44, 44);
    if (native_button_size.width() > 0 && native_button_size.height() > 0)
        button->setFixedSize(native_button_size);
    const QPixmap icon_pixmap = createWritingDirectionIcon(button).pixmap(native_icon_size);
    QLabel *label = qobject_cast<QLabel *>(button);
    if (label != nullptr) {
        label->setPixmap(icon_pixmap);
        label->setAlignment(native_label == nullptr ? Qt::AlignCenter : native_label->alignment());
        if (native_label != nullptr) {
            label->setMargin(native_label->margin());
            label->setIndent(native_label->indent());
        }
    }
    typedef void (*TouchLabelPixmapSetterFn)(void *, const QPixmap &);
    TouchLabelPixmapSetterFn set_deselected_pixmap = reinterpret_cast<TouchLabelPixmapSetterFn>(
        nickelturn_touch_label_set_deselected_pixmap);
    TouchLabelPixmapSetterFn set_selected_pixmap = reinterpret_cast<TouchLabelPixmapSetterFn>(
        nickelturn_touch_label_set_selected_pixmap);
    if (set_deselected_pixmap != nullptr)
        set_deselected_pixmap(touch_label_storage, icon_pixmap);
    if (set_selected_pixmap != nullptr)
        set_selected_pixmap(touch_label_storage, icon_pixmap);
    button->setToolTip(QString::fromUtf8("直橫排互換"));
    button->setFocusPolicy(native_button == nullptr ? Qt::NoFocus : native_button->focusPolicy());
    QPushButton *click_shim = new QPushButton(button_parent);
    click_shim->setObjectName(QString::fromLatin1("nickelTurnClickShim"));
    click_shim->setVisible(false);
    const bool tap_connection = QObject::connect(
        button, SIGNAL(tapped(bool)), click_shim, SIGNAL(pressed()));
    if (!tap_connection) {
        nickelturn_event_log("NickelTurn: cannot connect TouchLabel::tapped(bool) to click shim");
        delete click_shim;
        delete button;
        return;
    }
    QObject::connect(click_shim, &QPushButton::pressed, click_shim,
                     []() { handleReaderToolbarToggleClick(); });
    const int anchor_index = anchor.layout->indexOf(anchor.widget);
    anchor.layout->insertWidget(anchor_index + 1, button);
    g_reader_toolbar_toggle_button = button;
    if (!g_reader_toolbar_button_setup_log_written) {
        g_reader_toolbar_button_uses_stats_anchor = uses_stats_anchor;
        g_reader_toolbar_button_setup_log_pending = true;
    }
}

// 原生 ReadingMenuView 建構並建立工具列 widget 後立即插入按鈕，避免顯示後再觸發第二次排版。
extern "C" void NickelTurnReadingMenuViewConstructor(
    void *reading_menu_view, void *parent, const QByteArray &name, bool compact) {
    typedef void (*ReadingMenuViewConstructorFn)(void *, void *, const QByteArray &, bool);
    ReadingMenuViewConstructorFn original = reinterpret_cast<ReadingMenuViewConstructorFn>(
        nickelturn_original_reading_menu_view_constructor);
    if (original == nullptr) {
        nickelturn_log("NickelTurn original ReadingMenuView constructor pointer missing");
        return;
    }
    original(reading_menu_view, parent, name, compact);
    addReaderToolbarToggleButton(reading_menu_view);
}

static void updateReaderToolbarToggleButton() {
    if (g_reader_toolbar_toggle_button.isNull())
        return;
    const ActiveReaderState state = snapshotActiveReaderState();
    const bool has_active_kepub = !state.content_id.isEmpty();
    g_reader_toolbar_toggle_button->setVisible(has_active_kepub);
    const bool ready = has_active_kepub && !g_pending_toolbar_toggle_request &&
                       g_pending_native_anchor_ticks == 0u;
    g_reader_toolbar_toggle_button->setEnabled(ready);
}

// 每 250ms 在 Qt 主執行緒處理延後工作與更新按鈕狀態。
static void processNickelTurnTimer() {
    processPendingHorizontalSourceMode();
    processPendingNativeAnchorRestore();
    processCssHandoffDiagnostic();
    if (g_reader_toolbar_button_setup_log_pending && !g_reader_toolbar_button_setup_log_written) {
        g_reader_toolbar_button_setup_log_pending = false;
        g_reader_toolbar_button_setup_log_written = true;
        const char *native_class = g_reader_toolbar_native_anchor.isNull() ||
                                           g_reader_toolbar_native_anchor->metaObject() == nullptr
                                       ? "unknown"
                                       : g_reader_toolbar_native_anchor->metaObject()->className();
        const char *button_class = g_reader_toolbar_toggle_button.isNull() ||
                                           g_reader_toolbar_toggle_button->metaObject() == nullptr
                                       ? "unknown"
                                       : g_reader_toolbar_toggle_button->metaObject()->className();
        nickelturn_event_log("NickelTurn: reader toolbar button inserted anchor=%s native-class=%s native-abstract-button=%d button-class=%s tap-signal=TouchLabel::tapped(bool) shim=QPushButton::pressed",
                             g_reader_toolbar_button_uses_stats_anchor ? "statsButton" : "fontButton",
                             native_class,
                             g_reader_toolbar_native_anchor.isNull() ||
                                     qobject_cast<QAbstractButton *>(g_reader_toolbar_native_anchor.data()) == nullptr
                                 ? 0 : 1,
                             button_class);
    }
    if (g_reader_toolbar_input_log_pending && !g_reader_toolbar_input_log_written) {
        if (!g_reader_toolbar_input_sequence_ended && g_reader_toolbar_input_ticks_remaining > 0u)
            --g_reader_toolbar_input_ticks_remaining;
        if (g_reader_toolbar_input_sequence_ended || g_reader_toolbar_input_ticks_remaining == 0u) {
            g_reader_toolbar_input_log_pending = false;
            g_reader_toolbar_input_log_written = true;
            QStringList event_names;
            if ((g_reader_toolbar_input_event_mask & (1u << 0)) != 0u) event_names.append(QString::fromLatin1("mouse-press"));
            if ((g_reader_toolbar_input_event_mask & (1u << 1)) != 0u) event_names.append(QString::fromLatin1("mouse-release"));
            if ((g_reader_toolbar_input_event_mask & (1u << 2)) != 0u) event_names.append(QString::fromLatin1("touch-begin"));
            if ((g_reader_toolbar_input_event_mask & (1u << 3)) != 0u) event_names.append(QString::fromLatin1("touch-update"));
            if ((g_reader_toolbar_input_event_mask & (1u << 4)) != 0u) event_names.append(QString::fromLatin1("touch-end"));
            nickelturn_event_log("NickelTurn: first toolbar input events=%s first-target=%s last-target=%s",
                                 event_names.join(QString::fromLatin1(",")).toUtf8().constData(),
                                 g_reader_toolbar_input_first_target,
                                 g_reader_toolbar_input_last_target);
        }
    }
    processPendingReaderToolbarToggleClick();
    updateReaderToolbarToggleButton();
}

// 建立 NickelTurn 狀態目錄並啟動延遲工作用的 Qt timer。
static bool installNickelTurnRuntime() {
    if (!ensureNickelTurnDirectories())
        return false;
    QCoreApplication *app = QCoreApplication::instance();
    if (app == nullptr) {
        nickelturn_event_log("NickelTurn Qt application unavailable; reader toolbar integration disabled");
        return false;
    }
    app->installEventFilter(new ReaderToolbarInputDiagnosticFilter(app));
    g_nickelturn_timer = new QTimer(app);
    g_nickelturn_timer->setInterval(250);
    QObject::connect(g_nickelturn_timer, &QTimer::timeout, processNickelTurnTimer);
    g_nickelturn_timer->start();
    return true;
}

// Hook KEPUB CSS 交接點；命中 JSON 模式時才附加直排或橫排 CSS。
extern "C" void NickelTurnWebkitAddCssToHtml(void *webkit_view, QString css) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    WebkitAddCssToHtmlFn original = reinterpret_cast<WebkitAddCssToHtmlFn>(nickelturn_original_webkit_add_css_to_html);
    if (original == nullptr) {
        nickelturn_log("NickelTurn original WebkitView::addCssToHtml pointer missing");
        return;
    }

    const bool kepub_caller = wasCalledByKepubCssBuilder(caller);
    if (kepub_caller) {
        // 必須在附加 JSON 覆寫前讀取 css，才能取得書籍原始而非有效排版方向。
        observeVerticalSourceModeFromCss(css);
        QString content_id;
        char override_mode[16];
        if (getActiveModeJsonOverride(&content_id, override_mode, sizeof(override_mode))) {
            recordCssHandoffDiagnostic(CssHandoffDiagnosticAppended, content_id, override_mode);
            if (strcmp(override_mode, "vertical-rl") == 0)
                css.append(QLatin1String(kVerticalOverrideCss));
            else
                css.append(QLatin1String(kHorizontalOverrideCss));
        } else if (content_id.isEmpty()) {
            recordCssHandoffDiagnostic(CssHandoffDiagnosticNoActiveContent, content_id, nullptr);
        } else {
            recordCssHandoffDiagnostic(CssHandoffDiagnosticNoOverride, content_id, nullptr);
        }
    }

    original(webkit_view, css);
}

// 在 Nickel 啟動時驗證所有實際使用的 firmware symbol／offset，再啟用功能。
extern "C" int nickelturn_initialize(void) {
    nickelturn_event_log("NickelTurn: firmware profile=%s Build ID=%s",
                         kFirmwareVersion, kFirmwareBuildId);
    // 移除舊版 NickelMenu 整合留下的 NickelTurn 專屬檔案。
    removeLegacyNickelMenuArtifact(kNickelTurnNickelMenuConfig, "config");
    removeLegacyNickelMenuArtifact(kNickelTurnToggleScript, "toggle script");
    removeLegacyNickelMenuArtifact(kNickelTurnReadyFlag, "ready marker");
    void *const required_symbols[] = {
        nickelturn_original_webkit_add_css_to_html,
        nickelturn_kepub_add_css_to_html,
        nickelturn_update_html_css,
        nickelturn_reload_chapter,
        nickelturn_webkit_web_view,
        nickelturn_custom_set_page_progression_direction,
        nickelturn_original_reading_view_left_to_right_page_progress_direction,
        nickelturn_bookmark_for_current_location,
        nickelturn_bookmark_shared_data_destructor,
        nickelturn_scobject_destructor,
        nickelturn_kepub_go_to_bookmark_anchor,
        nickelturn_bookmark_shared_data_assign,
        nickelturn_original_content_get_book_id,
        nickelturn_content_get_book_id,
        nickelturn_kepub_render_shortcover,
        nickelturn_original_webkit_set_writing_direction,
        nickelturn_kepub_set_writing_direction,
        nickelturn_webkit_set_writing_direction,
        nickelturn_original_reading_menu_view_constructor,
    };
    const char *const required_symbol_names[] = {
        "WebkitView::addCssToHtml", "KepubBookReader::addCssToHtml",
        "KepubBookReaderBase::updateHtmlCss", "KepubBookReaderBase::reloadChapter",
        "WebkitView::webView", "CustomWebView::setPageProgressionDirection",
        "ReadingView::leftToRightPageProgressDirection",
        "KepubBookReaderBase::bookmarkForCurrentLocation",
        "Bookmark shared-data destructor", "ScObject destructor",
        "KepubBookReaderBase::goToBookmarkAnchor", "Bookmark shared-data assignment",
        "Content::getBookId", "Content::getBookId KEPUB caller guard",
        "KepubBookReader::renderShortcover", "WebkitView::setWritingDirection original",
        "KepubBookReader::setWritingDirection", "WebkitView::setWritingDirection KEPUB caller guard",
        "ReadingMenuView constructor",
    };
    if (!areRequiredSymbolsAvailable(required_symbols,
                                     required_symbol_names,
                                     sizeof(required_symbols) / sizeof(required_symbols[0]))) {
        return 1;
    }

    Dl_info base_info;
    if (dladdr(nickelturn_kepub_add_css_to_html, &base_info) == 0 || base_info.dli_fbase == nullptr) {
        nickelturn_event_log("NickelTurn: cannot determine libnickel base address");
        return 1;
    }
    const FirmwareOffsetGuard guards[] = {
        {nickelturn_kepub_add_css_to_html, kKepubAddCssToHtmlOffset,
         "KepubBookReader::addCssToHtml", &g_kepub_add_css_start},
        {nickelturn_update_html_css, kUpdateHtmlCssOffset,
         "KepubBookReaderBase::updateHtmlCss", nullptr},
        {nickelturn_reload_chapter, kReloadChapterOffset,
         "KepubBookReaderBase::reloadChapter", nullptr},
        {nickelturn_custom_set_page_progression_direction,
         kCustomSetPageProgressionDirectionOffset,
         "CustomWebView::setPageProgressionDirection", nullptr},
        {nickelturn_kepub_render_shortcover, kKepubRenderShortcoverOffset,
         "KepubBookReader::renderShortcover", &g_kepub_render_shortcover_start},
        {nickelturn_content_get_book_id, kContentGetBookIdOffset, "Content::getBookId", nullptr},
        {nickelturn_kepub_set_writing_direction, kKepubSetWritingDirectionOffset,
         "KepubBookReader::setWritingDirection", &g_kepub_set_writing_direction_start},
        {nickelturn_webkit_set_writing_direction, kWebkitSetWritingDirectionOffset,
         "WebkitView::setWritingDirection", nullptr},
        {nickelturn_bookmark_for_current_location, kBookmarkForCurrentLocationOffset,
         "KepubBookReaderBase::bookmarkForCurrentLocation", nullptr},
        {nickelturn_kepub_go_to_bookmark_anchor, kKepubGoToBookmarkAnchorOffset,
         "KepubBookReaderBase::goToBookmarkAnchor", nullptr},
        {nickelturn_bookmark_shared_data_assign, kBookmarkSharedDataAssignOffset,
         "Bookmark shared-data assignment", nullptr},
        {nickelturn_original_reading_menu_view_constructor,
         kReadingMenuViewConstructorOffset,
         "ReadingMenuView constructor", nullptr},
    };
    for (size_t i = 0; i < sizeof(guards) / sizeof(guards[0]); ++i) {
        if (!validateLibnickelOffset(guards[i].symbol, base_info.dli_fbase, guards[i].offset,
                                     guards[i].symbol_name, guards[i].normalized_address)) {
            return 1;
        }
    }

    g_native_kepub_api.update_html_css =
        reinterpret_cast<KepubUpdateHtmlCssFn>(nickelturn_update_html_css);
    g_native_kepub_api.reload_chapter =
        reinterpret_cast<KepubReloadChapterFn>(nickelturn_reload_chapter);
    g_native_kepub_api.set_writing_direction =
        reinterpret_cast<KepubSetWritingDirectionFn>(nickelturn_kepub_set_writing_direction);
    g_native_kepub_api.web_view = reinterpret_cast<WebkitWebViewFn>(nickelturn_webkit_web_view);
    g_native_kepub_api.set_page_progression = reinterpret_cast<CustomSetPageProgressionDirectionFn>(
        nickelturn_custom_set_page_progression_direction);
    g_native_kepub_api.bookmark_for_current_location =
        reinterpret_cast<BookmarkForCurrentLocationFn>(nickelturn_bookmark_for_current_location);
    g_native_kepub_api.destroy_bookmark_shared_data =
        reinterpret_cast<BookmarkStorageDestructorFn>(nickelturn_bookmark_shared_data_destructor);
    g_native_kepub_api.destroy_scobject =
        reinterpret_cast<BookmarkStorageDestructorFn>(nickelturn_scobject_destructor);
    g_native_kepub_api.go_to_bookmark_anchor =
        reinterpret_cast<KepubGoToBookmarkAnchorFn>(nickelturn_kepub_go_to_bookmark_anchor);
    g_native_kepub_api.assign_bookmark_shared_data =
        reinterpret_cast<BookmarkSharedDataAssignFn>(nickelturn_bookmark_shared_data_assign);
    if (!isNativeKepubApiReady()) {
        return 1;
    }

    g_firmware_matches = true;
    parseModeJsonAtInit();
    if (!installNickelTurnRuntime())
        return 1;
    nickelturn_event_log("NickelTurn: initialization complete; reader toolbar integration ready");
    return 0;
}
