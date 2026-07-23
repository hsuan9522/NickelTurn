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

static struct nh_info NickelTurn = {
    .name = "NickelTurn",
    .desc = "KEPUB vertical/horizontal layout toggle",
    .uninstall_flag = "/mnt/onboard/.adds/nickelturn/uninstall",
    // 啟動時 crash 會留下 .failsafe library，供下一次開機自動復原。
    .failsafe_delay = 20,
};

static struct nh_hook NickelTurnHook[] = {
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
    {0},
};

static struct nh_dlsym NickelTurnDlsym[] = {
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
