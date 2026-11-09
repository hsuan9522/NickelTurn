#include <NickelHook.h>
#include "nickelturn_log.h"

// 外掛描述維持 C 語言。NickelTC 的舊 C++ 編譯器無法編譯
// NickelHook(...) 巨集所使用的 designated initializer。
extern int nickelturn_initialize(void);
extern bool nickelturn_uninstall(void);
extern void *nickelturn_original_webkit_add_css_to_html;
extern void *nickelturn_kepub_add_css_to_html;
extern void *nickelturn_update_html_css;
extern void *nickelturn_reload_chapter;
extern void *nickelturn_webkit_web_view;
extern void *nickelturn_custom_set_page_progression_direction;
extern void *nickelturn_original_reading_view_left_to_right_page_progress_direction;
extern void *nickelturn_bookmark_for_current_location;
extern void *nickelturn_bookmark_shared_data_destructor;
extern void *nickelturn_scobject_destructor;
extern void *nickelturn_kepub_go_to_bookmark_anchor;
extern void *nickelturn_bookmark_shared_data_assign;
extern void *nickelturn_original_content_get_book_id;
extern void *nickelturn_content_get_book_id;
extern void *nickelturn_kepub_render_shortcover;
extern void *nickelturn_original_webkit_set_writing_direction;
extern void *nickelturn_kepub_set_writing_direction;
extern void *nickelturn_webkit_set_writing_direction;
extern void *nickelturn_original_reading_menu_view_constructor;
extern void *nickelturn_reading_menu_view_font_button;
extern void *nickelturn_reading_menu_view_stats_button;
extern void *nickelturn_touch_label_constructor;
extern void *nickelturn_touch_label_set_deselected_pixmap;
extern void *nickelturn_touch_label_set_selected_pixmap;

static struct nh_info NickelTurn = {
    .name = "NickelTurn",
    .desc = "KEPUB vertical/horizontal layout toggle",
    .uninstall_flag = "/mnt/onboard/.adds/nickel-turn/uninstall",
    // 啟動時 crash 會留下 .failsafe library，供下一次開機自動復原。
    .failsafe_delay = 20,
};

static struct nh_hook NickelTurnHook[] = {
    // ！！韌體升級檢查點：NickelHook 以這些 mangled symbol 名稱解析／替換函式。
    // 新版若改名、移除或改變 hook ABI，需依新 libnickel 更新名稱與實作，
    // 並同步檢查 nickelturn.cc 的 dlsym 清單、offset guard 和函式型別。
    {
        .sym = "_ZN10WebkitView12addCssToHtmlE7QString",
        .sym_new = "NickelTurnWebkitAddCssToHtml",
        .lib = "libnickel.so.1",
        .out = &nickelturn_original_webkit_add_css_to_html,
        .desc = "NickelTurn KEPUB CSS handoff",
    },
    {
        .sym = "_ZNK7Content9getBookIdEv",
        .sym_new = "NickelTurnContentGetBookId",
        .lib = "libnickel.so.1",
        .out = &nickelturn_original_content_get_book_id,
        .desc = "NickelTurn active KEPUB ContentID observation",
    },
    {
        .sym = "_ZN10WebkitView19setWritingDirectionE16WritingDirection",
        .sym_new = "NickelTurnWebkitSetWritingDirection",
        .lib = "libnickel.so.1",
        .out = &nickelturn_original_webkit_set_writing_direction,
        .desc = "NickelTurn active KEPUB writing direction observation",
    },
    {
        .sym = "_ZNK11ReadingView32leftToRightPageProgressDirectionEv",
        .sym_new = "NickelTurnReadingViewLeftToRightPageProgressDirection",
        .lib = "libnickel.so.1",
        .out = &nickelturn_original_reading_view_left_to_right_page_progress_direction,
        .desc = "NickelTurn active KEPUB swipe page-progression override",
    },
    {
        .sym = "_ZN15ReadingMenuViewC1EP7QWidgetRK10QByteArrayb",
        .sym_new = "NickelTurnReadingMenuViewConstructor",
        .lib = "libnickel.so.1",
        .out = &nickelturn_original_reading_menu_view_constructor,
        .desc = "NickelTurn reader toolbar button",
    },
    {0},
};

