#include "aura/photos.hpp"
#include "../../third_party/tjpgd/tjpgd.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {
// Independent of LVGL's allocator. The worker has a hard 4 MiB decode budget.
constexpr std::size_t kDecodeBudget = 4U * 1024U * 1024U;
struct alignas(std::max_align_t) Allocation {
    std::size_t bytes;
};
thread_local std::size_t allocated = 0;
void* photo_alloc(std::size_t n) {
    if (n > kDecodeBudget - allocated || n > std::numeric_limits<std::size_t>::max() - sizeof(Allocation))
        return nullptr;
    auto* p = static_cast<Allocation*>(std::malloc(n + sizeof(Allocation)));
    if (!p) return nullptr;
    p->bytes = n;
    allocated += n;
    return p + 1;
}
void photo_free(void* p) {
    if (p) {
        auto* h = static_cast<Allocation*>(p) - 1;
        allocated -= h->bytes;
        std::free(h);
    }
}
void* photo_realloc(void* p, std::size_t n) {
    if (!p) return photo_alloc(n);
    const auto old = (static_cast<Allocation*>(p) - 1)->bytes;
    void* next = photo_alloc(n);
    if (next) {
        std::memcpy(next, p, std::min(old, n));
        photo_free(p);
    }
    return next;
}
}  // namespace
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 16384
#define STBI_MALLOC(n) photo_alloc(n)
#define STBI_REALLOC(p, n) photo_realloc(p, n)
#define STBI_FREE(p) photo_free(p)
#include "../../third_party/stb/stb_image.h"

