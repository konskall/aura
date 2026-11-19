// Included inside AuraUi::Impl (aura_ui.cpp): the Αρχεία app, what the memory
// card holds (the owner, 2026-11-18). The screens hold names and sizes; the
// platform's worker reads the card and deletes.

    FileBrowserSnapshot files{};
    std::size_t file_page{0};
    FileEntry file_selected;
    std::string file_selected_path;
    lv_obj_t* file_spinner{};
    static constexpr std::size_t kFilesPerPage = 8;

    const l10n::FileStrings& fx() const { return l10n::file_strings(interface_language); }

    static void files_open_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        self->file_page = 0;
        if (self->callbacks.on_files_open) self->callbacks.on_files_open(std::string{});
        self->navigate(UiView::Files);
    }

    // Back in Αρχεία: up one folder while there is one.
    bool files_go_up() {
        if (current_view != UiView::Files || files.path.empty() || !callbacks.on_files_open) return false;
        return callbacks.on_files_open(file_path_parent(files.path));
    }

    static void files_refresh_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->accept_action() && self->callbacks.on_files_refresh) self->callbacks.on_files_refresh();
    }

    static void files_page_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        self->file_page = user_value(event);
        self->render(UiView::Files);
    }

    void select_file(std::size_t index) {
        file_selected = files.entries[index];
        file_selected_path = file_path_join(files.path, file_selected.name);
    }

    // A folder opens; a file shows what is known about it.
    static void file_row_event(lv_event_t* event) {
        auto* self = self_of(event);
        const auto index = user_value(event);
        if (!self->accept_action() || index >= self->files.entries.size()) return;
        const auto& entry = self->files.entries[index];
        if (entry.folder) {
            if (self->callbacks.on_files_open) {
                self->callbacks.on_files_open(file_path_join(self->files.path, entry.name));
            }
            return;
        }
        self->select_file(index);
        self->navigate(UiView::FileDetails);
    }

    static void file_more_event(lv_event_t* event) {
        auto* self = self_of(event);
        const auto index = user_value(event);
        if (!self->accept_action() || index >= self->files.entries.size()) return;
        self->select_file(index);
        self->navigate(UiView::FileDetails);
    }

    // The folder of the photos app: its pictures open in the viewer.
    std::optional<std::size_t> selected_photo_index() const {
        if (file_path_parent(file_selected_path) != "aura/photos") return std::nullopt;
        const auto found = std::find(photos.names.begin(), photos.names.end(), file_selected.name);
        if (found == photos.names.end()) return std::nullopt;
        return static_cast<std::size_t>(found - photos.names.begin());
    }

    static void file_open_photo_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        const auto index = self->selected_photo_index();
        if (index && self->callbacks.on_photo_select && self->callbacks.on_photo_select(*index)) {
            self->photo_fullscreen = false;
            self->navigate(UiView::PhotoViewer);
        } else {
            self->show_toast(self->p().bad_file, ui::tokens::coral);
        }
    }

    static void file_delete_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->accept_action() && !file_path_protected(self->file_selected_path)) {
            self->navigate(UiView::FileDelete);
        }
    }

    static void file_delete_cancel_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (self->accept_action()) self->go_back();
    }

    static void file_delete_confirm_event(lv_event_t* event) {
        auto* self = self_of(event);
        if (!self->accept_action()) return;
        if (!self->callbacks.on_files_delete || !self->callbacks.on_files_delete(self->file_selected_path)) {
            self->show_toast(self->fx().delete_failed, ui::tokens::coral);
            return;
        }
        self->return_to(UiView::Files);
    }

    ui::IconRole file_role(const FileEntry& entry) const {
        switch (file_kind(entry.name, entry.folder)) {
            case FileKind::Folder: return ui::IconRole::Folder;
            case FileKind::Image: return ui::IconRole::Image;
            case FileKind::Audio: return ui::IconRole::Audio;
            case FileKind::Document: return ui::IconRole::File;
            case FileKind::Other: break;
        }
        return ui::IconRole::File;
    }

    std::uint32_t file_accent(const FileEntry& entry) const {
        switch (file_kind(entry.name, entry.folder)) {
            case FileKind::Folder: return ui::tokens::amber;
            case FileKind::Image: return ui::tokens::mint;
            case FileKind::Audio: return ui::tokens::magenta;
            case FileKind::Document: return ui::tokens::blue;
            case FileKind::Other: break;
        }
        return ui::tokens::text_secondary;
    }

    const char* file_kind_name(const FileEntry& entry) const {
        switch (file_kind(entry.name, entry.folder)) {
            case FileKind::Folder: return fx().folder;
            case FileKind::Image: return fx().kind_image;
            case FileKind::Audio: return fx().kind_audio;
            case FileKind::Document: return fx().kind_document;
            case FileKind::Other: break;
        }
        return fx().kind_other;
    }

    // "2 Νοεμβρίου", with the year when it is not this one.
    std::string file_date_text(std::int64_t minute) const {
        if (minute <= 0) return {};
        const auto when = from_minute_index(minute);
        const int this_year = clock.clock_valid ? from_minute_index(clock.now_minute).year : when.year;
        char text[48];
        if (when.year == this_year) {
            std::snprintf(text, sizeof(text), "%d %s", when.day, v().month_names[when.month - 1]);
        } else {
            std::snprintf(text, sizeof(text), "%d %s %d", when.day, v().month_names[when.month - 1], when.year);
        }
        return text;
    }

    // The row's second line: a folder is named as one, a file by its size and day.
    std::string file_row_detail(const FileEntry& entry) const {
        if (entry.folder) return fx().folder;
        auto detail = file_size_text(entry.bytes, fx().decimal);
        const auto day = file_date_text(entry.modified_minute);
        if (!day.empty()) detail += " · " + day;
        return detail;
    }

    // "Κάρτα SD / aura / photos": where something is on the card.
    std::string file_location(std::string_view folder) const {
        std::string where = fx().card;
        if (!folder.empty()) where += " / " + file_path_display(folder);
        return where;
    }

    void render_files() {
        file_spinner = nullptr;
        const std::string leaf(file_path_leaf(files.path));
        make_header(files.path.empty() ? fx().title : leaf.c_str(), ui::IconRole::Refresh, files_refresh_event,
                    ui::tokens::blue, static_cast<bool>(callbacks.on_files_refresh));
        auto* list = make_list();
        int y = 0;
        if (files.path.empty()) {
            // The root: the card's space, and the way files arrive on it.
            char space[96] = {};
            if (files.space_known) {
                const auto free_text = file_size_text(files.free_bytes, fx().decimal);
                const auto total_text = file_size_text(files.total_bytes, fx().decimal);
                std::snprintf(space, sizeof(space), fx().space_format, free_text.c_str(), total_text.c_str());
            }
            auto* card = list_row(list, y, fx().card, files.space_known ? space : nullptr, ui::IconRole::Storage,
                                  ui::tokens::blue);
            if (files.demo) {
                auto* demo = ui::make_tag(card, t().demo, ui::tokens::amber);
                lv_obj_align(demo, LV_ALIGN_RIGHT_MID, 0, 0);
            }
            y += kRowPitch;
            settings_row(list, y, f().entry, f().intro, ui::IconRole::Download, ui::tokens::mint, UiView::FileTransfer);
            y += kRowPitch;
        } else {
            line(list, file_location(files.path).c_str(), kGutter + 4, y + 4, kWidth - 8, ui::font_caption(),
                 ui::tokens::text_secondary);
            y += 44;
        }
        if (files.error == FileBrowserError::NoCard) {
            label(list, fx().no_card, kGutter, y + 24, kWidth, ui::font_headline(), ui::tokens::text,
                  LV_TEXT_ALIGN_CENTER);
            label(list, fx().no_card_hint, kGutter + 8, y + 72, kWidth - 16, ui::font_caption(),
                  ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            list_end(list, y + 150);
            return;
        }
        if (files.entries.empty()) {
            if (files.loading) {
                file_spinner = spinner_arc(list, 64, ui::tokens::blue, 5);
                lv_obj_set_pos(file_spinner, (ui::tokens::display_width - 64) / 2, y + 24);
                label(list, fx().loading, kGutter, y + 104, kWidth, ui::font_caption(), ui::tokens::text_secondary,
                      LV_TEXT_ALIGN_CENTER);
            } else {
                const bool failed = files.error != FileBrowserError::None;
                label(list, failed ? fx().read_failed : fx().empty, kGutter + 8, y + 48, kWidth - 16, ui::font_body(),
                      ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            }
            list_end(list, y + 160);
            return;
        }
        const std::size_t pages = (files.entries.size() + kFilesPerPage - 1) / kFilesPerPage;
        file_page = std::min(file_page, pages - 1);
        const std::size_t last = std::min(files.entries.size(), (file_page + 1) * kFilesPerPage);
        for (std::size_t index = file_page * kFilesPerPage; index < last; ++index) {
            const auto& entry = files.entries[index];
            const auto detail = file_row_detail(entry);
            auto* row = list_row(list, y, entry.name.c_str(), detail.c_str(), file_role(entry), file_accent(entry),
                                 kRowHeight, 248);
            tappable(row, file_row_event, index);
            auto* more = round_button(row, 356, 16, 56, ui::IconRole::More, ui::tokens::text, file_more_event, true);
            set_value(more, index);
            y += kRowPitch;
        }
        if (pages > 1) {
            auto* prev = round_button(list, kGutter, y, 56, ui::IconRole::ChevronLeft, ui::tokens::blue, files_page_event);
            set_value(prev, file_page ? file_page - 1 : 0);
            ui::set_button_disabled(prev, file_page == 0);
            char page[48];
            std::snprintf(page, sizeof(page), fx().page_format, static_cast<unsigned>(file_page + 1),
                          static_cast<unsigned>(pages));
            line(list, page, 96, y + 12, 288, ui::font_caption(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            auto* next = round_button(list, 400, y, 56, ui::IconRole::Chevron, ui::tokens::blue, files_page_event);
            set_value(next, file_page + 1);
            ui::set_button_disabled(next, file_page + 1 >= pages);
            y += 72;
        }
        if (files.truncated) {
            char shown[64];
            std::snprintf(shown, sizeof(shown), fx().truncated_format, static_cast<unsigned>(kMaxFileEntries));
            line(list, shown, kGutter, y, kWidth, ui::font_caption(), ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);
            y += 40;
        }
        list_end(list, y);
    }

    void render_file_details() {
        make_header(file_selected.name.c_str());
        if (files.demo) header_tag(t().demo, ui::tokens::amber);
        auto* list = make_list(kHeader, kActionY - 8 - kHeader);
        int y = 0;
        const auto where = file_location(file_path_parent(file_selected_path));
        list_row(list, y, fx().location, where.c_str(), ui::IconRole::Location, ui::tokens::blue);
        y += kRowPitch;
        list_row(list, y, fx().kind, file_kind_name(file_selected), file_role(file_selected), file_accent(file_selected));
        y += kRowPitch;
        if (!file_selected.folder) {
            const auto size = file_size_text(file_selected.bytes, fx().decimal);
            list_row(list, y, fx().size, size.c_str(), ui::IconRole::Storage, ui::tokens::indigo);
            y += kRowPitch;
        }
        const auto stamp = note_stamp(file_selected.modified_minute);
        if (!stamp.empty()) {
            list_row(list, y, fx().modified, stamp.c_str(), ui::IconRole::Clock, ui::tokens::indigo);
            y += kRowPitch;
        }
        list_end(list, y);
        if (file_path_protected(file_selected_path)) {
            label(content, fx().device_file, kGutter + 8, kActionY + 8, kWidth - 16, ui::font_caption(),
                  ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            return;
        }
        const bool opens = selected_photo_index().has_value() && callbacks.on_photo_select;
        if (opens) {
            pill(content, fx().open, ui::ButtonTone::Primary, kGutter, kActionY, kHalfWidth, file_open_photo_event);
        }
        auto* remove = pill(content, fx().delete_item, ui::ButtonTone::Danger, opens ? kGutter + kHalfWidth + 16 : kGutter,
                            kActionY, opens ? kHalfWidth : kWidth, file_delete_event);
        ui::set_button_disabled(remove, !callbacks.on_files_delete || files.deleting);
    }

    void render_file_delete() {
        make_header(fx().delete_title);
        if (files.demo) header_tag(t().demo, ui::tokens::amber);
        auto* well = icon_well(content, 80, ui::IconRole::Delete, ui::tokens::coral, 40);
        lv_obj_set_pos(well, 200, 96);
        auto* name = label(content, file_selected.name.c_str(), 32, 192, 416, ui::font_caption(), ui::tokens::text,
                           LV_TEXT_ALIGN_CENTER);
        lv_obj_set_height(name, 96);
        label(content, file_selected.folder ? fx().delete_folder_hint : fx().delete_file_hint, 32, 296, 416,
              ui::font_caption(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
        auto* cancel = pill(content, fx().cancel, ui::ButtonTone::Secondary, 24, 396, 204, file_delete_cancel_event);
        auto* confirm = pill(content, fx().delete_confirm, ui::ButtonTone::Danger, 240, 396, 216,
                             file_delete_confirm_event);
        lv_obj_set_height(cancel, 64);
        lv_obj_set_height(confirm, 64);
    }
