#include "aura/file_browser.hpp"

#include "aura/text_util.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

namespace aura {
namespace {

constexpr std::string_view kDeviceFolder{"aura"};

char lower_ascii(char ch) noexcept { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch; }

bool name_before(std::string_view left, std::string_view right) noexcept {
    const auto count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) {
        const char a = lower_ascii(left[index]);
        const char b = lower_ascii(right[index]);
        if (a != b) return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
    }
    if (left.size() != right.size()) return left.size() < right.size();
    return left < right;
}

bool entry_before(const FileEntry& left, const FileEntry& right) noexcept {
    if (left.folder != right.folder) return left.folder;
    return name_before(left.name, right.name);
}

std::string_view extension(std::string_view name) noexcept {
    const auto dot = name.rfind('.');
    return dot == std::string_view::npos || dot == 0 ? std::string_view{} : name.substr(dot + 1);
}

bool extension_is(std::string_view value, std::initializer_list<std::string_view> list) noexcept {
    return std::any_of(list.begin(), list.end(), [value](std::string_view known) {
        return value.size() == known.size() &&
               std::equal(value.begin(), value.end(), known.begin(),
                          [](char a, char b) { return lower_ascii(a) == lower_ascii(b); });
    });
}

}  // namespace

bool file_name_valid(std::string_view name) noexcept {
    if (name.empty() || name.size() > kMaxFileNameBytes || name == "." || name == "..") return false;
    return std::none_of(name.begin(), name.end(), [](char ch) {
        const auto byte = static_cast<unsigned char>(ch);
        return byte < 0x20 || byte == 0x7F || ch == '/' || ch == '\\';
    });
}

bool file_path_valid(std::string_view path) noexcept {
    if (path.size() > kMaxFilePathBytes) return false;
    while (!path.empty()) {
        const auto slash = path.find('/');
        if (!file_name_valid(path.substr(0, slash))) return false;
        if (slash == std::string_view::npos) return true;
        path.remove_prefix(slash + 1);
        if (path.empty()) return false;  // a trailing '/'
    }
    return true;
}

std::string file_path_join(std::string_view folder, std::string_view name) {
    std::string path(folder);
    if (!path.empty()) path += '/';
    path += name;
    return path;
}

std::string file_path_parent(std::string_view path) {
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string{} : std::string(path.substr(0, slash));
}

std::string_view file_path_leaf(std::string_view path) noexcept {
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

FileKind file_kind(std::string_view name, bool folder) noexcept {
    if (folder) return FileKind::Folder;
    const auto type = extension(name);
    if (extension_is(type, {"jpg", "jpeg", "png", "gif", "bmp", "webp"})) return FileKind::Image;
    if (extension_is(type, {"wav", "mp3", "ogg", "m4a", "aac", "flac", "opus"})) return FileKind::Audio;
    if (extension_is(type, {"txt", "pdf", "md", "doc", "docx", "csv", "json", "log", "xml", "xlsx"}))
        return FileKind::Document;
    return FileKind::Other;
}

bool file_path_protected(std::string_view path) noexcept {
    if (path.empty()) return true;
    if (path == kDeviceFolder) return true;
    const auto first = path.find('/');
    const auto top = path.substr(0, first);
    if (top != kDeviceFolder) return false;
    const auto inside = path.substr(first + 1);
    const auto next = inside.find('/');
    // What is inside these two is the owner's; the folders themselves stay.
    const bool owners = inside.substr(0, next) == "photos" || inside.substr(0, next) == "files";
    return !(owners && next != std::string_view::npos && next + 1 < inside.size());
}

void file_list_add(std::vector<FileEntry>& entries, FileEntry entry, bool& truncated) {
    const auto place = std::upper_bound(entries.begin(), entries.end(), entry, entry_before);
    if (entries.size() >= kMaxFileEntries) {
        truncated = true;
        if (place == entries.end()) return;
        entries.pop_back();
    }
    const auto index = std::min<std::size_t>(static_cast<std::size_t>(place - entries.begin()), entries.size());
    entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(index), std::move(entry));
}

std::string file_size_text(std::uint64_t bytes, const char* decimal) {
    static constexpr std::array<const char*, 5> kUnits{"B", "KB", "MB", "GB", "TB"};
    std::size_t unit = 0;
    std::uint64_t whole = bytes;
    std::uint64_t remainder = 0;
    while (whole >= 1024U && unit + 1 < kUnits.size()) {
        remainder = whole % 1024U;
        whole /= 1024U;
        ++unit;
    }
    char text[40];
    if (unit == 0 || whole >= 100U) {
        // Rounded to a whole number, above 100 of the unit.
        const auto rounded = whole + (unit > 0 && remainder >= 512U ? 1U : 0U);
        std::snprintf(text, sizeof(text), "%llu %s", static_cast<unsigned long long>(rounded), kUnits[unit]);
        return text;
    }
    auto tenths = (remainder * 10U + 512U) / 1024U;
    if (tenths == 10U) {
        ++whole;
        tenths = 0;
    }
    if (tenths == 0) {
        std::snprintf(text, sizeof(text), "%llu %s", static_cast<unsigned long long>(whole), kUnits[unit]);
    } else {
        std::snprintf(text, sizeof(text), "%llu%s%u %s", static_cast<unsigned long long>(whole), decimal,
                      static_cast<unsigned>(tenths), kUnits[unit]);
    }
    return text;
}

