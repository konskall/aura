#include "aura/fuel_prices.hpp"

#include "aura/text_util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

// stb_image's inflate, compiled with the photo decoder (photo_decoder.cpp).
extern "C" int stbi_zlib_decode_noheader_buffer(char* obuffer, int olen, const char* ibuffer, int ilen);

namespace aura {
namespace {
constexpr const char* kSource = "European Commission Weekly Oil Bulletin";
constexpr const char* kSourceUrl = kFuelBulletinPage;
constexpr std::string_view kBulletinSite = "https://energy.ec.europa.eu";
// The workbook's XML files are 10 KB and 2 KB.
constexpr std::size_t kWorkbookFileLimit = 64U * 1024U;

bool valid_date(const std::string& value) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 7) continue;
        if (value[index] < '0' || value[index] > '9') return false;
    }
    const int year = std::stoi(value.substr(0, 4));
    const int month = std::stoi(value.substr(5, 2));
    const int day = std::stoi(value.substr(8, 2));
    if (year < 2000 || year > 2199 || month < 1 || month > 12 || day < 1 || day > 31) return false;
    const auto round_trip = from_minute_index(days_from_civil(year, month, day) * 1440);
    return round_trip.year == year && round_trip.month == month && round_trip.day == day;
}

bool valid_price(std::int64_t value) noexcept { return value >= 500 && value <= 5000; }

std::uint32_t little16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U);
}

std::uint32_t little32(const std::uint8_t* bytes) noexcept { return little16(bytes) | (little16(bytes + 2) << 16U); }

// One file of a ZIP, unpacked; empty when it is absent, damaged or too large.
std::string zip_file(const std::uint8_t* zip, std::size_t size, std::string_view name) {
    constexpr std::size_t kEndRecord = 22;
    constexpr std::size_t kDirectoryEntry = 46;
    constexpr std::size_t kLocalHeader = 30;
    if (!zip || size < kEndRecord) return {};
    std::size_t end = size - kEndRecord;
    while (little32(zip + end) != 0x06054B50U) {
        if (end == 0 || size - end > kEndRecord + 0xFFFFU) return {};
        --end;
    }
    const auto entries = little16(zip + end + 10);
    std::size_t at = little32(zip + end + 16);
    for (std::uint32_t index = 0; index < entries; ++index) {
        if (at > size || size - at < kDirectoryEntry || little32(zip + at) != 0x02014B50U) return {};
        const auto method = little16(zip + at + 10);
        const std::size_t packed = little32(zip + at + 20);
        const std::size_t unpacked = little32(zip + at + 24);
        const std::size_t name_size = little16(zip + at + 28);
        const std::size_t extra = little16(zip + at + 30);
        const std::size_t comment = little16(zip + at + 32);
        const std::size_t local = little32(zip + at + 42);
        if (size - at - kDirectoryEntry < name_size) return {};
        const std::string_view entry(reinterpret_cast<const char*>(zip + at + kDirectoryEntry), name_size);
        if (entry != name) {
            at += kDirectoryEntry + name_size + extra + comment;
            continue;
        }
        if (unpacked == 0 || unpacked > kWorkbookFileLimit || local > size || size - local < kLocalHeader ||
            little32(zip + local) != 0x04034B50U) {
            return {};
        }
        const std::size_t data = local + kLocalHeader + little16(zip + local + 26) + little16(zip + local + 28);
        if (data > size || packed > size - data) return {};
        std::string out(unpacked, '\0');
        if (method == 0) {
            if (packed != unpacked) return {};
            std::copy_n(zip + data, unpacked, out.begin());
        } else if (method != 8 || stbi_zlib_decode_noheader_buffer(out.data(), static_cast<int>(unpacked),
                                                                  reinterpret_cast<const char*>(zip + data),
                                                                  static_cast<int>(packed)) !=
                                      static_cast<int>(unpacked)) {
            return {};
        }
        return out;
    }
    return {};
}

// The position of "Greece" among the workbook's shared strings; -1 when absent.
int greece_string(std::string_view strings) {
    int index = 0;
    for (std::size_t at = strings.find("<si"); at != std::string_view::npos; at = strings.find("<si", at + 3), ++index) {
        const auto close = strings.find('>', at);
        if (close == std::string_view::npos) return -1;
        if (strings[close - 1] == '/') continue;  // an empty string
        const auto end = strings.find("</si>", close);
        if (end == std::string_view::npos) return -1;
        const auto item = strings.substr(close + 1, end - close - 1);
        const auto text_start = item.find('>');
        const auto text_end = item.find("</t>");
        if (text_start != std::string_view::npos && text_end != std::string_view::npos && text_end > text_start &&
            item.substr(text_start + 1, text_end - text_start - 1) == "Greece") {
            return index;
        }
    }
    return -1;
}

