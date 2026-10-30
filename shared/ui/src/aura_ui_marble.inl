// Μάρμαρο (Marble): where its signature screens differ from Halo's layout.
// Included inside AuraUi::Impl; the shared components already draw the
// Marble kit. Today keeps the shared screen, which the kit already draws as
// the reference asks (serif greeting and date over hairline rows).
// References: docs/design/themes/marble-*.png.

    lv_obj_t* marble_rule(lv_obj_t* parent, int x, int y, int width, int height) {
        auto* rule = lv_obj_create(parent);
        ui::reset(rule);
        lv_obj_set_size(rule, width, height);
        lv_obj_set_pos(rule, x, y);
        lv_obj_set_style_bg_color(rule, ui::color(ui::tokens::separator), 0);
        lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
        return rule;
    }

    // A small still pearl: an ivory sphere lit from the top left, drawn with a
    // gradient and a highlight (this panel has no shadows).
    lv_obj_t* marble_pearl(lv_obj_t* parent, int center_x, int center_y, int size) {
        auto* pearl = make_dot(parent, size, ui::tokens::text);
        lv_obj_set_pos(pearl, center_x - size / 2, center_y - size / 2);
        lv_obj_set_style_bg_grad_color(pearl, ui::color(ui::tokens::text_tertiary), 0);
        lv_obj_set_style_bg_grad_dir(pearl, LV_GRAD_DIR_VER, 0);
        auto* light = make_dot(pearl, size / 3, 0xFFFFFF, LV_OPA_70);
        lv_obj_set_pos(light, size / 5, size / 8);
        return pearl;
    }

    // «…» in Greek, “…” in English.
    std::string quoted(const char* text) const {
        const int size = std::snprintf(nullptr, 0, c().answer_quote_format, text);
        if (size <= 0) return text;
        std::string result(static_cast<std::size_t>(size), '\0');
        std::snprintf(result.data(), result.size() + 1, c().answer_quote_format, text);
        return result;
    }

    // Answer: a small pearl in place of the title, the question as a serif
    // quote (up to two lines), the answer, then today's actions.
    void render_answer_marble() {
        if (can_speak_answer()) make_header("", ui::IconRole::VolumeUp, speak_answer_event, ui::tokens::mint);
        else make_header("");
        marble_pearl(content, kHaloCenterX, 48, 40);
        constexpr int kQuoteY = 104;
        const auto text = quoted(snapshot.user_text.empty() ? t().ask_example : snapshot.user_text.c_str());
        auto* question = label(content, text.c_str(), kGutter, kQuoteY, kWidth, ui::font_title(),
                               ui::tokens::text_secondary);
        const int line_height = lv_font_get_line_height(ui::font_title());
        const int two_line_height = 2 * line_height + lv_obj_get_style_text_line_space(question, LV_PART_MAIN);
        lv_obj_update_layout(question);
        const bool two_lines = lv_obj_get_height(question) > line_height;
        if (lv_obj_get_height(question) > two_line_height) {
            lv_label_set_long_mode(question, LV_LABEL_LONG_DOT);
            lv_obj_set_height(question, two_line_height);
        }
        const int reader_y = kQuoteY + (two_lines ? two_line_height : line_height) + 16;
        answer_body(reader_y, kActionY - 12 - reader_y);
    }

    // Weather: the city as the title (tap = choose a city); the serif
    // temperature with the condition and range beside it; the hours between
    // hairlines; the measurements as a two-column list. The selected hour or
    // day, hazards, the days and the source follow below.
    void render_weather_ready_marble(bool refreshing) {
        const auto& data = weather.data;
        make_header("", ui::IconRole::Refresh, weather_refresh_event, ui::tokens::ice,
                    static_cast<bool>(callbacks.on_weather_refresh));
        weather_city_title();
        make_weather_scroll();

        // Two digits sit beside the condition; a minus or a third digit takes
        // the smaller numerals.
        const int whole = whole_degrees(data.temperature_c);
        char degrees[16];
        std::snprintf(degrees, sizeof(degrees), "%d°", whole);
        const bool wide = whole <= -10 || whole >= 100;
        label(weather_scroll, degrees, kGutter - 4, 0, 216, wide ? ui::font_numerals_small() : ui::font_numerals_large(),
              ui::tokens::text);
        constexpr int kSideX = 244;
        constexpr int kSideWidth = ui::tokens::display_width - kSideX - kGutter;
        line(weather_scroll, weather_condition_text(data.condition), kSideX, 22, kSideWidth, ui::font_body(),
             ui::tokens::text);
        char range[64];
        std::snprintf(range, sizeof(range), c().weather_range_format, whole_degrees(data.daily_minimum_c),
                      whole_degrees(data.daily_maximum_c));
        line(weather_scroll, range, kSideX, 58, kSideWidth, ui::font_caption(), ui::tokens::text_secondary);
        const bool demo = data.origin == WeatherDataOrigin::Demo;
        const bool cached = data.origin == WeatherDataOrigin::Cache || data.stale;
        if (demo || cached || refreshing) {
            auto* badge = ui::make_tag(weather_scroll,
                demo ? v().demo_badge : (refreshing ? v().weather_refreshing : v().weather_cached),
                demo ? ui::tokens::amber : (refreshing ? ui::tokens::ice : ui::tokens::magenta));
            lv_obj_set_pos(badge, kSideX, 92);
        }
        marble_rule(weather_scroll, kGutter, 128, kWidth, 1);

        // The hours: time, condition and serif temperature; a tap describes the hour below.
        const std::size_t hourly_count = std::min<std::size_t>(data.hourly_count, weather_hour_cards.size());
        const int column = hourly_count > 0 ? kWidth / static_cast<int>(hourly_count) : kWidth;
        for (std::size_t index = 0; index < hourly_count; ++index) {
            const auto& hour = data.hourly[index];
            auto* cell = lv_button_create(weather_scroll);
            ui::reset(cell);
            lv_obj_set_clickable(cell, true);
            lv_obj_set_size(cell, column, 116);
            lv_obj_set_pos(cell, kGutter + static_cast<int>(index) * column, 134);
            lv_obj_set_style_radius(cell, ui::tokens::radius_medium, 0);
            lv_obj_set_style_bg_color(cell, ui::color(ui::tokens::surface_high), LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_PRESSED);
            const auto time = weather_time_label(hour.time);
            line(cell, time.c_str(), 0, 6, column, ui::font_caption(), ui::tokens::text_secondary,
                 LV_TEXT_ALIGN_CENTER);
            auto* icon = icon_view(cell, weather_condition_icon(hour.condition, hour.is_day), 30,
                                   ui::tokens::text_secondary);
            lv_obj_set_pos(icon, (column - 30) / 2, 40);
            char value[16];
            std::snprintf(value, sizeof(value), "%d°", whole_degrees(hour.temperature_c));
            line(cell, value, 0, 74, column, ui::font_title(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
            lv_obj_set_user_data(cell, reinterpret_cast<void*>(static_cast<std::uintptr_t>(index)));
            lv_obj_add_event_cb(cell, weather_hour_event, LV_EVENT_CLICKED, this);
            weather_hour_cards[index] = cell;
        }
        marble_rule(weather_scroll, kGutter, 256, kWidth, 1);

        // The measurements: name and value, two to a row, each over a hairline.
        std::array<WeatherMetricCard, 9> metric_cards{};
        const std::size_t metric_count = weather_metric_cards(metric_cards);
        constexpr int kMetricTop = 264;
        constexpr int kMetricRow = 56;
        constexpr int kMetricWidth = (kWidth - 24) / 2;
        for (std::size_t index = 0; index < metric_count; ++index) {
            const auto& metric = metric_cards[index];
            auto* row = lv_obj_create(weather_scroll);
            ui::reset(row);
            lv_obj_set_size(row, kMetricWidth, kMetricRow);
            lv_obj_set_pos(row, kGutter + static_cast<int>(index % 2) * (kMetricWidth + 24),
                           kMetricTop + static_cast<int>(index / 2) * kMetricRow);
            lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_width(row, 1, 0);
            lv_obj_set_style_border_color(row, ui::color(ui::tokens::separator), 0);
            lv_obj_set_layout(row, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, 12, 0);
            auto* title = ui::make_label(row, metric.title, ui::tokens::text_secondary, ui::font_caption());
            lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
            lv_obj_set_height(title, lv_font_get_line_height(ui::font_caption()));
            lv_obj_set_flex_grow(title, 1);
            ui::make_label(row, metric.value.data(), ui::tokens::text, ui::font_body());
        }
        int y = kMetricTop + static_cast<int>((metric_count + 1) / 2) * kMetricRow + 16;

        const int detail_height = 2 * lv_font_get_line_height(ui::font_caption());
        weather_detail_label = label(weather_scroll, "", kGutter, y, kWidth, ui::font_caption(),
                                     ui::tokens::text_secondary);
        lv_label_set_long_mode(weather_detail_label, LV_LABEL_LONG_DOT);
        lv_obj_set_height(weather_detail_label, detail_height);
        y += detail_height + 16;

        if (data.provider == WeatherProvider::TlesDetector) {
            weather_hazard_block(y);
            y += 188;
        }

        // The days: name, condition and the serif range; a tap describes the day above.
        line(weather_scroll, v().weather_daily, kGutter, y, kWidth, ui::font_caption(), ui::tokens::text_tertiary);
        y += 36;
        const std::size_t daily_count = std::min<std::size_t>(data.daily_count, weather_day_cards.size());
        for (std::size_t index = 0; index < daily_count; ++index) {
            const auto& day_item = data.daily[index];
            auto* row = lv_button_create(weather_scroll);
            ui::reset(row);
            lv_obj_set_clickable(row, true);
            lv_obj_set_size(row, kWidth, 64);
            lv_obj_set_pos(row, kGutter, y);
            lv_obj_set_style_radius(row, ui::tokens::radius_small, 0);
            lv_obj_set_style_bg_color(row, ui::color(ui::tokens::surface_high), LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
            const auto day = weather_day_label(day_item.date, index);
            line(row, day.c_str(), 8, 16, 168, ui::font_body(), ui::tokens::text);
            auto* icon = icon_view(row, weather_condition_icon(day_item.condition), 30, ui::tokens::text_secondary);
            lv_obj_set_pos(icon, 196, 17);
            char value[28];
            std::snprintf(value, sizeof(value), "%d° / %d°", whole_degrees(day_item.minimum_c),
                          whole_degrees(day_item.maximum_c));
            line(row, value, 240, 12, kWidth - 248, ui::font_title(), ui::tokens::text, LV_TEXT_ALIGN_RIGHT);
            lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<std::uintptr_t>(index)));
            lv_obj_add_event_cb(row, weather_day_event, LV_EVENT_CLICKED, this);
            weather_day_cards[index] = row;
            marble_rule(weather_scroll, kGutter, y + 64, kWidth, 1);
            y += 65;
        }
        y += 12;
        const std::string updated = weather_updated_text();
        line(weather_scroll, updated.c_str(), kGutter, y, kWidth, ui::font_caption(), ui::tokens::text_tertiary);
        list_end(weather_scroll, y + lv_font_get_line_height(ui::font_caption()), 24);
    }

    // ---- Timer: a watch dial ----------------------------------------------

    static constexpr int kTimerDialY = 232;

    // The arc's share of the timer still to run, in thousandths.
    static std::int32_t timer_permille(const TimerItem& timer) {
        if (timer.duration_s == 0) return 0;
        return static_cast<std::int32_t>(std::min<std::uint64_t>(1000U, timer.remaining_ms / timer.duration_s));
    }

    // A dial's digits: the look's small numerals; an hour or more ("1:05:00")
    // takes the display numerals to stay inside the dial.
    const lv_font_t* timer_digits_font(const std::string& text) const {
        return text.size() <= 5 ? ui::font_numerals_small() : ui::font_display();
    }

    // Keeps the digits centred on `timer_digits_y` when their length changes the font.
    void place_timer_digits(const std::string& text) {
        const auto* font = timer_digits_font(text);
        if (lv_obj_get_style_text_font(clock_item_label, LV_PART_MAIN) == font) return;
        lv_obj_set_style_text_font(clock_item_label, font, 0);
        lv_obj_set_y(clock_item_label, timer_digits_y - lv_font_get_line_height(font) / 2);
    }

    // "Λήγει 18:32" while running on a known clock (to the nearest minute),
    // «Σε παύση» while paused, nothing without a clock.
    std::string timer_end_text(const TimerItem& timer) const {
        if (timer.paused) return c().paused;
        if (!clock.clock_valid) return {};
        const auto seconds = static_cast<std::int64_t>((timer.remaining_ms + 999U) / 1000U);
        const auto end = from_minute_index(clock.now_minute + (seconds + 30) / 60);
        char text[48];
        std::snprintf(text, sizeof(text), c().timer_ends_format, end.hour, end.minute);
        return text;
    }

    // Sixty ticks, a hairline track and a champagne arc that shrinks with the
    // time left; the serif digits, the label and the end time inside; pause
    // (ivory) and cancel below as round controls.
    void render_timer_marble(const TimerItem& timer) {
        constexpr int kDial = 288;
        constexpr int kTicks = 264;
        timer_digits_y = 196;
        auto* ticks = lv_scale_create(content);
        ui::reset(ticks);
        lv_obj_set_size(ticks, kTicks, kTicks);
        lv_obj_set_pos(ticks, kHaloCenterX - kTicks / 2, kTimerDialY - kTicks / 2);
        lv_scale_set_mode(ticks, LV_SCALE_MODE_ROUND_INNER);
        lv_scale_set_total_tick_count(ticks, 60);
        lv_scale_set_major_tick_every(ticks, 5);
        lv_scale_set_label_show(ticks, false);
        lv_scale_set_angle_range(ticks, 354);
        lv_scale_set_rotation(ticks, 270);
        lv_obj_set_style_arc_opa(ticks, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_length(ticks, 7, LV_PART_ITEMS);
        lv_obj_set_style_line_width(ticks, 1, LV_PART_ITEMS);
        lv_obj_set_style_line_color(ticks, ui::color(ui::tokens::text_tertiary), LV_PART_ITEMS);
        lv_obj_set_style_length(ticks, 13, LV_PART_INDICATOR);
        lv_obj_set_style_line_width(ticks, 2, LV_PART_INDICATOR);
        lv_obj_set_style_line_color(ticks, ui::color(ui::tokens::text_secondary), LV_PART_INDICATOR);

        clock_item_arc = lv_arc_create(content);
        lv_obj_set_size(clock_item_arc, kDial, kDial);
        lv_obj_set_pos(clock_item_arc, kHaloCenterX - kDial / 2, kTimerDialY - kDial / 2);
        lv_obj_set_style_pad_all(clock_item_arc, 0, 0);
        lv_obj_set_style_bg_opa(clock_item_arc, LV_OPA_TRANSP, 0);
        lv_arc_set_rotation(clock_item_arc, 270);
        lv_arc_set_bg_angles(clock_item_arc, 0, 360);
        lv_arc_set_range(clock_item_arc, 0, 1000);
        lv_arc_set_value(clock_item_arc, timer_permille(timer));
        lv_obj_set_style_arc_color(clock_item_arc, ui::color(ui::tokens::separator), LV_PART_MAIN);
        lv_obj_set_style_arc_width(clock_item_arc, 2, LV_PART_MAIN);
        lv_obj_set_style_arc_color(clock_item_arc, ui::color(ui::tokens::indigo), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(clock_item_arc, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(clock_item_arc, true, LV_PART_INDICATOR);
        lv_obj_set_style_opa(clock_item_arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_clickable(clock_item_arc, false);

        const auto text = countdown_text(timer.remaining_ms);
        const auto* font = timer_digits_font(text);
        clock_item_label = label(content, text.c_str(), 0, timer_digits_y - lv_font_get_line_height(font) / 2,
                                 ui::tokens::display_width, font,
                                 timer.paused ? ui::tokens::text_secondary : ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        const std::string name = timer.label.empty() ? duration_words(timer.duration_s) : timer.label;
        line(content, name.c_str(), 130, 244, 220, ui::font_body(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        const auto ends = timer_end_text(timer);
        if (!ends.empty()) {
            line(content, ends.c_str(), 140, 282, 200, ui::font_caption(), ui::tokens::text_tertiary,
                 LV_TEXT_ALIGN_CENTER);
        }

        auto* pause = round_button(content, 152, 384, 80, timer.paused ? ui::IconRole::Play : ui::IconRole::Pause,
                                   ui::tokens::text, timer_pause_event);
        lv_obj_set_style_border_width(pause, 0, 0);
        lv_obj_set_style_bg_color(pause, ui::color(ui::tokens::blue), 0);
        lv_obj_set_style_bg_opa(pause, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(pause, ui::color(ui::tokens::text_secondary), LV_STATE_PRESSED);
        lv_obj_set_style_image_recolor(lv_obj_get_child(pause, 0), ui::color(ui::tokens::on_primary), 0);
        round_button(content, 256, 388, 72, ui::IconRole::Close, ui::tokens::text, clock_item_delete_event, true);
    }

    // A complication: an icon, a serif value and a quiet caption on one line,
    // with no box; the whole 64 px row opens its screen.
    lv_obj_t* marble_complication(int x, int width, ui::IconRole role, const char* value, const char* caption,
                                  lv_event_cb_t callback) {
        auto* item = lv_button_create(content);
        ui::reset(item);
        lv_obj_set_clickable(item, true);
        lv_obj_set_pos(item, x, 404);
        lv_obj_set_size(item, width, ui::tokens::CHIP_HEIGHT);
        lv_obj_set_style_radius(item, ui::tokens::radius_small, 0);
        lv_obj_set_style_bg_color(item, ui::color(ui::tokens::surface_high), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_pad_hor(item, 6, 0);
        lv_obj_set_layout(item, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 10, 0);
        icon_view(item, role, 30, ui::tokens::text_secondary);
        auto* number = ui::make_label(item, value, ui::tokens::text, ui::font_title());
        lv_label_set_long_mode(number, LV_LABEL_LONG_CLIP);
        if (caption) {
            auto* note = ui::make_label(item, caption, ui::tokens::text_tertiary, ui::font_caption());
            lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);
            lv_obj_set_flex_grow(note, 1);
            lv_obj_set_height(note, lv_font_get_line_height(ui::font_caption()));
        }
        if (callback) lv_obj_add_event_cb(item, callback, LV_EVENT_CLICKED, this);
        return item;
    }

    // Home: serif numerals, the date (tap = Today), the pearl (tap = talk),
    // then the weather (or the running timer) and the next alarm side by side.
    void render_home_marble() {
        constexpr int kClockY = 30;
        if (status.clock_valid) {
            home_clock = label(content, "", 0, kClockY, ui::tokens::display_width, ui::font_numerals_large(),
                               ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        } else {
            auto* brand = label(content, t().aura_name, 0, 64, ui::tokens::display_width, ui::font_brand(),
                                ui::tokens::text, LV_TEXT_ALIGN_CENTER);
            lv_obj_set_style_text_letter_space(brand, 10, 0);
        }
        home_date = line(content, "", kGutter, kClockY + lv_font_get_line_height(ui::font_numerals_large()) - 8,
                         kWidth, ui::font_body(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
        update_home_text();
        attach_briefing_target();

        create_presence(ui::MercuryState::Idle, ui::tokens::indigo);
        auto* talk = lv_button_create(content);
        ui::reset(talk);
        lv_obj_set_size(talk, ui::tokens::MERCURY_RECT.width, ui::tokens::MERCURY_RECT.height);
        lv_obj_set_pos(talk, kHaloCenterX - ui::tokens::MERCURY_RECT.width / 2,
                       hero_y() - ui::tokens::MERCURY_RECT.height / 2);
        lv_obj_set_style_radius(talk, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(talk, ui::color(ui::tokens::text), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(talk, LV_OPA_10, LV_STATE_PRESSED);
        lv_obj_set_clickable(talk, static_cast<bool>(callbacks.on_ask));
        lv_obj_add_event_cb(talk, ask_event, LV_EVENT_CLICKED, this);
        line(content, callbacks.on_ask ? t().home_hint : t().provider_unavailable, kGutter,
             hero_y() + ui::tokens::MERCURY_RECT.height / 2 + 4, kWidth, ui::font_caption(),
             ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);

        // The bottom row: one or two complications, then Apps. Side by side they
        // show only an icon and a value, as a watch does; DEMO data still says so.
        constexpr int kRowWidth = 344;
        const auto alarm = next_alarm(clock);
        const bool paired = alarm.has_value();
        const int first_width = paired ? kRowWidth / 2 - 8 : kRowWidth;
        if (const auto* timer = soonest_timer()) {
            const auto countdown = countdown_text(timer->remaining_ms);
            const char* caption = timer->label.empty() ? (timer->paused ? c().paused : c().timer) : timer->label.c_str();
            auto* item = marble_complication(kGutter, first_width,
                                             timer->paused ? ui::IconRole::Pause : ui::IconRole::Timer,
                                             countdown.c_str(), paired ? nullptr : caption, open_clock_event);
            home_timer_label = lv_obj_get_child(item, 1);
        } else {
            const auto glance = weather_glance();
            const bool demo = weather.has_data && weather.data.origin == WeatherDataOrigin::Demo;
            // A cut caption must not lose its DEMO mark, so DEMO leads.
            std::string caption = demo ? std::string(v().demo_badge) + " · " + weather.data.location.name : glance.detail;
            if (paired && demo) caption = v().demo_badge;
            marble_complication(kGutter, first_width, glance.role, glance.title.c_str(),
                                paired && !demo ? nullptr : caption.c_str(), weather_open_event);
        }
        if (alarm) {
            marble_rule(content, kGutter + first_width + 8, 416, 1, 40);
            char time[8];
            std::snprintf(time, sizeof(time), "%02d:%02d", alarm->hour, alarm->minute);
            marble_complication(kGutter + first_width + 16, first_width, ui::IconRole::Reminder, time, nullptr,
                                open_clock_event);
        }
        auto* apps = nav_round_button(content, 392, 404, ui::tokens::CHIP_HEIGHT, ui::IconRole::Apps,
                                      ui::tokens::text, UiView::Launcher, true);
        if (notification_total() > 0) {
            auto* dot = make_dot(apps, 14, ui::tokens::indigo);
            lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -4, 4);
        }
        motion.enter(MotionMode::Idle);
    }