static struct nh_dlsym NickelTurnDlsym[] = {
    {
        .name = "_ZNK15ReadingMenuView10fontButtonEv",
        .out = &nickelturn_reading_menu_view_font_button,
        .desc = "ReadingMenuView native font button getter",
        .optional = true,
    },
    {
        .name = "_ZNK15ReadingMenuView11statsButtonEv",
        .out = &nickelturn_reading_menu_view_stats_button,
        .desc = "ReadingMenuView native stats button getter",
        .optional = true,
    },
    {
        .name = "_ZN10TouchLabelC1EP7QWidget6QFlagsIN2Qt10WindowTypeEE",
        .out = &nickelturn_touch_label_constructor,
        .desc = "Kobo TouchLabel constructor for reader toolbar tap control",
        .optional = true,
    },
    {
        .name = "_ZN10TouchLabel19setDeselectedPixmapERK7QPixmap",
        .out = &nickelturn_touch_label_set_deselected_pixmap,
        .desc = "Kobo TouchLabel normal icon setter",
        .optional = true,
    },
    {
        .name = "_ZN10TouchLabel17setSelectedPixmapERK7QPixmap",
        .out = &nickelturn_touch_label_set_selected_pixmap,
        .desc = "Kobo TouchLabel selected icon setter",
        .optional = true,
    },
    {
        .name = "_ZN15KepubBookReader12addCssToHtmlE7QString",
        .out = &nickelturn_kepub_add_css_to_html,
        .desc = "NickelTurn firmware guard",
    },
    {
        .name = "_ZN19KepubBookReaderBase13updateHtmlCssEv",
        .out = &nickelturn_update_html_css,
        .desc = "NickelTurn complete KEPUB CSS rebuild",
    },
    {
        .name = "_ZN19KepubBookReaderBase13reloadChapterEv",
        .out = &nickelturn_reload_chapter,
        .desc = "NickelTurn native KEPUB reflow completion",
    },
    {
        .name = "_ZNK10WebkitView7webViewEv",
        .out = &nickelturn_webkit_web_view,
        .desc = "NickelTurn active CustomWebView access",
    },
    {
        .name = "_ZN13CustomWebView27setPageProgressionDirectionERK10QByteArray",
        .out = &nickelturn_custom_set_page_progression_direction,
        .desc = "NickelTurn native page-progression direction",
    },
    {
        .name = "_ZN19KepubBookReaderBase26bookmarkForCurrentLocationEv",
        .out = &nickelturn_bookmark_for_current_location,
        .desc = "NickelTurn current KEPUB text anchor capture",
    },
    {
        .name = "_ZN18QSharedDataPointerI15BookmarkPrivateED1Ev",
        .out = &nickelturn_bookmark_shared_data_destructor,
        .desc = "NickelTurn captured Bookmark cleanup",
    },
    {
        .name = "_ZN8ScObjectD1Ev",
        .out = &nickelturn_scobject_destructor,
        .desc = "NickelTurn captured Bookmark base cleanup",
    },
    {
        .name = "_ZN19KepubBookReaderBase18goToBookmarkAnchorEv",
        .out = &nickelturn_kepub_go_to_bookmark_anchor,
        .desc = "NickelTurn native KEPUB bookmark-anchor restore",
    },
    {
        .name = "_ZN18QSharedDataPointerI15BookmarkPrivateEaSERKS1_",
        .out = &nickelturn_bookmark_shared_data_assign,
        .desc = "NickelTurn copy current Bookmark into KEPUB reader",
    },
    {
        .name = "_ZN15KepubBookReader16renderShortcoverERK10Shortcover21ReadingNavigationType",
        .out = &nickelturn_kepub_render_shortcover,
        .desc = "NickelTurn active KEPUB render caller guard",
    },
    {
        .name = "_ZNK7Content9getBookIdEv",
        .out = &nickelturn_content_get_book_id,
        .desc = "NickelTurn ContentID firmware guard",
    },
    {
        .name = "_ZN15KepubBookReader19setWritingDirectionE16WritingDirection",
        .out = &nickelturn_kepub_set_writing_direction,
        .desc = "NickelTurn KEPUB direction caller guard",
    },
    {
        .name = "_ZN10WebkitView19setWritingDirectionE16WritingDirection",
        .out = &nickelturn_webkit_set_writing_direction,
        .desc = "NickelTurn WebkitView direction setter firmware guard",
    },
    {0},
};

// 不使用 NickelHook(...)：NickelTC 內附的舊 GCC 不接受其全域 compound literal。
// 此處是 ABI 等價的 C initializer。
__attribute__((visibility("default"))) struct nh NickelHook = {
    .init = nickelturn_initialize,
    .info = &NickelTurn,
    .hook = NickelTurnHook,
    .dlsym = NickelTurnDlsym,
    .uninstall = nickelturn_uninstall,
};