struct Cell {
    bool shared{false};  // its value is the position of a shared string
    std::string value;
};

// A cell of a row: `<c r="B14" s="3"><v>1969</v></c>`; no value for `<c r="F14"/>`.
Cell cell_in(std::string_view row, const std::string& reference) {
    Cell cell;
    const auto at = row.find("<c r=\"" + reference + "\"");
    if (at == std::string_view::npos) return cell;
    const auto close = row.find('>', at);
    if (close == std::string_view::npos || row[close - 1] == '/') return cell;
    cell.shared = row.substr(at, close - at).find("t=\"s\"") != std::string_view::npos;
    const auto end = row.find("</c>", close);
    const auto start = row.find("<v>", close);
    if (end == std::string_view::npos || start == std::string_view::npos || start > end) return cell;
    const auto value_end = row.find("</v>", start);
    if (value_end == std::string_view::npos || value_end > end) return cell;
    cell.value = std::string(row.substr(start + 3, value_end - start - 3));
    return cell;
}

// A whole number near `text`, or -1 ("2038.4" is 2038).
std::int64_t rounded(const std::string& text) {
    if (text.empty()) return -1;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(value) || value < 0 || value > 1e9) return -1;
    return std::llround(value);
}

// The rows of the sheet, one at a time, with their numbers.
template <typename Visit>
void each_row(std::string_view sheet, Visit visit) {
    for (auto at = sheet.find("<row "); at != std::string_view::npos; at = sheet.find("<row ", at + 5)) {
        const auto number = sheet.find("r=\"", at);
        const auto open = sheet.find('>', at);
        const auto end = sheet.find("</row>", at);
        if (number == std::string_view::npos || open == std::string_view::npos || end == std::string_view::npos ||
            number > open) {
            return;
        }
        const auto number_end = sheet.find('"', number + 3);
        if (number_end == std::string_view::npos) return;
        if (visit(std::string(sheet.substr(number + 3, number_end - number - 3)), sheet.substr(open, end - open))) return;
    }
}
}  // namespace

bool FuelPriceModel::begin_fetch() {
    if (snapshot_.state == FuelPriceState::Loading) return false;
    snapshot_.state = FuelPriceState::Loading;
    snapshot_.error_code.clear();
    ++snapshot_.sequence;
    return true;
}

void FuelPriceModel::fetch_succeeded(FuelPriceData data) {
    snapshot_.state = FuelPriceState::Ready;
    snapshot_.data = std::move(data);
    snapshot_.error_code.clear();
    ++snapshot_.sequence;
}

void FuelPriceModel::fetch_failed(std::string error_code) {
    snapshot_.state = FuelPriceState::Error;
    snapshot_.error_code = sanitize_text(error_code, 64);
    if (snapshot_.error_code.empty()) snapshot_.error_code = "FUEL_DATA_UNAVAILABLE";
    ++snapshot_.sequence;
}

std::optional<FuelPriceData> parse_fuel_price_json(std::string_view json) {
    const auto object = parse_flat_json(json, 8192);
    if (!object || (object->size() != 8 && object->size() != 9)) return std::nullopt;
    if (const auto* ok = object->find("ok"); ok && (ok->kind != FlatJsonValue::Kind::Bool || !ok->boolean)) {
        return std::nullopt;
    }
    if (object->integer("schema") != 1 || object->string("country") != std::optional<std::string>("GR")) {
        return std::nullopt;
    }
    const auto date = object->string("period_date");
    const auto source = object->string("source");
    const auto source_url = object->string("source_url");
    const auto unleaded = object->integer("unleaded95_millieur_per_litre");
    const auto diesel = object->integer("diesel_millieur_per_litre");
    const auto* lpg = object->find("lpg_millieur_per_litre");
    if (!date || !valid_date(*date) || source != std::optional<std::string>(kSource) ||
        source_url != std::optional<std::string>(kSourceUrl) || !unleaded || !valid_price(*unleaded) ||
        !diesel || !valid_price(*diesel) || !lpg) return std::nullopt;

    FuelPriceData result;
    result.period_date = *date;
    result.source = *source;
    result.source_url = *source_url;
    result.unleaded95_millieur_per_litre = static_cast<int>(*unleaded);
    result.diesel_millieur_per_litre = static_cast<int>(*diesel);
    if (lpg->kind == FlatJsonValue::Kind::Integer && valid_price(lpg->integer)) {
        result.lpg_millieur_per_litre = static_cast<int>(lpg->integer);
    } else if (lpg->kind != FlatJsonValue::Kind::Null) {
        return std::nullopt;
    }
    return result;
}

