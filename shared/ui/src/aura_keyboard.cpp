#include "aura/aura_keyboard.hpp"

#include "aura/aura_theme.hpp"

#include <algorithm>
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
constexpr Ctrl kChar = static_cast<Ctrl>(LV_BUTTONMATRIX_CTRL_POPOVER | LV_BUTTONMATRIX_CTRL_NO_REPEAT);
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
    const char* both_lower;
    const char* both_upper;
};
#define AURA_PLAIN(l, u) {l, u, l, u, l, u, l, u}

constexpr std::array<GreekKey, 10> kGreekRow1{{
    AURA_PLAIN(";", ":"), AURA_PLAIN("ς", "ς"),
    {"ε", "Ε", "έ", "Έ", "ε", "Ε", "έ", "Έ"}, AURA_PLAIN("ρ", "Ρ"), AURA_PLAIN("τ", "Τ"),
    // Capitals keep the diaeresis and drop the tonos, so ΐ and ΰ capitalise to Ϊ and Ϋ.
    {"υ", "Υ", "ύ", "Ύ", "ϋ", "Ϋ", "ΰ", "Ϋ"}, AURA_PLAIN("θ", "Θ"),
    {"ι", "Ι", "ί", "Ί", "ϊ", "Ϊ", "ΐ", "Ϊ"}, {"ο", "Ο", "ό", "Ό", "ο", "Ο", "ό", "Ό"}, AURA_PLAIN("π", "Π"),
}};
constexpr std::array<GreekKey, 9> kGreekRow2{{
    {"α", "Α", "ά", "Ά", "α", "Α", "ά", "Ά"}, AURA_PLAIN("σ", "Σ"), AURA_PLAIN("δ", "Δ"), AURA_PLAIN("φ", "Φ"),
    AURA_PLAIN("γ", "Γ"), {"η", "Η", "ή", "Ή", "η", "Η", "ή", "Ή"}, AURA_PLAIN("ξ", "Ξ"), AURA_PLAIN("κ", "Κ"),
    AURA_PLAIN("λ", "Λ"),
}};
constexpr std::array<GreekKey, 7> kGreekRow3{{
    AURA_PLAIN("ζ", "Ζ"), AURA_PLAIN("χ", "Χ"), AURA_PLAIN("ψ", "Ψ"), {"ω", "Ω", "ώ", "Ώ", "ω", "Ω", "ώ", "Ώ"},
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
enum class Accent { None, Tonos, Diaeresis, Both };

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
    lv_obj_t* keyboard{};
    lv_obj_t* popup{};
    std::array<const char*, 4> variants{};
    std::size_t variant_count{0};
    int hovered{-1};
    bool popup_released{false};
    // Per-keyboard fonts keep both the current look and LVGL control symbols,
    // without losing the look's Plex fallback for combining Greek accents.
    lv_font_t font{};
    lv_font_t symbols{};
};

const char* greek_legend(const GreekKey& key, bool upper, Accent accent) {
    switch (accent) {
        case Accent::Tonos: return upper ? key.tonos_upper : key.tonos_lower;
        case Accent::Diaeresis: return upper ? key.dia_upper : key.dia_lower;
        case Accent::Both: return upper ? key.both_upper : key.both_lower;
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
            key(kAccent, with(static_cast<Ctrl>(LV_BUTTONMATRIX_CTRL_NO_REPEAT |
                                                LV_BUTTONMATRIX_CTRL_CLICK_TRIG), 1));
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

void close_variants(KeyboardState& state) {
    auto* popup = state.popup;
    state.popup = nullptr;
    state.variant_count = 0;
    state.hovered = -1;
    if (popup) lv_obj_delete(popup);
}

void choose_variant(KeyboardState& state, std::size_t index) {
    const char* selected = index < state.variant_count ? state.variants[index] : nullptr;
    close_variants(state);
    if (!selected) return;
    if (auto* area = lv_keyboard_get_textarea(state.keyboard)) lv_textarea_add_text(area, selected);
    state.shift = false;
    state.accent = Accent::None;
    apply(state.keyboard, state);
}

void variant_click(lv_event_t* event) {
    auto* state = static_cast<KeyboardState*>(lv_event_get_user_data(event));
    const auto index = reinterpret_cast<std::uintptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(event)));
    choose_variant(*state, index);
}

void hover_variant(lv_event_t* event, KeyboardState& state) {
    auto* indev = lv_event_get_indev(event);
    if (!indev || !state.popup) return;
    lv_point_t point{};
    lv_indev_get_point(indev, &point);
    state.hovered = -1;
    for (std::size_t index = 0; index < state.variant_count; ++index) {
        auto* button = lv_obj_get_child(state.popup, static_cast<std::int32_t>(index));
        lv_area_t bounds{};
        lv_obj_get_coords(button, &bounds);
        const bool inside = point.x >= bounds.x1 && point.x <= bounds.x2 &&
                            point.y >= bounds.y1 && point.y <= bounds.y2;
        if (inside) state.hovered = static_cast<int>(index);
        lv_obj_set_style_bg_color(button, color(inside ? tokens::blue : tokens::surface), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(button, 0), color(inside ? tokens::on_primary : tokens::text), 0);
    }
}

void open_variants(lv_event_t* event, KeyboardState& state) {
    if (state.popup || state.profile != KeyboardProfile::Text || !state.greek || state.layer != Layer::Letters) return;
    const auto selected = lv_buttonmatrix_get_selected_button(state.keyboard);
    if (selected == LV_BUTTONMATRIX_BUTTON_NONE) return;
    const char* legend = lv_buttonmatrix_get_button_text(state.keyboard, selected);
    if (!legend) return;
    const bool upper = state.shift || state.caps_lock;
    const auto row_key = [&](const auto& row) -> const GreekKey* {
        for (const auto& key : row) {
            if (std::strcmp(legend, greek_legend(key, upper, state.accent)) == 0) return &key;
        }
        return nullptr;
    };
    const GreekKey* key = row_key(kGreekRow1);
    if (!key) key = row_key(kGreekRow2);
    if (!key) key = row_key(kGreekRow3);
    if (!key) return;
    state.variant_count = 0;
    for (const auto accent : {Accent::None, Accent::Tonos, Accent::Diaeresis, Accent::Both}) {
        const char* variant = greek_legend(*key, upper, accent);
        bool duplicate = false;
        for (std::size_t index = 0; index < state.variant_count; ++index) {
            duplicate = duplicate || std::strcmp(state.variants[index], variant) == 0;
        }
        if (!duplicate) state.variants[state.variant_count++] = variant;
    }
    if (state.variant_count < 2) { state.variant_count = 0; return; }
    lv_obj_update_layout(state.keyboard);
    lv_area_t keyboard_area{};
    lv_obj_get_coords(state.keyboard, &keyboard_area);
    lv_point_t point{(keyboard_area.x1 + keyboard_area.x2) / 2, keyboard_area.y1 + 32};
    if (auto* indev = lv_event_get_indev(event)) lv_indev_get_point(indev, &point);
    const int width = static_cast<int>(state.variant_count) * 56 + 56;
    const int screen_width = lv_display_get_horizontal_resolution(lv_obj_get_display(state.keyboard));
    const int left = std::clamp(static_cast<int>(point.x) - width / 2, 6, screen_width - width - 6);
    const int top = std::max(72, static_cast<int>(point.y) - 104);
    state.popup = lv_obj_create(state.keyboard);
    reset(state.popup);
    lv_obj_set_size(state.popup, width, 64);
    lv_obj_set_pos(state.popup, left - keyboard_area.x1 - 4, top - keyboard_area.y1 - 4);
    lv_obj_set_style_bg_color(state.popup, color(tokens::surface_high), 0);
    lv_obj_set_style_bg_opa(state.popup, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(state.popup, tokens::radius_small, 0);
    lv_obj_set_style_border_width(state.popup, 1, 0);
    lv_obj_set_style_border_color(state.popup, color(tokens::separator), 0);
    for (std::size_t index = 0; index <= state.variant_count; ++index) {
        auto* button = lv_button_create(state.popup);
        reset(button);
        lv_obj_set_clickable(button, true);
        lv_obj_set_pos(button, 6 + static_cast<int>(index) * 56, 6);
        lv_obj_set_size(button, index < state.variant_count ? 52 : 44, 52);
        lv_obj_set_style_radius(button, 10, 0);
        lv_obj_set_style_bg_color(button, color(tokens::surface), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        auto* text = make_label(button, index < state.variant_count ? state.variants[index] : LV_SYMBOL_CLOSE,
                                tokens::text, &state.font);
        lv_obj_center(text);
        lv_obj_set_user_data(button, reinterpret_cast<void*>(index));
        lv_obj_add_event_cb(button, variant_click, LV_EVENT_CLICKED, &state);
    }
    state.popup_released = false;
    state.hovered = -1;
    lv_obj_update_layout(state.popup);
}

void keyboard_gesture(lv_event_t* event) {
    auto* state = static_cast<KeyboardState*>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_LONG_PRESSED) open_variants(event, *state);
    else if (code == LV_EVENT_PRESSED && state->popup) close_variants(*state);
    else if (code == LV_EVENT_PRESSING && state->popup && !state->popup_released) hover_variant(event, *state);
    else if (code == LV_EVENT_RELEASED && state->popup && !state->popup_released) {
        hover_variant(event, *state);
        if (state->hovered >= 0) choose_variant(*state, static_cast<std::size_t>(state->hovered));
        else state->popup_released = true;  // also allow hold, release, then tap an option
    }
}

void keyboard_event(lv_event_t* event) {
    auto* keyboard = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    auto* state = static_cast<KeyboardState*>(lv_event_get_user_data(event));
    if (!keyboard || !state) return;
    if (state->popup) return;  // suppress the original plain letter on long-press release
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
                        (state->accent == Accent::Tonos ? Accent::Diaeresis :
                         (state->accent == Accent::Diaeresis ? Accent::Both : Accent::None));
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

}  // namespace

lv_obj_t* create_keyboard(lv_obj_t* parent, lv_obj_t* textarea, KeyboardProfile profile, bool greek_first) {
    auto* keyboard = lv_keyboard_create(parent);
    // lv_keyboard_create aligns to BOTTOM_MID; callers position it absolutely.
    lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
    auto* state = new KeyboardState{};
    state->keyboard = keyboard;
    state->font = *font_headline();
    state->symbols = *LV_FONT_DEFAULT;
    state->symbols.fallback = state->font.fallback;
    state->font.fallback = &state->symbols;
    state->profile = profile;
    state->greek = profile == KeyboardProfile::Text && greek_first;
    // A new sentence or name starts with a capital.
    const char* existing = textarea ? lv_textarea_get_text(textarea) : nullptr;
    state->shift = profile == KeyboardProfile::Text && (!existing || existing[0] == '\0');

    lv_obj_remove_event_cb(keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(keyboard, keyboard_event, LV_EVENT_VALUE_CHANGED, state);
    lv_obj_add_event_cb(keyboard, keyboard_delete, LV_EVENT_DELETE, state);
    lv_obj_add_event_cb(keyboard, keyboard_gesture, LV_EVENT_ALL, state);
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
    lv_obj_set_style_text_font(keyboard, &state->font, LV_PART_ITEMS);
    lv_obj_set_overflow_visible(keyboard, true);
    lv_obj_set_style_text_color(keyboard, color(tokens::text), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, color(tokens::surface_high), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, tokens::radius_small, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, color(tokens::surface), kItemsChecked);
    lv_obj_set_style_text_color(keyboard, color(tokens::text_secondary), kItemsChecked);
    lv_obj_set_style_bg_color(keyboard, color(tokens::blue), kItemsPressed);
    lv_obj_set_style_text_color(keyboard, color(tokens::on_primary), kItemsPressed);
    return keyboard;
}

}  // namespace aura::ui
