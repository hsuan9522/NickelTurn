#pragma once

// Verified against pre-libnickel.so.1.0.0, Build ID
// f043a6aed1c782880be5d6075a22619bd8e49dcb.
const char kFirmwareVersion[] = "4.45.23697";
const char kFirmwareBuildId[] = "f043a6aed1c782880be5d6075a22619bd8e49dcb";
const uintptr_t kKepubAddCssToHtmlOffset = 0x00fa4a90u;
const uintptr_t kKepubAddCssToHtmlSize = 0x000000e8u;
const uintptr_t kUpdateHtmlCssOffset = 0x00b81510u;
const uintptr_t kReloadChapterOffset = 0x00b76058u;
const uintptr_t kBookmarkForCurrentLocationOffset = 0x00b7ac10u;
const uintptr_t kKepubGoToBookmarkAnchorOffset = 0x00b75c88u;
const uintptr_t kBookmarkSharedDataAssignOffset = 0x00c72424u;
// Private reader layout field; this value is covered by the historical 4.45 runtime-tested build.
const size_t kReaderBookmarkOffset = 0x1c0u;
const uintptr_t kContentGetBookIdOffset = 0x00970038u;
const uintptr_t kKepubRenderShortcoverOffset = 0x00fa56e8u;
const uintptr_t kKepubRenderShortcoverSize = 0x00000210u;
const uintptr_t kKepubSetWritingDirectionOffset = 0x00fa5694u;
const uintptr_t kKepubSetWritingDirectionSize = 0x00000054u;
const uintptr_t kWebkitSetWritingDirectionOffset = 0x00bc8b50u;
const uintptr_t kCustomSetPageProgressionDirectionOffset = 0x00bc11ccu;
// ReadingMenuView constructor, verified in the supplied firmware ELF.
const uintptr_t kReadingMenuViewConstructorOffset = 0x00fc2308u;
