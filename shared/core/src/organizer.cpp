#include "aura/organizer.hpp"

#include "aura/text_util.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace aura {
namespace {

std::string escape_line(const std::string& text) {
    std::string output;
    output.reserve(text.size());
    for (const char ch : text) {
        if (ch == '\\') output += "\\\\";
        else if (ch == '\n') output += "\\n";
        else if (ch == '\t') output += "\\t";
        else output.push_back(ch);
    }
    return output;
}

std::string unescape_line(const std::string& text) {
    std::string output;
    output.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '\\' || index + 1 >= text.size()) {
            output.push_back(text[index]);
            continue;
        }
        const char next = text[++index];
        output.push_back(next == 'n' ? '\n' : (next == 't' ? '\t' : next));
    }
    return output;
}

bool parse_unsigned(const std::string& text, std::uint64_t& value) {
    if (text.empty() || text.size() > 19) return false;
    std::uint64_t result = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        if (result > (UINT64_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    value = result;
    return true;
}

bool parse_signed(const std::string& text, std::int64_t& value) {
    if (text.empty()) return false;
    const bool negative = text.front() == '-';
    std::uint64_t magnitude = 0;
    if (!parse_unsigned(negative ? text.substr(1) : text, magnitude) || magnitude > INT64_MAX) return false;
    value = negative ? -static_cast<std::int64_t>(magnitude) : static_cast<std::int64_t>(magnitude);
    return true;
}

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (true) {
        const auto tab = line.find('\t', start);
        if (tab == std::string::npos) {
            result.push_back(line.substr(start));
            return result;
        }
        result.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

std::string one_line(std::string text) {
    std::replace(text.begin(), text.end(), '\n', ' ');
    return text;
}

}  // namespace

void OrganizerModel::sort_items() {
    std::sort(snapshot_.events.begin(), snapshot_.events.end(), [](const auto& left, const auto& right) {
        return left.start_minute == right.start_minute ? left.id < right.id : left.start_minute < right.start_minute;
    });
    std::stable_sort(snapshot_.tasks.begin(), snapshot_.tasks.end(), [](const auto& left, const auto& right) {
        if (left.completed != right.completed) return !left.completed;
        return left.id > right.id;
    });
    std::stable_sort(snapshot_.shopping.begin(), snapshot_.shopping.end(), [](const auto& left, const auto& right) {
        if (left.checked != right.checked) return !left.checked;
        return left.id > right.id;
    });
}

OrganizerResult OrganizerModel::add_event(const std::string& title, std::int64_t start_minute,
                                           std::uint32_t duration_minutes, std::uint32_t* id) {
    auto clean = sanitize_text(title, kMaxTextBytes, true);
    if (clean.empty()) return OrganizerResult::Empty;
    if (start_minute <= 0 || duration_minutes == 0 || duration_minutes > kMaxDurationMinutes) {
        return OrganizerResult::InvalidTime;
    }
    if (snapshot_.events.size() >= kMaxEvents) return OrganizerResult::Full;
    if (next_id_ > kMaxId) compact_ids();
    OrganizerEvent event;
    event.id = next_id_++;
    event.start_minute = start_minute;
    event.duration_minutes = duration_minutes;
    event.title = std::move(clean);
    if (id) *id = event.id;
    snapshot_.events.push_back(std::move(event));
    sort_items();
    changed();
    return OrganizerResult::Ok;
}

OrganizerResult OrganizerModel::add_task(const std::string& text, std::int64_t created_minute,
                                          std::int64_t due_minute, std::uint32_t* id) {
    auto clean = sanitize_text(text, kMaxTextBytes, true);
    if (clean.empty()) return OrganizerResult::Empty;
    if (created_minute < 0 || due_minute < 0) return OrganizerResult::InvalidTime;
    if (snapshot_.tasks.size() >= kMaxTasks) return OrganizerResult::Full;
    if (next_id_ > kMaxId) compact_ids();
    OrganizerTask task;
    task.id = next_id_++;
    task.created_minute = created_minute;
    task.due_minute = due_minute;
    task.text = std::move(clean);
    if (id) *id = task.id;
    snapshot_.tasks.push_back(std::move(task));
    sort_items();
    changed();
    return OrganizerResult::Ok;
}

OrganizerResult OrganizerModel::add_shopping(const std::string& text, std::uint32_t* id) {
    auto clean = sanitize_text(text, kMaxTextBytes, true);
    if (clean.empty()) return OrganizerResult::Empty;
    if (snapshot_.shopping.size() >= kMaxShopping) return OrganizerResult::Full;
    if (next_id_ > kMaxId) compact_ids();
    ShoppingItem item;
    item.id = next_id_++;
    item.text = std::move(clean);
    if (id) *id = item.id;
    snapshot_.shopping.push_back(std::move(item));
    sort_items();
    changed();
    return OrganizerResult::Ok;
}

std::size_t OrganizerModel::prune_events_before(std::int64_t minute) {
    const auto before = snapshot_.events.size();
    snapshot_.events.erase(std::remove_if(snapshot_.events.begin(), snapshot_.events.end(),
                                          [minute](const OrganizerEvent& event) {
                                              return event.start_minute + event.duration_minutes <= minute;
                                          }),
                           snapshot_.events.end());
    const auto removed = before - snapshot_.events.size();
    if (removed > 0) changed();
    return removed;
}

// Renumbers every item from 1 when ids would leave the interface's 30 bits
// (after about a billion additions, or from a damaged file).
void OrganizerModel::compact_ids() {
    std::uint32_t id = 1;
    for (auto& item : snapshot_.events) item.id = id++;
    for (auto& item : snapshot_.tasks) item.id = id++;
    for (auto& item : snapshot_.shopping) item.id = id++;
    next_id_ = id;
}

std::int64_t organizer_default_start(std::int64_t now_minute) noexcept {
    return ((now_minute + 30 + 29) / 30) * 30;
}

OrganizerResult OrganizerModel::set_checked(OrganizerKind kind, std::uint32_t id, bool checked) {
    if (kind == OrganizerKind::Task) {
        for (auto& item : snapshot_.tasks) {
            if (item.id != id) continue;
            if (item.completed != checked) {
                item.completed = checked;
                sort_items();
                changed();
            }
            return OrganizerResult::Ok;
        }
    } else if (kind == OrganizerKind::Shopping) {
        for (auto& item : snapshot_.shopping) {
            if (item.id != id) continue;
            if (item.checked != checked) {
                item.checked = checked;
                sort_items();
                changed();
            }
            return OrganizerResult::Ok;
        }
    }
    return OrganizerResult::NotFound;
}

OrganizerResult OrganizerModel::toggle(OrganizerKind kind, std::uint32_t id) {
    if (kind == OrganizerKind::Task) {
        for (const auto& item : snapshot_.tasks) if (item.id == id) return set_checked(kind, id, !item.completed);
    } else if (kind == OrganizerKind::Shopping) {
        for (const auto& item : snapshot_.shopping) if (item.id == id) return set_checked(kind, id, !item.checked);
    }
    return OrganizerResult::NotFound;
}

OrganizerResult OrganizerModel::remove(OrganizerKind kind, std::uint32_t id) {
    auto erase_id = [id](auto& items) {
        const auto found = std::find_if(items.begin(), items.end(), [id](const auto& item) { return item.id == id; });
        if (found == items.end()) return false;
        items.erase(found);
        return true;
    };
    const bool removed = kind == OrganizerKind::Event ? erase_id(snapshot_.events) :
                         (kind == OrganizerKind::Task ? erase_id(snapshot_.tasks) : erase_id(snapshot_.shopping));
    if (!removed) return OrganizerResult::NotFound;
    changed();
    return OrganizerResult::Ok;
}

std::vector<OrganizerEvent> OrganizerModel::events_between(std::int64_t first_minute,
                                                            std::int64_t last_minute) const {
    std::vector<OrganizerEvent> result;
    if (last_minute <= first_minute) return result;
    for (const auto& event : snapshot_.events) {
        if (event.start_minute >= last_minute) break;
        if (event.start_minute + event.duration_minutes > first_minute) result.push_back(event);
    }
    return result;
}

std::string OrganizerModel::context_for(const std::string& question, std::int64_t now_minute) const {
    if (!question_mentions_organizer(question)) return {};
    std::string result;
    auto append = [&](std::string line) {
        if (result.size() + line.size() <= kContextBytes) result += std::move(line);
    };
    const auto end = now_minute > 0 ? now_minute + 7 * 1440 : INT64_MAX;
    for (const auto& event : snapshot_.events) {
        if (event.start_minute < now_minute || event.start_minute >= end) continue;
        const auto when = from_minute_index(event.start_minute);
        char stamp[40];
        std::snprintf(stamp, sizeof(stamp), "event %04d-%02d-%02d %02d:%02d: ", when.year, when.month, when.day,
                      when.hour, when.minute);
        append(std::string(stamp) + one_line(event.title) + "\n");
    }
    for (const auto& task : snapshot_.tasks) {
        if (task.completed) continue;
        append(std::string("task: ") + one_line(task.text) + "\n");
    }
    if (question_mentions_shopping(question)) {
        for (const auto& item : snapshot_.shopping) {
            if (!item.checked) append(std::string("shopping: ") + one_line(item.text) + "\n");
        }
    }
    return result;
}

std::string OrganizerModel::serialize() const {
    std::ostringstream output;
    output << "aura-organizer 1\nN\t" << next_id_ << '\n';
    for (const auto& item : snapshot_.events) {
        output << "E\t" << item.id << '\t' << item.start_minute << '\t' << item.duration_minutes << '\t'
               << escape_line(item.title) << '\n';
    }
    for (const auto& item : snapshot_.tasks) {
        output << "T\t" << item.id << '\t' << item.created_minute << '\t' << item.due_minute << '\t'
               << (item.completed ? 1 : 0) << '\t' << escape_line(item.text) << '\n';
    }
    for (const auto& item : snapshot_.shopping) {
        output << "S\t" << item.id << '\t' << (item.checked ? 1 : 0) << '\t' << escape_line(item.text) << '\n';
    }
    return output.str();
}

bool OrganizerModel::restore(const std::string& data) {
    std::istringstream input(data);
    std::string line;
    if (!std::getline(input, line) || line != "aura-organizer 1") return false;
    OrganizerSnapshot restored;
    std::uint32_t next_id = 1;
    while (std::getline(input, line)) {
        const auto part = fields(line);
        std::uint64_t id = 0;
        if (part.size() == 2 && part[0] == "N") {
            std::uint64_t value = 0;
            if (parse_unsigned(part[1], value) && value > 0 && value < UINT32_MAX) next_id = static_cast<std::uint32_t>(value);
            continue;
        }
        if (part.size() < 4 || !parse_unsigned(part[1], id) || id == 0 || id >= UINT32_MAX) continue;
        if (part[0] == "E" && part.size() == 5 && restored.events.size() < kMaxEvents) {
            std::int64_t start = 0;
            std::uint64_t duration = 0;
            auto title = sanitize_text(unescape_line(part[4]), kMaxTextBytes, true);
            if (!parse_signed(part[2], start) || !parse_unsigned(part[3], duration) || start <= 0 || duration == 0 ||
                duration > kMaxDurationMinutes || title.empty()) continue;
            restored.events.push_back({static_cast<std::uint32_t>(id), start, static_cast<std::uint32_t>(duration),
                                       std::move(title)});
        } else if (part[0] == "T" && part.size() == 6 && restored.tasks.size() < kMaxTasks) {
            std::int64_t created = 0;
            std::int64_t due = 0;
            std::uint64_t completed = 0;
            auto text = sanitize_text(unescape_line(part[5]), kMaxTextBytes, true);
            if (!parse_signed(part[2], created) || !parse_signed(part[3], due) ||
                !parse_unsigned(part[4], completed) || created < 0 || due < 0 || completed > 1 || text.empty()) continue;
            restored.tasks.push_back({static_cast<std::uint32_t>(id), created, due, completed != 0, std::move(text)});
        } else if (part[0] == "S" && part.size() == 4 && restored.shopping.size() < kMaxShopping) {
            std::uint64_t checked = 0;
            auto text = sanitize_text(unescape_line(part[3]), kMaxTextBytes, true);
            if (!parse_unsigned(part[2], checked) || checked > 1 || text.empty()) continue;
            restored.shopping.push_back({static_cast<std::uint32_t>(id), checked != 0, std::move(text)});
        } else {
            continue;
        }
        next_id = std::max(next_id, static_cast<std::uint32_t>(id) + 1U);
    }
    // The sequence keeps counting, so an observer never sees an old value.
    restored.sequence = snapshot_.sequence;
    snapshot_ = std::move(restored);
    next_id_ = next_id;
    if (next_id_ > kMaxId) compact_ids();
    sort_items();
    changed();
    return true;
}

void OrganizerModel::clear() {
    snapshot_.events.clear();
    snapshot_.tasks.clear();
    snapshot_.shopping.clear();
    next_id_ = 1;
    changed();
}

bool question_mentions_shopping(const std::string& question) {
    const auto folded = fold_text(question);
    return folded_contains_any(folded, {"αγορ", "ψων", "σουπερ", "shopping", "grocery", "groceries"});
}

bool question_mentions_organizer(const std::string& question) {
    const auto folded = fold_text(question);
    return question_mentions_shopping(question) ||
           folded_contains_any(folded, {"ημερολογ", "προγραμμα", "ραντεβου", "εργασ", "δουλει", "υποχρεω",
                                        "calendar", "agenda", "appointment", "task", "todo", "to do"});
}

}  // namespace aura
