#pragma once
#include "aura/language.hpp"
namespace aura::l10n {
struct TransferStrings {
    const char *title, *entry, *intro, *start, *stop, *starting, *scan, *pin, *remaining, *completed;
    const char *ready, *receiving, *expired, *locked, *failed, *local_only, *network, *storage, *exists, *image,
        *interrupted;
    const char *trust, *open_browser;
    // The phone sign-in (ADR 0010): the QR's line, the PC's column, the hint.
    const char *phone_scan, *phone_pc, *phone_wifi;
};
const TransferStrings& transfer_strings(InterfaceLanguage language);
}  // namespace aura::l10n
