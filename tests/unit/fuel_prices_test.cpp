#include "aura/fuel_prices.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void put16(std::string& out, std::size_t value) {
    out += static_cast<char>(value & 0xFFU);
    out += static_cast<char>((value >> 8U) & 0xFFU);
}

void put32(std::string& out, std::size_t value) {
    put16(out, value & 0xFFFFU);
    put16(out, (value >> 16U) & 0xFFFFU);
}

// `data` as a raw deflate stream of stored blocks.
std::string deflated(const std::string& data) {
    constexpr std::size_t kBlock = 0xFFFFU;
    std::string out;
    for (std::size_t at = 0;; at += kBlock) {
        const auto size = std::min(kBlock, data.size() - at);
        const bool last = at + size == data.size();
        out += last ? '\x01' : '\x00';
        put16(out, size);
        put16(out, ~size & 0xFFFFU);
        out.append(data, at, size);
        if (last) return out;
    }
}

struct ZipFile {
    std::string name;
    std::string data;
};

// A ZIP as a workbook is one, every file deflated. DEMO data only.
std::string zip_of(const std::vector<ZipFile>& files) {
    std::string body;
    std::string directory;
    for (const auto& file : files) {
        const auto packed = deflated(file.data);
        const auto offset = body.size();
        put32(body, 0x04034B50U);
        put16(body, 20);
        put16(body, 0);
        put16(body, 8);
        put32(body, 0);
        put32(body, 0);
        put32(body, packed.size());
        put32(body, file.data.size());
        put16(body, file.name.size());
        put16(body, 0);
        body += file.name + packed;
        put32(directory, 0x02014B50U);
        put16(directory, 20);
        put16(directory, 20);
        put16(directory, 0);
        put16(directory, 8);
        put32(directory, 0);
        put32(directory, 0);
        put32(directory, packed.size());
        put32(directory, file.data.size());
        put16(directory, file.name.size());
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put32(directory, 0);
        put32(directory, offset);
        directory += file.name;
    }
    std::string out = body + directory;
    put32(out, 0x06054B50U);
    put16(out, 0);
    put16(out, 0);
    put16(out, files.size());
    put16(out, files.size());
    put32(out, directory.size());
    put32(out, body.size());
    put16(out, 0);
    return out;
}

const std::string kStrings =
    "<sst><si><t>Prices in force</t></si><si><t>Germany</t></si><si><t xml:space=\"preserve\">Greece</t></si></sst>";

std::string sheet_with(const std::string& greece) {
    return "<worksheet><sheetData><row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c></row>"
           "<row r=\"2\"><c r=\"A2\" s=\"6\"><v>46342</v></c></row>"
           "<row r=\"13\"><c r=\"A13\" t=\"s\"><v>1</v></c><c r=\"B13\"><v>1800</v></c><c r=\"C13\"><v>1700</v></c></row>"
           "<row r=\"14\" spans=\"1:7\">" + greece + "</row></sheetData></worksheet>";
}

std::optional<aura::FuelPriceData> bulletin(const std::string& greece, const std::string& strings = kStrings) {
    const auto zip = zip_of({{"xl/sharedStrings.xml", strings}, {"xl/worksheets/sheet1.xml", sheet_with(greece)}});
    return aura::parse_fuel_bulletin(reinterpret_cast<const std::uint8_t*>(zip.data()), zip.size());
}
}  // namespace

