#include "aura/aura_keyboard.hpp"

#include "aura/aura_theme.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace aura::ui {

namespace {

using Ctrl = lv_buttonmatrix_ctrl_t;

// Key legends are layout symbols (like LVGL's own "ABC"/"1#"), not copy.
constexpr const char* kShift = LV_SYMBOL_UP;
constexpr const char* kBackspace = LV_SYMBOL_BACKSPACE;
constexpr const char* kOk = LV_SYMBOL_OK;
constexpr const char* kNumbers = "123";
constexpr const char* kMoreSymbols = "#+=";
constexpr const char* kGreekLetters = "ΑΒΓ";
constexpr const char* kLatinLetters = "ABC";
constexpr const char* kAccent = "\xCE\x84";  // U+0384 GREEK TONOS, used as a dead key
constexpr const char* kSpace = " ";
constexpr const char* kSpacer = "  ";        // hidden half key that centres a row

constexpr Ctrl width(int units) { return static_cast<Ctrl>(units); }
constexpr Ctrl with(Ctrl base, int units) { return static_cast<Ctrl>(base | units); }
constexpr Ctrl kChar = static_cast<Ctrl>(LV_BUTTONMATRIX_CTRL_POPOVER);
constexpr Ctrl kControl = static_cast<Ctrl>(LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG |
                                            LV_BUTTONMATRIX_CTRL_CHECKED);
constexpr Ctrl kRepeatControl = static_cast<Ctrl>(LV_BUTTONMATRIX_CTRL_CHECKED);
constexpr Ctrl kHidden = static_cast<Ctrl>(LV_BUTTONMATRIX_CTRL_HIDDEN);

constexpr lv_style_selector_t kItemsPressed =
    static_cast<lv_style_selector_t>(LV_PART_ITEMS) | static_cast<lv_style_selector_t>(LV_STATE_PRESSED);
constexpr lv_style_selector_t kItemsChecked =
    static_cast<lv_style_selector_t>(LV_PART_ITEMS) | static_cast<lv_style_selector_t>(LV_STATE_CHECKED);

// One Greek key: plain, tonos and diaeresis forms in both cases.
struct GreekKey {
    const char* lower;
    const char* upper;
    const char* tonos_lower;
    const char* tonos_upper;
    const char* dia_lower;
    const char* dia_upper;
};
#define AURA_PLAIN(l, u) {l, u, l, u, l, u}

constexpr std::array<GreekKey, 10> kGreekRow1{{
    AURA_PLAIN(";", ":"), AURA_PLAIN("ς", "Σ"),
    {"ε", "Ε", "έ", "Έ", "ε", "Ε"}, AURA_PLAIN("ρ", "Ρ"), AURA_PLAIN("τ", "Τ"),
    {"υ", "Υ", "ύ", "Ύ", "ϋ", "Ϋ"}, AURA_PLAIN("θ", "Θ"),
    {"ι", "Ι", "ί", "Ί", "ϊ", "Ϊ"}, {"ο", "Ο", "ό", "Ό", "ο", "Ο"}, AURA_PLAIN("π", "Π"),
}};
constexpr std::array<GreekKey, 9> kGreekRow2{{
    {"α", "Α", "ά", "Ά", "α", "Α"}, AURA_PLAIN("σ", "Σ"), AURA_PLAIN("δ", "Δ"), AURA_PLAIN("φ", "Φ"),
    AURA_PLAIN("γ", "Γ"), {"η", "Η", "ή", "Ή", "η", "Η"}, AURA_PLAIN("ξ", "Ξ"), AURA_PLAIN("κ", "Κ"),
    AURA_PLAIN("λ", "Λ"),
}};
constexpr std::array<GreekKey, 7> kGreekRow3{{
    AURA_PLAIN("ζ", "Ζ"), AURA_PLAIN("χ", "Χ"), AURA_PLAIN("ψ", "Ψ"), {"ω", "Ω", "ώ", "Ώ", "ω", "Ω"},
    AURA_PLAIN("β", "Β"), AURA_PLAIN("ν", "Ν"), AURA_PLAIN("μ", "Μ"),
}};
#undef AURA_PLAIN

constexpr std::array<const char*, 10> kLatinRow1Lower{"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"};
constexpr std::array<const char*, 10> kLatinRow1Upper{"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"};
constexpr std::array<const char*, 9> kLatinRow2Lower{"a", "s", "d", "f", "g", "h", "j", "k", "l"};
constexpr std::array<const char*, 9> kLatinRow2Upper{"A", "S", "D", "F", "G", "H", "J", "K", "L"};
constexpr std::array<const char*, 7> kLatinRow3Lower{"z", "x", "c", "v", "b", "n", "m"};
constexpr std::array<const char*, 7> kLatinRow3Upper{"Z", "X", "C", "V", "B", "N", "M"};

constexpr std::array<const char*, 10> kDigits{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
constexpr std::array<const char*, 10> kSymbolsText{"-", "/", ":", ";", "(", ")", "€", "&", "@", "\""};
constexpr std::array<const char*, 10> kSymbolsAscii{"-", "/", ":", ";", "(", ")", "$", "&", "@", "\""};
constexpr std::array<const char*, 10> kMoreRow1{"[", "]", "{", "}", "#", "%", "^", "*", "+", "="};
constexpr std::array<const char*, 10> kMoreRow2Text{"_", "\\", "|", "~", "<", ">", "$", "£", "°", "•"};
constexpr std::array<const char*, 10> kMoreRow2Ascii{"_", "\\", "|", "~", "<", ">", "`", "'", "€", "$"};
constexpr std::array<const char*, 5> kPunctuation{".", ",", "?", "!", "'"};

enum class Layer { Letters, Symbols, MoreSymbols };
enum class Accent { None, Tonos, Diaeresis };

struct KeyboardState {
    KeyboardProfile profile{KeyboardProfile::Text};
    bool greek{true};
    bool shift{false};
    bool caps_lock{false};
    std::uint32_t last_shift_ms{0};
    Layer layer{Layer::Letters};
    Accent accent{Accent::None};
    // LVGL keeps a pointer to the active map, so maps are double-buffered and
    // owned here until the keyboard is deleted.
    std::array<std::vector<const char*>, 2> maps{};
    std::array<std::vector<Ctrl>, 2> ctrls{};
    std::size_t active{0};
};

const char* greek_legend(const GreekKey& key, bool upper, Accent accent) {
    switch (accent) {
        case Accent::Tonos: return upper ? key.tonos_upper : key.tonos_lower;
        case Accent::Diaeresis: return upper ? key.dia_upper : key.dia_lower;
        case Accent::None: break;
    }
    return upper ? key.upper : key.lower;
}

void build(KeyboardState& state, std::vector<const char*>& map, std::vector<Ctrl>& ctrl) {
    map.clear();
    ctrl.clear();
    const auto key = [&](const char* legend, Ctrl control) {
        map.push_back(legend);
        ctrl.push_back(control);
    };
    const auto row_end = [&] { map.push_back("\n"); };
    const bool text = state.profile == KeyboardProfile::Text;
    const bool upper = state.shift || state.caps_lock;

    if (state.layer == Layer::Letters) {
        if (text && state.greek) {
            for (const auto& item : kGreekRow1) key(greek_legend(item, upper, state.accent), with(kChar, 1));
            row_end();
            for (const auto& item : kGreekRow2) key(greek_legend(item, upper, state.accent), with(kChar, 1));
            key(kAccent, with(kControl, 1));
            row_end();
            key(kShift, with(kControl, 3));
            for (const auto& item : kGreekRow3) key(greek_legend(item, upper, state.accent), with(kChar, 2));
            key(kBackspace, with(kRepeatControl, 3));
            row_end();
        } else {
            for (const auto* legend : upper ? kLatinRow1Upper : kLatinRow1Lower) key(legend, with(kChar, 1));
            row_end();
            key(kSpacer, with(kHidden, 1));
            for (const auto* legend : upper ? kLatinRow2Upper : kLatinRow2Lower) key(legend, with(kChar, 2));
            key(kSpacer, with(kHidden, 1));
            row_end();
            key(kShift, with(kControl, 3));
            for (const auto* legend : upper ? kLatinRow3Upper : kLatinRow3Lower) key(legend, with(kChar, 2));
            key(kBackspace, with(kRepeatControl, 3));
            row_end();
        }
        key(kNumbers, with(kControl, 3));
        if (text) {
            key(state.greek ? kLatinLetters : kGreekLetters, with(kControl, 2));
            key(",", with(kChar, 2));
        } else {
            key("@", with(kChar, 2));
        }
        key(kSpace, width(6));
        key(".", with(kChar, 2));
        key(kOk, with(kControl, 3));
    } else {
        const bool more = state.layer == Layer::MoreSymbols;
        for (const auto* legend : more ? kMoreRow1 : kDigits) key(legend, with(kChar, 1));
        row_end();
        const auto& second = more ? (text ? kMoreRow2Text : kMoreRow2Ascii) : (text ? kSymbolsText : kSymbolsAscii);
        for (const auto* legend : second) key(legend, with(kChar, 1));
        row_end();
        key(more ? kNumbers : kMoreSymbols, with(kControl, 3));
        for (const auto* legend : kPunctuation) key(legend, with(kChar, 2));
        key(kBackspace, with(kRepeatControl, 3));
        row_end();
        key(text && state.greek ? kGreekLetters : kLatinLetters, with(kControl, 3));
        key(kSpace, width(10));
        key(kOk, with(kControl, 3));
    }
    map.push_back("");
}

void apply(lv_obj_t* keyboard, KeyboardState& state) {
    const std::size_t next = 1U - state.active;
    build(state, state.maps[next], state.ctrls[next]);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_1, state.maps[next].data(), state.ctrls[next].data());
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_USER_1);
    state.active = next;
}

