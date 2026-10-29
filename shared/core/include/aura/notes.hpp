#pragma once

#include "aura/voice.hpp"

#include <cstdint>
#include <future>
#include <string>
#include <vector>

namespace aura {

enum class NoteTranscript { None, Pending, Ready, Failed };

struct NoteItem {
    std::uint32_t id{0};
    std::int64_t created_minute{0};  // minute_index(); 0 when the clock was unknown
    std::string text;                // typed text or transcript
    std::string audio_file;          // platform file name of a voice note, or empty
    std::uint32_t audio_ms{0};
    NoteTranscript transcript{NoteTranscript::None};
};

struct NotesSnapshot {
    std::vector<NoteItem> notes;  // newest first
    std::uint64_t sequence{0};
};

enum class NoteResult { Ok, Full, Empty, NotFound };

// Personal notes kept on the device (simulator: runtime folder; firmware:
// microSD). Contents are never logged and reach the assistant only when a
// question is about notes (see `context_for`).
class NotesModel {
public:
    static constexpr std::size_t kMaxNotes = 50;
    static constexpr std::size_t kMaxTextBytes = 600;
    static constexpr std::size_t kContextBytes = 1600;

    NoteResult add_text(const std::string& text, std::int64_t created_minute, std::uint32_t* id = nullptr);
    NoteResult add_voice(const std::string& audio_file, std::uint32_t audio_ms, std::int64_t created_minute,
                         std::uint32_t* id = nullptr);
    NoteResult set_transcript(std::uint32_t id, const std::string& text);
    NoteResult set_transcript_failed(std::uint32_t id);
    NoteResult remove(std::uint32_t id, std::string* removed_audio_file = nullptr);
    const NoteItem* find(std::uint32_t id) const noexcept;

    // Recent note texts for a question about notes; empty otherwise.
    std::string context_for(const std::string& question) const;

    std::string serialize() const;
    bool restore(const std::string& data);

    const NotesSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    NoteItem* find_mutable(std::uint32_t id) noexcept;
    void changed() noexcept { ++snapshot_.sequence; }

    NotesSnapshot snapshot_{};
    std::uint32_t next_id_{1};
};

// "σημείωσ…", "note" and friends, after Greek/ASCII folding.
bool question_mentions_notes(const std::string& question);

// A voice note's audio file name. The random `nonce` keeps it unique when the
// clock is still unknown (minute 0) or the counter restarted with the device,
// so a new note never overwrites an older note's audio.
std::string voice_note_file_name(std::int64_t minute, std::uint32_t counter, std::uint32_t nonce);

// Voice-note transcriptions run beside the assistant's own recognition. Each
// remembers its note, so a spoken question or a cancelled listen in the
// meantime never drops one. At most kMaxPending run at once; a further note
// is marked failed at once instead of waiting forever.
class NoteTranscriptions {
public:
    static constexpr std::size_t kMaxPending = 4;

    bool start(std::uint32_t note, std::future<TranscriptResult> job, NotesModel& notes);
    // Applies the finished transcriptions to their notes.
    void poll(NotesModel& notes);
    bool empty() const noexcept { return jobs_.empty(); }
    // Hands the unfinished jobs over: a std::future blocks in its destructor,
    // so shutdown must not destroy them on the UI thread.
    std::vector<std::future<TranscriptResult>> release();

private:
    struct Job {
        std::uint32_t note{0};
        std::future<TranscriptResult> result;
    };
    std::vector<Job> jobs_;
};

}  // namespace aura
