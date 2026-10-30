// Σέλας (Aurora): where its signature screens differ from Halo's layout.
// Included inside AuraUi::Impl; the shared components already draw the glass
// kit. Light fields sit only where nothing animates. Today keeps the shared
// screen, which the kit already draws as the reference asks (greeting, glass
// rows with tinted chips). References: docs/design/themes/aurora-*.png.

    // A small orb for headers: teal into violet, lit from the top left.
    lv_obj_t* aurora_orb(lv_obj_t* parent, int center_x, int center_y, int size) {
        auto* orb = make_dot(parent, size, ui::tokens::ice);
        lv_obj_set_pos(orb, center_x - size / 2, center_y - size / 2);
        lv_obj_set_style_bg_grad_color(orb, ui::color(ui::tokens::lilac), 0);
        lv_obj_set_style_bg_grad_dir(orb, LV_GRAD_DIR_HOR, 0);
        auto* light = make_dot(orb, size / 3, 0xFFFFFF, LV_OPA_60);
        lv_obj_set_pos(light, size / 5, size / 7);
        return orb;
    }

    // A glass tile on Home: a tinted chip, a value and a quiet caption; the
    // whole 84 px tile opens its screen. Returns the value label.
    lv_obj_t* aurora_tile(int x, int width, ui::IconRole role, std::uint32_t accent, const char* value,
                          const char* caption, lv_event_cb_t callback) {
        auto* tile = lv_button_create(content);
        ui::reset(tile);
        ui::style_glass(tile, ui::tokens::radius_medium);
        lv_obj_set_clickable(tile, true);
        lv_obj_set_size(tile, width, 84);
        lv_obj_set_pos(tile, x, 380);
        lv_obj_set_style_bg_color(tile, ui::color(ui::tokens::surface_glass), LV_STATE_PRESSED);
        auto* chip = icon_well(tile, 48, role, accent, 28);
        lv_obj_align(chip, LV_ALIGN_LEFT_MID, 14, 0);
        constexpr int kTextX = 74;
        auto* number = line(tile, value, kTextX, caption ? 8 : 24, width - kTextX - 12, ui::font_headline(),
                            ui::tokens::text);
        if (caption) {
            line(tile, caption, kTextX, 44, width - kTextX - 12, ui::font_caption(), ui::tokens::text_secondary);
        }
        if (callback) lv_obj_add_event_cb(tile, callback, LV_EVENT_CLICKED, this);
        return number;
    }

    // Home: the clock at the top left, the presence centred with the hint,
    // glass tiles for the weather (or a running timer) and the next alarm, and
    // Apps at the top right. Two light fields tint the still corners.
    void render_home_aurora() {
        ui::light_field(content, 36, 470, 170, ui::tokens::ice, 50);
        ui::light_field(content, 476, 30, 170, ui::tokens::indigo, 64);
        if (status.clock_valid) {
            home_clock = label(content, "", 30, 34, 330, ui::font_display(), ui::tokens::text);
        } else {
            auto* brand = label(content, t().aura_name, 30, 40, 330, ui::font_brand(), ui::tokens::text);
            lv_obj_set_style_text_letter_space(brand, 10, 0);
        }
        home_date = line(content, "", 32, 34 + lv_font_get_line_height(ui::font_display()) + 4, 330, ui::font_body(),
                         ui::tokens::text_secondary);
        update_home_text();
        attach_briefing_target();
        auto* apps = nav_round_button(content, 392, 46, ui::tokens::HEADER_BUTTON, ui::IconRole::Apps,
                                      ui::tokens::text, UiView::Launcher, true);
        if (notification_total() > 0) {
            auto* dot = make_dot(apps, 14, ui::tokens::magenta);
            lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -4, 4);
        }

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
             hero_y() + ui::tokens::MERCURY_RECT.height / 2 + 6, kWidth, ui::font_body(), ui::tokens::text_secondary,
             LV_TEXT_ALIGN_CENTER);

        // The tiles: one or two side by side; DEMO data still says so first.
        const auto alarm = next_alarm(clock);
        const int first_width = alarm ? (kWidth - 16) / 2 : kWidth;
        if (const auto* timer = soonest_timer()) {
            const auto countdown = countdown_text(timer->remaining_ms);
            const char* caption = timer->label.empty() ? (timer->paused ? c().paused : c().timer) : timer->label.c_str();
            home_timer_label = aurora_tile(kGutter, first_width, timer->paused ? ui::IconRole::Pause : ui::IconRole::Timer,
                                           ui::tokens::coral, countdown.c_str(), caption, open_clock_event);
        } else {
            const auto glance = weather_glance();
            const bool demo = weather.has_data && weather.data.origin == WeatherDataOrigin::Demo;
            const std::string caption =
                demo ? std::string(v().demo_badge) + " · " + weather.data.location.name : glance.detail;
            aurora_tile(kGutter, first_width, glance.role, glance.accent, glance.title.c_str(), caption.c_str(),
                        weather_open_event);
        }
        if (alarm) {
            char time[8];
            std::snprintf(time, sizeof(time), "%02d:%02d", alarm->hour, alarm->minute);
            const char* day = nullptr;
            if (clock.clock_valid) {
                const auto now = static_cast<int>(((clock.now_minute % 1440) + 1440) % 1440);
                day = alarm->hour * 60 + alarm->minute > now ? c().today : c().tomorrow;
            }
            aurora_tile(kGutter + first_width + 16, first_width, ui::IconRole::Reminder, ui::tokens::amber, time, day,
                        open_clock_event);
        }
        motion.enter(MotionMode::Idle);
    }

    // Answer: the orb and the assistant's name in the header, the question in
    // a glass bubble on the right, the answer, then Ask again with the speaker
    // as a round glass button beside it; the conversation, when there is one,
    // is the header's action.
    void render_answer_aurora() {
        const bool conversation = extras.memory_turns > 0 && callbacks.on_conversation_clear;
        if (conversation) make_header("", ui::IconRole::List, conversation_history_event, ui::tokens::lilac);
        else make_header("");
        aurora_orb(content, 118, 48, 36);
        line(content, c().conversation_aura, 146, 28, 228, ui::font_title(), ui::tokens::text);

        auto* bubble = lv_obj_create(content);
        ui::reset(bubble);
        ui::style_glass(bubble, 22);
        lv_obj_set_style_pad_hor(bubble, 18, 0);
        lv_obj_set_style_pad_ver(bubble, 10, 0);
        lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        auto* question = ui::make_label(bubble, snapshot.user_text.empty() ? t().ask_example : snapshot.user_text.c_str(),
                                        ui::tokens::text, ui::font_body());
        lv_obj_set_style_max_width(question, kWidth - 72, 0);
        lv_obj_update_layout(bubble);
        const int two_lines = 2 * lv_font_get_line_height(ui::font_body()) +
                              lv_obj_get_style_text_line_space(question, LV_PART_MAIN);
        if (lv_obj_get_height(question) > two_lines) {
            lv_obj_set_width(question, kWidth - 72);
            lv_label_set_long_mode(question, LV_LABEL_LONG_DOT);
            lv_obj_set_height(question, two_lines);
        }
        lv_obj_align(bubble, LV_ALIGN_TOP_RIGHT, -kGutter, 98);
        lv_obj_update_layout(bubble);
        const int reader_y = lv_obj_get_y(bubble) + lv_obj_get_height(bubble) + 18;
        answer_reader(reader_y, kActionY - 14 - reader_y);
        if (answer_confirming()) {
            answer_actions();
            return;
        }
        const bool speak = can_speak_answer();
        pill(content, t().ask_again, ui::ButtonTone::Primary, kGutter, kActionY, speak ? kWidth - 96 : kWidth, ask_event);
        if (speak) {
            round_button(content, ui::tokens::display_width - kGutter - 80, kActionY, 80, ui::IconRole::VolumeUp,
                         ui::tokens::text, speak_answer_event, true);
        }
    }

    // A transparent button inside a glass panel: a selection outline appears
    // on it, never on the panel's edge.
    lv_obj_t* glass_cell(lv_obj_t* parent, int x, int y, int width, int height) {
        auto* cell = lv_button_create(parent);
        ui::reset(cell);
        lv_obj_set_clickable(cell, true);
        lv_obj_set_size(cell, width, height);
        lv_obj_set_pos(cell, x, y);
        lv_obj_set_style_radius(cell, ui::tokens::radius_small, 0);
        lv_obj_set_style_bg_color(cell, ui::color(ui::tokens::surface_glass), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_PRESSED);
        return cell;
    }

    lv_obj_t* glass_panel(int x, int y, int width, int height) {
        auto* panel = lv_obj_create(weather_scroll);
        ui::reset(panel);
        ui::style_glass(panel, ui::tokens::radius_medium);
        lv_obj_set_size(panel, width, height);
        lv_obj_set_pos(panel, x, y);
        return panel;
    }

    // Weather: the city as the title (tap = choose a city); the temperature
    // over a sky of light fields; the hours in a glass strip; the measurements
    // in glass tiles, three to a row (icon, name, value); then the selected
    // hour or day, hazards, the days in a glass panel and the source.
    void render_weather_ready_aurora(bool refreshing) {
        const auto& data = weather.data;
        ui::light_field(content, 430, 26, 200, ui::tokens::indigo, 70);
        ui::light_field(content, 36, 0, 150, ui::tokens::coral, 36);
        make_header("", ui::IconRole::Refresh, weather_refresh_event, ui::tokens::ice,
                    static_cast<bool>(callbacks.on_weather_refresh));
        weather_city_title();
        make_weather_scroll();

        const int whole = whole_degrees(data.temperature_c);
        char degrees[16];
        std::snprintf(degrees, sizeof(degrees), "%d°", whole);
        const bool wide = whole <= -10 || whole >= 100;
        label(weather_scroll, degrees, kGutter, 0, 220, wide ? ui::font_numerals_small() : ui::font_numerals_large(),
              ui::tokens::text);
        constexpr int kSideX = 236;
        constexpr int kSideWidth = ui::tokens::display_width - kSideX - kGutter;
        line(weather_scroll, weather_condition_text(data.condition), kSideX, 18, kSideWidth, ui::font_headline(),
             ui::tokens::text, LV_TEXT_ALIGN_RIGHT);
        char range[64];
        std::snprintf(range, sizeof(range), c().weather_range_format, whole_degrees(data.daily_minimum_c),
                      whole_degrees(data.daily_maximum_c));
        line(weather_scroll, range, kSideX, 58, kSideWidth, ui::font_caption(), ui::tokens::text_secondary,
             LV_TEXT_ALIGN_RIGHT);
        const bool demo = data.origin == WeatherDataOrigin::Demo;
        const bool cached = data.origin == WeatherDataOrigin::Cache || data.stale;
        if (demo || cached || refreshing) {
            auto* badge = ui::make_tag(weather_scroll,
                demo ? v().demo_badge : (refreshing ? v().weather_refreshing : v().weather_cached),
                demo ? ui::tokens::amber : (refreshing ? ui::tokens::ice : ui::tokens::magenta));
            lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -kGutter, 92);
        }

        // The hours in one glass strip; a tap describes the hour below.
        constexpr int kStripY = 128;
        constexpr int kStripHeight = 112;
        auto* strip = glass_panel(kGutter, kStripY, kWidth, kStripHeight);
        const std::size_t hourly_count = std::min<std::size_t>(data.hourly_count, weather_hour_cards.size());
        const int column = hourly_count > 0 ? kWidth / static_cast<int>(hourly_count) : kWidth;
        for (std::size_t index = 0; index < hourly_count; ++index) {
            const auto& hour = data.hourly[index];
            auto* cell = glass_cell(strip, static_cast<int>(index) * column, 0, column, kStripHeight - 2);
            const auto time = weather_time_label(hour.time);
            line(cell, time.c_str(), 0, 8, column, ui::font_caption(), ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
            auto* icon = icon_view(cell, weather_condition_icon(hour.condition, hour.is_day), 30,
                                   weather_condition_accent(hour.condition, hour.is_day));
            lv_obj_set_pos(icon, (column - 30) / 2, 40);
            char value[16];
            std::snprintf(value, sizeof(value), "%d°", whole_degrees(hour.temperature_c));
            line(cell, value, 0, 72, column, ui::font_headline(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
            lv_obj_set_user_data(cell, reinterpret_cast<void*>(static_cast<std::uintptr_t>(index)));
            lv_obj_add_event_cb(cell, weather_hour_event, LV_EVENT_CLICKED, this);
            weather_hour_cards[index] = cell;
        }

        // The measurements: glass tiles, three to a row.
        std::array<WeatherMetricCard, 9> metric_cards{};
        const std::size_t metric_count = weather_metric_cards(metric_cards);
        constexpr int kTileTop = kStripY + kStripHeight + 12;
        constexpr int kTileWidth = (kWidth - 24) / 3;
        constexpr int kTileHeight = 112;
        for (std::size_t index = 0; index < metric_count; ++index) {
            const auto& metric = metric_cards[index];
            auto* tile = glass_panel(kGutter + static_cast<int>(index % 3) * (kTileWidth + 12),
                                     kTileTop + static_cast<int>(index / 3) * (kTileHeight + 12), kTileWidth,
                                     kTileHeight);
            auto* icon = icon_view(tile, metric.icon, 24, metric.accent);
            lv_obj_set_pos(icon, 12, 10);
            line(tile, metric.title, 12, 40, kTileWidth - 24, ui::font_caption(), ui::tokens::text_secondary);
            line(tile, metric.value.data(), 12, 70, kTileWidth - 24, ui::font_headline(), ui::tokens::text);
        }
        int y = kTileTop + static_cast<int>((metric_count + 2) / 3) * (kTileHeight + 12) + 4;

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

        // The days in one glass panel; a tap describes the day above.
        line(weather_scroll, v().weather_daily, kGutter, y, kWidth, ui::font_caption(), ui::tokens::text_tertiary);
        y += 36;
        constexpr int kDayRow = 64;
        const std::size_t daily_count = std::min<std::size_t>(data.daily_count, weather_day_cards.size());
        auto* days = glass_panel(kGutter, y, kWidth, static_cast<int>(daily_count) * kDayRow + 8);
        for (std::size_t index = 0; index < daily_count; ++index) {
            const auto& day_item = data.daily[index];
            auto* row = glass_cell(days, 4, 4 + static_cast<int>(index) * kDayRow, kWidth - 10, kDayRow);
            const auto day = weather_day_label(day_item.date, index);
            line(row, day.c_str(), 14, 15, 164, ui::font_body(), ui::tokens::text);
            auto* icon = icon_view(row, weather_condition_icon(day_item.condition), 30,
                                   weather_condition_accent(day_item.condition));
            lv_obj_set_pos(icon, 192, 17);
            char value[28];
            std::snprintf(value, sizeof(value), "%d° / %d°", whole_degrees(day_item.minimum_c),
                          whole_degrees(day_item.maximum_c));
            line(row, value, 236, 14, kWidth - 10 - 250, ui::font_headline(), ui::tokens::text, LV_TEXT_ALIGN_RIGHT);
            lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<std::uintptr_t>(index)));
            lv_obj_add_event_cb(row, weather_day_event, LV_EVENT_CLICKED, this);
            weather_day_cards[index] = row;
        }
        y += static_cast<int>(daily_count) * kDayRow + 8 + 16;
        const std::string updated = weather_updated_text();
        line(weather_scroll, updated.c_str(), kGutter, y, kWidth, ui::font_caption(), ui::tokens::text_tertiary);
        list_end(weather_scroll, y + lv_font_get_line_height(ui::font_caption()), 24);
    }

    // ---- Timer: a ring of light --------------------------------------------

    static constexpr int kTimerSegments = 36;

    // Teal through blue and violet to rose around the ring.
    static std::uint32_t aurora_segment_color(int index) {
        constexpr std::array<std::uint32_t, 4> stops{0x38E1C9U, 0x3A7BFFU, 0x9D6BFFU, 0xFF6FB1U};
        const float position = static_cast<float>(index) * 3.0F / static_cast<float>(kTimerSegments - 1);
        const auto from = static_cast<std::size_t>(std::min(2.0F, std::floor(position)));
        const float share = position - static_cast<float>(from);
        const auto mix = [&](int shift) {
            const auto a = static_cast<float>((stops[from] >> shift) & 0xFFU);
            const auto b = static_cast<float>((stops[from + 1] >> shift) & 0xFFU);
            return static_cast<std::uint32_t>(a + (b - a) * share + 0.5F) << shift;
        };
        return mix(16) | mix(8) | mix(0);
    }

    // The segments still to run are lit, from the top clockwise.
    void light_timer_segments(const TimerItem& timer) {
        const std::uint64_t total = static_cast<std::uint64_t>(timer.duration_s) * 1000U;
        const int lit = total == 0 ? 0 :
            static_cast<int>((timer.remaining_ms * kTimerSegments + total - 1) / total);
        for (int index = 0; index < kTimerSegments; ++index) {
            auto* segment = clock_item_segments[static_cast<std::size_t>(index)];
            const bool hidden = index >= lit;
            if (segment && lv_obj_is_hidden(segment) != hidden) lv_obj_set_hidden(segment, hidden);
        }
    }

    // A ring of 36 segments over a dark track, the digits and the label
    // inside, and the gradient pause and a glass cancel below.
    void render_timer_aurora(const TimerItem& timer) {
        constexpr int kRing = 280;
        constexpr int kCenterY = 222;
        constexpr int kStroke = 12;
        timer_digits_y = kCenterY - 22;
        auto* track = lv_obj_create(content);
        ui::reset(track);
        lv_obj_set_size(track, kRing, kRing);
        lv_obj_set_pos(track, kHaloCenterX - kRing / 2, kCenterY - kRing / 2);
        lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(track, kStroke, 0);
        lv_obj_set_style_border_color(track, ui::color(ui::tokens::surface_high), 0);
        for (int index = 0; index < kTimerSegments; ++index) {
            auto* segment = lv_arc_create(content);
            lv_obj_remove_style_all(segment);
            lv_obj_set_size(segment, kRing, kRing);
            lv_obj_set_pos(segment, kHaloCenterX - kRing / 2, kCenterY - kRing / 2);
            lv_arc_set_rotation(segment, 270);
            // Each covers its 10 degrees less a 2 degree gap.
            lv_arc_set_angles(segment, static_cast<lv_value_precise_t>(index * 10 + 1),
                              static_cast<lv_value_precise_t>(index * 10 + 9));
            lv_obj_set_style_arc_opa(segment, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_arc_width(segment, kStroke, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(segment, ui::color(aurora_segment_color(index)), LV_PART_INDICATOR);
            lv_obj_set_style_opa(segment, LV_OPA_TRANSP, LV_PART_KNOB);
            lv_obj_set_clickable(segment, false);
            clock_item_segments[static_cast<std::size_t>(index)] = segment;
        }
        light_timer_segments(timer);

        const auto text = countdown_text(timer.remaining_ms);
        const auto* font = timer_digits_font(text);
        clock_item_label = label(content, text.c_str(), 0, timer_digits_y - lv_font_get_line_height(font) / 2,
                                 ui::tokens::display_width, font,
                                 timer.paused ? ui::tokens::text_secondary : ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        const std::string name = timer.label.empty() ? duration_words(timer.duration_s) : timer.label;
        line(content, name.c_str(), 150, kCenterY + 24, 180, ui::font_body(), ui::tokens::text_secondary,
             LV_TEXT_ALIGN_CENTER);

        auto* pause = round_button(content, 152, 384, 80, timer.paused ? ui::IconRole::Play : ui::IconRole::Pause,
                                   ui::tokens::text, timer_pause_event);
        lv_obj_set_style_border_width(pause, 0, 0);
        lv_obj_set_style_bg_color(pause, ui::color(ui::tokens::blue), 0);
        lv_obj_set_style_bg_grad_color(pause, ui::color(ui::tokens::primary_end), 0);
        lv_obj_set_style_bg_grad_dir(pause, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(pause, LV_OPA_COVER, 0);
        lv_obj_set_style_image_recolor(lv_obj_get_child(pause, 0), ui::color(ui::tokens::on_primary), 0);
        round_button(content, 256, 388, 72, ui::IconRole::Close, ui::tokens::text, clock_item_delete_event, true);
    }
