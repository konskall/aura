#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace aura {

// The Αρχεία app (the owner, 2026-11-18): what the memory card holds, where it
// is, and deleting what the owner put there. The UI and the model hold names
// and sizes only; a platform worker does all the file I/O through mailboxes.
//
// A path is relative to the card's root, with '/' between its names: "" is the
// root and "aura/photos/a.jpg" a file. Nothing outside the card can be named.
constexpr std::size_t kMaxFileEntries = 200;
constexpr std::size_t kMaxFilePathBytes = 240;
constexpr std::size_t kMaxFileNameBytes = 128;
// A folder deleted with what it holds: the worker stops after this many files.
constexpr std::size_t kMaxFileEraseEntries = 4096;
constexpr std::size_t kMaxFileEraseDepth = 12;

enum class FileBrowserError { None, NoCard, NotFound, Read, Delete, Protected, Busy };
enum class FileKind { Folder, Image, Audio, Document, Other };

struct FileEntry {
    std::string name;
    bool folder{false};
    std::uint64_t bytes{0};
    std::int64_t modified_minute{0};  // local minute index; 0 when unknown
};

bool file_name_valid(std::string_view name) noexcept;
// A valid path: "" or names joined by '/', none of them "." or "..".
bool file_path_valid(std::string_view path) noexcept;
std::string file_path_join(std::string_view folder, std::string_view name);
std::string file_path_parent(std::string_view path);
std::string_view file_path_leaf(std::string_view path) noexcept;
FileKind file_kind(std::string_view name, bool folder) noexcept;
// The device's own files, which the Αρχεία app shows but never deletes: its
// folder and everything in it, except what is in the photos and files folders.
bool file_path_protected(std::string_view path) noexcept;
// Folders first, then by name; the first kMaxFileEntries stay.
void file_list_add(std::vector<FileEntry>& entries, FileEntry entry, bool& truncated);
// "850 B", "12 KB", "3,4 MB", "1,2 GB": `decimal` is the language's separator.
std::string file_size_text(std::uint64_t bytes, const char* decimal);
// The file's place on the card, for the screen: "aura / photos".
std::string file_path_display(std::string_view path);

struct FileBrowserSnapshot {
    std::string path;                // the open folder
    std::vector<FileEntry> entries;  // folders first, at most kMaxFileEntries
    bool truncated{false};           // the folder holds more than is listed
    bool loading{false};
    bool deleting{false};
    FileBrowserError error{FileBrowserError::None};
    bool space_known{false};
    std::uint64_t total_bytes{0};
    std::uint64_t free_bytes{0};
    bool demo{false};
    std::uint64_t erased{0};  // how many deletions finished: photos may be gone
    std::uint64_t sequence{0};
};

enum class FileJobKind { List, Erase };
struct FileJob {
    FileJobKind kind{FileJobKind::List};
    std::uint64_t generation{0};
    std::string path;
};
struct FileResult {
    FileJobKind kind{FileJobKind::List};
    std::uint64_t generation{0};
    std::string path;
    std::vector<FileEntry> entries;
    bool truncated{false};
    FileBrowserError error{FileBrowserError::None};
    bool space_known{false};
    std::uint64_t total_bytes{0};
    std::uint64_t free_bytes{0};
};

class FileMailbox {
public:
    static constexpr std::size_t kMaxJobs = 4;
    bool submit(FileJob job);
    bool take(FileJob& job);
    void publish(FileResult result);
    bool consume(FileResult& result);

private:
    std::mutex mutex_;
    std::deque<FileJob> jobs_;
    std::deque<FileResult> results_;
};

class IFileStorage {
public:
    virtual ~IFileStorage() = default;
    // The folder's entries, through file_list_add().
    virtual FileBrowserError list(const std::string& path, std::vector<FileEntry>& entries, bool& truncated) = 0;
    // A file, or a folder with everything in it.
    virtual FileBrowserError erase(const std::string& path) = 0;
    virtual bool space(std::uint64_t& total_bytes, std::uint64_t& free_bytes) = 0;
};

// Runs only on the platform's file worker; no LVGL and no model.
class FileWorker {
public:
    FileWorker(FileMailbox& mailbox, IFileStorage& storage) : mailbox_(mailbox), storage_(storage) {}
    // One job; false when there was none.
    bool step();

private:
    FileMailbox& mailbox_;
    IFileStorage& storage_;
};

// The owner/UI thread's model. A result for a folder no longer open is dropped.
class FileBrowserModel {
public:
    explicit FileBrowserModel(FileMailbox& mailbox) : mailbox_(mailbox) {}
    const FileBrowserSnapshot& snapshot() const noexcept { return snapshot_; }
    bool open(const std::string& path);
    bool open_parent();
    bool refresh();
    // There is nothing to read (no card): says so at once.
    void unavailable(FileBrowserError error);
    // Exactly this path, after the owner's confirmation; a protected one is refused.
    bool erase(const std::string& path);
    void poll();
    void seed_demo();

private:
    void changed() noexcept { ++snapshot_.sequence; }
    FileMailbox& mailbox_;
    FileBrowserSnapshot snapshot_{};
    std::uint64_t generation_{0};
};

}  // namespace aura
