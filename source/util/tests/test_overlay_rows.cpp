#include "test_harness.h"

#include "overlay_rows.hpp"

#include <limits>
#include <string>

namespace {

namespace rows = onion::overlay::rows;
using onion::overlay::BarAlign;
using onion::overlay::BarEdge;

constexpr float kW = 1920.0f;
constexpr float kH = 1080.0f;

int as_int(float value) { return static_cast<int>(value); }

rows::Options base_options() {
  rows::Options o{};
  o.screen_w = kW;
  o.screen_h = kH;
  o.edge = BarEdge::Top;
  o.align = BarAlign::Left;
  o.font_pt = 18;
  return o;
}

rows::Readings live_readings() {
  rows::Readings r{};
  r.fps_valid = true;
  r.fps = 60.0f;
  r.fps_avg = 59.4f;
  r.fps_low = 48.0f;
  r.cpu_valid = true;
  r.cpu_avg = 32.0f;
  r.cpu_core = {10, 20, 30, 40, 50, 60, 70, 80};
  r.cpu_temp_valid = true;
  r.cpu_temp = 61;
  r.gpu_valid = true;
  r.vram_used_mb = 4120;
  r.vram_total_mb = 8240;
  r.soc_temp_valid = true;
  r.soc_temp = 58;
  r.ram_valid = true;
  r.ram_used_mb = 9200;
  r.ram_total_mb = 16000;
  r.ip = "192.168.1.20";
  r.fan_valid = true;
  r.fan_pct = 45.0f;
  r.title_id = "PPSA01234";
  r.session_s = 754;
  return r;
}

const rows::Text *find_text(const rows::Scene &s, const std::string &id) {
  for (const rows::Text &t : s.texts)
    if (t.id == id)
      return &t;
  return nullptr;
}

const rows::Rect *find_rect(const rows::Scene &s, const std::string &id) {
  for (const rows::Rect &r : s.rects)
    if (r.id == id)
      return &r;
  return nullptr;
}

int count_rect_prefix(const rows::Scene &s, const std::string &prefix) {
  int n = 0;
  for (const rows::Rect &r : s.rects)
    if (r.id.compare(0, prefix.size(), prefix) == 0)
      ++n;
  return n;
}

bool ids_unique(const rows::Scene &s) {
  for (size_t i = 0; i < s.rects.size(); ++i)
    for (size_t j = i + 1; j < s.rects.size(); ++j)
      if (s.rects[i].id == s.rects[j].id)
        return false;
  for (size_t i = 0; i < s.texts.size(); ++i)
    for (size_t j = i + 1; j < s.texts.size(); ++j)
      if (s.texts[i].id == s.texts[j].id)
        return false;
  return true;
}

int test_empty_when_nothing_enabled(void) {
  rows::Options o = base_options();
  o.show_fps = o.show_cpu = o.show_gpu = o.show_ram = false;
  rows::Scene s = rows::build_scene(o, live_readings(), {});
  TEST_ASSERT_TRUE(s.rects.empty());
  TEST_ASSERT_TRUE(s.texts.empty());

  rows::Options unknown = base_options();
  unknown.screen_w = 0.0f;
  TEST_ASSERT_TRUE(rows::build_scene(unknown, live_readings(), {}).texts.empty());
  return 0;
}

int test_corner_placement(void) {
  /* default margin 0: flush against the screen corner */
  rows::Options o = base_options();
  rows::Scene tl = rows::build_scene(o, live_readings(), {});
  TEST_ASSERT_EQ_INT(0, as_int(tl.x));
  TEST_ASSERT_EQ_INT(0, as_int(tl.y));
  TEST_ASSERT_EQ_INT(0, as_int(find_rect(tl, "id_orow_bg")->x));

  o.edge = BarEdge::Bottom;
  o.align = BarAlign::Right;
  rows::Scene br = rows::build_scene(o, live_readings(), {});
  TEST_ASSERT_EQ_INT(as_int(kW), as_int(br.x + br.w));
  TEST_ASSERT_EQ_INT(as_int(kH), as_int(br.y + br.h));

  o.align = BarAlign::Center;
  rows::Scene bc = rows::build_scene(o, live_readings(), {});
  TEST_ASSERT_EQ_INT(as_int((kW - bc.w) / 2.0f), as_int(bc.x));
  return 0;
}

int test_edge_margin(void) {
  for (int px : {5, 10, 20}) {
    rows::Options o = base_options();
    o.margin = static_cast<float>(px);
    rows::Scene tl = rows::build_scene(o, live_readings(), {});
    TEST_ASSERT_EQ_INT(px, as_int(tl.x));
    TEST_ASSERT_EQ_INT(px, as_int(tl.y));

    o.edge = BarEdge::Bottom;
    o.align = BarAlign::Right;
    rows::Scene br = rows::build_scene(o, live_readings(), {});
    TEST_ASSERT_EQ_INT(as_int(kW) - px, as_int(br.x + br.w));
    TEST_ASSERT_EQ_INT(as_int(kH) - px, as_int(br.y + br.h));

    /* centred stays centred whatever the margin */
    o.align = BarAlign::Center;
    rows::Scene bc = rows::build_scene(o, live_readings(), {});
    TEST_ASSERT_EQ_INT(as_int((kW - bc.w) / 2.0f), as_int(bc.x));
  }
  return 0;
}

int test_chrome_follows_background(void) {
  rows::Options o = base_options();
  rows::Scene on = rows::build_scene(o, live_readings(), {});
  const rows::Rect *bg = find_rect(on, "id_orow_bg");
  TEST_ASSERT_TRUE(bg != nullptr);
  /* surface is the back-most widget and covers the panel exactly */
  TEST_ASSERT_TRUE(on.rects.front().id == "id_orow_bg");
  TEST_ASSERT_EQ_INT(as_int(on.x), as_int(bg->x));
  TEST_ASSERT_EQ_INT(as_int(on.h), as_int(bg->h));
  TEST_ASSERT_TRUE(find_rect(on, "id_orow_rail") != nullptr);

  o.background = false;
  rows::Scene off = rows::build_scene(o, live_readings(), {});
  TEST_ASSERT_TRUE(find_rect(off, "id_orow_bg") == nullptr);
  TEST_ASSERT_TRUE(find_rect(off, "id_orow_rail") == nullptr);
  TEST_ASSERT_TRUE(find_text(off, "id_orow_cpu_v") != nullptr);
  return 0;
}

int test_values_and_labels(void) {
  rows::Options o = base_options();
  o.show_ip = o.show_fan = true;
  rows::Scene s = rows::build_scene(o, live_readings(), {});
  TEST_ASSERT_TRUE(ids_unique(s));
  TEST_ASSERT_STREQ("60", find_text(s, "id_orow_fps_v")->text.c_str());
  TEST_ASSERT_STREQ("59", find_text(s, "id_orow_fps_avg_v")->text.c_str());
  TEST_ASSERT_STREQ("48", find_text(s, "id_orow_fps_low_v")->text.c_str());
  TEST_ASSERT_STREQ("16.7 ms", find_text(s, "id_orow_fps_ft_v")->text.c_str());
  TEST_ASSERT_STREQ("32 %", find_text(s, "id_orow_cpu_v")->text.c_str());
  TEST_ASSERT_STREQ("61 \xC2\xB0" "C",
                    find_text(s, "id_orow_cpu_temp_v")->text.c_str());
  TEST_ASSERT_STREQ("4120 MB", find_text(s, "id_orow_gpu_v")->text.c_str());
  TEST_ASSERT_STREQ("9200 MB", find_text(s, "id_orow_ram_v")->text.c_str());
  TEST_ASSERT_STREQ("192.168.1.20", find_text(s, "id_orow_ip_v")->text.c_str());
  TEST_ASSERT_STREQ("45 %", find_text(s, "id_orow_fan_v")->text.c_str());
  TEST_ASSERT_STREQ("PPSA01234  12:34",
                    find_text(s, "id_orow_session")->text.c_str());
  TEST_ASSERT_STREQ(rows::kBrandText, find_text(s, "id_orow_brand")->text.c_str());
  TEST_ASSERT_TRUE(find_text(s, "id_orow_cpu_c0_v") == nullptr);
  return 0;
}

int test_no_game_collapses_fps_section(void) {
  rows::Readings r = live_readings();
  r.fps_valid = false;
  r.title_id.clear();
  rows::Scene s = rows::build_scene(base_options(), r, {});
  TEST_ASSERT_STREQ("NO GAME DETECTED", find_text(s, "id_orow_fps_v")->text.c_str());
  TEST_ASSERT_TRUE(find_text(s, "id_orow_fps_avg_v") == nullptr);
  TEST_ASSERT_STREQ("NO GAME", find_text(s, "id_orow_session")->text.c_str());

  rows::Scene live = rows::build_scene(base_options(), live_readings(), {});
  TEST_ASSERT_TRUE(s.h < live.h);

  rows::Options no_fps = base_options();
  no_fps.show_fps = false;
  rows::Scene quiet = rows::build_scene(no_fps, r, {});
  TEST_ASSERT_TRUE(find_text(quiet, "id_orow_session") == nullptr);
  TEST_ASSERT_TRUE(find_text(quiet, "id_orow_brand") != nullptr);
  return 0;
}

int test_header_session_text(void) {
  rows::Readings r = live_readings();
  r.title_id.clear(); /* daemon had no title id for this process */
  rows::Scene untitled = rows::build_scene(base_options(), r, {});
  TEST_ASSERT_STREQ("12:34", find_text(untitled, "id_orow_session")->text.c_str());

  r = live_readings();
  r.session_s = 3723; /* past an hour: clock only, so it clears the brand */
  rows::Scene long_run = rows::build_scene(base_options(), r, {});
  TEST_ASSERT_STREQ("1:02:03", find_text(long_run, "id_orow_session")->text.c_str());
  return 0;
}

int test_non_finite_values_are_dropped(void) {
  rows::History h;
  h.push(std::numeric_limits<float>::infinity());
  h.push(std::numeric_limits<float>::quiet_NaN());
  TEST_ASSERT_EQ_INT(0, h.size());

  rows::Readings r = live_readings();
  r.fan_pct = std::numeric_limits<float>::quiet_NaN();
  rows::Options o = base_options();
  o.show_fan = true;
  rows::Scene s = rows::build_scene(o, r, {});
  TEST_ASSERT_TRUE(find_rect(s, "id_orow_fan_track") != nullptr);
  TEST_ASSERT_TRUE(find_rect(s, "id_orow_fan_fill") == nullptr);
  return 0;
}

int test_missing_readings_show_dash(void) {
  rows::Readings r{};
  rows::Scene s = rows::build_scene(base_options(), r, {});
  TEST_ASSERT_STREQ("\xE2\x80\x94", find_text(s, "id_orow_cpu_v")->text.c_str());
  TEST_ASSERT_STREQ("\xE2\x80\x94", find_text(s, "id_orow_gpu_temp_v")->text.c_str());
  /* empty bar, but the track is still drawn */
  TEST_ASSERT_TRUE(find_rect(s, "id_orow_cpu_track") != nullptr);
  TEST_ASSERT_TRUE(find_rect(s, "id_orow_cpu_fill") == nullptr);
  return 0;
}

int test_bars_scale_with_usage(void) {
  rows::Scene s = rows::build_scene(base_options(), live_readings(), {});
  const rows::Rect *track = find_rect(s, "id_orow_gpu_track");
  const rows::Rect *fill = find_rect(s, "id_orow_gpu_fill");
  TEST_ASSERT_TRUE(track && fill);
  TEST_ASSERT_EQ_INT(as_int(track->w / 2.0f), as_int(fill->w)); /* 4120/8240 */
  TEST_ASSERT_EQ_INT(as_int(track->x), as_int(fill->x));
  return 0;
}

int test_temperature_colors(void) {
  using rows::kCpuTempScale;
  TEST_ASSERT_TRUE(rows::temp_color(30, kCpuTempScale).b ==
                   rows::kTempCool.b);
  TEST_ASSERT_TRUE(rows::temp_color(50, kCpuTempScale).g ==
                   rows::kTempWarm.g);
  TEST_ASSERT_TRUE(rows::temp_color(70, kCpuTempScale).r ==
                   rows::kTempHot.r);
  TEST_ASSERT_TRUE(rows::temp_color(80, kCpuTempScale).b ==
                   rows::kTempCrit.b);

  rows::Readings r = live_readings();
  r.cpu_temp = 82;
  rows::Scene s = rows::build_scene(base_options(), r, {});
  TEST_ASSERT_TRUE(find_text(s, "id_orow_cpu_temp_v")->color.b ==
                   rows::kTempCrit.b);
  return 0;
}

int test_section_order_and_dividers(void) {
  rows::Options o = base_options();
  o.order = {onion::overlay::kMetricMemory, onion::overlay::kMetricFps,
             onion::overlay::kMetricCpu, onion::overlay::kMetricGpu,
             onion::overlay::kMetricIp, onion::overlay::kMetricFan};
  rows::Scene s = rows::build_scene(o, live_readings(), {});
  TEST_ASSERT_TRUE(find_text(s, "id_orow_ram_l")->y <
                   find_text(s, "id_orow_fps_l")->y);
  TEST_ASSERT_TRUE(find_text(s, "id_orow_fps_l")->y <
                   find_text(s, "id_orow_cpu_l")->y);
  /* 4 visible sections → 3 rules between them */
  TEST_ASSERT_EQ_INT(3, count_rect_prefix(s, "id_orow_div"));
  return 0;
}

int test_per_core_rows(void) {
  rows::Options o = base_options();
  o.per_core_cpu = true;
  rows::Scene s = rows::build_scene(o, live_readings(), {});
  TEST_ASSERT_STREQ("10  20  30  40", find_text(s, "id_orow_cpu_c0_v")->text.c_str());
  TEST_ASSERT_STREQ("50  60  70  80", find_text(s, "id_orow_cpu_c1_v")->text.c_str());

  o.show_cpu = false; /* per-core alone still shows the CPU section */
  rows::Scene only = rows::build_scene(o, live_readings(), {});
  TEST_ASSERT_TRUE(find_text(only, "id_orow_cpu_v") != nullptr);
  return 0;
}

int test_sparks_are_right_anchored_and_capped(void) {
  rows::Histories h{};
  for (int i = 0; i < 5; ++i)
    h.cpu.push(static_cast<float>(i * 20));
  rows::Scene s = rows::build_scene(base_options(), live_readings(), h);
  /* body + cap per point */
  TEST_ASSERT_EQ_INT(10, count_rect_prefix(s, "id_orow_cpu_sp"));
  const rows::Rect *newest = find_rect(s, "id_orow_cpu_sp4b");
  const rows::Rect *oldest = find_rect(s, "id_orow_cpu_sp0b");
  TEST_ASSERT_TRUE(newest && oldest);
  TEST_ASSERT_TRUE(newest->x > oldest->x);
  TEST_ASSERT_TRUE(newest->h > oldest->h); /* 80% vs 0% on a 0-100 domain */
  /* spark stays left of the value column */
  const rows::Text *value = find_text(s, "id_orow_cpu_v");
  const rows::Metrics m(18);
  TEST_ASSERT_TRUE(newest->x + newest->w <=
                   value->x + value->w - m.value_reserve);

  for (int i = 0; i < 40; ++i)
    h.cpu.push(50.0f);
  rows::Scene full = rows::build_scene(base_options(), live_readings(), h);
  TEST_ASSERT_EQ_INT(2 * rows::kSparkPoints,
                     count_rect_prefix(full, "id_orow_cpu_sp"));

  /* a single sample is not a trend */
  rows::Histories one{};
  one.ram.push(40.0f);
  rows::Scene none = rows::build_scene(base_options(), live_readings(), one);
  TEST_ASSERT_EQ_INT(0, count_rect_prefix(none, "id_orow_ram_sp"));
  return 0;
}

int test_frame_time_graph(void) {
  rows::Histories h{};
  for (int i = 0; i < 40; ++i)
    h.fps.push(i % 2 ? 60.0f : 30.0f);
  rows::Scene s = rows::build_scene(base_options(), live_readings(), h);
  TEST_ASSERT_EQ_INT(2 * rows::kGraphPoints,
                     count_rect_prefix(s, "id_orow_fps_ftg"));
  /* 30 fps = 33 ms frame → taller column than 60 fps = 16 ms */
  const rows::Rect *a = find_rect(s, "id_orow_fps_ftg0b"); /* oldest kept */
  const rows::Rect *b = find_rect(s, "id_orow_fps_ftg1b");
  TEST_ASSERT_TRUE(a && b);
  TEST_ASSERT_TRUE(a->h != b->h);
  return 0;
}

int test_large_font_scales_panel(void) {
  rows::Options o = base_options();
  rows::Scene medium = rows::build_scene(o, live_readings(), {});
  o.font_pt = 24;
  rows::Scene large = rows::build_scene(o, live_readings(), {});
  TEST_ASSERT_TRUE(large.w > medium.w);
  TEST_ASSERT_TRUE(large.h > medium.h);
  TEST_ASSERT_EQ_INT(24, large.value_pt);
  TEST_ASSERT_TRUE(large.label_pt < large.value_pt);
  return 0;
}

int test_history_ring(void) {
  rows::History h;
  TEST_ASSERT_EQ_INT(0, h.size());
  for (int i = 0; i < rows::kHistoryCap + 5; ++i)
    h.push(static_cast<float>(i));
  TEST_ASSERT_EQ_INT(rows::kHistoryCap, h.size());
  TEST_ASSERT_EQ_INT(5, as_int(h.at(0)));
  TEST_ASSERT_EQ_INT(rows::kHistoryCap + 4, as_int(h.at(rows::kHistoryCap - 1)));
  h.clear();
  TEST_ASSERT_EQ_INT(0, h.size());
  return 0;
}

int test_fps_stats(void) {
  rows::FpsStats st;
  TEST_ASSERT_EQ_INT(0, as_int(st.average()));
  for (int i = 0; i < 99; ++i)
    st.push(60.0f);
  st.push(20.0f);
  st.push(0.0f); /* ignored */
  TEST_ASSERT_EQ_INT(100, st.size());
  TEST_ASSERT_EQ_INT(20, as_int(st.low_1pct()));
  TEST_ASSERT_EQ_INT(59, as_int(st.average())); /* 59.6 */
  st.clear();
  TEST_ASSERT_EQ_INT(0, st.size());
  return 0;
}

} // namespace

