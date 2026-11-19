#pragma once
#include "aura/language.hpp"
namespace aura::l10n {
// The Αρχεία app: what the memory card holds.
struct FileStrings {
    const char *title, *card, *space_format, *no_card, *no_card_hint, *loading, *empty, *read_failed, *truncated_format;
    const char *page_format, *folder, *location, *size, *modified, *kind, *kind_image, *kind_audio, *kind_document,
        *kind_other, *device_file;
    const char *open, *delete_item, *delete_title, *delete_file_hint, *delete_folder_hint, *delete_confirm, *cancel,
        *deleted, *delete_failed, *deleting, *refresh, *decimal;
};
const FileStrings& file_strings(InterfaceLanguage language);
}  // namespace aura::l10n
