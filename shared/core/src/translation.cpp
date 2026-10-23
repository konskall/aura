#include "aura/translation.hpp"

#include "aura/text_util.hpp"

#include <algorithm>

namespace aura {

bool translation_language_supported(const std::string& code) noexcept {
    return code == "en" || code == "de" || code == "fr" || code == "it" || code == "es";
}

std::uint32_t TranslationModel::begin(TranslateSide side, TranslationStatus status) {
    auto& entries = snapshot_.entries;
    if (entries.size() >= kMaxEntries) entries.erase(entries.begin());
    TranslationEntry entry;
    entry.id = next_id_++;
    entry.side = side;
    entry.status = status;
    entries.push_back(std::move(entry));
    changed();
    return entries.back().id;
}

bool TranslationModel::set_status(std::uint32_t id, TranslationStatus status) {
    auto* entry = find_mutable(id);
    if (!entry) return false;
    entry->status = status;
    changed();
    return true;
}

bool TranslationModel::set_source(std::uint32_t id, const std::string& text) {
    auto* entry = find_mutable(id);
    if (!entry) return false;
    entry->source = sanitize_text(text, kMaxTextBytes, true);
    entry->status = TranslationStatus::Translating;
    changed();
    return true;
}

bool TranslationModel::complete(std::uint32_t id, const std::string& translated) {
    auto* entry = find_mutable(id);
    if (!entry) return false;
    entry->translated = sanitize_text(translated, kMaxTextBytes, true);
    if (entry->translated.empty()) return fail(id, "EMPTY_TRANSLATION");
    entry->status = TranslationStatus::Ready;
    entry->error_code.clear();
    changed();
    return true;
}

bool TranslationModel::fail(std::uint32_t id, const std::string& code) {
    auto* entry = find_mutable(id);
    if (!entry) return false;
    entry->status = TranslationStatus::Failed;
    entry->error_code = sanitize_text(code, 48);
    changed();
    return true;
}

bool TranslationModel::cancel(std::uint32_t id) {
    auto& entries = snapshot_.entries;
    const auto found = std::find_if(entries.begin(), entries.end(),
                                    [id](const TranslationEntry& entry) { return entry.id == id; });
    if (found == entries.end()) return false;
    entries.erase(found);
    changed();
    return true;
}

bool TranslationModel::set_their_language(const std::string& code) {
    if (!translation_language_supported(code)) return false;
    if (snapshot_.their_language != code) {
        snapshot_.their_language = code;
        snapshot_.entries.clear();
        changed();
    }
    return true;
}

void TranslationModel::clear() {
    if (snapshot_.entries.empty()) return;
    snapshot_.entries.clear();
    changed();
}

const TranslationEntry* TranslationModel::find(std::uint32_t id) const noexcept {
    for (const auto& entry : snapshot_.entries) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

TranslationEntry* TranslationModel::find_mutable(std::uint32_t id) noexcept {
    for (auto& entry : snapshot_.entries) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

const std::string& TranslationModel::source_language(TranslateSide side) const noexcept {
    return side == TranslateSide::Mine ? snapshot_.my_language : snapshot_.their_language;
}

const std::string& TranslationModel::target_language(TranslateSide side) const noexcept {
    return side == TranslateSide::Mine ? snapshot_.their_language : snapshot_.my_language;
}

}  // namespace aura