extern "C" int test_overlay_rows_suite(void) {
  int failures = 0;
  failures += onion_test_run("overlay_rows.empty", test_empty_when_nothing_enabled);
  failures += onion_test_run("overlay_rows.corners", test_corner_placement);
  failures += onion_test_run("overlay_rows.margin", test_edge_margin);
  failures += onion_test_run("overlay_rows.background", test_chrome_follows_background);
  failures += onion_test_run("overlay_rows.values", test_values_and_labels);
  failures += onion_test_run("overlay_rows.no_game", test_no_game_collapses_fps_section);
  failures += onion_test_run("overlay_rows.header_session", test_header_session_text);
  failures += onion_test_run("overlay_rows.non_finite", test_non_finite_values_are_dropped);
  failures += onion_test_run("overlay_rows.missing", test_missing_readings_show_dash);
  failures += onion_test_run("overlay_rows.bars", test_bars_scale_with_usage);
  failures += onion_test_run("overlay_rows.temp_colors", test_temperature_colors);
  failures += onion_test_run("overlay_rows.order", test_section_order_and_dividers);
  failures += onion_test_run("overlay_rows.per_core", test_per_core_rows);
  failures += onion_test_run("overlay_rows.sparks", test_sparks_are_right_anchored_and_capped);
  failures += onion_test_run("overlay_rows.frame_graph", test_frame_time_graph);
  failures += onion_test_run("overlay_rows.large_font", test_large_font_scales_panel);
  failures += onion_test_run("overlay_rows.history", test_history_ring);
  failures += onion_test_run("overlay_rows.fps_stats", test_fps_stats);
  return failures;
}
