#include "aura/photos.hpp"
#include "aura/text_util.hpp"

#include <algorithm>
#include <cctype>

namespace aura {
bool photo_filename_valid(std::string_view name) {
    if (name.empty() || name.size() > 120 || name.front() == '.' || sanitize_text(name, 120, false) != name)
        return false;
    for (const unsigned char ch : name)
        if (ch < 32 || ch == 127 || ch == '/' || ch == '\\' || ch == ':' || ch == '"') return false;
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const auto dot = lower.rfind('.');
    if (dot == std::string::npos) return false;
    const auto ext = lower.substr(dot);
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
}
bool photo_settings_valid(const PhotoSettings& s) {
    return (s.interval_seconds == 5 || s.interval_seconds == 10 || s.interval_seconds == 30 ||
            s.interval_seconds == 60) &&
           (s.ambient_name.empty() || photo_filename_valid(s.ambient_name));
}
std::string serialize_photo_settings(const PhotoSettings& s) {
    if (!photo_settings_valid(s)) return {};
    return "{\"version\":1,\"seconds\":" + std::to_string(s.interval_seconds) + ",\"ambient\":\"" +
           json_escape(s.ambient_name) + "\"}\n";
}
bool parse_photo_settings(std::string_view text, PhotoSettings& s) {
    const auto json = parse_flat_json(text, 512);
    if (!json || json->size() != 3 || json->integer("version") != 1) return false;
    const auto seconds = json->integer("seconds");
    const auto name = json->string("ambient");
    if (!seconds || *seconds < 0 || *seconds > 60 || !name) return false;
    PhotoSettings candidate{static_cast<unsigned>(*seconds), *name};
    if (!photo_settings_valid(candidate)) return false;
    s = std::move(candidate);
    return true;
}
bool PhotoMailbox::submit(PhotoJob job) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (jobs_.size() >= 4) return false;
    jobs_.push_back(std::move(job));
    return true;
}
bool PhotoMailbox::take(PhotoJob& job) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (jobs_.empty()) return false;
    job = std::move(jobs_.front());
    jobs_.pop_front();
    return true;
}
void PhotoMailbox::publish(PhotoResult result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (results_.size() >= 8) results_.pop_front();
    results_.push_back(std::move(result));
}
bool PhotoMailbox::consume(PhotoResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (results_.empty()) return false;
    result = std::move(results_.front());
    results_.pop_front();
    return true;
}
bool PhotoMailbox::pending() {
    std::lock_guard<std::mutex> lock(mutex_);
    return !jobs_.empty();
}
void PhotoMailbox::discard_pending() {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.clear();
    results_.clear();
}
void PhotoWorker::initialize() {
    PhotoResult result;
    result.kind = PhotoJobKind::Save;
    result.initial_settings = true;
    parse_photo_settings(storage_.read_settings(), result.settings);
    if (!result.settings.ambient_name.empty()) {
        PhotoBytes bytes;
        auto error = storage_.read(result.settings.ambient_name, bytes);
        if (error == PhotoError::None) result.image = decode_photo(bytes.data(), bytes.size(), error);
    }
    mailbox_.publish(std::move(result));
}
bool PhotoWorker::step() {
    PhotoJob job;
    if (!mailbox_.take(job)) return false;
    PhotoResult result;
    result.kind = job.kind;
    result.generation = job.generation;
    if (job.kind == PhotoJobKind::Scan) {
        result.error = storage_.scan(result.names);
        if (result.error != PhotoError::None) result.names.clear();
        result.names.erase(std::remove_if(result.names.begin(), result.names.end(),
                                          [](const auto& n) { return !photo_filename_valid(n); }),
                           result.names.end());
        std::sort(result.names.begin(), result.names.end());
        result.names.erase(std::unique(result.names.begin(), result.names.end()), result.names.end());
        if (result.names.size() > kMaxPhotos) result.names.resize(kMaxPhotos);
        if (result.error == PhotoError::None && result.names.empty()) result.error = PhotoError::Empty;
    } else if (job.kind == PhotoJobKind::Load) {
        PhotoBytes bytes;
        result.error = photo_filename_valid(job.name) ? storage_.read(job.name, bytes) : PhotoError::Read;
        if (result.error == PhotoError::None) result.image = decode_photo(bytes.data(), bytes.size(), result.error);
    } else if (job.kind == PhotoJobKind::Delete) {
        result.name = job.name;
        result.error =
            photo_filename_valid(job.name) && storage_.erase(job.name) ? PhotoError::None : PhotoError::Delete;
    } else {
        result.error =
            photo_settings_valid(job.settings) && storage_.save_settings(serialize_photo_settings(job.settings))
                ? PhotoError::None
                : PhotoError::Save;
    }
    mailbox_.publish(std::move(result));
    return true;
}
bool PhotoModel::refresh() {
    if (snapshot_.demo || snapshot_.scanning || snapshot_.deleting) return false;
    if (!mailbox_.submit({PhotoJobKind::Scan, generation_ + 1, {}, {}})) return false;
    ++generation_;
    snapshot_.scanning = true;
    snapshot_.loading = false;
    snapshot_.playing = false;
    snapshot_.error = PhotoError::None;
    ++snapshot_.sequence;
    return true;
}
bool PhotoModel::select(std::size_t index) {
    if (index >= snapshot_.names.size() || snapshot_.scanning || snapshot_.deleting) return false;
    if (snapshot_.demo) {
        snapshot_.selected = index;
        ++snapshot_.sequence;
        return true;
    }
    if (!mailbox_.submit({PhotoJobKind::Load, generation_ + 1, snapshot_.names[index], {}})) return false;
    ++generation_;
    snapshot_.selected = index;
    snapshot_.image.reset();
    snapshot_.loading = true;
    snapshot_.error = PhotoError::None;
    slide_ms_ = 0;
    ++snapshot_.sequence;
    return true;
}
bool PhotoModel::advance(int direction) {
    const auto count = snapshot_.names.size();
    if (count < 2 || snapshot_.loading || snapshot_.scanning) return false;
    return select(direction < 0 ? (snapshot_.selected + count - 1) % count : (snapshot_.selected + 1) % count);
}
void PhotoModel::toggle_playing() { set_playing(!snapshot_.playing); }
void PhotoModel::set_playing(bool playing) {
    if (!playing) {
        if (snapshot_.playing) {
            snapshot_.playing = false;
            ++snapshot_.sequence;
        }
        return;
    }
    if (snapshot_.names.size() < 2 || !snapshot_.image || snapshot_.loading) return;
    snapshot_.playing = true;
    slide_ms_ = 0;
    ++snapshot_.sequence;
}
bool PhotoModel::save(PhotoSettings settings) {
    if (!photo_settings_valid(settings)) return false;
    if (!snapshot_.demo && !mailbox_.submit({PhotoJobKind::Save, save_generation_ + 1, {}, settings})) return false;
    ++save_generation_;
    settings_changed_ = true;
    snapshot_.settings = std::move(settings);
    snapshot_.saving = !snapshot_.demo;
    snapshot_.error = PhotoError::None;
    ++snapshot_.sequence;
    return true;
}
bool PhotoModel::set_interval(unsigned seconds) {
    auto s = snapshot_.settings;
    s.interval_seconds = seconds;
    return save(std::move(s));
}
bool PhotoModel::erase(const std::string& name) {
    if (snapshot_.deleting || snapshot_.scanning || !photo_filename_valid(name) ||
        std::find(snapshot_.names.begin(), snapshot_.names.end(), name) == snapshot_.names.end())
        return false;
    if (snapshot_.demo) {
        snapshot_.names.erase(std::remove(snapshot_.names.begin(), snapshot_.names.end(), name), snapshot_.names.end());
        snapshot_.selected = 0;
        snapshot_.playing = false;
        if (snapshot_.names.empty()) snapshot_.image.reset();
        if (snapshot_.settings.ambient_name == name) {
            auto s = snapshot_.settings;
            s.ambient_name.clear();
            save(s);
            snapshot_.ambient.reset();
        }
        ++snapshot_.sequence;
        return true;
    }
    if (!mailbox_.submit({PhotoJobKind::Delete, generation_ + 1, name, {}})) return false;
    ++generation_;
    snapshot_.deleting = true;
    snapshot_.loading = false;
    snapshot_.playing = false;
    snapshot_.error = PhotoError::None;
    ++snapshot_.sequence;
    return true;
}
bool PhotoModel::external_removal() {
    if (!refresh()) return false;
    snapshot_.image.reset();
    return true;
}
bool PhotoModel::set_ambient(bool enabled) {
    auto s = snapshot_.settings;
    if (enabled) {
        if (!snapshot_.image || snapshot_.selected >= snapshot_.names.size()) return false;
        s.ambient_name = snapshot_.names[snapshot_.selected];
    } else
        s.ambient_name.clear();
    if (!save(std::move(s))) return false;
    snapshot_.ambient = enabled ? snapshot_.image : nullptr;
    return true;
}
void PhotoModel::poll(std::uint32_t elapsed_ms, bool visible) {
    PhotoResult result;
    while (mailbox_.consume(result)) {
        if (snapshot_.demo) continue;
        if (result.initial_settings) {
            if (!settings_changed_) {
                snapshot_.settings = std::move(result.settings);
                snapshot_.ambient = std::move(result.image);
                ++snapshot_.sequence;
            }
            continue;
        }
        if (result.kind == PhotoJobKind::Save) {
            if (result.generation == save_generation_) {
                snapshot_.saving = false;
                snapshot_.error = result.error;
                ++snapshot_.sequence;
            }
            continue;
        }
        if (result.generation != generation_) continue;
        snapshot_.error = result.error;
        if (result.kind == PhotoJobKind::Delete) {
            snapshot_.deleting = false;
            if (result.error == PhotoError::None) {
                snapshot_.names.erase(std::remove(snapshot_.names.begin(), snapshot_.names.end(), result.name),
                                      snapshot_.names.end());
                snapshot_.selected =
                    snapshot_.names.empty() ? 0 : std::min(snapshot_.selected, snapshot_.names.size() - 1);
                snapshot_.image.reset();
                if (snapshot_.settings.ambient_name == result.name) {
                    auto s = snapshot_.settings;
                    s.ambient_name.clear();
                    snapshot_.ambient.reset();
                    save(s);
                }
            }
        } else if (result.kind == PhotoJobKind::Scan) {
            snapshot_.scanning = false;
            snapshot_.names = std::move(result.names);
            snapshot_.selected = 0;
            snapshot_.image.reset();
            if (result.error == PhotoError::Storage) snapshot_.ambient.reset();
            if (result.error != PhotoError::Storage && !snapshot_.settings.ambient_name.empty() &&
                std::find(snapshot_.names.begin(), snapshot_.names.end(), snapshot_.settings.ambient_name) ==
                    snapshot_.names.end()) {
                auto s = snapshot_.settings;
                s.ambient_name.clear();
                snapshot_.ambient.reset();
                save(s);
            }
        } else {
            snapshot_.loading = false;
            snapshot_.image = std::move(result.image);
            if (!snapshot_.image) snapshot_.playing = false;
        }
        ++snapshot_.sequence;
    }
    if (!visible && snapshot_.playing) {
        snapshot_.playing = false;
        ++snapshot_.sequence;
    }
    if (visible && snapshot_.playing && !snapshot_.loading) {
        slide_ms_ += std::min<std::uint32_t>(elapsed_ms, 1000);
        if (slide_ms_ >= snapshot_.settings.interval_seconds * 1000U) {
            slide_ms_ = 0;
            advance(1);
        }
    }
}
void PhotoModel::reset() {
    mailbox_.discard_pending();
    if (snapshot_.demo) {
        const auto next = snapshot_.sequence + 1;
        const bool desktop = snapshot_.desktop;
        snapshot_ = {};
        snapshot_.desktop = desktop;
        snapshot_.sequence = next;
        ++generation_;
        return;
    }
    snapshot_.playing = false;
    snapshot_.ambient.reset();
    snapshot_.image.reset();
    ++generation_;
    snapshot_.loading = snapshot_.scanning = snapshot_.deleting = false;
    save({});
}
void PhotoModel::seed_demo() {
    const auto sequence = snapshot_.sequence;
    const bool desktop = snapshot_.desktop;
    snapshot_ = {};
    snapshot_.desktop = desktop;
    snapshot_.demo = true;
    snapshot_.sequence = sequence + 1;
    snapshot_.names = {"DEMO Aurora.png", "DEMO Sea.jpg", "DEMO Sunset.jpg"};
    auto image = std::make_shared<PhotoImage>();
    image->width = 480;
    image->height = 320;
    if (!image->pixels.resize(480 * 320)) return;
    for (unsigned y = 0; y < 320; ++y)
        for (unsigned x = 0; x < 480; ++x) {
            const unsigned r = 20 + x * 100 / 480, g = 30 + y * 100 / 320, b = 100 + x * 110 / 480;
            image->pixels[y * 480 + x] = static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    snapshot_.image = image;
}
}  // namespace aura
