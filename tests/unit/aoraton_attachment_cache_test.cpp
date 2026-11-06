// The microSD cache of encrypted Aoraton attachments: names, the size bound
// (oldest out first, a file read counts as use), damaged reads, no card.
#include "aura/aoraton_attachment_cache.hpp"

#if defined(_WIN32)
#include "aura/windows_aoraton_cache_files.hpp"

#include <filesystem>
#include <fstream>
#endif

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::string name(char digit) { return std::string(64, digit); }

struct MemoryFiles final : aura::IAoratonCacheFiles {
    bool card{true};
    bool fail_reads{false};
    std::map<std::string, std::string> data;
    std::map<std::string, std::int64_t> modified;
    int reads{0};
    int writes{0};

    bool list(std::vector<aura::AoratonCacheEntry>& entries) override {
        if (!card) return false;
        for (const auto& item : data) entries.push_back({item.first, item.second.size(), modified[item.first]});
        return true;
    }
    bool read(const std::string& file, std::size_t max_bytes, std::string& out) override {
        ++reads;
        const auto found = data.find(file);
        if (fail_reads || found == data.end() || found->second.size() > max_bytes) return false;
        out = found->second;
        return true;
    }
    bool write(const std::string& file, const std::string& bytes) override {
        if (!card) return false;
        ++writes;
        data[file] = bytes;
        return true;
    }
    void remove(const std::string& file) override { data.erase(file); }
};

}  // namespace

int main() {
    using aura::AoratonAttachmentCache;

    expect(aura::aoraton_cache_name_valid(name('a')) && aura::aoraton_cache_name_valid(name('0')),
           "a SHA-256 in lower-case hex names a file");
    expect(!aura::aoraton_cache_name_valid(std::string(63, 'a')) && !aura::aoraton_cache_name_valid(name('A')) &&
               !aura::aoraton_cache_name_valid("../" + std::string(61, 'a')) && !aura::aoraton_cache_name_valid(""),
           "anything else is refused, so no name reaches outside the folder");

    {
        MemoryFiles files;
        AoratonAttachmentCache cache(files, 1000);
        cache.store(name('a'), std::string(400, 'x'));
        std::string out;
        expect(cache.load(name('a'), 1000, out) && out == std::string(400, 'x'), "a stored file comes back exactly");
        expect(!cache.load(name('a'), 399, out) && out.empty(), "a file larger than the caller's bound is not read");
        cache.store("../escape", "x");
        cache.store(name('b'), std::string(1001, 'y'));
        cache.store(name('c'), std::string());
        expect(files.writes == 1, "a bad name, a file larger than the whole cache or an empty one is not kept");
        expect(!cache.load(name('d'), 1000, out), "an unknown file is a miss");

        cache.store(name('b'), std::string(400, 'b'));
        expect(cache.load(name('a'), 1000, out), "opening a file counts as using it");
        cache.store(name('c'), std::string(400, 'c'));
        expect(files.data.count(name('a')) == 1 && files.data.count(name('c')) == 1 && files.data.count(name('b')) == 0 &&
                   cache.total_bytes() == 800,
               "past the bound the least recently used file goes");
        cache.store(name('c'), std::string(300, 'C'));
        expect(cache.total_bytes() == 700 && cache.load(name('c'), 1000, out) && out == std::string(300, 'C'),
               "storing a file again replaces it");
        cache.erase(name('a'));
        expect(files.data.count(name('a')) == 0 && cache.total_bytes() == 300, "a damaged file can be dropped");
        cache.clear();
        expect(files.data.empty() && cache.total_bytes() == 0, "signing out empties the cache");
    }

    {
        // After a restart the files' times order them, and the bound applies
        // before anything else happens.
        MemoryFiles files;
        files.data[name('d')] = std::string(600, 'd');
        files.modified[name('d')] = 30;
        files.data[name('e')] = std::string(600, 'e');
        files.modified[name('e')] = 10;
        AoratonAttachmentCache cache(files, 1000);
        std::string out;
        expect(cache.load(name('d'), 1000, out) && files.data.count(name('e')) == 0 && cache.total_bytes() == 600,
               "after a restart the oldest file goes first");
        files.fail_reads = true;
        expect(!cache.load(name('d'), 1000, out) && files.data.count(name('d')) == 0 && cache.total_bytes() == 0,
               "a file that cannot be read is forgotten and removed");
    }

    {
        MemoryFiles files;
        files.card = false;
        AoratonAttachmentCache cache(files, 1000);
        cache.store(name('a'), "sealed");
        std::string out;
        expect(files.writes == 0 && !cache.load(name('a'), 1000, out), "without a card nothing is kept");
    }

#if defined(_WIN32)
    {
        // The simulator's folder: <name>.enc files, written whole or not at all.
        const auto folder = std::filesystem::temp_directory_path() / "aura-aoraton-cache-test";
        std::error_code error;
        std::filesystem::remove_all(folder, error);
        aura::simulator::WindowsAoratonCacheFiles files(folder / "cache");
        std::vector<aura::AoratonCacheEntry> entries;
        expect(files.list(entries) && entries.empty(), "a folder that does not exist yet is an empty cache");
        expect(files.write(name('a'), "sealed bytes"), "a file is written");
        { std::ofstream stray(folder / "cache" / "notes.txt"); stray << "not ours"; }
        { std::ofstream partial(folder / "cache" / (name('b') + ".tmp")); partial << "half"; }
        entries.clear();
        expect(files.list(entries) && entries.size() == 1 && entries[0].name == name('a') && entries[0].size == 12 &&
                   entries[0].modified > 0,
               "only complete <SHA-256>.enc files are listed, with their size and time");
        std::string out;
        expect(files.read(name('a'), 64, out) && out == "sealed bytes", "it reads back exactly");
        expect(!files.read(name('a'), 4, out) && out.empty(), "a file over the bound is not read");
        files.remove(name('a'));
        entries.clear();
        expect(files.list(entries) && entries.empty() && !std::filesystem::exists(folder / "cache" / (name('a') + ".enc")),
               "a removed file is gone");
        std::filesystem::remove_all(folder, error);
    }
#endif

    std::cout << "aoraton attachment cache tests passed\n";
    return 0;
}