int main() {
    using namespace aura;

    // The Weekly Oil Bulletin, read by the device itself (the owner, 2026-11-18).
    const std::string greece =
        "<c r=\"A14\" s=\"1\" t=\"s\"><v>2</v></c><c r=\"B14\" s=\"3\"><v>1969</v></c>"
        "<c r=\"C14\" s=\"3\"><v>2038.4</v></c><c r=\"F14\" s=\"3\"/><c r=\"G14\" s=\"3\"/>";
    const auto weekly = bulletin(greece);
    expect(weekly && weekly->period_date == "2026-11-16" && weekly->unleaded95_millieur_per_litre == 1969 &&
               weekly->diesel_millieur_per_litre == 2038 && !weekly->lpg_millieur_per_litre && !weekly->demo &&
               weekly->source == "European Commission Weekly Oil Bulletin",
           "Greece's row of the workbook, rounded, with no LPG");
    const auto with_lpg = bulletin(greece.substr(0, greece.size() - 22) + "<c r=\"G14\" s=\"3\"><v>905</v></c>");
    expect(with_lpg && with_lpg->lpg_millieur_per_litre == 905, "the LPG column");
    expect(!bulletin("<c r=\"A14\" t=\"s\"><v>1</v></c><c r=\"B14\"><v>1969</v></c><c r=\"C14\"><v>2038</v></c>"),
           "another country's row is not Greece's");
    expect(!bulletin("<c r=\"A14\" t=\"s\"><v>2</v></c><c r=\"B14\"><v>1969</v></c>"), "a missing price is refused");
    expect(!bulletin("<c r=\"A14\" t=\"s\"><v>2</v></c><c r=\"B14\"><v>99999</v></c><c r=\"C14\"><v>2038</v></c>"),
           "a price out of range is refused");
    expect(!bulletin(greece, "<sst><si><t>Germany</t></si></sst>"), "no Greece in the shared strings");
    const auto whole = zip_of({{"xl/sharedStrings.xml", kStrings}, {"xl/worksheets/sheet1.xml", sheet_with(greece)}});
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(whole.data());
    expect(!parse_fuel_bulletin(bytes, whole.size() / 2) && !parse_fuel_bulletin(bytes, 4) &&
               !parse_fuel_bulletin(nullptr, 0), "a cut workbook is refused");
    auto damaged = whole;
    damaged[whole.find(deflated(kStrings).substr(0, 8))] = '\x07';
    expect(!parse_fuel_bulletin(reinterpret_cast<const std::uint8_t*>(damaged.data()), damaged.size()),
           "a damaged workbook is refused");
    const auto huge = zip_of({{"xl/sharedStrings.xml", kStrings + std::string(70000, ' ')},
                              {"xl/worksheets/sheet1.xml", sheet_with(greece)}});
    expect(!parse_fuel_bulletin(reinterpret_cast<const std::uint8_t*>(huge.data()), huge.size()),
           "a file too large is refused, whatever it holds");

    const std::string page =
        "<a href=\"/old/history.xlsx\">History</a> <h3>Prices with taxes latest prices</h3> "
        "<a href=\"/data/report.pdf\">PDF</a> "
        "<a href=\"/document/download/abc_en?filename=Weekly%20prices.xlsx&amp;v=2\">Download</a>"
        "<a href=\"/document/download/later.xlsx\">Later</a>";
    expect(fuel_bulletin_link(page) ==
               "https://energy.ec.europa.eu/document/download/abc_en?filename=Weekly%20prices.xlsx&v=2",
           "the week's workbook is the first after its heading");
    expect(fuel_bulletin_link("<a href=\"/a.xlsx\">x</a>").empty(), "no heading, no link");
    expect(fuel_bulletin_link("PRICES WITH TAXES LATEST PRICES <a href=\"https://example.com/a.xlsx\">x</a>").empty(),
           "a workbook from anywhere else is refused");
    expect(fuel_bulletin_link("Prices with taxes latest prices <a href=\"/report.pdf\">x</a>").empty(),
           "no workbook, no link");

    const char* valid = R"({"ok":true,"schema":1,"country":"GR","period_date":"2026-10-19","source":"European Commission Weekly Oil Bulletin","source_url":"https://energy.ec.europa.eu/data-and-analysis/weekly-oil-bulletin_en","unleaded95_millieur_per_litre":2191,"diesel_millieur_per_litre":2126,"lpg_millieur_per_litre":null})";
    const auto parsed = parse_fuel_price_json(valid);
    expect(parsed && parsed->unleaded95_millieur_per_litre == 2191 &&
           parsed->diesel_millieur_per_litre == 2126 && !parsed->lpg_millieur_per_litre,
           "official bridge snapshot parses and preserves unavailable LPG");
    expect(!parse_fuel_price_json(R"({"schema":1,"country":"GR"})"), "partial payload is rejected");

    FuelPriceModel model;
    expect(model.begin_fetch() && model.snapshot().state == FuelPriceState::Loading, "fetch starts");
    model.fetch_succeeded(*parsed);
    expect(model.snapshot().state == FuelPriceState::Ready && model.snapshot().data.source.find("Commission") !=
           std::string::npos, "official result becomes visible");
    model.fetch_failed("OFFLINE");
    expect(model.snapshot().state == FuelPriceState::Error && model.snapshot().error_code == "OFFLINE",
           "failure is explicit");

    // Prices refresh on their own: soon while missing, failed or stale, rarely when fresh.
    FuelPriceSnapshot fresh;
    fresh.state = FuelPriceState::Ready;
    FuelPriceSnapshot stale = fresh;
    stale.data.stale = true;
    FuelPriceSnapshot failed;
    failed.state = FuelPriceState::Error;
    expect(fuel_refresh_interval_ms(fresh) >= 3U * 3600U * 1000U, "fresh prices are checked a few times a day");
    expect(fuel_refresh_interval_ms(stale) <= 30U * 60U * 1000U &&
               fuel_refresh_interval_ms(failed) <= 30U * 60U * 1000U &&
               fuel_refresh_interval_ms(FuelPriceSnapshot{}) <= 30U * 60U * 1000U,
           "missing, failed or stale prices are retried within half an hour");
    std::cout << "Fuel price tests passed\n";
    expect(fuel_price_text(1912, ",") == "1,912", "Greek prices use a decimal comma");
    expect(fuel_price_text(1912, ".") == "1.912", "English prices use a decimal point");
    expect(fuel_price_text(1005, ".") == "1.005", "the thousandths keep their zeros");
    expect(fuel_price_text(-1, ".") == "\u2014", "an unknown price is a dash");

    return 0;
}
