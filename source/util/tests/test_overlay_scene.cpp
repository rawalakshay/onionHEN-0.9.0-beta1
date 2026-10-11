#include "test_harness.h"

#include "overlay_scene.hpp"

#include <cmath>
#include <limits>
#include <string>

namespace {

namespace sc = onion::overlay::scene;
using onion::overlay::BarAlign;
using onion::overlay::BarEdge;

constexpr float kW = 1920.0f;
constexpr float kH = 1080.0f;
const char *const kDash = "\xE2\x80\x94";

int as_int(float value) { return static_cast<int>(std::lround(value)); }

sc::Options base_options(sc::Detail detail = sc::Detail::Panel) {
  sc::Options o{};
  o.screen_w = kW;
  o.screen_h = kH;
  o.edge = BarEdge::Top;
  o.align = BarAlign::Left;
  o.detail = detail;
  o.font_pt = 18;
  o.margin = 0.0f;
  return o;
}

sc::Readings live_readings() {
  sc::Readings r{};
  r.fps_valid = true;
  r.fps = 60.0f;
  r.fps_avg = 59.4f;
  r.fps_low = 48.0f;
  r.fps_target = 60;
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

sc::Histories flat_history(float fps = 60.0f) {
  sc::Histories h{};
  for (int i = 0; i < sc::kGraphPoints; ++i)
    h.frame_ms.push(1000.0f / fps);
  return h;
}

const sc::Text *find_text(const sc::Scene &s, const std::string &id) {
  for (const sc::Text &t : s.texts)
    if (t.id == id)
      return &t;
  return nullptr;
}

const sc::Rect *find_rect(const sc::Scene &s, const std::string &id) {
  for (const sc::Rect &r : s.rects)
    if (r.id == id)
      return &r;
  return nullptr;
}

int count_rect_prefix(const sc::Scene &s, const std::string &prefix) {
  int n = 0;
  for (const sc::Rect &r : s.rects)
    if (r.id.compare(0, prefix.size(), prefix) == 0)
      ++n;
  return n;
}

bool ids_unique(const sc::Scene &s) {
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

bool same_color(const sc::Rgba &a, const sc::Rgba &b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/* Widgets the renderer creates: one Panel per rect, cell + label per text. */
int widget_count(const sc::Scene &s) {
  return static_cast<int>(s.rects.size() + 2 * s.texts.size());
}

const char *text_of(const sc::Scene &s, const std::string &id) {
  const sc::Text *t = find_text(s, id);
  return t ? t->text.c_str() : nullptr;
}

int test_empty_when_nothing_enabled(void) {
  for (sc::Detail d : {sc::Detail::Compact, sc::Detail::Panel}) {
    sc::Options o = base_options(d);
    o.show_fps = o.show_cpu = o.show_gpu = o.show_ram = false;
    o.show_ip = o.show_fan = o.per_core_cpu = false;
    sc::Scene s = sc::build_scene(o, live_readings(), {});
    TEST_ASSERT_TRUE(s.rects.empty() && s.texts.empty());
  }
  sc::Options o = base_options();
  o.screen_w = 0.0f;
  sc::Scene s = sc::build_scene(o, live_readings(), {});
  TEST_ASSERT_TRUE(s.rects.empty() && s.texts.empty());
  return 0;
}

int test_corner_placement_and_margin(void) {
  for (float margin : {0.0f, 5.0f, 10.0f, 20.0f}) {
    sc::Options o = base_options();
    o.margin = margin;
    sc::Scene tl = sc::build_scene(o, live_readings(), flat_history());
    TEST_ASSERT_EQ_INT(as_int(margin), as_int(tl.x));
    TEST_ASSERT_EQ_INT(as_int(margin), as_int(tl.y));

    o.edge = BarEdge::Bottom;
    o.align = BarAlign::Right;
    sc::Scene br = sc::build_scene(o, live_readings(), flat_history());
    TEST_ASSERT_EQ_INT(as_int(kW - margin), as_int(br.x + br.w));
    TEST_ASSERT_EQ_INT(as_int(kH - margin), as_int(br.y + br.h));

    o.align = BarAlign::Center;
    sc::Scene c = sc::build_scene(o, live_readings(), flat_history());
    TEST_ASSERT_EQ_INT(as_int((kW - c.w) * 0.5f), as_int(c.x));
  }
  return 0;
}

int test_opacity(void) {
  sc::Options o = base_options();
  o.opacity = 0;
  sc::Scene none = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_TRUE(find_rect(none, "id_osc_bg") == nullptr);
  TEST_ASSERT_TRUE(find_rect(none, "id_osc_edge_t") == nullptr);
  TEST_ASSERT_TRUE(find_text(none, "id_osc_cpu_v") != nullptr);

  for (int pct : {40, 65, 85}) {
    o.opacity = pct;
    sc::Scene s = sc::build_scene(o, live_readings(), flat_history());
    TEST_ASSERT_TRUE(!s.rects.empty());
    const sc::Rect &bg = s.rects.front(); /* drawn first, under everything */
    TEST_ASSERT_STREQ("id_osc_bg", bg.id.c_str());
    TEST_ASSERT_EQ_INT(pct, as_int(bg.color.a * 100.0f));
    TEST_ASSERT_EQ_INT(as_int(s.w), as_int(bg.w));
    TEST_ASSERT_EQ_INT(as_int(s.h), as_int(bg.h));
    TEST_ASSERT_TRUE(find_rect(s, "id_osc_edge_r") != nullptr);
  }
  /* The old accent rail and brand square are gone. */
  sc::Scene s = sc::build_scene(base_options(), live_readings(), flat_history());
  TEST_ASSERT_EQ_INT(0, count_rect_prefix(s, "id_osc_rail"));
  TEST_ASSERT_EQ_INT(0, count_rect_prefix(s, "id_osc_mark"));
  return 0;
}

int test_panel_values(void) {
  sc::Options o = base_options();
  o.show_ip = true;
  o.show_fan = true;
  sc::Scene s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_TRUE(ids_unique(s));
  TEST_ASSERT_STREQ("PPSA01234", text_of(s, "id_osc_head_l"));
  TEST_ASSERT_STREQ("12:34", text_of(s, "id_osc_head_r"));
  TEST_ASSERT_STREQ("60", text_of(s, "id_osc_fps_v"));
  TEST_ASSERT_STREQ("FPS", text_of(s, "id_osc_fps_l"));
  TEST_ASSERT_STREQ("59", text_of(s, "id_osc_avg_v"));
  TEST_ASSERT_STREQ("48", text_of(s, "id_osc_low_v"));
  TEST_ASSERT_STREQ("LOW", text_of(s, "id_osc_low_l"));
  TEST_ASSERT_STREQ("16.7", text_of(s, "id_osc_ft_v"));
  TEST_ASSERT_STREQ("ms", text_of(s, "id_osc_ft_u"));
  TEST_ASSERT_STREQ("32%", text_of(s, "id_osc_cpu_v"));
  TEST_ASSERT_STREQ("61\xC2\xB0", text_of(s, "id_osc_cpu_r"));
  TEST_ASSERT_STREQ("4.0", text_of(s, "id_osc_gpu_v"));
  TEST_ASSERT_STREQ("/ 8 GB", text_of(s, "id_osc_gpu_u"));
  TEST_ASSERT_STREQ("58\xC2\xB0", text_of(s, "id_osc_gpu_r"));
  TEST_ASSERT_STREQ("9.0", text_of(s, "id_osc_ram_v"));
  TEST_ASSERT_STREQ("/ 15.6 GB", text_of(s, "id_osc_ram_u"));
  TEST_ASSERT_STREQ("192.168.1.20", text_of(s, "id_osc_ip_v"));
  TEST_ASSERT_STREQ("45%", text_of(s, "id_osc_fan_v"));
  /* The hero runs in its own large font; labels are smaller than values. */
  const sc::Text *hero = find_text(s, "id_osc_fps_v");
  const sc::Text *cpu_l = find_text(s, "id_osc_cpu_l");
  const sc::Text *cpu_v = find_text(s, "id_osc_cpu_v");
  TEST_ASSERT_TRUE(hero && cpu_l && cpu_v);
  TEST_ASSERT_TRUE(hero->pt > 2 * cpu_v->pt);
  TEST_ASSERT_TRUE(cpu_l->pt < cpu_v->pt);
  TEST_ASSERT_TRUE(same_color(sc::kLabel, cpu_l->color));
  TEST_ASSERT_TRUE(same_color(sc::kText, cpu_v->color));
  return 0;
}

int test_brand_header(void) {
  sc::Options o = base_options();
  sc::Scene plain = sc::build_scene(o, live_readings(), flat_history());
  for (const sc::Text &t : plain.texts)
    TEST_ASSERT_TRUE(t.text != sc::kBrandText); /* off by default */

  o.brand = true;
  sc::Scene s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_STREQ(sc::kBrandText, text_of(s, "id_osc_head_l"));
  TEST_ASSERT_STREQ("PPSA01234  12:34", text_of(s, "id_osc_head_r"));

  sc::Readings r = live_readings();
  r.session_s = 3723; /* 1:02:03 — title dropped so the clock fits */
  s = sc::build_scene(o, r, flat_history());
  TEST_ASSERT_STREQ("1:02:03", text_of(s, "id_osc_head_r"));
  return 0;
}

int test_no_game(void) {
  sc::Scene live = sc::build_scene(base_options(), live_readings(), flat_history());
  sc::Readings r = live_readings();
  r.fps_valid = false;
  sc::Scene s = sc::build_scene(base_options(), r, flat_history());
  TEST_ASSERT_STREQ("NO GAME DETECTED", text_of(s, "id_osc_fps_v"));
  TEST_ASSERT_TRUE(find_text(s, "id_osc_avg_v") == nullptr);
  TEST_ASSERT_EQ_INT(0, count_rect_prefix(s, "id_osc_col"));
  TEST_ASSERT_STREQ("NO GAME", text_of(s, "id_osc_head_l"));
  TEST_ASSERT_TRUE(s.h < live.h);

  /* FPS off and no brand: no header at all. */
  sc::Options o = base_options();
  o.show_fps = false;
  s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_TRUE(find_text(s, "id_osc_head_l") == nullptr);
  TEST_ASSERT_TRUE(find_text(s, "id_osc_head_r") == nullptr);
  TEST_ASSERT_TRUE(find_text(s, "id_osc_cpu_v") != nullptr);
  return 0;
}

int test_non_finite_and_missing(void) {
  sc::Readings r = live_readings();
  r.cpu_avg = std::numeric_limits<float>::quiet_NaN();
  r.fps = std::numeric_limits<float>::infinity();
  r.cpu_core[3] = std::numeric_limits<float>::quiet_NaN();
  sc::Options o = base_options(sc::Detail::Full);
  sc::Scene s = sc::build_scene(o, r, flat_history());
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_cpu_v"));
  TEST_ASSERT_STREQ("NO GAME DETECTED", text_of(s, "id_osc_fps_v"));
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_cpu_track") != nullptr);
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_cpu_fill") == nullptr);

  r = live_readings();
  r.gpu_valid = false;
  r.soc_temp_valid = false;
  r.ram_valid = false;
  r.ip.clear();
  r.fan_valid = false;
  o.show_ip = o.show_fan = true;
  s = sc::build_scene(o, r, flat_history());
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_gpu_v"));
  TEST_ASSERT_TRUE(find_text(s, "id_osc_gpu_u") == nullptr);
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_gpu_r"));
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_ram_v"));
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_ip_v"));
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_fan_v"));
  return 0;
}

int test_bars(void) {
  sc::Readings r = live_readings();
  sc::Scene s = sc::build_scene(base_options(), r, flat_history());
  const sc::Rect *track = find_rect(s, "id_osc_gpu_track");
  const sc::Rect *fill = find_rect(s, "id_osc_gpu_fill");
  TEST_ASSERT_TRUE(track && fill);
  TEST_ASSERT_EQ_INT(as_int(track->w * 0.5f), as_int(fill->w)); /* 4120/8240 */
  TEST_ASSERT_EQ_INT(3, as_int(track->h));
  TEST_ASSERT_TRUE(same_color(sc::kFill, fill->color));

  r.ram_used_mb = 15000; /* 94% → nearly full */
  s = sc::build_scene(base_options(), r, flat_history());
  fill = find_rect(s, "id_osc_ram_fill");
  TEST_ASSERT_TRUE(fill && same_color(sc::kWarn, fill->color));
  return 0;
}

int test_temperature_colors(void) {
  TEST_ASSERT_TRUE(same_color(sc::kText, sc::temp_color(50, sc::kCpuTempScale)));
  TEST_ASSERT_TRUE(same_color(sc::kWarn, sc::temp_color(65, sc::kCpuTempScale)));
  TEST_ASSERT_TRUE(same_color(sc::kBad, sc::temp_color(80, sc::kCpuTempScale)));
  TEST_ASSERT_TRUE(same_color(sc::kWarn, sc::temp_color(80, sc::kGpuTempScale)));
  TEST_ASSERT_TRUE(same_color(sc::kBad, sc::temp_color(85, sc::kGpuTempScale)));

  sc::Readings r = live_readings();
  r.soc_temp = 70;
  sc::Scene s = sc::build_scene(base_options(), r, flat_history());
  TEST_ASSERT_TRUE(same_color(sc::kWarn, find_text(s, "id_osc_gpu_r")->color));
  TEST_ASSERT_TRUE(same_color(sc::kText, find_text(s, "id_osc_cpu_r")->color));
  return 0;
}

int test_fps_state_colors(void) {
  TEST_ASSERT_TRUE(sc::fps_state(59.0f, 60) == sc::FpsState::Good);
  TEST_ASSERT_TRUE(sc::fps_state(50.0f, 60) == sc::FpsState::Warn);
  TEST_ASSERT_TRUE(sc::fps_state(40.0f, 60) == sc::FpsState::Bad);
  TEST_ASSERT_TRUE(sc::fps_state(29.0f, 30) == sc::FpsState::Good);

  sc::Readings r = live_readings();
  sc::Scene s = sc::build_scene(base_options(), r, flat_history());
  TEST_ASSERT_TRUE(same_color(sc::kText, find_text(s, "id_osc_fps_v")->color));
  /* 48 against a 60 cap is a warning; values only colour when off-target */
  TEST_ASSERT_TRUE(same_color(sc::kWarn, find_text(s, "id_osc_low_v")->color));
  r.fps = 35.0f;
  s = sc::build_scene(base_options(), r, flat_history());
  TEST_ASSERT_TRUE(same_color(sc::kBad, find_text(s, "id_osc_fps_v")->color));
  return 0;
}

int test_order_and_dividers(void) {
  sc::Options o = base_options();
  o.order = {onion::overlay::kMetricMemory, onion::overlay::kMetricFps,
             onion::overlay::kMetricCpu, onion::overlay::kMetricGpu,
             onion::overlay::kMetricIp, onion::overlay::kMetricFan};
  sc::Scene s = sc::build_scene(o, live_readings(), flat_history());
  const sc::Text *ram = find_text(s, "id_osc_ram_l");
  const sc::Text *fps = find_text(s, "id_osc_fps_v");
  const sc::Text *cpu = find_text(s, "id_osc_cpu_l");
  const sc::Text *gpu = find_text(s, "id_osc_gpu_l");
  TEST_ASSERT_TRUE(ram && fps && cpu && gpu);
  TEST_ASSERT_TRUE(ram->y < fps->y && fps->y < cpu->y && cpu->y < gpu->y);
  /* lines | FPS block | lines: a rule at each switch, none between lines */
  TEST_ASSERT_EQ_INT(2, count_rect_prefix(s, "id_osc_div"));

  s = sc::build_scene(base_options(), live_readings(), flat_history());
  TEST_ASSERT_EQ_INT(1, count_rect_prefix(s, "id_osc_div"));

  /* Compact follows the same order. */
  o.detail = sc::Detail::Compact;
  s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_TRUE(find_text(s, "id_osc_ram_l")->x <
                   find_text(s, "id_osc_fps_v")->x);
  TEST_ASSERT_TRUE(find_text(s, "id_osc_fps_v")->x <
                   find_text(s, "id_osc_cpu_l")->x);
  return 0;
}

int test_per_core_and_full(void) {
  sc::Options o = base_options();
  sc::Scene s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_TRUE(find_text(s, "id_osc_cpu_c0_v") == nullptr);
  TEST_ASSERT_TRUE(find_text(s, "id_osc_ip_v") == nullptr);

  o.per_core_cpu = true;
  s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_STREQ("10  20  30  40", text_of(s, "id_osc_cpu_c0_v"));
  TEST_ASSERT_STREQ("50  60  70  80", text_of(s, "id_osc_cpu_c1_v"));

  /* Full forces per-core, IP and fan whatever the toggles say. */
  o = base_options(sc::Detail::Full);
  o.show_ip = o.show_fan = o.per_core_cpu = false;
  s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_TRUE(find_text(s, "id_osc_cpu_c0_v") != nullptr);
  TEST_ASSERT_STREQ("192.168.1.20", text_of(s, "id_osc_ip_v"));
  TEST_ASSERT_STREQ("45%", text_of(s, "id_osc_fan_v"));
  TEST_ASSERT_TRUE(ids_unique(s));
  return 0;
}

int test_frame_time_graph(void) {
  sc::Histories h{};
  for (int i = 0; i < sc::kGraphPoints; ++i)
    h.frame_ms.push(i % 2 ? 1000.0f / 60.0f : 1000.0f / 30.0f);
  sc::Scene s = sc::build_scene(base_options(), live_readings(), h);
  TEST_ASSERT_EQ_INT(sc::kGraphPoints, count_rect_prefix(s, "id_osc_col"));
  const sc::Rect *well = find_rect(s, "id_osc_graph_bg");
  const sc::Rect *target = find_rect(s, "id_osc_target");
  const sc::Rect *a = find_rect(s, "id_osc_col0"); /* 33.3 ms = 2× target */
  const sc::Rect *b = find_rect(s, "id_osc_col1"); /* 16.7 ms = target */
  TEST_ASSERT_TRUE(well && target && a && b);
  /* Fixed 0 .. 2×target scale: 33 ms fills the graph, 16.7 ms half of it,
   * and the target line sits on top of the 16.7 ms columns. */
  TEST_ASSERT_EQ_INT(as_int(well->h), as_int(a->h));
  TEST_ASSERT_EQ_INT(as_int(well->h * 0.5f), as_int(b->h));
  TEST_ASSERT_EQ_INT(as_int(b->y), as_int(target->y));
  /* Newest column ends at the right edge. */
  const sc::Rect *last = find_rect(s, "id_osc_col39");
  TEST_ASSERT_TRUE(last != nullptr);
  TEST_ASSERT_EQ_INT(as_int(well->x + well->w), as_int(last->x + last->w));

  /* A steady 60 with ±0.3 fps of noise stays flat instead of filling the
   * graph (the old auto-scaled trend did). */
  sc::Histories noisy{};
  for (int i = 0; i < sc::kGraphPoints; ++i)
    noisy.frame_ms.push(1000.0f / (i % 2 ? 59.7f : 60.3f));
  s = sc::build_scene(base_options(), live_readings(), noisy);
  const float h0 = find_rect(s, "id_osc_col0")->h;
  const float h1 = find_rect(s, "id_osc_col1")->h;
  TEST_ASSERT_TRUE(std::fabs(h0 - h1) <= 1.0f);
  return 0;
}

int test_stutter_columns(void) {
  sc::Histories h = flat_history();
  h.frame_ms.push(1000.0f / 20.0f); /* one 50 ms hitch at a ~60 average */
  sc::Scene s = sc::build_scene(base_options(), live_readings(), h);
  const sc::Rect *hitch = find_rect(s, "id_osc_col39");
  const sc::Rect *fine = find_rect(s, "id_osc_col38");
  TEST_ASSERT_TRUE(hitch && fine);
  TEST_ASSERT_TRUE(same_color(sc::kBad, hitch->color));
  TEST_ASSERT_TRUE(same_color(sc::kGraphColumn, fine->color));
  /* Off the top of the 0..33 ms scale: clamped to the full height. */
  TEST_ASSERT_EQ_INT(as_int(find_rect(s, "id_osc_graph_bg")->h),
                     as_int(hitch->h));
  return 0;
}

int test_compact_line(void) {
  sc::Options o = base_options(sc::Detail::Compact);
  o.align = BarAlign::Center;
  o.margin = 10.0f;
  sc::Scene s = sc::build_scene(o, live_readings(), {});
  TEST_ASSERT_TRUE(ids_unique(s));
  TEST_ASSERT_STREQ("60", text_of(s, "id_osc_fps_v"));
  TEST_ASSERT_STREQ("16.7", text_of(s, "id_osc_ft_v"));
  TEST_ASSERT_STREQ("32%", text_of(s, "id_osc_cpu_v"));
  TEST_ASSERT_STREQ("61\xC2\xB0", text_of(s, "id_osc_cpu_t"));
  TEST_ASSERT_STREQ("58\xC2\xB0", text_of(s, "id_osc_gpu_t"));
  TEST_ASSERT_STREQ("VRAM", text_of(s, "id_osc_vram_l")); /* not "GPU %" */
  TEST_ASSERT_STREQ("50%", text_of(s, "id_osc_vram_v"));
  TEST_ASSERT_STREQ("9.0", text_of(s, "id_osc_ram_v"));
  TEST_ASSERT_STREQ("GB", text_of(s, "id_osc_ram_u"));
  /* Separators between the four groups are 1 px rects, not "|" glyphs. */
  TEST_ASSERT_EQ_INT(3, count_rect_prefix(s, "id_osc_sep"));
  for (const sc::Text &t : s.texts)
    TEST_ASSERT_TRUE(t.text != "|");

  /* The background hugs the content instead of spanning the screen. */
  const sc::Rect *bg = find_rect(s, "id_osc_bg");
  TEST_ASSERT_TRUE(bg != nullptr);
  TEST_ASSERT_TRUE(s.w < kW * 0.5f);
  TEST_ASSERT_EQ_INT(as_int(s.w), as_int(bg->w));
  TEST_ASSERT_EQ_INT(as_int((kW - s.w) * 0.5f), as_int(s.x));
  TEST_ASSERT_EQ_INT(10, as_int(s.y));
  float right = 0.0f;
  for (const sc::Text &t : s.texts)
    right = std::max(right, t.x);
  TEST_ASSERT_TRUE(right < s.x + s.w);
  return 0;
}

/* Values sit in fixed-width slots, so nothing moves as digits change. */
int test_compact_slots_are_stable(void) {
  sc::Options o = base_options(sc::Detail::Compact);
  o.align = BarAlign::Center;
  o.show_fan = true;
  sc::Readings lo = live_readings();
  lo.fps = 9.0f;
  lo.cpu_avg = 5.0f;
  lo.cpu_temp = 9;
  lo.soc_temp = 9;
  lo.vram_used_mb = 10;
  lo.ram_used_mb = 512;
  lo.fan_pct = 0.0f;
  sc::Readings hi = live_readings();
  hi.fps = 120.0f;
  hi.cpu_avg = 100.0f;
  hi.cpu_temp = 100;
  hi.soc_temp = 100;
  hi.vram_used_mb = hi.vram_total_mb;
  hi.ram_used_mb = 15999;
  hi.fan_pct = 100.0f;
  sc::Scene a = sc::build_scene(o, lo, {});
  sc::Scene b = sc::build_scene(o, hi, {});
  TEST_ASSERT_EQ_INT(as_int(a.w), as_int(b.w));
  TEST_ASSERT_EQ_INT(as_int(a.x), as_int(b.x));
  TEST_ASSERT_EQ_INT(static_cast<int>(a.texts.size()),
                     static_cast<int>(b.texts.size()));
  for (size_t i = 0; i < a.texts.size(); ++i) {
    TEST_ASSERT_STREQ(a.texts[i].id.c_str(), b.texts[i].id.c_str());
    TEST_ASSERT_EQ_INT(as_int(a.texts[i].x), as_int(b.texts[i].x));
    TEST_ASSERT_EQ_INT(as_int(a.texts[i].w), as_int(b.texts[i].w));
  }
  /* Losing the game does not move anything either. */
  sc::Readings gone = live_readings();
  gone.fps_valid = false;
  sc::Scene c = sc::build_scene(o, gone, {});
  TEST_ASSERT_EQ_INT(as_int(a.w), as_int(c.w));
  TEST_ASSERT_STREQ(kDash, text_of(c, "id_osc_fps_v"));
  return 0;
}

int test_minimal_pill(void) {
  sc::Options o = base_options(sc::Detail::Minimal);
  /* The pill is the FPS counter: metric toggles do not hide it. */
  o.show_fps = o.show_cpu = o.show_gpu = o.show_ram = false;
  sc::Scene s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_STREQ("60", text_of(s, "id_osc_fps_v"));
  TEST_ASSERT_STREQ("FPS", text_of(s, "id_osc_fps_l"));
  TEST_ASSERT_TRUE(find_text(s, "id_osc_cpu_v") == nullptr);
  const sc::Rect *dot = find_rect(s, "id_osc_dot");
  TEST_ASSERT_TRUE(dot && same_color(sc::kGood, dot->color));

  sc::Readings r = live_readings();
  r.fps = 30.0f;
  s = sc::build_scene(o, r, flat_history());
  TEST_ASSERT_TRUE(same_color(sc::kBad, find_rect(s, "id_osc_dot")->color));
  r.fps_target = 30;
  s = sc::build_scene(o, r, flat_history());
  TEST_ASSERT_TRUE(same_color(sc::kGood, find_rect(s, "id_osc_dot")->color));

  r.fps_valid = false;
  s = sc::build_scene(o, r, flat_history());
  TEST_ASSERT_STREQ(kDash, text_of(s, "id_osc_fps_v"));
  TEST_ASSERT_TRUE(same_color(sc::kLabel, find_rect(s, "id_osc_dot")->color));
  return 0;
}

int test_companion_fps_pill(void) {
  /* Off by default. */
  sc::Scene plain = sc::build_scene(base_options(), live_readings(), flat_history());
  TEST_ASSERT_TRUE(find_text(plain, "id_osc_pill_fps_v") == nullptr);

  sc::Options o = base_options(); /* panel, top-left */
  o.fps_pill = true;
  o.margin = 10.0f;
  sc::Scene s = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_TRUE(ids_unique(s));
  /* The panel itself is unchanged and still owns the scene box. */
  TEST_ASSERT_EQ_INT(as_int(plain.w), as_int(s.w));
  TEST_ASSERT_STREQ("60", text_of(s, "id_osc_fps_v")); /* panel hero */
  TEST_ASSERT_STREQ("60", text_of(s, "id_osc_pill_fps_v"));
  /* Pill sits in the opposite (top-right) corner with its own background. */
  const sc::Rect *bg = find_rect(s, "id_osc_pill_bg");
  TEST_ASSERT_TRUE(bg != nullptr);
  TEST_ASSERT_EQ_INT(as_int(kW - 10.0f), as_int(bg->x + bg->w));
  TEST_ASSERT_EQ_INT(10, as_int(bg->y));
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_pill_dot") != nullptr);

  /* Right-aligned overlay → pill on the left; bottom edge follows along. */
  o.align = BarAlign::Right;
  o.edge = BarEdge::Bottom;
  s = sc::build_scene(o, live_readings(), flat_history());
  bg = find_rect(s, "id_osc_pill_bg");
  TEST_ASSERT_TRUE(bg != nullptr);
  TEST_ASSERT_EQ_INT(10, as_int(bg->x));
  TEST_ASSERT_EQ_INT(as_int(kH - 10.0f), as_int(bg->y + bg->h));

  /* Works with compact, and still shows when every metric is hidden. */
  o = base_options(sc::Detail::Compact);
  o.fps_pill = true;
  o.show_fps = o.show_cpu = o.show_gpu = o.show_ram = false;
  s = sc::build_scene(o, live_readings(), {});
  TEST_ASSERT_STREQ("60", text_of(s, "id_osc_pill_fps_v"));
  TEST_ASSERT_TRUE(find_rect(s, "id_osc_bg") == nullptr);

  /* The minimal level already is the pill: no second copy. */
  o = base_options(sc::Detail::Minimal);
  o.fps_pill = true;
  s = sc::build_scene(o, live_readings(), {});
  TEST_ASSERT_TRUE(find_text(s, "id_osc_pill_fps_v") == nullptr);
  TEST_ASSERT_STREQ("60", text_of(s, "id_osc_fps_v"));
  return 0;
}

int test_target_inference(void) {
  TEST_ASSERT_EQ_INT(60, sc::infer_target(0.0f));
  TEST_ASSERT_EQ_INT(60, sc::infer_target(std::numeric_limits<float>::quiet_NaN()));
  TEST_ASSERT_EQ_INT(30, sc::infer_target(29.8f));
  TEST_ASSERT_EQ_INT(30, sc::infer_target(31.0f));
  TEST_ASSERT_EQ_INT(40, sc::infer_target(39.5f));
  TEST_ASSERT_EQ_INT(60, sc::infer_target(45.0f));
  TEST_ASSERT_EQ_INT(60, sc::infer_target(62.0f));
  TEST_ASSERT_EQ_INT(120, sc::infer_target(90.0f));
  TEST_ASSERT_EQ_INT(120, sc::infer_target(200.0f));

  sc::TargetTracker t{3};
  TEST_ASSERT_EQ_INT(60, t.target());
  TEST_ASSERT_EQ_INT(60, t.update(30.0f));
  TEST_ASSERT_EQ_INT(60, t.update(30.0f));
  TEST_ASSERT_EQ_INT(60, t.update(59.0f)); /* a 60 reading resets the streak */
  TEST_ASSERT_EQ_INT(60, t.update(30.0f));
  TEST_ASSERT_EQ_INT(60, t.update(30.0f));
  TEST_ASSERT_EQ_INT(30, t.update(30.0f)); /* three in a row: switch */
  t.reset();
  TEST_ASSERT_EQ_INT(60, t.target());
  return 0;
}

int test_formatting(void) {
  TEST_ASSERT_STREQ("9.6", sc::fmt_gb(9870).c_str());
  TEST_ASSERT_STREQ("16", sc::fmt_gb_total(16384).c_str());
  TEST_ASSERT_STREQ("12.3", sc::fmt_gb_total(12544).c_str());
  TEST_ASSERT_STREQ("58\xC2\xB0", sc::fmt_temp(58).c_str());
  TEST_ASSERT_STREQ("100%", sc::fmt_pct(140.0f).c_str());
  TEST_ASSERT_STREQ("60", sc::fmt_fps(59.6f).c_str()); /* never "59.6" */
  TEST_ASSERT_STREQ("16.7", sc::fmt_ms(1000.0f / 60.0f).c_str());
  return 0;
}

/* Renderer cost: everything on, full history. Today's rows panel is ~250. */
int test_widget_budget(void) {
  sc::Histories h = flat_history();
  struct Case {
    sc::Detail detail;
    int max_widgets;
  } cases[] = {{sc::Detail::Minimal, 10},
               {sc::Detail::Compact, 52},
               {sc::Detail::Panel, 110},
               {sc::Detail::Full, 150}};
  for (const Case &c : cases) {
    sc::Options o = base_options(c.detail);
    o.show_ip = o.show_fan = true;
    o.brand = true;
    sc::Scene s = sc::build_scene(o, live_readings(), h);
    TEST_ASSERT_TRUE(!s.rects.empty());
    TEST_ASSERT_TRUE(ids_unique(s));
    if (widget_count(s) > c.max_widgets)
      return onion_test_fail(__FILE__, __LINE__, "level %d uses %d widgets > %d",
                             static_cast<int>(c.detail), widget_count(s),
                             c.max_widgets);
  }
  return 0;
}

int test_large_font_scales(void) {
  sc::Options o = base_options();
  sc::Scene medium = sc::build_scene(o, live_readings(), flat_history());
  o.font_pt = 24;
  sc::Scene large = sc::build_scene(o, live_readings(), flat_history());
  TEST_ASSERT_TRUE(large.w > medium.w);
  TEST_ASSERT_TRUE(large.h > medium.h);
  TEST_ASSERT_EQ_INT(as_int(284.0f * 24.0f / 18.0f), as_int(large.w));
  TEST_ASSERT_EQ_INT(284, as_int(medium.w));
  TEST_ASSERT_TRUE(find_text(large, "id_osc_fps_v")->pt >
                   find_text(medium, "id_osc_fps_v")->pt);
  return 0;
}

int test_frame_count_window(void) {
  sc::FrameCountWindow w;
  const uint64_t t0 = 10000000000ull;
  /* First read only sets the base. */
  TEST_ASSERT_TRUE(w.take(1, 1000000, t0) < 0.0f);
  /* 250 ms later, 15 more frames: exactly 60 fps. */
  TEST_ASSERT_EQ_INT(60, as_int(w.take(1, 1015000, t0 + 250000000ull)));
  /* Too short a window keeps the base for next time. */
  TEST_ASSERT_TRUE(w.take(1, 1016000, t0 + 300000000ull) < 0.0f);
  TEST_ASSERT_EQ_INT(30, as_int(w.take(1, 1022500, t0 + 500000000ull)));
  /* No new frames over a window is a real freeze, not "no data". */
  TEST_ASSERT_EQ_INT(0, as_int(w.take(1, 1022500, t0 + 750000000ull)));
  /* New epoch (game restarted): never difference across it. */
  TEST_ASSERT_TRUE(w.take(2, 500, t0 + 1000000000ull) < 0.0f);
  TEST_ASSERT_EQ_INT(40, as_int(w.take(2, 10500, t0 + 1250000000ull)));
  /* A count going backwards re-bases too. */
  TEST_ASSERT_TRUE(w.take(2, 100, t0 + 1500000000ull) < 0.0f);
  w.reset();
  TEST_ASSERT_TRUE(w.take(2, 20000, t0 + 1750000000ull) < 0.0f);
  return 0;
}

int test_ring(void) {
  sc::Ring<8> h;
  TEST_ASSERT_EQ_INT(0, h.size());
  for (int i = 0; i < 8 + 5; ++i)
    h.push(static_cast<float>(i));
  h.push(std::numeric_limits<float>::quiet_NaN()); /* ignored */
  TEST_ASSERT_EQ_INT(8, h.size());
  TEST_ASSERT_EQ_INT(5, as_int(h.at(0)));
  TEST_ASSERT_EQ_INT(12, as_int(h.at(7)));
  h.clear();
  TEST_ASSERT_EQ_INT(0, h.size());
  return 0;
}

int test_fps_stats(void) {
  sc::FpsStats st;
  TEST_ASSERT_EQ_INT(0, as_int(st.average()));
  for (int i = 0; i < 99; ++i)
    st.push(60.0f);
  st.push(20.0f);
  st.push(0.0f); /* ignored */
  TEST_ASSERT_EQ_INT(100, st.size());
  TEST_ASSERT_EQ_INT(20, as_int(st.low_1pct()));
  TEST_ASSERT_EQ_INT(60, as_int(st.average())); /* 59.6 */
  st.clear();
  TEST_ASSERT_EQ_INT(0, st.size());
  return 0;
}

} // namespace

extern "C" int test_overlay_scene_suite(void) {
  int failures = 0;
  failures += onion_test_run("overlay_scene.empty", test_empty_when_nothing_enabled);
  failures += onion_test_run("overlay_scene.placement", test_corner_placement_and_margin);
  failures += onion_test_run("overlay_scene.opacity", test_opacity);
  failures += onion_test_run("overlay_scene.panel_values", test_panel_values);
  failures += onion_test_run("overlay_scene.brand", test_brand_header);
  failures += onion_test_run("overlay_scene.no_game", test_no_game);
  failures += onion_test_run("overlay_scene.non_finite", test_non_finite_and_missing);
  failures += onion_test_run("overlay_scene.bars", test_bars);
  failures += onion_test_run("overlay_scene.temp_colors", test_temperature_colors);
  failures += onion_test_run("overlay_scene.fps_colors", test_fps_state_colors);
  failures += onion_test_run("overlay_scene.order", test_order_and_dividers);
  failures += onion_test_run("overlay_scene.per_core_full", test_per_core_and_full);
  failures += onion_test_run("overlay_scene.frame_graph", test_frame_time_graph);
  failures += onion_test_run("overlay_scene.stutter", test_stutter_columns);
  failures += onion_test_run("overlay_scene.compact", test_compact_line);
  failures += onion_test_run("overlay_scene.compact_stable", test_compact_slots_are_stable);
  failures += onion_test_run("overlay_scene.minimal", test_minimal_pill);
  failures += onion_test_run("overlay_scene.fps_pill", test_companion_fps_pill);
  failures += onion_test_run("overlay_scene.target", test_target_inference);
  failures += onion_test_run("overlay_scene.format", test_formatting);
  failures += onion_test_run("overlay_scene.budget", test_widget_budget);
  failures += onion_test_run("overlay_scene.large_font", test_large_font_scales);
  failures += onion_test_run("overlay_scene.frame_window", test_frame_count_window);
  failures += onion_test_run("overlay_scene.ring", test_ring);
  failures += onion_test_run("overlay_scene.fps_stats", test_fps_stats);
  return failures;
}
