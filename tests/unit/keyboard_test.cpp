#include "aura/aura_keyboard.hpp"
#include "aura/aura_theme.hpp"
#include "aura/aura_appearance.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {
void expect(bool ok, const char* detail) {
    if (!ok) {
        std::cerr << "FAILED: " << detail << '\n';
        std::exit(1);
    }
}
std::uint32_t key(lv_obj_t* keyboard, const char* text) {
    const auto* map = lv_buttonmatrix_get_map(keyboard);
    std::uint32_t id = 0;
    for (std::size_t index = 0; map[index][0]; ++index) {
        if (std::strcmp(map[index], "\n") == 0) continue;
        if (std::strcmp(map[index], text) == 0) return id;
        ++id;
    }
    return LV_BUTTONMATRIX_BUTTON_NONE;
}
void press(lv_obj_t* keyboard, const char* text, lv_event_code_t event = LV_EVENT_VALUE_CHANGED) {
    const auto id = key(keyboard, text);
    expect(id != LV_BUTTONMATRIX_BUTTON_NONE, "requested legend exists");
    lv_buttonmatrix_set_selected_button(keyboard, id);
    lv_obj_send_event(keyboard, event, nullptr);
}
void variant(lv_obj_t* keyboard, const char* base, const char* selected) {
    auto* area = lv_keyboard_get_textarea(keyboard);
    const std::string before = lv_textarea_get_text(area);
    press(keyboard, base, LV_EVENT_LONG_PRESSED);
    expect(lv_obj_get_child_count(keyboard) == 1, "long press opens a variants panel");
    lv_obj_send_event(keyboard, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_send_event(keyboard, LV_EVENT_RELEASED, nullptr);
    expect(before == lv_textarea_get_text(area), "release never inserts the plain vowel");
    auto* popup = lv_obj_get_child(keyboard, 0);
    bool found = false;
    for (std::uint32_t i = 0; i < lv_obj_get_child_count(popup); ++i) {
        auto* button = lv_obj_get_child(popup, static_cast<std::int32_t>(i));
        if (std::strcmp(lv_label_get_text(lv_obj_get_child(button, 0)), selected) == 0) {
            lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
            found = true;
            break;
        }
    }
    expect(found && lv_obj_get_child_count(keyboard) == 0, "variant tap closes the panel");
    expect(before + selected == lv_textarea_get_text(area), "exactly the selected Greek form is inserted");
}
// The options `base`'s long press offers, then its close button (which inserts nothing).
std::string variants_of(lv_obj_t* keyboard, const char* base) {
    press(keyboard, base, LV_EVENT_LONG_PRESSED);
    std::string options;
    auto* popup = lv_obj_get_child(keyboard, 0);
    const std::uint32_t count = popup ? lv_obj_get_child_count(popup) : 0;
    for (std::uint32_t i = 0; i + 1 < count; ++i) {
        options += lv_label_get_text(lv_obj_get_child(lv_obj_get_child(popup, static_cast<std::int32_t>(i)), 0));
        options += ' ';
    }
    if (count)
        lv_obj_send_event(lv_obj_get_child(popup, static_cast<std::int32_t>(count - 1)), LV_EVENT_CLICKED, nullptr);
    return options;
}
}  // namespace

int main() {
    lv_init();
    auto* display = lv_display_create(480, 480);
    auto* root = lv_obj_create(lv_screen_active());
    lv_obj_set_size(root, 480, 480);
    auto* area = lv_textarea_create(root);
    // Existing text disables the initial one-shot capital.
    lv_textarea_set_text(area, " ");
    auto* keyboard = aura::ui::create_keyboard(root, area, aura::ui::KeyboardProfile::Text);
    lv_obj_set_pos(keyboard, 0, 208);
    lv_obj_set_size(keyboard, 480, 272);
    for (const auto* vowel : {"α", "ε", "η", "ι", "ο", "υ", "ω"}) {
        expect(key(keyboard, vowel) != LV_BUTTONMATRIX_BUTTON_NONE, "Greek vowel exists");
    }
    variant(keyboard, "α", "ά");
    variant(keyboard, "ε", "έ");
    variant(keyboard, "η", "ή");
    variant(keyboard, "ι", "ί");
    variant(keyboard, "ο", "ό");
    variant(keyboard, "υ", "ύ");
    variant(keyboard, "ω", "ώ");
    variant(keyboard, "ι", "ϊ");
    variant(keyboard, "υ", "ϋ");
    variant(keyboard, "ι", "ΐ");
    variant(keyboard, "υ", "ΰ");
    press(keyboard, LV_SYMBOL_UP);
    // Greek capitals keep the diaeresis and drop the tonos, so no option needs
    // a combining accent (Manrope has none; Roboto Flex sets it beside the letter).
    expect(variants_of(keyboard, "Ι") == "Ι Ί Ϊ " && variants_of(keyboard, "Υ") == "Υ Ύ Ϋ ",
           "capital iota and upsilon offer tonos or diaeresis, never both");
    variant(keyboard, "Ι", "Ϊ");
    press(keyboard, LV_SYMBOL_UP);
    variant(keyboard, "Υ", "Ϋ");
    // Three dead-key taps produce the same combined accent as the popup.
    lv_textarea_set_text(area, "");
    press(keyboard, "΄");
    press(keyboard, "΄");
    press(keyboard, "΄");
    press(keyboard, "ΐ");
    expect(std::string(lv_textarea_get_text(area)) == "ΐ", "dead key supports tonos plus diaeresis");
    press(keyboard, "ς");
    expect(std::string(lv_textarea_get_text(area)) == "ΐς", "final sigma remains available");
    press(keyboard, "α", LV_EVENT_LONG_PRESSED);
    lv_obj_delete(keyboard);  // also destroys an open popup safely
    keyboard = aura::ui::create_keyboard(root, area, aura::ui::KeyboardProfile::Password);
    expect(key(keyboard, "α") == LV_BUTTONMATRIX_BUTTON_NONE, "password keyboard remains Latin");
    press(keyboard, "a", LV_EVENT_LONG_PRESSED);
    expect(lv_obj_get_child_count(keyboard) == 0, "password keys never offer Greek variants");
    lv_obj_delete(root);
    lv_display_delete(display);
    lv_deinit();
    std::cout << "keyboard_test: PASS\n";
}