void keyboard_event(lv_event_t* event) {
    auto* keyboard = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    auto* state = static_cast<KeyboardState*>(lv_event_get_user_data(event));
    if (!keyboard || !state) return;
    const std::uint32_t id = lv_buttonmatrix_get_selected_button(keyboard);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;
    const char* legend = lv_buttonmatrix_get_button_text(keyboard, id);
    if (!legend) return;
    const auto is = [legend](const char* value) { return std::strcmp(legend, value) == 0; };

    if (is(kShift)) {
        // Tap: one capital. Second tap within 400 ms: caps lock. Tap again: off.
        if (state->caps_lock) {
            state->caps_lock = false;
            state->shift = false;
        } else if (state->shift && lv_tick_elaps(state->last_shift_ms) < 400U) {
            state->caps_lock = true;
            state->shift = false;
        } else {
            state->shift = !state->shift;
        }
        state->last_shift_ms = lv_tick_get();
        apply(keyboard, *state);
        return;
    }
    if (is(kAccent)) {
        state->accent = state->accent == Accent::None ? Accent::Tonos :
                        (state->accent == Accent::Tonos ? Accent::Diaeresis : Accent::None);
        apply(keyboard, *state);
        return;
    }
    if (is(kNumbers) || is(kMoreSymbols)) {
        state->layer = is(kNumbers) ? Layer::Symbols : Layer::MoreSymbols;
        state->accent = Accent::None;
        apply(keyboard, *state);
        return;
    }
    if (is(kGreekLetters) || is(kLatinLetters)) {
        if (state->profile == KeyboardProfile::Text) state->greek = is(kGreekLetters);
        state->layer = Layer::Letters;
        state->accent = Accent::None;
        apply(keyboard, *state);
        return;
    }
    // OK may submit the form and delete this keyboard; touch nothing after it.
    if (is(kOk)) {
        lv_keyboard_def_event_cb(event);
        return;
    }
    const bool consumes_modifiers = !is(kBackspace);
    lv_keyboard_def_event_cb(event);
    if (!consumes_modifiers || state->layer != Layer::Letters) return;
    const bool had_shift = state->shift && !state->caps_lock;
    const bool had_accent = state->accent != Accent::None;
    if (!had_shift && !had_accent) return;
    state->shift = state->caps_lock;
    state->accent = Accent::None;
    apply(keyboard, *state);
}

