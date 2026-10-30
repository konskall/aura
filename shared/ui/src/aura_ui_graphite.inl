// Γραφίτης (Graphite): where its signature screens differ from Halo's layout.
// Included inside AuraUi::Impl; the shared components already draw the
// graphite kit (solid platters, squircles, a white Primary). Colour appears
// only where something is happening. References: docs/design/themes/graphite-*.png.

    // The weather compass's eight marks; lv_line keeps a pointer to its points.
    std::array<std::array<lv_point_precise_t, 2>, 8> compass_points{};

    // ---- Apps: live tiles ----------------------------------------------------

    struct LauncherLive {
        std::string status;
        bool active{false};
    };

    // An app's live line under its name, and whether something is happening
    // there (only those tiles take their colour).
    LauncherLive launcher_live(LauncherApp app) const {
        LauncherLive live;
        char text[64];
        switch (app) {
            case LauncherApp::Weather:
                if (weather.has_data) {
                    live.status = weather_glance().title + " · " + weather.data.location.name;
                    live.active = true;
                }
                break;
            case LauncherApp::Clock:
                if (const auto* timer = soonest_timer()) {
                    live.status = countdown_text(timer->remaining_ms);
                    live.active = true;
                } else if (const auto alarm = next_alarm(clock)) {
                    std::snprintf(text, sizeof(text), "%02d:%02d", alarm->hour, alarm->minute);
                    live.status = text;
                    live.active = true;
                }
                break;
            case LauncherApp::Messages: {
                const auto unread = static_cast<int>(aoraton.notifications.size());
                if (unread == 1) live.status = c().briefing_messages_one;
                if (unread > 1) {
                    std::snprintf(text, sizeof(text), c().briefing_messages_format, unread);
                    live.status = text;
                }
                live.active = unread > 0;
                break;
            }
            case LauncherApp::Radio:
                if (radio.state == RadioState::Playing) {
                    live.status = radio.station.name.empty() ? c().radio_playing : radio.station.name;
                    live.active = true;
                }
                break;
            case LauncherApp::Notifications:
                live.active = notification_total() > 0;
                break;
            case LauncherApp::Conversation:
                live.active = !extras.conversation_history.empty();
                break;
            default:
                break;
        }
        return live;
    }

    // A two-column live tile: a squircle (in its colour only when active),
    // the name and, when there is one, the live line.
    void graphite_app_cell(lv_obj_t* cell, int width, std::size_t app_index, const char* title, ui::IconRole role,
                           std::uint32_t accent, int inset) {
        const auto live = launcher_live(launcher_layout.order[app_index]);
        ui::style_platter(cell, accent, ui::tokens::radius_medium, false);
        lv_obj_set_style_pad_all(cell, 0, 0);
        auto* well = icon_well(cell, 44, role, accent, 24);
        if (!live.active) {
            lv_obj_set_style_bg_color(well, ui::color(ui::tokens::surface_high), 0);
            lv_obj_set_style_bg_opa(well, LV_OPA_COVER, 0);
            lv_obj_set_style_image_recolor(lv_obj_get_child(well, 0), ui::color(ui::tokens::text_secondary), 0);
        }
        lv_obj_set_pos(well, 12, 20 - inset);
        constexpr int kTextX = 66;
        const int text_width = width - kTextX - 6;
        if (live.status.empty()) {
            line(cell, title, kTextX, 24 - inset, text_width, ui::font_body(), ui::tokens::text);
            return;
        }
        line(cell, title, kTextX, 10 - inset, text_width, ui::font_body(), ui::tokens::text);
        line(cell, live.status.c_str(), kTextX, 46 - inset, text_width, ui::font_caption(), ui::tokens::text_secondary);
    }

    // ---- Home: the hero clock and live tiles --------------------------------

    // A live tile: an icon and a name, a large condensed value and a quiet
    // detail; the whole 112 px tile opens its screen. Returns the value label.
    lv_obj_t* graphite_tile(int x, int y, ui::IconRole role, std::uint32_t accent, const char* name, const char* value,
                            const char* detail, lv_event_cb_t callback) {
        auto* tile = lv_button_create(content);
        ui::reset(tile);
        ui::style_platter(tile, accent, ui::tokens::radius_medium, false);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_set_clickable(tile, true);
        lv_obj_set_size(tile, 210, 112);
        lv_obj_set_pos(tile, x, y);
        auto* icon = icon_view(tile, role, 26, accent);
        lv_obj_set_pos(icon, 14, 12);
        line(tile, name, 48, 8, 152, ui::font_body(), ui::tokens::text);
        auto* number = line(tile, value, 14, 52, detail ? 112 : 182, ui::font_numerals_small(), ui::tokens::text);
        if (detail) line(tile, detail, 112, 74, 84, ui::font_caption(), ui::tokens::text_secondary, LV_TEXT_ALIGN_RIGHT);
        if (callback) lv_obj_add_event_cb(tile, callback, LV_EVENT_CLICKED, this);
        return number;
    }

    // Home: the hero clock at the left, the day and date at the right, four
    // live tiles (the weather, the alarm or a running timer, messages, and
    // «Ρώτα την Αύρα», which starts listening), Apps at the top right. No
    // large presence here: it appears on the listening screen.
    void render_home_graphite() {
        if (status.clock_valid) {
            home_clock = label(content, "", 20, 36, 260, ui::font_hero(), ui::tokens::text);
        } else {
            auto* brand = label(content, t().aura_name, kGutter, 80, 300, ui::font_brand(), ui::tokens::text);
            lv_obj_set_style_text_letter_space(brand, 10, 0);
        }
        home_date = label(content, "", 296, 128, 160, ui::font_headline(), ui::tokens::text_secondary,
                          LV_TEXT_ALIGN_RIGHT);
        update_home_text();
        attach_briefing_target();
        auto* apps = nav_round_button(content, 392, 46, ui::tokens::HEADER_BUTTON, ui::IconRole::Apps,
                                      ui::tokens::text, UiView::Launcher, true);
        if (notification_total() > 0) {
            auto* dot = make_dot(apps, 14, ui::tokens::magenta);
            lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -4, 4);
        }

        constexpr int kLeft = 24;
        constexpr int kRight = 246;
        constexpr int kTop = 226;
        constexpr int kBottom = 350;
        // The weather; DEMO data says so before the city.
        const auto glance = weather_glance();
        const bool demo = weather.has_data && weather.data.origin == WeatherDataOrigin::Demo;
        std::string place = weather.has_data ? weather.data.location.name : std::string(v().weather);
        if (demo) place = std::string(v().demo_badge) + " · " + place;
        char range[32] = {};
        if (weather.has_data) {
            std::snprintf(range, sizeof(range), "%d° / %d°", whole_degrees(weather.data.daily_maximum_c),
                          whole_degrees(weather.data.daily_minimum_c));
        }
        graphite_tile(kLeft, kTop, glance.role, glance.accent, place.c_str(), glance.title.c_str(),
                      weather.has_data ? range : nullptr, weather_open_event);
        // A running timer, else the next alarm.
        if (const auto* timer = soonest_timer()) {
            const auto countdown = countdown_text(timer->remaining_ms);
            home_timer_label = graphite_tile(kRight, kTop, timer->paused ? ui::IconRole::Pause : ui::IconRole::Timer,
                                             ui::tokens::coral, timer->label.empty() ? c().timer : timer->label.c_str(),
                                             countdown.c_str(), nullptr, open_clock_event);
        } else {
            const auto alarm = next_alarm(clock);
            char time[8] = "—";
            const char* day = nullptr;
            if (alarm) {
                std::snprintf(time, sizeof(time), "%02d:%02d", alarm->hour, alarm->minute);
                if (clock.clock_valid) {
                    const auto now = static_cast<int>(((clock.now_minute % 1440) + 1440) % 1440);
                    day = alarm->hour * 60 + alarm->minute > now ? c().today : c().tomorrow;
                }
            }
            graphite_tile(kRight, kTop, ui::IconRole::Reminder, alarm ? ui::tokens::amber : ui::tokens::text_tertiary,
                          c().alarm, time, day, open_clock_event);
        }
        // Messages: the unread count opens the rooms.
        const auto unread = static_cast<int>(aoraton.notifications.size());
        char count[12];
        std::snprintf(count, sizeof(count), "%d", unread);
        auto* messages = lv_obj_get_parent(graphite_tile(kLeft, kBottom, ui::IconRole::Message,
                                                         unread > 0 ? ui::tokens::pink : ui::tokens::text_tertiary,
                                                         v().apps_messages, count, nullptr, nullptr));
        set_target(messages, UiView::AoratonRooms);
        lv_obj_add_event_cb(messages, nav_event, LV_EVENT_CLICKED, this);
        // «Ρώτα την Αύρα»: a violet tile with the assistant's mark and a microphone.
        auto* ask = lv_button_create(content);
        ui::reset(ask);
        lv_obj_set_size(ask, 210, 112);
        lv_obj_set_pos(ask, kRight, kBottom);
        lv_obj_set_style_radius(ask, ui::tokens::radius_medium, 0);
        lv_obj_set_style_bg_color(ask, ui::color(ui::tokens::lilac), 0);
        lv_obj_set_style_bg_opa(ask, 56, 0);
        lv_obj_set_style_bg_opa(ask, LV_OPA_40, LV_STATE_PRESSED);
        auto* mark = icon_view(ask, ui::IconRole::Sparkles, 30, ui::tokens::lilac);
        lv_obj_set_pos(mark, 16, 16);
        auto* mic = make_dot(ask, 46, ui::tokens::text);
        lv_obj_set_pos(mic, 148, 12);
        auto* mic_icon = icon_view(mic, ui::IconRole::Microphone, 26, ui::tokens::canvas);
        lv_obj_center(mic_icon);
        line(ask, c().graphite_ask, 16, 68, 182, ui::font_headline(), ui::tokens::text);
        lv_obj_set_clickable(ask, static_cast<bool>(callbacks.on_ask));
        lv_obj_add_event_cb(ask, ask_event, LV_EVENT_CLICKED, this);
        motion.enter(MotionMode::Idle);
    }

    // ---- Answer -------------------------------------------------------------

    // Answer: the assistant's mark and name, the question as a quote, the
    // answer as text and today's actions. (No device signal says a turn was
    // answered from the loaded forecast, so there is no weather card.)
    void render_answer_graphite() {
        if (can_speak_answer()) make_header("", ui::IconRole::VolumeUp, speak_answer_event, ui::tokens::mint);
        else make_header("");
        auto* mark = make_dot(content, 32, ui::tokens::lilac, LV_OPA_20);
        lv_obj_set_pos(mark, 100, 32);
        lv_obj_set_style_border_width(mark, 2, 0);
        lv_obj_set_style_border_color(mark, ui::color(ui::tokens::lilac), 0);
        auto* core = make_dot(mark, 10, ui::tokens::lilac);
        lv_obj_center(core);
        line(content, c().conversation_aura, 142, 28, 220, ui::font_title(), ui::tokens::text);
        const auto question = quoted(snapshot.user_text.empty() ? t().ask_example : snapshot.user_text.c_str());
        const int reader_y = 100 + two_line_label(content, question.c_str(), kGutter, 100, kWidth, ui::font_body(),
                                                  ui::tokens::text_secondary) + 14;
        answer_body(reader_y, kActionY - 12 - reader_y);
    }

    // ---- Weather: instruments -------------------------------------------------

    lv_obj_t* graphite_panel(int x, int y, int width, int height) {
        auto* panel = lv_obj_create(weather_scroll);
        ui::reset(panel);
        ui::style_platter(panel, ui::tokens::blue, ui::tokens::radius_medium, false);
        lv_obj_set_style_pad_all(panel, 0, 0);
        lv_obj_set_size(panel, width, height);
        lv_obj_set_pos(panel, x, y);
        return panel;
    }

    // Weather: the city as the title (tap = choose a city); the condensed
    // temperature with the condition, "feels like" and the day's range as a
    // bar; the hours as bars; three gauges (a humidity ring, a compass, an air
    // quality bar or the chance of rain); then the selected hour or day,
    // hazards, the days and the source.
    void render_weather_ready_graphite(bool refreshing) {
        const auto& data = weather.data;
        make_header("", ui::IconRole::Refresh, weather_refresh_event, ui::tokens::ice,
                    static_cast<bool>(callbacks.on_weather_refresh));
        weather_city_title();
        make_weather_scroll();

        const int whole = whole_degrees(data.temperature_c);
        char degrees[16];
        std::snprintf(degrees, sizeof(degrees), "%d°", whole);
        const bool wide = whole <= -10 || whole >= 100;
        label(weather_scroll, degrees, kGutter - 4, -4, 128, wide ? ui::font_numerals_small() : ui::font_numerals_large(),
              ui::tokens::text);
        constexpr int kSideX = 148;
        constexpr int kSideWidth = ui::tokens::display_width - kSideX - kGutter;
        line(weather_scroll, weather_condition_text(data.condition), kSideX, 0, kSideWidth, ui::font_title(),
             ui::tokens::text);
        char feels[48];
        std::snprintf(feels, sizeof(feels), "%s %d°", v().feels_like, whole_degrees(data.apparent_temperature_c));
        line(weather_scroll, feels, kSideX, 44, kSideWidth, ui::font_body(), ui::tokens::text_secondary);
        // The day's range: cool to warm, with the temperature now on it.
        const int low = whole_degrees(data.daily_minimum_c);
        const int high = whole_degrees(data.daily_maximum_c);
        auto* bar = lv_obj_create(weather_scroll);
        ui::reset(bar);
        lv_obj_set_size(bar, kSideWidth, 6);
        lv_obj_set_pos(bar, kSideX, 88);
        lv_obj_set_style_radius(bar, 3, 0);
        lv_obj_set_style_bg_color(bar, ui::color(ui::tokens::ice), 0);
        lv_obj_set_style_bg_grad_color(bar, ui::color(ui::tokens::amber), 0);
        lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        const float share = high > low ? std::clamp(static_cast<float>(whole - low) / static_cast<float>(high - low),
                                                    0.0F, 1.0F) : 0.5F;
        auto* now = make_dot(weather_scroll, 18, ui::tokens::text);
        lv_obj_set_pos(now, kSideX + static_cast<int>(share * static_cast<float>(kSideWidth)) - 9, 82);
        lv_obj_set_style_border_width(now, 3, 0);
        lv_obj_set_style_border_color(now, ui::color(ui::tokens::canvas), 0);
        char edge[16];
        std::snprintf(edge, sizeof(edge), "%d°", low);
        line(weather_scroll, edge, kSideX, 100, 80, ui::font_caption(), ui::tokens::text_secondary);
        std::snprintf(edge, sizeof(edge), "%d°", high);
        line(weather_scroll, edge, kSideX + kSideWidth - 80, 100, 80, ui::font_caption(), ui::tokens::text_secondary,
             LV_TEXT_ALIGN_RIGHT);
        const bool demo = data.origin == WeatherDataOrigin::Demo;
        const bool cached = data.origin == WeatherDataOrigin::Cache || data.stale;
        if (demo || cached || refreshing) {
            auto* badge = ui::make_tag(weather_scroll,
                demo ? v().demo_badge : (refreshing ? v().weather_refreshing : v().weather_cached),
                demo ? ui::tokens::amber : (refreshing ? ui::tokens::ice : ui::tokens::magenta));
            lv_obj_set_pos(badge, kGutter, 94);
        }

        // The hours as bars: the temperature above, the hour below; a tap
        // describes the hour.
        constexpr int kHoursY = 132;
        constexpr int kHoursHeight = 120;
        auto* hours = graphite_panel(kGutter, kHoursY, kWidth, kHoursHeight);
        const std::size_t hourly_count = std::min<std::size_t>(data.hourly_count, weather_hour_cards.size());
        float coolest = 1000.0F;
        float warmest = -1000.0F;
        for (std::size_t index = 0; index < hourly_count; ++index) {
            coolest = std::min(coolest, data.hourly[index].temperature_c);
            warmest = std::max(warmest, data.hourly[index].temperature_c);
        }
        const int column = hourly_count > 0 ? kWidth / static_cast<int>(hourly_count) : kWidth;
        for (std::size_t index = 0; index < hourly_count; ++index) {
            const auto& hour = data.hourly[index];
            auto* cell = lv_button_create(hours);
            ui::reset(cell);
            lv_obj_set_clickable(cell, true);
            lv_obj_set_size(cell, column, kHoursHeight);
            lv_obj_set_pos(cell, static_cast<int>(index) * column, 0);
            lv_obj_set_style_radius(cell, ui::tokens::radius_small, 0);
            lv_obj_set_style_bg_color(cell, ui::color(ui::tokens::surface_high), LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_PRESSED);
            char value[16];
            std::snprintf(value, sizeof(value), "%d°", whole_degrees(hour.temperature_c));
            line(cell, value, 0, 4, column, ui::font_headline(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
            const float warmth = warmest > coolest ? (hour.temperature_c - coolest) / (warmest - coolest) : 0.5F;
            const int height = 14 + static_cast<int>(warmth * 30.0F);
            auto* column_bar = lv_obj_create(cell);
            ui::reset(column_bar);
            lv_obj_set_size(column_bar, 32, height);
            lv_obj_set_pos(column_bar, (column - 32) / 2, 88 - height);
            lv_obj_set_style_radius(column_bar, 8, 0);
            lv_obj_set_style_bg_color(column_bar, ui::color(ui::tokens::ice), 0);
            lv_obj_set_style_bg_opa(column_bar, static_cast<lv_opa_t>(110 + warmth * 145.0F), 0);
            const auto time = weather_time_label(hour.time);
            line(cell, time.c_str(), 0, 90, column, ui::font_caption(), ui::tokens::text_tertiary, LV_TEXT_ALIGN_CENTER);
            lv_obj_set_user_data(cell, reinterpret_cast<void*>(static_cast<std::uintptr_t>(index)));
            lv_obj_add_event_cb(cell, weather_hour_event, LV_EVENT_CLICKED, this);
            weather_hour_cards[index] = cell;
        }

        // Three gauges.
        constexpr int kGaugeY = kHoursY + kHoursHeight + 8;
        constexpr int kGaugeWidth = (kWidth - 24) / 3;
        constexpr int kGaugeHeight = 124;
        // Humidity: a ring.
        auto* humidity = graphite_panel(kGutter, kGaugeY, kGaugeWidth, kGaugeHeight);
        line(humidity, v().humidity, 8, 4, kGaugeWidth - 16, ui::font_caption(), ui::tokens::text_secondary,
             LV_TEXT_ALIGN_CENTER);
        auto* ring = lv_arc_create(humidity);
        lv_obj_set_size(ring, 48, 48);
        lv_obj_set_pos(ring, (kGaugeWidth - 48) / 2, 36);
        lv_obj_set_style_pad_all(ring, 0, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_arc_set_rotation(ring, 270);
        lv_arc_set_bg_angles(ring, 0, 360);
        lv_arc_set_range(ring, 0, 100);
        lv_arc_set_value(ring, data.humidity_percent);
        lv_obj_set_style_arc_color(ring, ui::color(ui::tokens::surface_high), LV_PART_MAIN);
        lv_obj_set_style_arc_width(ring, 7, LV_PART_MAIN);
        lv_obj_set_style_arc_color(ring, ui::color(ui::tokens::ice), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(ring, 7, LV_PART_INDICATOR);
        lv_obj_set_style_opa(ring, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_clickable(ring, false);
        char value[28];
        std::snprintf(value, sizeof(value), "%d%%", data.humidity_percent);
        line(humidity, value, 4, 88, kGaugeWidth - 8, ui::font_headline(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        // Wind: a compass of eight marks (the forecast gives no direction, so
        // there is no needle) with the speed below.
        auto* wind = graphite_panel(kGutter + kGaugeWidth + 12, kGaugeY, kGaugeWidth, kGaugeHeight);
        line(wind, v().wind, 8, 4, kGaugeWidth - 16, ui::font_caption(), ui::tokens::text_secondary,
             LV_TEXT_ALIGN_CENTER);
        const float compass_x = static_cast<float>(kGaugeWidth) / 2.0F;
        constexpr float kCompassY = 60.0F;
        for (std::size_t mark = 0; mark < compass_points.size(); ++mark) {
            const float angle = static_cast<float>(mark) * 6.2831853F / 8.0F;
            const bool cardinal = mark % 2 == 0;
            const float inner = cardinal ? 13.0F : 18.0F;
            compass_points[mark][0] = {static_cast<lv_value_precise_t>(compass_x + std::sin(angle) * inner),
                                       static_cast<lv_value_precise_t>(kCompassY - std::cos(angle) * inner)};
            compass_points[mark][1] = {static_cast<lv_value_precise_t>(compass_x + std::sin(angle) * 23.0F),
                                       static_cast<lv_value_precise_t>(kCompassY - std::cos(angle) * 23.0F)};
            auto* stroke = lv_line_create(wind);
            ui::reset(stroke);
            lv_line_set_points(stroke, compass_points[mark].data(), 2);
            lv_obj_set_style_line_width(stroke, cardinal ? 3 : 2, 0);
            lv_obj_set_style_line_rounded(stroke, true, 0);
            lv_obj_set_style_line_color(stroke, ui::color(mark == 0 ? ui::tokens::text : ui::tokens::text_tertiary), 0);
        }
        std::snprintf(value, sizeof(value), "%.0f km/h", static_cast<double>(data.wind_kmh));
        line(wind, value, 4, 88, kGaugeWidth - 8, ui::font_headline(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        // Air: five segments lit to the European AQI band, or to the chance of
        // rain when the forecast carries no air quality.
        const bool air = data.metrics.has(WeatherMetric::AirQuality);
        auto* third = graphite_panel(kGutter + 2 * (kGaugeWidth + 12), kGaugeY, kGaugeWidth, kGaugeHeight);
        line(third, air ? v().weather_air_quality : v().weather_rain, 8, 4, kGaugeWidth - 16, ui::font_caption(),
             ui::tokens::text_secondary, LV_TEXT_ALIGN_CENTER);
        const int level = air ? std::clamp((data.metrics.european_aqi + 19) / 20, 1, 5) :
                                std::clamp((data.rain_probability_percent + 19) / 20, 0, 5);
        constexpr std::array<std::uint32_t, 5> kAirColours{0x3DDC97U, 0xC9D65BU, 0xFFB547U, 0xFF7A59U, 0xFF5D7DU};
        constexpr int kSegment = 18;
        const int segments_x = (kGaugeWidth - (5 * kSegment + 4 * 4)) / 2;
        for (int segment = 0; segment < 5; ++segment) {
            auto* piece = lv_obj_create(third);
            ui::reset(piece);
            lv_obj_set_size(piece, kSegment, 8);
            lv_obj_set_pos(piece, segments_x + segment * (kSegment + 4), 56);
            lv_obj_set_style_radius(piece, 4, 0);
            const bool lit = segment < level;
            lv_obj_set_style_bg_color(piece, ui::color(lit ? (air ? kAirColours[static_cast<std::size_t>(segment)] :
                                                                     ui::tokens::ice) :
                                                              ui::tokens::surface_high), 0);
            lv_obj_set_style_bg_opa(piece, LV_OPA_COVER, 0);
        }
        if (air) std::snprintf(value, sizeof(value), "%d AQI", data.metrics.european_aqi);
        else std::snprintf(value, sizeof(value), "%d%%", data.rain_probability_percent);
        line(third, value, 4, 88, kGaugeWidth - 8, ui::font_headline(), ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        int y = kGaugeY + kGaugeHeight + 16;

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

        // The days, one platter each; a tap describes the day above.
        line(weather_scroll, v().weather_daily, kGutter, y, kWidth, ui::font_caption(), ui::tokens::text_tertiary);
        y += 36;
        const std::size_t daily_count = std::min<std::size_t>(data.daily_count, weather_day_cards.size());
        for (std::size_t index = 0; index < daily_count; ++index) {
            const auto& day_item = data.daily[index];
            auto* row = lv_button_create(weather_scroll);
            ui::reset(row);
            ui::style_platter(row, ui::tokens::blue, ui::tokens::radius_small, false);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_set_clickable(row, true);
            lv_obj_set_size(row, kWidth, 64);
            lv_obj_set_pos(row, kGutter, y);
            const auto day = weather_day_label(day_item.date, index);
            line(row, day.c_str(), 16, 15, 160, ui::font_body(), ui::tokens::text);
            auto* icon = icon_view(row, weather_condition_icon(day_item.condition), 30,
                                   weather_condition_accent(day_item.condition));
            lv_obj_set_pos(icon, 192, 17);
            char range_text[28];
            std::snprintf(range_text, sizeof(range_text), "%d° / %d°", whole_degrees(day_item.minimum_c),
                          whole_degrees(day_item.maximum_c));
            line(row, range_text, 236, 14, kWidth - 252, ui::font_headline(), ui::tokens::text, LV_TEXT_ALIGN_RIGHT);
            lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<std::uintptr_t>(index)));
            lv_obj_add_event_cb(row, weather_day_event, LV_EVENT_CLICKED, this);
            weather_day_cards[index] = row;
            y += 72;
        }
        y += 8;
        const std::string updated = weather_updated_text();
        line(weather_scroll, updated.c_str(), kGutter, y, kWidth, ui::font_caption(), ui::tokens::text_tertiary);
        list_end(weather_scroll, y + lv_font_get_line_height(ui::font_caption()), 24);
    }

    // ---- Timer: an instrument dial ------------------------------------------

    // Ticks outside an amber arc that shrinks with the time left; condensed
    // digits, the label and the end time inside; an amber squircle pause and a
    // graphite squircle stop below.
    void render_timer_graphite(const TimerItem& timer) {
        constexpr int kCenterY = 240;
        timer_digits_y = 212;
        make_timer_dial(timer, {kCenterY, 260, 300, 14, 14, ui::tokens::surface_high, ui::tokens::amber, 6, 14, 3,
                                ui::tokens::text_tertiary, ui::tokens::text});
        const auto text = countdown_text(timer.remaining_ms);
        const auto* font = timer_digits_font(text);
        clock_item_label = label(content, text.c_str(), 0, timer_digits_y - lv_font_get_line_height(font) / 2,
                                 ui::tokens::display_width, font,
                                 timer.paused ? ui::tokens::text_secondary : ui::tokens::text, LV_TEXT_ALIGN_CENTER);
        const std::string name = timer.label.empty() ? duration_words(timer.duration_s) : timer.label;
        line(content, name.c_str(), 140, kCenterY + 18, 200, ui::font_headline(), ui::tokens::text,
             LV_TEXT_ALIGN_CENTER);
        const auto ends = timer_end_text(timer);
        if (!ends.empty()) {
            line(content, ends.c_str(), 160, kCenterY + 56, 160, ui::font_caption(), ui::tokens::text_secondary,
                 LV_TEXT_ALIGN_CENTER);
        }
        auto* pause = round_button(content, 152, 384, 80, timer.paused ? ui::IconRole::Play : ui::IconRole::Pause,
                                   ui::tokens::amber, timer_pause_event);
        lv_obj_set_style_bg_color(pause, ui::color(ui::tokens::amber), 0);
        lv_obj_set_style_bg_opa(pause, LV_OPA_COVER, 0);
        lv_obj_set_style_image_recolor(lv_obj_get_child(pause, 0), ui::color(ui::tokens::on_primary), 0);
        round_button(content, 256, 388, 72, ui::IconRole::Stop, ui::tokens::text, clock_item_delete_event, true);
    }

    // ---- Today: a timeline ----------------------------------------------------

    // Today: the date and today's namedays as the subtitle; then a timeline:
    // the time on the left (from the models: the alarm, the next reminder,
    // «Τώρα» for the weather) and a tile on the right for each line.
    void render_briefing_graphite() {
        const auto briefing = current_briefing();
        if (capabilities.speech_output && callbacks.on_speak) {
            make_header(c().app_today, ui::IconRole::VolumeUp, briefing_speak_event, ui::tokens::mint);
        } else {
            make_header(c().app_today);
        }
        std::string subtitle = date_text();
        for (const auto& item : briefing.items) {
            if (item.kind != BriefingItemKind::Namedays || item.text.empty()) continue;
            if (!subtitle.empty()) subtitle += " · ";
            subtitle += std::string(c().briefing_namedays_title) + ": " + item.text;
        }
        line(content, subtitle.empty() ? c().app_today : subtitle.c_str(), kGutter + 4, 96, kWidth - 8,
             ui::font_caption(), ui::tokens::text_secondary);
        auto lines = briefing_lines(briefing);
        lines.erase(std::remove_if(lines.begin(), lines.end(),
                                   [this](const BriefingLine& item) { return item.title == c().briefing_namedays_title; }),
                    lines.end());
        if (lines.empty()) {
            state_symbol(144, 112, ui::IconRole::Sunrise, ui::tokens::magenta);
            state_text(280, c().briefing_empty, nullptr);
            return;
        }
        auto* list = make_list(136, ui::tokens::display_height - 136);
        constexpr int kRow = 76;
        constexpr int kTileX = 112;
        if (lines.size() > 1) {
            auto* rail = lv_obj_create(list);
            ui::reset(rail);
            lv_obj_set_size(rail, 2, static_cast<int>(lines.size() - 1) * kRow);
            lv_obj_set_pos(rail, 97, 36);
            lv_obj_set_style_bg_color(rail, ui::color(ui::tokens::separator), 0);
            lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
        }
        int y = 0;
        for (const auto& item : lines) {
            if (!item.time.empty()) {
                line(list, item.time.c_str(), 8, y + 18, 76, ui::font_headline(),
                     item.time == c().briefing_now ? ui::tokens::text : ui::tokens::text_secondary, LV_TEXT_ALIGN_RIGHT);
            }
            auto* dot = make_dot(list, 12, ui::tokens::canvas);
            lv_obj_set_pos(dot, 92, y + 30);
            lv_obj_set_style_border_width(dot, 2, 0);
            lv_obj_set_style_border_color(dot, ui::color(item.accent), 0);
            auto* tile = item.interactive ? lv_button_create(list) : lv_obj_create(list);
            ui::reset(tile);
            ui::style_platter(tile, item.accent, ui::tokens::radius_small, false);
            lv_obj_set_style_pad_all(tile, 0, 0);
            lv_obj_set_size(tile, ui::tokens::display_width - kGutter - kTileX, 68);
            lv_obj_set_pos(tile, kTileX, y + 2);
            auto* icon = icon_view(tile, item.role, 26, item.accent);
            lv_obj_set_pos(icon, 14, 21);
            const std::string& title = item.bare_title.empty() ? item.title : item.bare_title;
            constexpr int kTextWidth = 280;
            if (item.detail.empty()) {
                line(tile, title.c_str(), 50, 15, kTextWidth, ui::font_headline(), ui::tokens::text);
            } else {
                line(tile, title.c_str(), 50, 4, kTextWidth, ui::font_headline(), ui::tokens::text);
                line(tile, item.detail.c_str(), 50, 38, kTextWidth, ui::font_caption(), ui::tokens::text_secondary);
            }
            if (item.interactive) tappable(tile, briefing_line_event, static_cast<std::uintptr_t>(item.target));
            y += kRow;
        }
        list_end(list, y);
    }