std::string file_path_display(std::string_view path) {
    std::string text;
    while (!path.empty()) {
        const auto slash = path.find('/');
        if (!text.empty()) text += " / ";
        text += path.substr(0, slash);
        if (slash == std::string_view::npos) break;
        path.remove_prefix(slash + 1);
    }
    return text;
}

bool FileMailbox::submit(FileJob job) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (jobs_.size() >= kMaxJobs) return false;
    jobs_.push_back(std::move(job));
    return true;
}

bool FileMailbox::take(FileJob& job) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (jobs_.empty()) return false;
    job = std::move(jobs_.front());
    jobs_.pop_front();
    return true;
}

void FileMailbox::publish(FileResult result) {
    std::lock_guard<std::mutex> lock(mutex_);
    results_.push_back(std::move(result));
    while (results_.size() > kMaxJobs) results_.pop_front();
}

bool FileMailbox::consume(FileResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (results_.empty()) return false;
    result = std::move(results_.front());
    results_.pop_front();
    return true;
}

bool file_text_previewable(std::string_view name) noexcept {
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return false;
    std::string extension;
    for (const char ch : name.substr(dot + 1)) {
        extension += static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    }
    for (const char* known : {"txt", "md", "csv", "json", "log", "ini", "srt"}) {
        if (extension == known) return true;
    }
    return false;
}

bool file_playable(std::string_view name) noexcept {
    if (name.size() < 5) return false;
    const auto extension = name.substr(name.size() - 4);
    return extension == ".mp3" || extension == ".MP3" || extension == ".Mp3" || extension == ".mP3";
}

