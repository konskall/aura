#pragma once
#include "aura/language.hpp"
namespace aura::l10n {
struct PhotoStrings {
    const char *title, *settings, *loading, *empty, *missing, *bad_file, *too_large, *save_failed;
    const char *card_hint, *desktop_hint, *refresh, *slideshow, *interval, *seconds, *minute;
    const char *ambient, *ambient_hint, *ambient_none, *local_hint, *count, *previous, *next, *page;
    const char* use_photo;
    const char *delete_photo, *delete_title, *delete_hint, *delete_confirm, *cancel, *delete_failed, *deleting;
};
const PhotoStrings& photo_strings(InterfaceLanguage language);
}  // namespace aura::l10n
