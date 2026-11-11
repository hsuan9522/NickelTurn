#pragma once

// Verified against libnickel.so.1.0.0, Build ID
// 8515657b0cb4e08354dba85716b3b93e1984e2d5.
const char kFirmwareVersion[] = "4.46.23852";
const char kFirmwareBuildId[] = "8515657b0cb4e08354dba85716b3b93e1984e2d5";
const uintptr_t kKepubAddCssToHtmlOffset = 0x00fb06e8u;
const uintptr_t kKepubAddCssToHtmlSize = 0x000000e8u;
const uintptr_t kUpdateHtmlCssOffset = 0x00b89c68u;
const uintptr_t kReloadChapterOffset = 0x00b7e7b0u;
const uintptr_t kBookmarkForCurrentLocationOffset = 0x00b83368u;
const uintptr_t kKepubGoToBookmarkAnchorOffset = 0x00b7e3e0u;
const uintptr_t kBookmarkSharedDataAssignOffset = 0x00c7aadcu;
// Private reader layout field; validated by the successful 4.46 device test.
const size_t kReaderBookmarkOffset = 0x1c0u;
const uintptr_t kContentGetBookIdOffset = 0x00977df0u;
const uintptr_t kKepubRenderShortcoverOffset = 0x00fb1340u;
const uintptr_t kKepubRenderShortcoverSize = 0x00000210u;
const uintptr_t kKepubSetWritingDirectionOffset = 0x00fb12ecu;
const uintptr_t kKepubSetWritingDirectionSize = 0x00000054u;
const uintptr_t kWebkitSetWritingDirectionOffset = 0x00bd12a8u;
const uintptr_t kCustomSetPageProgressionDirectionOffset = 0x00bc9924u;
// ReadingMenuView constructor, verified in the supplied firmware ELF.
const uintptr_t kReadingMenuViewConstructorOffset = 0x00fcdf40u;
// ReadingMenuScrubberController::loadView stores its view pointer at this+0x14.
const size_t kScrubberControllerViewOffset = 0x14u;