bool FileWorker::step() {
    FileJob job;
    if (!mailbox_.take(job)) return false;
    FileResult result;
    result.kind = job.kind;
    result.generation = job.generation;
    result.path = job.path;
    if (!file_path_valid(job.path)) {
        result.error = FileBrowserError::NotFound;
    } else if (job.kind == FileJobKind::Erase) {
        // Checked here too: the worker never deletes the device's own files.
        result.error = file_path_protected(job.path) ? FileBrowserError::Protected : storage_.erase(job.path);
    } else if (job.kind == FileJobKind::Rename) {
        // And never renames them.
        if (file_path_protected(job.path)) result.error = FileBrowserError::Protected;
        else if (!file_name_valid(job.new_name)) result.error = FileBrowserError::Rename;
        else result.error = storage_.rename(job.path, job.new_name);
    } else if (job.kind == FileJobKind::Preview) {
        const auto leaf = file_path_leaf(job.path);
        std::vector<std::uint8_t> bytes;
        if (file_kind(leaf, false) == FileKind::Image) {
            result.error = storage_.read(job.path, kMaxPhotoFileBytes + 1, bytes);
            if (result.error == FileBrowserError::None) {
                PhotoError photo_error = PhotoError::None;
                if (bytes.size() <= kMaxPhotoFileBytes) result.image = decode_photo(bytes, photo_error);
                if (!result.image) result.error = FileBrowserError::Read;
            }
        } else if (file_text_previewable(leaf)) {
            result.error = storage_.read(job.path, kMaxFilePreviewTextBytes, bytes);
            if (result.error == FileBrowserError::None) {
                const std::string_view raw(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                result.text = sanitize_fragment(raw.substr(0, complete_utf8_prefix(raw)), true);
            }
        } else {
            result.error = FileBrowserError::Unsupported;
        }
    } else {
        result.error = storage_.list(job.path, result.entries, result.truncated);
    }
    result.space_known = storage_.space(result.total_bytes, result.free_bytes);
    mailbox_.publish(std::move(result));
    return true;
}

bool FileBrowserModel::open(const std::string& path) {
    if (!file_path_valid(path)) return false;
    FileJob job;
    job.kind = FileJobKind::List;
    job.generation = ++generation_;
    job.path = path;
    if (!mailbox_.submit(std::move(job))) {
        snapshot_.error = FileBrowserError::Busy;
        changed();
        return false;
    }
    if (snapshot_.path != path) {
        snapshot_.entries.clear();
        snapshot_.truncated = false;
    }
    snapshot_.path = path;
    snapshot_.loading = true;
    snapshot_.error = FileBrowserError::None;
    changed();
    return true;
}

bool FileBrowserModel::open_parent() {
    return !snapshot_.path.empty() && open(file_path_parent(snapshot_.path));
}

bool FileBrowserModel::refresh() { return open(snapshot_.path); }

void FileBrowserModel::unavailable(FileBrowserError error) {
    snapshot_.loading = false;
    snapshot_.deleting = false;
    snapshot_.entries.clear();
    snapshot_.truncated = false;
    snapshot_.error = error;
    changed();
}

bool FileBrowserModel::erase(const std::string& path) {
    if (snapshot_.deleting) return false;
    if (!file_path_valid(path) || file_path_protected(path)) {
        snapshot_.error = FileBrowserError::Protected;
        changed();
        return false;
    }
    FileJob job;
    job.kind = FileJobKind::Erase;
    job.generation = ++generation_;
    job.path = path;
    if (!mailbox_.submit(std::move(job))) {
        snapshot_.error = FileBrowserError::Busy;
        changed();
        return false;
    }
    snapshot_.deleting = true;
    snapshot_.error = FileBrowserError::None;
    changed();
    return true;
}

bool FileBrowserModel::rename(const std::string& path, const std::string& new_name) {
    if (snapshot_.renaming) return false;
    if (!file_path_valid(path) || file_path_protected(path)) {
        snapshot_.error = FileBrowserError::Protected;
        changed();
        return false;
    }
    if (!file_name_valid(new_name) || new_name == file_path_leaf(path)) return false;
    FileJob job;
    job.kind = FileJobKind::Rename;
    job.generation = ++generation_;
    job.path = path;
    job.new_name = new_name;
    if (!mailbox_.submit(std::move(job))) {
        snapshot_.error = FileBrowserError::Busy;
        changed();
        return false;
    }
    snapshot_.renaming = true;
    snapshot_.error = FileBrowserError::None;
    changed();
    return true;
}

bool FileBrowserModel::preview(const std::string& path) {
    if (!file_path_valid(path)) return false;
    const auto leaf = file_path_leaf(path);
    snapshot_.preview_path = path;
    snapshot_.preview_image.reset();
    snapshot_.preview_text.clear();
    if (file_kind(leaf, false) != FileKind::Image && !file_text_previewable(leaf)) {
        snapshot_.previewing = false;
        snapshot_.preview_error = FileBrowserError::Unsupported;
        changed();
        return false;
    }
    FileJob job;
    job.kind = FileJobKind::Preview;
    job.generation = ++preview_generation_;
    job.path = path;
    if (!mailbox_.submit(std::move(job))) {
        snapshot_.previewing = false;
        snapshot_.preview_error = FileBrowserError::Busy;
        changed();
        return false;
    }
    snapshot_.previewing = true;
    snapshot_.preview_error = FileBrowserError::None;
    changed();
    return true;
}

void FileBrowserModel::poll() {
    FileResult result;
    while (mailbox_.consume(result)) {
        if (result.space_known) {
            snapshot_.space_known = true;
            snapshot_.total_bytes = result.total_bytes;
            snapshot_.free_bytes = result.free_bytes;
        }
        if (result.kind == FileJobKind::Rename) {
            snapshot_.renaming = false;
            snapshot_.error = result.error;
            if (result.error == FileBrowserError::None) ++snapshot_.renamed;
            changed();
            refresh();
            continue;
        }
        if (result.kind == FileJobKind::Preview) {
            // Only the look last asked for.
            if (result.generation != preview_generation_ || result.path != snapshot_.preview_path) continue;
            snapshot_.previewing = false;
            snapshot_.preview_error = result.error;
            snapshot_.preview_image = std::move(result.image);
            snapshot_.preview_text = std::move(result.text);
            changed();
            continue;
        }
        if (result.kind == FileJobKind::Erase) {
            snapshot_.deleting = false;
            snapshot_.error = result.error;
            if (result.error == FileBrowserError::None) ++snapshot_.erased;
            changed();
            // The open folder as it is now.
            refresh();
            continue;
        }
        if (result.generation != generation_ || result.path != snapshot_.path) continue;
        snapshot_.loading = false;
        snapshot_.error = result.error;
        snapshot_.entries = std::move(result.entries);
        snapshot_.truncated = result.truncated;
        snapshot_.demo = false;
        changed();
    }
}

void FileBrowserModel::seed_demo() {
    snapshot_ = {};
    snapshot_.demo = true;
    snapshot_.space_known = true;
    snapshot_.total_bytes = 32ULL * 1024ULL * 1024ULL * 1024ULL;
    snapshot_.free_bytes = 24ULL * 1024ULL * 1024ULL * 1024ULL;
    snapshot_.entries = {{"aura", true, 0, 0}, {"DEMO Μουσική", true, 0, 0}, {"DEMO σημειώσεις.txt", false, 2048, 0}};
    changed();
}

}  // namespace aura