void keyboard_delete(lv_event_t* event) {
    delete static_cast<KeyboardState*>(lv_event_get_user_data(event));
}

// 28 px legends; LVGL's symbol font draws shift/backspace/OK.
const lv_font_t* key_font() {
    static const lv_font_t font = [] {
        lv_font_t value = *font_headline();
        value.fallback = LV_FONT_DEFAULT;
        return value;
    }();
    return &font;
}

}  // namespace

lv_obj_t* create_keyboard(lv_obj_t* parent, lv_obj_t* textarea, KeyboardProfile profile) {
    auto* keyboard = lv_keyboard_create(parent);
    // lv_keyboard_create aligns to BOTTOM_MID; callers position it absolutely.
    lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
    auto* state = new KeyboardState{};
    state->profile = profile;
    state->greek = profile == KeyboardProfile::Text;
    // A new sentence or name starts with a capital.
    const char* existing = textarea ? lv_textarea_get_text(textarea) : nullptr;
    state->shift = profile == KeyboardProfile::Text && (!existing || existing[0] == '\0');

    lv_obj_remove_event_cb(keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(keyboard, keyboard_event, LV_EVENT_VALUE_CHANGED, state);
    lv_obj_add_event_cb(keyboard, keyboard_delete, LV_EVENT_DELETE, state);
    lv_keyboard_set_popovers(keyboard, true);
    apply(keyboard, *state);
    if (textarea) lv_keyboard_set_textarea(keyboard, textarea);

    // Graphite keys on the black canvas; control keys sit one step darker.
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(keyboard, 0, 0);
    lv_obj_set_style_radius(keyboard, 0, 0);
    lv_obj_set_style_pad_all(keyboard, 4, 0);
    lv_obj_set_style_pad_row(keyboard, 6, 0);
    lv_obj_set_style_pad_column(keyboard, 5, 0);
    lv_obj_set_style_text_font(keyboard, key_font(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, color(tokens::text), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, color(tokens::surface_high), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, tokens::radius_small, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, color(tokens::surface), kItemsChecked);
    lv_obj_set_style_text_color(keyboard, color(tokens::text_secondary), kItemsChecked);
    lv_obj_set_style_bg_color(keyboard, color(tokens::blue), kItemsPressed);
    lv_obj_set_style_text_color(keyboard, color(tokens::text), kItemsPressed);
    return keyboard;
}

}  // namespace aura::ui
