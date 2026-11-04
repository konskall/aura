#pragma once

#include <cstdint>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace aura {

constexpr std::size_t kMaxPhotos = 128;
constexpr std::size_t kMaxPhotoFileBytes = 4U * 1024U * 1024U;
bool photo_filename_valid(std::string_view name);

enum class PhotoError { None, Storage, Empty, Read, Format, TooLarge, Save, Delete };
// Large buffers fail explicitly on ESP32, where C++ exceptions are disabled.
template <typename T>
class PhotoBuffer {
public:
    bool resize(std::size_t count) {
        if (count > kMaxPhotoFileBytes / sizeof(T)) return false;
        std::unique_ptr<T[]> next(count ? new (std::nothrow) T[count]() : nullptr);
        if (count && !next) return false;
        data_ = std::move(next);
        size_ = count;
        return true;
    }
    void clear() {
        data_.reset();
        size_ = 0;
    }
    T* data() { return data_.get(); }
    const T* data() const { return data_.get(); }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    T& operator[](std::size_t i) { return data_[i]; }
    const T& operator[](std::size_t i) const { return data_[i]; }

private:
    std::unique_ptr<T[]> data_;
    std::size_t size_{0};
};
using PhotoBytes = PhotoBuffer<std::uint8_t>;
struct PhotoImage {
    unsigned width{0}, height{0};
    PhotoBuffer<std::uint16_t> pixels;  // RGB565, at most 480 x 480
};
struct PhotoSettings {
    unsigned interval_seconds{10};
    std::string ambient_name;
};
bool photo_settings_valid(const PhotoSettings& settings);
std::string serialize_photo_settings(const PhotoSettings& settings);
bool parse_photo_settings(std::string_view text, PhotoSettings& settings);
std::shared_ptr<const PhotoImage> decode_photo(const std::uint8_t* data, std::size_t size, PhotoError& error);
inline std::shared_ptr<const PhotoImage> decode_photo(const std::vector<std::uint8_t>& bytes, PhotoError& error) {
    return decode_photo(bytes.data(), bytes.size(), error);
}

// UI state contains names and bounded decoded pixels, never paths or file handles.
struct PhotoSnapshot {
    std::vector<std::string> names;
    std::size_t selected{0};
    std::shared_ptr<const PhotoImage> image;
    std::shared_ptr<const PhotoImage> ambient;
    PhotoSettings settings;
    PhotoError error{PhotoError::None};
    bool scanning{false}, loading{false}, playing{false}, saving{false}, deleting{false};
    bool demo{false}, desktop{false};
    std::uint64_t sequence{0};
};
enum class PhotoJobKind { Scan, Load, Save, Delete };
struct PhotoJob {
    PhotoJobKind kind{PhotoJobKind::Scan};
    std::uint64_t generation{0};
    std::string name;
    PhotoSettings settings;
};
struct PhotoResult {
    PhotoJobKind kind{PhotoJobKind::Scan};
    std::uint64_t generation{0};
    std::vector<std::string> names;
    std::shared_ptr<const PhotoImage> image;
    PhotoError error{PhotoError::None};
    bool initial_settings{false};
    PhotoSettings settings;
    std::string name;
};
class PhotoMailbox {
public:
    bool submit(PhotoJob job);
    bool take(PhotoJob& job);
    void publish(PhotoResult result);
    bool consume(PhotoResult& result);
    bool pending();
    void discard_pending();

private:
    std::mutex mutex_;
    std::deque<PhotoJob> jobs_;
    std::deque<PhotoResult> results_;
};
class IPhotoStorage {
public:
    virtual ~IPhotoStorage() = default;
    virtual PhotoError scan(std::vector<std::string>& names) = 0;
    virtual PhotoError read(const std::string& name, PhotoBytes& bytes) = 0;
    virtual std::string read_settings() = 0;
    virtual bool save_settings(const std::string& text) = 0;
    virtual bool erase(const std::string&) { return false; }
};
// Run only on the platform's file worker. No LVGL calls or model mutation.
class PhotoWorker {
public:
    PhotoWorker(PhotoMailbox& mailbox, IPhotoStorage& storage) : mailbox_(mailbox), storage_(storage) {}
    void initialize();
    bool step();

private:
    PhotoMailbox& mailbox_;
    IPhotoStorage& storage_;
};
// Owner/UI-thread model. Generation checks discard superseded file results.
class PhotoModel {
public:
    explicit PhotoModel(PhotoMailbox& mailbox, bool desktop = false) : mailbox_(mailbox) {
        snapshot_.desktop = desktop;
    }
    const PhotoSnapshot& snapshot() const { return snapshot_; }
    bool refresh();
    bool select(std::size_t index);
    bool advance(int direction);
    void toggle_playing();
    void set_playing(bool playing);
    bool set_interval(unsigned seconds);
    bool set_ambient(bool enabled);
    bool erase(const std::string& name);  // exact name, after UI confirmation
    bool external_removal();              // invalidate cached pixels/preferences via a fresh scan
    void poll(std::uint32_t elapsed_ms, bool viewer_visible);
    void reset();
    void seed_demo();

private:
    bool save(PhotoSettings settings);
    PhotoMailbox& mailbox_;
    PhotoSnapshot snapshot_;
    std::uint64_t generation_{0}, save_generation_{0};
    std::uint32_t slide_ms_{0};
    bool settings_changed_{false};
};
}  // namespace aura