std::string fuel_bulletin_link(std::string_view page) {
    constexpr std::string_view kHeading = "prices with taxes latest prices";
    const auto heading = std::search(page.begin(), page.end(), kHeading.begin(), kHeading.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == b;
    });
    if (heading == page.end()) return {};
    constexpr std::string_view kHref = "href=\"";
    auto at = page.find(kHref, static_cast<std::size_t>(heading - page.begin()));
    while (at != std::string_view::npos) {
        at += kHref.size();
        const auto end = page.find('"', at);
        if (end == std::string_view::npos) return {};
        std::string link(page.substr(at, end - at));
        if (link.find(".xlsx") != std::string::npos) {
            for (auto amp = link.find("&amp;"); amp != std::string::npos; amp = link.find("&amp;", amp + 1)) {
                link.erase(amp + 1, 4);
            }
            if (!link.empty() && link.front() == '/') link.insert(0, kBulletinSite);
            const bool plain = std::all_of(link.begin(), link.end(), [](unsigned char ch) { return ch > 0x20 && ch < 0x7F; });
            return plain && link.rfind(std::string(kBulletinSite) + "/", 0) == 0 ? link : std::string{};
        }
        at = page.find(kHref, end);
    }
    return {};
}

std::optional<FuelPriceData> parse_fuel_bulletin(const std::uint8_t* workbook, std::size_t size) {
    const auto strings = zip_file(workbook, size, "xl/sharedStrings.xml");
    const auto sheet = zip_file(workbook, size, "xl/worksheets/sheet1.xml");
    const int greece = greece_string(strings);
    if (sheet.empty() || greece < 0) return std::nullopt;
    std::int64_t serial = -1;  // the bulletin's date, in A2, as a spreadsheet's day number
    std::optional<FuelPriceData> result;
    each_row(sheet, [&](const std::string& number, std::string_view row) {
        const auto first = cell_in(row, "A" + number);
        if (number == "2") serial = rounded(first.value);
        if (!first.shared || first.value != std::to_string(greece)) return false;
        const auto unleaded = rounded(cell_in(row, "B" + number).value);
        const auto diesel = rounded(cell_in(row, "C" + number).value);
        const auto lpg = cell_in(row, "G" + number).value;
        if (!valid_price(unleaded) || !valid_price(diesel) || (!lpg.empty() && !valid_price(rounded(lpg)))) return true;
        FuelPriceData data;
        data.source = kSource;
        data.source_url = kSourceUrl;
        data.unleaded95_millieur_per_litre = static_cast<int>(unleaded);
        data.diesel_millieur_per_litre = static_cast<int>(diesel);
        if (!lpg.empty()) data.lpg_millieur_per_litre = static_cast<int>(rounded(lpg));
        result = std::move(data);
        return true;
    });
    if (!result || serial < 30000 || serial > 80000) return std::nullopt;
    const auto day = from_minute_index((days_from_civil(1899, 12, 30) + serial) * 1440);
    char date[16];
    std::snprintf(date, sizeof(date), "%04d-%02d-%02d", day.year, day.month, day.day);
    result->period_date = date;
    return valid_date(result->period_date) ? result : std::nullopt;
}

bool fuel_prices_stale(const FuelPriceData& data, int current_year, int current_month, int current_day,
                       int maximum_age_days) noexcept {
    if (!valid_date(data.period_date) || maximum_age_days < 0) return true;
    const int year = std::stoi(data.period_date.substr(0, 4));
    const int month = std::stoi(data.period_date.substr(5, 2));
    const int day = std::stoi(data.period_date.substr(8, 2));
    const auto age = days_from_civil(current_year, current_month, current_day) - days_from_civil(year, month, day);
    return age < 0 || age > maximum_age_days;
}

std::string fuel_price_text(int millieur_per_litre, const char* decimal_separator) {
    if (millieur_per_litre < 0) return "\u2014";
    char value[24];
    std::snprintf(value, sizeof(value), "%d%s%03d", millieur_per_litre / 1000,
                  decimal_separator ? decimal_separator : ",", millieur_per_litre % 1000);
    return value;
}

const char* fuel_price_state_name(FuelPriceState state) noexcept {
    switch (state) {
        case FuelPriceState::Unavailable: return "unavailable";
        case FuelPriceState::Loading: return "loading";
        case FuelPriceState::Ready: return "ready";
        case FuelPriceState::Error: return "error";
    }
    return "unknown";
}

std::uint32_t fuel_refresh_interval_ms(const FuelPriceSnapshot& snapshot) noexcept {
    const bool current = snapshot.state == FuelPriceState::Ready && !snapshot.data.stale;
    return current ? 6U * 3600U * 1000U : 15U * 60U * 1000U;
}

}  // namespace aura