namespace aura {
namespace {
struct Input {
    const std::uint8_t* bytes;
    std::size_t length;
    std::size_t size() const { return length; }
    bool empty() const { return length == 0; }
    const std::uint8_t* data() const { return bytes; }
    std::uint8_t operator[](std::size_t i) const { return bytes[i]; }
};
// Read only the bounded TIFF orientation entry; all offsets stay inside APP1.
unsigned orientation(const Input& b) {
    if (b.size() < 4 || b[0] != 255 || b[1] != 216) return 1;
    for (std::size_t pos = 2; pos + 4 < b.size();) {
        if (b[pos++] != 255) return 1;
        const auto marker = b[pos++];
        if (marker == 218 || marker == 217) break;
        if (marker == 255) {
            --pos;
            continue;
        }
        const std::size_t size = (b[pos] << 8U) | b[pos + 1];
        if (size < 2 || size > b.size() - pos) break;
        const auto start = pos + 2;
        if (marker == 225 && size >= 16 && std::memcmp(b.data() + start, "Exif\0\0", 6) == 0) {
            const auto base = start + 6, end = pos + size;
            const bool little = b[base] == 'I' && b[base + 1] == 'I';
            if (!little && !(b[base] == 'M' && b[base + 1] == 'M')) return 1;
            auto u16 = [&](std::size_t at) -> unsigned {
                return little ? b[at] | (b[at + 1] << 8U) : (b[at] << 8U) | b[at + 1];
            };
            auto u32 = [&](std::size_t at) -> std::uint32_t {
                return little ? u16(at) | (u16(at + 2) << 16U) : (u16(at) << 16U) | u16(at + 2);
            };
            if (u16(base + 2) != 42) return 1;
            const auto offset = u32(base + 4);
            if (offset > end - base || end - base - offset < 2) return 1;
            const auto dir = base + offset;
            const auto count = std::min<unsigned>(u16(dir), 128);
            for (unsigned i = 0; i < count; ++i) {
                const auto at = dir + 2 + i * 12;
                if (at > end || end - at < 12) break;
                if (u16(at) == 0x112 && u16(at + 2) == 3 && u32(at + 4) == 1) {
                    const auto v = u16(at + 8);
                    return v >= 1 && v <= 8 ? v : 1;
                }
            }
            return 1;
        }
        pos += size;
    }
    return 1;
}
struct Raster {
    Input input;
    std::size_t position{0};
    unsigned source_w{0}, source_h{0}, width{0}, height{0}, orient{1};
    std::shared_ptr<PhotoImage> image;
    explicit Raster(Input b) : input(b), orient(orientation(b)) {}
    bool prepare(unsigned w, unsigned h) {
        source_w = w;
        source_h = h;
        const auto edge = std::max(w, h);
        width = std::max(1U, w * std::min(480U, edge) / edge);
        height = std::max(1U, h * std::min(480U, edge) / edge);
        image = std::make_shared<PhotoImage>();
        image->width = orient >= 5 ? height : width;
        image->height = orient >= 5 ? width : height;
        return image->pixels.resize(image->width * image->height);
    }
    void pixel(unsigned x, unsigned y, const std::uint8_t* rgb, unsigned alpha = 255) {
        unsigned ox = x, oy = y;
        switch (orient) {
            case 2: ox = width - 1 - x; break;
            case 3:
                ox = width - 1 - x;
                oy = height - 1 - y;
                break;
            case 4: oy = height - 1 - y; break;
            case 5:
                ox = y;
                oy = x;
                break;
            case 6:
                ox = height - 1 - y;
                oy = x;
                break;
            case 7:
                ox = height - 1 - y;
                oy = width - 1 - x;
                break;
            case 8:
                ox = y;
                oy = width - 1 - x;
                break;
            default: break;
        }
        const unsigned r = rgb[0] * alpha / 255U, g = rgb[1] * alpha / 255U, b = rgb[2] * alpha / 255U;
        image->pixels[oy * image->width + ox] =
            static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
};
std::size_t jpeg_read(JDEC* jd, std::uint8_t* out, std::size_t count) {
    auto& r = *static_cast<Raster*>(jd->device);
    count = std::min(count, r.input.size() - r.position);
    if (out) std::memcpy(out, r.input.data() + r.position, count);
    r.position += count;
    return count;
}
int jpeg_output(JDEC* jd, void* bytes, JRECT* rect) {
    auto& r = *static_cast<Raster*>(jd->device);
    const auto left = (rect->left * r.width + r.source_w - 1) / r.source_w;
    const auto top = (rect->top * r.height + r.source_h - 1) / r.source_h;
    const auto right = std::min(r.width, ((rect->right + 1U) * r.width + r.source_w - 1) / r.source_w);
    const auto bottom = std::min(r.height, ((rect->bottom + 1U) * r.height + r.source_h - 1) / r.source_h);
    for (auto y = top; y < bottom; ++y)
        for (auto x = left; x < right; ++x) {
            const auto sx = x * r.source_w / r.width, sy = y * r.source_h / r.height;
            const auto offset = ((sy - rect->top) * (rect->right - rect->left + 1U) + sx - rect->left) * 3U;
            // The pinned LVGL TJpgDec emits BGR888, unlike stb_image's RGB.
            const auto* bgr = static_cast<const std::uint8_t*>(bytes) + offset;
            const std::uint8_t rgb[]{bgr[2], bgr[1], bgr[0]};
            r.pixel(x, y, rgb);
        }
    return 1;
}
}  // namespace
std::shared_ptr<const PhotoImage> decode_photo(const std::uint8_t* data, std::size_t size, PhotoError& error) {
    const Input bytes{data, size};
    error = PhotoError::Format;
    if (bytes.size() > kMaxPhotoFileBytes) {
        error = PhotoError::TooLarge;
        return {};
    }
    if (bytes.empty() || !data) return {};
    Raster raster(bytes);
    if (bytes.size() >= 2 && bytes[0] == 255 && bytes[1] == 216) {
        JDEC decoder{};
        PhotoBytes work;
        if (!work.resize(32768)) {
            error = PhotoError::TooLarge;
            return {};
        }
        if (jd_prepare(&decoder, jpeg_read, work.data(), work.size(), &raster) == JDR_OK) {
            if (!decoder.width || !decoder.height || decoder.width > 12000 || decoder.height > 12000 ||
                static_cast<std::uint64_t>(decoder.width) * decoder.height > 40000000U) {
                error = PhotoError::TooLarge;
                return {};
            }
            // LVGL's vendored codec removed TJpgDec's internal reduction path.
            // Sample the original MCU coordinates into the bounded output.
            if (!raster.prepare(decoder.width, decoder.height)) {
                error = PhotoError::TooLarge;
                return {};
            }
            if (jd_decomp(&decoder, jpeg_output, 0) == JDR_OK) {
                error = PhotoError::None;
                return raster.image;
            }
            return {};
        }
    }
    // PNG and small progressive JPEG: reject expansion before allocating pixels.
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels)) return {};
    if (w <= 0 || h <= 0 || static_cast<std::uint64_t>(w) * h > 1024U * 1024U) {
        error = PhotoError::TooLarge;
        return {};
    }
    auto* rgba = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!rgba) return {};
    if (!raster.prepare(static_cast<unsigned>(w), static_cast<unsigned>(h))) {
        stbi_image_free(rgba);
        error = PhotoError::TooLarge;
        return {};
    }
    for (unsigned y = 0; y < raster.height; ++y)
        for (unsigned x = 0; x < raster.width; ++x) {
            const auto offset = (static_cast<std::size_t>(y * raster.source_h / raster.height) * raster.source_w +
                                 x * raster.source_w / raster.width) *
                                4U;
            raster.pixel(x, y, rgba + offset, rgba[offset + 3]);
        }
    stbi_image_free(rgba);
    error = PhotoError::None;
    return raster.image;
}
}  // namespace aura
