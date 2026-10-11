/* Copyright (C) 2025 OnionHEN
 *
 * Game monitor overlay layout, four detail levels:
 *   Minimal  status dot + FPS pill
 *   Compact  one fitted line, fixed-width value slots
 *   Panel    corner panel: big FPS, frame-time graph, CPU / GPU / RAM lines
 *   Full     panel + per-core CPU, IP and fan
 *
 * Pure layout: readings + history in, a flat list of rectangles and text runs
 * out (absolute RootWidget coordinates, back-to-front). The ShellUI renderer
 * (overlay_scene_renderer.cpp) reconciles that list into UI2.Panel /
 * UI2.Label widgets by id, so this file has no Mono dependency and is covered
 * by host tests.
 *
 * Colour is reserved for state (FPS under target, hot temperatures, nearly
 * full memory); everything else is dim labels and white values.
 */
#pragma once

#include "overlay_layout.hpp"
#include "overlay_text_metrics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

namespace onion::overlay::scene {

struct Rgba {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;
};

constexpr Rgba hex(uint32_t rgb, float a = 1.0f) {
  return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
          static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
          static_cast<float>(rgb & 0xFF) / 255.0f, a};
}

constexpr Rgba with_alpha(Rgba c, float a) { return {c.r, c.g, c.b, a}; }

/* Palette. */
inline constexpr Rgba kText = hex(0xF3F5F9);
inline constexpr Rgba kLabel = hex(0x8E98AA);
inline constexpr uint32_t kSurfaceRgb = 0x0B0E13;
inline constexpr Rgba kHairline = hex(0xFFFFFF, 0.09f);
inline constexpr Rgba kSeparator = hex(0xFFFFFF, 0.14f);
inline constexpr Rgba kTrack = hex(0xFFFFFF, 0.10f);
inline constexpr Rgba kFill = hex(0xC9D2E0);
inline constexpr Rgba kGraphWell = hex(0xFFFFFF, 0.04f);
inline constexpr Rgba kGraphColumn = hex(0xC9D2E0, 0.55f);
inline constexpr Rgba kTargetLine = hex(0xFFFFFF, 0.35f);
inline constexpr Rgba kGood = hex(0x6FD39A);
inline constexpr Rgba kWarn = hex(0xF3C969);
inline constexpr Rgba kBad = hex(0xFF7A7A);

/* Shown in the panel header when Options::brand is on. */
inline constexpr const char *kBrandText = "4aLse Benchmarks";

/** Usage bars turn warn above this percentage. */
inline constexpr float kBarWarnPct = 85.0f;

/** A frame-time column is a stutter when its rate is under this × average. */
inline constexpr float kStutterRatio = 0.6f;

/** Thermal thresholds in °C: warn from `hot`, bad from `crit`. */
struct TempScale {
  int hot;
  int crit;
};
inline constexpr TempScale kCpuTempScale = {65, 80};
inline constexpr TempScale kGpuTempScale = {65, 85};

inline Rgba temp_color(int celsius, TempScale scale) {
  if (celsius >= scale.crit)
    return kBad;
  if (celsius >= scale.hot)
    return kWarn;
  return kText;
}

enum class FpsState : uint8_t { Good, Warn, Bad };

/** How close the frame rate is to the title's target. */
inline FpsState fps_state(float fps, int target) {
  const float t = static_cast<float>(target > 0 ? target : 60);
  if (fps >= 0.95f * t)
    return FpsState::Good;
  if (fps >= 0.8f * t)
    return FpsState::Warn;
  return FpsState::Bad;
}

inline Rgba fps_state_color(FpsState s) {
  switch (s) {
  case FpsState::Good:
    return kGood;
  case FpsState::Warn:
    return kWarn;
  case FpsState::Bad:
  default:
    return kBad;
  }
}

/* Large numbers stay white while things are fine. */
inline Rgba fps_value_color(FpsState s) {
  return s == FpsState::Good ? kText : fps_state_color(s);
}

/** Common frame caps, lowest first. */
inline constexpr std::array<int, 4> kFpsTargets = {30, 40, 60, 120};

/** The cap a title most likely runs at: the first cap its average fits
 *  under (with 5% slack). 60 when nothing is known yet. */
inline int infer_target(float avg) {
  if (!(avg > 0.0f) || !std::isfinite(avg))
    return 60;
  for (int t : kFpsTargets) {
    if (avg <= static_cast<float>(t) * 1.05f)
      return t;
  }
  return kFpsTargets.back();
}

/**
 * infer_target() with hysteresis: a new cap must win `hold` consecutive
 * updates before it replaces the current one, so a brief dip does not flip
 * the target line and every colour with it.
 */
class TargetTracker {
public:
  explicit TargetTracker(int hold = 20) : hold_(hold) {}

  int update(float avg) {
    const int t = infer_target(avg);
    if (t == target_) {
      pending_ = 0;
      streak_ = 0;
    } else if (t == pending_) {
      if (++streak_ >= hold_) {
        target_ = t;
        pending_ = 0;
        streak_ = 0;
      }
    } else {
      pending_ = t;
      streak_ = 1;
      if (streak_ >= hold_) {
        target_ = t;
        pending_ = 0;
        streak_ = 0;
      }
    }
    return target_;
  }
  int target() const { return target_; }
  void reset() { *this = TargetTracker(hold_); }

private:
  int hold_;
  int target_ = 60;
  int pending_ = 0;
  int streak_ = 0;
};

/** Fixed-size ring of the most recent samples, read oldest → newest. */
template <int Cap> class Ring {
public:
  static constexpr int kCap = Cap;

  void push(float v) {
    if (!std::isfinite(v))
      return;
    buf_[head_] = v;
    head_ = (head_ + 1) % Cap;
    if (count_ < Cap)
      ++count_;
  }
  void clear() {
    head_ = 0;
    count_ = 0;
  }
  int size() const { return count_; }
  /** i = 0 is the oldest retained sample. */
  float at(int i) const {
    const int start = (head_ - count_ + Cap) % Cap;
    return buf_[(start + i) % Cap];
  }

private:
  std::array<float, Cap> buf_{};
  int head_ = 0;
  int count_ = 0;
};

/* Frame-time graph: one bucket per 250 ms, ~10 s on screen. */
inline constexpr int kGraphPoints = 40;
inline constexpr int kBucketMs = 250;

/**
 * Average and low over a sliding window of FPS samples. The daemon publishes
 * an already-smoothed rate (not per-frame times), so "low" is the 1st
 * percentile of sampled rates, not a per-frame 1% low.
 */
class FpsStats {
public:
  static constexpr int kCap = 1800; /* ~60 s at 30 samples/s */

  void push(float fps) {
    if (!(fps > 0.0f) || !std::isfinite(fps))
      return;
    buf_[head_] = fps;
    head_ = (head_ + 1) % kCap;
    if (count_ < kCap)
      ++count_;
  }
  void clear() {
    head_ = 0;
    count_ = 0;
  }
  int size() const { return count_; }
  float average() const {
    if (count_ == 0)
      return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < count_; ++i)
      sum += buf_[i];
    return static_cast<float>(sum / count_);
  }
  float low_1pct() const {
    if (count_ == 0)
      return 0.0f;
    std::vector<float> sorted(buf_.begin(), buf_.begin() + count_);
    const auto nth = sorted.begin() + ((count_ - 1) / 100);
    std::nth_element(sorted.begin(), nth, sorted.end());
    return *nth;
  }

private:
  std::array<float, kCap> buf_{};
  int head_ = 0;
  int count_ = 0;
};

/**
 * Exact frame rate between successive reads of the daemon's running frame
 * count (OnionFpsSample frames_milli / frames_mono_ns / ext_epoch), so the
 * graph and stats need not re-average an already-averaged rate.
 */
class FrameCountWindow {
public:
  /** Shorter windows are too coarse at a 30 Hz daemon tick. */
  static constexpr uint64_t kMinWindowNs = 100000000ull;

  /**
   * Frames per second since the previous successful take() (0 = a real
   * freeze), or a negative value when there is no usable window yet: first
   * read, new epoch, count went backwards, or less than kMinWindowNs elapsed
   * (the base is kept for the next call).
   */
  float take(uint32_t epoch, uint64_t frames_milli, uint64_t mono_ns) {
    if (!have_ || epoch != epoch_ || mono_ns < ns_ || frames_milli < frames_) {
      have_ = true;
      epoch_ = epoch;
      frames_ = frames_milli;
      ns_ = mono_ns;
      return -1.0f;
    }
    const uint64_t dns = mono_ns - ns_;
    if (dns < kMinWindowNs)
      return -1.0f;
    const double fps = (static_cast<double>(frames_milli - frames_) / 1000.0) /
                       (static_cast<double>(dns) / 1e9);
    frames_ = frames_milli;
    ns_ = mono_ns;
    return static_cast<float>(fps);
  }
  void reset() { have_ = false; }

private:
  bool have_ = false;
  uint32_t epoch_ = 0;
  uint64_t frames_ = 0;
  uint64_t ns_ = 0;
};

/** Latest sensor values; *_valid = false renders a dash. */
struct Readings {
  bool fps_valid = false;
  float fps = 0.0f;
  float fps_avg = 0.0f;
  float fps_low = 0.0f;
  /** Frame cap the colours and the target line compare against. */
  int fps_target = 60;
  bool cpu_valid = false;
  float cpu_avg = 0.0f;
  std::array<float, 8> cpu_core{};
  bool cpu_temp_valid = false;
  int cpu_temp = 0;
  bool gpu_valid = false;
  int vram_used_mb = 0;
  int vram_total_mb = 0;
  bool soc_temp_valid = false;
  int soc_temp = 0;
  bool ram_valid = false;
  int ram_used_mb = 0;
  int ram_total_mb = 0;
  std::string ip;
  bool fan_valid = false;
  float fan_pct = 0.0f;
  std::string title_id;
  /** Seconds since the current title started presenting; < 0 = unknown. */
  int session_s = -1;
};

struct Histories {
  /** Frame time in ms, one entry per kBucketMs. */
  Ring<kGraphPoints> frame_ms;
};

enum class Detail : uint8_t { Minimal, Compact, Panel, Full };

struct Options {
  float screen_w = 0.0f;
  float screen_h = 0.0f;
  BarEdge edge = BarEdge::Top;
  BarAlign align = BarAlign::Center;
  Detail detail = Detail::Compact;
  int font_pt = static_cast<int>(kFontH);
  /** Background opacity in percent; 0 = no background or outline. */
  int opacity = 65;
  /** Gap from the screen edges the overlay is anchored to (overlay.margin). */
  float margin = 10.0f;
  bool brand = false;
  /** Also draw the FPS pill in the opposite corner (compact/panel/full). */
  bool fps_pill = false;
  bool show_fps = true;
  bool show_cpu = true;
  bool show_gpu = true;
  bool show_ram = true;
  bool show_ip = false;
  bool show_fan = false;
  bool per_core_cpu = false;
  /* Must match onion::kOverlayMetric* / kOverlayOrderDefault. */
  std::array<int, kMetricCount> order{kMetricFps, kMetricCpu, kMetricGpu,
                                      kMetricMemory, kMetricIp, kMetricFan};
};

enum class TextAlign : uint8_t { Left, Right };

struct Rect {
  std::string id;
  float x, y, w, h;
  Rgba color;
};

/** A text run: the renderer places a transparent cell at x/y/w/h and the
 *  label inside it at margin_top, in a font of `pt` / `weight`. */
struct Text {
  std::string id;
  float x, y, w, h;
  float margin_top;
  std::string text;
  Rgba color;
  int pt;
  int weight;
  TextAlign align;
};

struct Scene {
  float screen_w = 0.0f, screen_h = 0.0f;
  float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
  std::vector<Rect> rects; /* back to front */
  std::vector<Text> texts;
};

/**
 * true: values use the label's own right alignment over the slot width.
 * false: values are left-aligned at an estimated x (fallback if
 * HorizontalAlignment=Right ever misbehaves in ShellUI).
 */
inline constexpr bool kNativeRightAlign = true;
inline constexpr int kHAlignLeft = 0;
inline constexpr int kHAlignRight = 2;

/*
 * Font weights handed to UI2.UIFont (style 1). On hardware (2026-10-11)
 * 600/700/800 all rendered in the same light face; 900 is what the original
 * edge bar used, so values and the big FPS number use it. Labels stay light
 * on purpose: they are the dim, secondary text.
 */
inline constexpr int kWeightLabel = 600;
inline constexpr int kWeightValue = 900;
inline constexpr int kWeightHero = 900;

/** Every length is designed at 18 pt and scales with the font setting. */
struct Metrics {
  float k;
  int value_pt;  /* compact values */
  int label_pt;  /* dim labels */
  int small_pt;  /* header, graph caption */
  int stat_pt;   /* panel values */
  int hero_pt;   /* panel FPS */
  int pill_pt;   /* minimal FPS */
  float pad_x, pad_y, gap, sep_gap, line_h, small_h, hero_h, bar_h, bar_gap;
  float panel_w, value_col, graph_h, graph_gap, dot, section_gap;

  explicit Metrics(int font_pt) {
    value_pt = font_pt > 0 ? font_pt : static_cast<int>(kFontH);
    k = static_cast<float>(value_pt) / 18.0f;
    auto px = [this](float v) { return std::round(v * k); };
    auto pt = [this](float v) { return static_cast<int>(std::lround(v * k)); };
    label_pt = pt(13.0f);
    small_pt = pt(12.0f);
    stat_pt = pt(16.0f);
    hero_pt = pt(45.0f);
    pill_pt = pt(22.0f);
    pad_x = px(14.0f);
    pad_y = px(10.0f);
    gap = px(6.0f);
    sep_gap = px(14.0f);
    line_h = px(34.0f);
    small_h = px(22.0f);
    hero_h = px(56.0f);
    bar_h = std::max(2.0f, px(3.0f));
    bar_gap = px(4.0f);
    panel_w = px(284.0f);
    value_col = px(48.0f);
    graph_h = px(40.0f);
    graph_gap = std::max(1.0f, px(1.5f));
    dot = px(10.0f);
    section_gap = px(8.0f);
  }

  /** Top inset that puts a run of `pt` on the baseline a run of `ref_pt`
   *  would have when centred in a row of height `row_h`. */
  static float text_top(float row_h, int pt, int ref_pt) {
    const float box = static_cast<float>(ref_pt) * 1.25f;
    const float top = (row_h - box) * 0.5f;
    return std::round(top + static_cast<float>(ref_pt - pt) * 0.85f);
  }
};

namespace detail {

inline std::string fmt(const char *format, double v) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), format, v);
  return buf;
}

inline constexpr const char *kDash = "\xE2\x80\x94";  /* — */
inline constexpr const char *kDeg = "\xC2\xB0";       /* ° */

inline float pct(int used, int total) {
  if (total <= 0)
    return 0.0f;
  const float p = 100.0f * static_cast<float>(used) / static_cast<float>(total);
  return std::clamp(p, 0.0f, 100.0f);
}

inline std::string format_session(int seconds) {
  if (seconds < 0)
    return {};
  const int h = seconds / 3600;
  const int m = (seconds % 3600) / 60;
  const int s = seconds % 60;
  char buf[24];
  if (h > 0)
    std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
  else
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
  return buf;
}

/** NaN/Inf readings render as missing ("—", empty bar), never "nan %". */
inline Readings sanitized(const Readings &in) {
  Readings r = in;
  auto ok = [](float v) { return std::isfinite(v); };
  r.fps_valid = r.fps_valid && ok(r.fps) && ok(r.fps_avg) && ok(r.fps_low) &&
                r.fps > 0.0f;
  r.cpu_valid = r.cpu_valid && ok(r.cpu_avg);
  for (float &core : r.cpu_core)
    if (!ok(core))
      core = 0.0f;
  r.fan_valid = r.fan_valid && ok(r.fan_pct);
  if (r.fps_target <= 0)
    r.fps_target = 60;
  return r;
}

} // namespace detail

/* Value formatting shared by every level. */
inline std::string fmt_fps(float fps) {
  return detail::fmt("%.0f", std::clamp(fps, 0.0f, 999.0f));
}
inline std::string fmt_ms(float ms) {
  return detail::fmt("%.1f", std::clamp(ms, 0.0f, 999.9f));
}
inline std::string fmt_pct(float pct) {
  return detail::fmt("%.0f%%", std::clamp(pct, 0.0f, 100.0f));
}
inline std::string fmt_temp(int celsius) {
  return std::to_string(celsius) + detail::kDeg;
}
/** MB → "9.6" (GB, one decimal). */
inline std::string fmt_gb(int mb) {
  return detail::fmt("%.1f", static_cast<double>(std::max(mb, 0)) / 1024.0);
}
/** MB → "16" or "12.3" (GB; whole numbers drop the decimal). */
inline std::string fmt_gb_total(int mb) {
  const double gb = static_cast<double>(std::max(mb, 0)) / 1024.0;
  const double rounded = std::round(gb * 10.0) / 10.0;
  if (std::fabs(rounded - std::round(rounded)) < 0.05)
    return detail::fmt("%.0f", rounded);
  return detail::fmt("%.1f", rounded);
}

namespace detail {

/** Accumulates rects/texts in overlay-local coordinates. */
class Builder {
public:
  Builder(const Metrics &m, Scene &scene) : m_(m), scene_(scene) {}

  const Metrics &m() const { return m_; }

  void rect(std::string id, float x, float y, float w, float h, Rgba c) {
    /* negated so NaN sizes are dropped too */
    if (!(w > 0.0f) || !(h > 0.0f))
      return;
    scene_.rects.push_back({std::move(id), x, y, w, h, c});
  }

  /**
   * Text in a row starting at `row_y` of height `row_h`, baseline-aligned
   * with a run of `ref_pt`. Left runs start at x; right runs end at x + w.
   */
  void text(std::string id, float x, float w, float row_y, float row_h,
            std::string s, Rgba c, int pt, int weight, TextAlign align,
            int ref_pt) {
    if (align == TextAlign::Right && !kNativeRightAlign) {
      const float est = text_advance(s.c_str(), static_cast<float>(pt));
      x = x + w - est;
      w = est;
      align = TextAlign::Left;
    }
    scene_.texts.push_back({std::move(id), x, row_y, w, row_h,
                            Metrics::text_top(row_h, pt, ref_pt), std::move(s),
                            c, pt, weight, align});
  }

  /** Width to give a left-aligned run so its real glyphs never clip. */
  static float left_w(const std::string &s, int pt) {
    return std::ceil(text_advance(s.c_str(), static_cast<float>(pt)) * 1.25f +
                     6.0f);
  }

private:
  const Metrics &m_;
  Scene &scene_;
};

/** Width reserved for a value slot: the widest of the worst-case strings, so
 *  changing digits never move anything. */
inline float slot_w(std::initializer_list<const char *> worst, int pt) {
  float w = 0.0f;
  for (const char *s : worst)
    w = std::max(w, text_advance(s, static_cast<float>(pt)));
  return std::ceil(w + 2.0f);
}

inline constexpr const char *kIdPrefix = "id_osc_";
/* The companion FPS pill keeps its own ids so it can sit next to a level
 * that uses the plain ones. */
inline constexpr const char *kPillIdPrefix = "id_osc_pill_";

/** Background + 1 px outline; nothing at opacity 0. */
inline void chrome(Scene &out, float w, float h, int opacity,
                   const std::string &prefix) {
  if (opacity <= 0)
    return;
  const float a = std::clamp(static_cast<float>(opacity) / 100.0f, 0.0f, 1.0f);
  out.rects.push_back({prefix + "bg", 0.0f, 0.0f, w, h, hex(kSurfaceRgb, a)});
  out.rects.push_back({prefix + "edge_t", 0.0f, 0.0f, w, 1.0f, kHairline});
  out.rects.push_back({prefix + "edge_b", 0.0f, h - 1.0f, w, 1.0f, kHairline});
  out.rects.push_back({prefix + "edge_l", 0.0f, 1.0f, 1.0f, h - 2.0f, kHairline});
  out.rects.push_back(
      {prefix + "edge_r", w - 1.0f, 1.0f, 1.0f, h - 2.0f, kHairline});
}

/** Anchor a w×h overlay on screen per edge / align / margin. */
inline void place_on_screen(Scene &s, const Options &opt, float w, float h) {
  s.w = w;
  s.h = h;
  const float margin = std::max(0.0f, opt.margin);
  switch (opt.align) {
  case BarAlign::Left:
    s.x = margin;
    break;
  case BarAlign::Right:
    s.x = std::round(opt.screen_w - margin - w);
    break;
  case BarAlign::Center:
  default:
    s.x = std::round((opt.screen_w - w) * 0.5f);
    break;
  }
  s.y = opt.edge == BarEdge::Bottom ? std::round(opt.screen_h - margin - h)
                                    : margin;
}

/** Chrome behind content, then everything shifted to screen coordinates. */
inline Scene finish(const Options &opt, Scene content, float w, float h,
                    const std::string &prefix = kIdPrefix) {
  Scene out{};
  out.screen_w = opt.screen_w;
  out.screen_h = opt.screen_h;
  w = std::round(w);
  h = std::round(h);
  place_on_screen(out, opt, w, h);
  chrome(out, w, h, opt.opacity, prefix);
  for (Rect &rc : content.rects)
    out.rects.push_back(std::move(rc));
  out.texts = std::move(content.texts);
  for (Rect &rc : out.rects) {
    rc.x += out.x;
    rc.y += out.y;
  }
  for (Text &t : out.texts) {
    t.x += out.x;
    t.y += out.y;
  }
  return out;
}

/* ------------------------------------------------------------------ */
/* Minimal: [dot] 60 FPS                                               */
/* ------------------------------------------------------------------ */

inline Scene build_minimal(const Options &opt, const Readings &r,
                           const std::string &prefix = kIdPrefix) {
  const Metrics m(opt.font_pt);
  Scene content{};
  Builder b(m, content);
  const float h = std::round(36.0f * m.k);
  float x = m.pad_x;

  const Rgba dot = r.fps_valid
                       ? fps_state_color(fps_state(r.fps, r.fps_target))
                       : kLabel;
  b.rect(prefix + "dot", x, std::round((h - m.dot) * 0.5f), m.dot, m.dot, dot);
  x += m.dot + std::round(8.0f * m.k);

  const float vw = slot_w({"888"}, m.pill_pt);
  b.text(prefix + "fps_v", x, vw, 0.0f, h,
         r.fps_valid ? fmt_fps(r.fps) : kDash, kText, m.pill_pt,
         kWeightHero, TextAlign::Right, m.pill_pt);
  x += vw + std::round(5.0f * m.k);
  const std::string unit = "FPS";
  b.text(prefix + "fps_l", x, Builder::left_w(unit, m.label_pt), 0.0f, h, unit,
         kLabel, m.label_pt, kWeightLabel, TextAlign::Left, m.pill_pt);
  x += text_advance(unit.c_str(), static_cast<float>(m.label_pt)) + m.pad_x;
  return finish(opt, std::move(content), x, h, prefix);
}

/** Opposite horizontal side on the same edge; a centred overlay pairs with
 *  a pill on the right. */
inline BarAlign opposite_align(BarAlign align) {
  return align == BarAlign::Right ? BarAlign::Left : BarAlign::Right;
}

/** Adds the companion FPS pill to an already placed scene. */
inline void add_fps_pill(Scene &main, const Options &opt, const Readings &r) {
  Options pill = opt;
  pill.align = opposite_align(opt.align);
  Scene p = build_minimal(pill, r, kPillIdPrefix);
  for (Rect &rc : p.rects)
    main.rects.push_back(std::move(rc));
  for (Text &t : p.texts)
    main.texts.push_back(std::move(t));
}

/* ------------------------------------------------------------------ */
/* Compact: 60 FPS 16.7 ms | CPU 42% 58° | GPU 67° VRAM 49% | RAM ...  */
/* ------------------------------------------------------------------ */

class CompactLine {
public:
  CompactLine(const Metrics &m, Builder &b, float h)
      : m_(m), b_(b), h_(h), x_(m.pad_x) {}

  /** Starts a metric group: a separator before every group but the first. */
  void group() {
    if (groups_++ == 0)
      return;
    x_ += m_.sep_gap;
    const float sh = std::round(16.0f * m_.k);
    b_.rect("id_osc_sep" + std::to_string(groups_ - 1), x_,
            std::round((h_ - sh) * 0.5f), 1.0f, sh, kSeparator);
    x_ += 1.0f + m_.sep_gap;
  }

  void label(const std::string &id, const std::string &s, float gap_after) {
    b_.text(id, x_, Builder::left_w(s, m_.label_pt), 0.0f, h_, s, kLabel,
            m_.label_pt, kWeightLabel, TextAlign::Left, m_.value_pt);
    x_ += text_advance(s.c_str(), static_cast<float>(m_.label_pt)) + gap_after;
  }

  /** Right-aligned value in a slot sized for `worst`. */
  void value(const std::string &id, const std::string &s,
             std::initializer_list<const char *> worst, Rgba c,
             float gap_after) {
    const float w = slot_w(worst, m_.value_pt);
    b_.text(id, x_, w, 0.0f, h_, s, c, m_.value_pt, kWeightValue,
            TextAlign::Right, m_.value_pt);
    x_ += w + gap_after;
  }

  /** Left-aligned value whose width follows the text (IP address). */
  void free_value(const std::string &id, const std::string &s) {
    b_.text(id, x_, Builder::left_w(s, m_.value_pt), 0.0f, h_, s, kText,
            m_.value_pt, kWeightValue, TextAlign::Left, m_.value_pt);
    x_ += text_advance(s.c_str(), static_cast<float>(m_.value_pt));
  }

  float width() const { return x_ + m_.pad_x; }
  int groups() const { return groups_; }

private:
  const Metrics &m_;
  Builder &b_;
  float h_;
  float x_;
  int groups_ = 0;
};

inline Scene build_compact(const Options &opt, const Readings &r) {
  const Metrics m(opt.font_pt);
  Scene content{};
  Builder b(m, content);
  const float h = m.line_h;
  CompactLine line(m, b, h);
  const float g = m.gap;
  const float vg = std::round(10.0f * m.k); /* value → next value */
  const float ug = std::round(4.0f * m.k);  /* value → its unit */

  std::array<int, kMetricCount> order = opt.order;
  normalize_metric_order(order);
  for (int metric : order) {
    switch (metric) {
    case kMetricFps: {
      if (!opt.show_fps)
        break;
      line.group();
      const FpsState st = fps_state(r.fps, r.fps_target);
      line.value("id_osc_fps_v", r.fps_valid ? fmt_fps(r.fps) : kDash,
                 {"888"}, r.fps_valid ? fps_value_color(st) : kText, ug);
      line.label("id_osc_fps_l", "FPS", vg);
      line.value("id_osc_ft_v",
                 r.fps_valid ? fmt_ms(1000.0f / r.fps) : kDash, {"99.9"},
                 kText, ug);
      line.label("id_osc_ft_u", "ms", 0.0f);
      break;
    }
    case kMetricCpu:
      if (!opt.show_cpu && !opt.per_core_cpu)
        break;
      line.group();
      line.label("id_osc_cpu_l", "CPU", g);
      line.value("id_osc_cpu_v", r.cpu_valid ? fmt_pct(r.cpu_avg) : kDash,
                 {"100%"}, kText, vg);
      line.value("id_osc_cpu_t",
                 r.cpu_temp_valid ? fmt_temp(r.cpu_temp) : kDash,
                 {"100\xC2\xB0"},
                 r.cpu_temp_valid ? temp_color(r.cpu_temp, kCpuTempScale)
                                  : kText,
                 0.0f);
      break;
    case kMetricGpu:
      if (!opt.show_gpu)
        break;
      line.group();
      line.label("id_osc_gpu_l", "GPU", g);
      line.value("id_osc_gpu_t",
                 r.soc_temp_valid ? fmt_temp(r.soc_temp) : kDash,
                 {"100\xC2\xB0"},
                 r.soc_temp_valid ? temp_color(r.soc_temp, kGpuTempScale)
                                  : kText,
                 vg);
      line.label("id_osc_vram_l", "VRAM", g);
      {
        const float p = pct(r.vram_used_mb, r.vram_total_mb);
        line.value("id_osc_vram_v", r.gpu_valid ? fmt_pct(p) : kDash,
                   {"100%"}, r.gpu_valid && p > kBarWarnPct ? kWarn : kText,
                   0.0f);
      }
      break;
    case kMetricMemory:
      if (!opt.show_ram)
        break;
      line.group();
      line.label("id_osc_ram_l", "RAM", g);
      {
        const float p = pct(r.ram_used_mb, r.ram_total_mb);
        line.value("id_osc_ram_v", r.ram_valid ? fmt_gb(r.ram_used_mb) : kDash,
                   {"88.8"}, r.ram_valid && p > kBarWarnPct ? kWarn : kText,
                   ug);
      }
      line.label("id_osc_ram_u", "GB", 0.0f);
      break;
    case kMetricIp:
      if (!opt.show_ip)
        break;
      line.group();
      line.label("id_osc_ip_l", "IP", g);
      line.free_value("id_osc_ip_v", r.ip.empty() ? kDash : r.ip);
      break;
    case kMetricFan:
      if (!opt.show_fan)
        break;
      line.group();
      line.label("id_osc_fan_l", "FAN", g);
      line.value("id_osc_fan_v", r.fan_valid ? fmt_pct(r.fan_pct) : kDash,
                 {"100%"}, kText, 0.0f);
      break;
    default:
      break;
    }
  }
  if (line.groups() == 0)
    return Scene{};
  return finish(opt, std::move(content), line.width(), h);
}

/* ------------------------------------------------------------------ */
/* Panel / Full                                                        */
/* ------------------------------------------------------------------ */

class PanelBuilder {
public:
  PanelBuilder(const Metrics &m, Builder &b)
      : m_(m), b_(b), x0_(m.pad_x), iw_(m.panel_w - 2.0f * m.pad_x),
        y_(m.pad_y) {}

  float y() const { return y_; }
  float height() const { return y_ + m_.pad_y; }

  void header(const Readings &r, const Options &opt) {
    std::string left, right;
    if (opt.show_fps) {
      if (r.fps_valid) {
        left = r.title_id;
        right = format_session(r.session_s);
      } else {
        left = "NO GAME";
      }
    }
    if (opt.brand) {
      /* Brand takes the left; title + clock move right while they fit. */
      std::string session = left;
      if (!right.empty())
        session = session.empty() ? right : session + "  " + right;
      if (r.session_s >= 3600 && r.fps_valid)
        session = right; /* h:mm:ss: keep the clock only */
      left = kBrandText;
      right = session;
    }
    if (left.empty() && right.empty())
      return;
    const float h = m_.small_h;
    if (!left.empty())
      b_.text("id_osc_head_l", x0_, iw_ * 0.62f, y_, h, left,
              opt.brand ? kText : kLabel, m_.small_pt,
              opt.brand ? kWeightValue : kWeightLabel, TextAlign::Left,
              m_.small_pt);
    if (!right.empty())
      b_.text("id_osc_head_r", x0_, iw_, y_, h, right, kLabel, m_.small_pt,
              kWeightLabel, TextAlign::Right, m_.small_pt);
    y_ += h + std::round(2.0f * m_.k);
    started_ = true;
  }

  /** Divider between blocks (FPS block ↔ metric lines). */
  void block_break() {
    if (!started_ || last_block_ == 0) {
      started_ = true;
      return;
    }
    y_ += m_.section_gap;
    b_.rect("id_osc_div" + std::to_string(divs_++), x0_, y_, iw_, 1.0f,
            kHairline);
    y_ += 1.0f + m_.section_gap;
  }

  void fps_block(const Readings &r, const Histories &h) {
    if (last_block_ != 0 && last_block_ != 1)
      block_break();
    last_block_ = 1;
    if (!r.fps_valid) {
      const float lh = m_.small_h + std::round(6.0f * m_.k);
      b_.text("id_osc_fps_l", x0_, Builder::left_w("FPS", m_.label_pt), y_, lh,
              "FPS", kLabel, m_.label_pt, kWeightLabel, TextAlign::Left,
              m_.stat_pt);
      b_.text("id_osc_fps_v", x0_, iw_, y_, lh, "NO GAME DETECTED", kLabel,
              m_.label_pt, kWeightLabel, TextAlign::Right, m_.stat_pt);
      y_ += lh;
      return;
    }

    /* Hero: big FPS + unit, AVG / LOW stacked on the right. */
    const float hh = m_.hero_h;
    const FpsState st = fps_state(r.fps, r.fps_target);
    const std::string fps = fmt_fps(r.fps);
    b_.text("id_osc_fps_v", x0_, Builder::left_w(fps, m_.hero_pt), y_, hh,
            fps, fps_value_color(st), m_.hero_pt, kWeightHero,
            TextAlign::Left, m_.hero_pt);
    const float ux = x0_ +
                     text_advance(fps.c_str(), static_cast<float>(m_.hero_pt)) +
                     std::round(6.0f * m_.k);
    b_.text("id_osc_fps_l", ux, Builder::left_w("FPS", m_.label_pt), y_, hh,
            "FPS", kLabel, m_.label_pt, kWeightLabel, TextAlign::Left,
            m_.hero_pt);

    const float sv = slot_w({"888"}, m_.stat_pt);
    const float sl = std::round(8.0f * m_.k);
    const float half = std::round(hh * 0.5f);
    const float sx = x0_ + iw_ - sv;
    b_.text("id_osc_avg_v", sx, sv, y_, half, fmt_fps(r.fps_avg), kText,
            m_.stat_pt, kWeightValue, TextAlign::Right, m_.stat_pt);
    b_.text("id_osc_avg_l", x0_, iw_ - sv - sl, y_, half, "AVG", kLabel,
            m_.small_pt, kWeightLabel, TextAlign::Right, m_.stat_pt);
    b_.text("id_osc_low_v", sx, sv, y_ + half, hh - half, fmt_fps(r.fps_low),
            fps_value_color(fps_state(r.fps_low, r.fps_target)), m_.stat_pt,
            kWeightValue, TextAlign::Right, m_.stat_pt);
    b_.text("id_osc_low_l", x0_, iw_ - sv - sl, y_ + half, hh - half, "LOW",
            kLabel, m_.small_pt, kWeightLabel, TextAlign::Right, m_.stat_pt);
    y_ += hh;

    /* Caption row: FRAME TIME ........ 16.7 ms */
    const float ch = m_.small_h;
    b_.text("id_osc_ft_l", x0_, iw_ * 0.6f, y_, ch, "FRAME TIME", kLabel,
            m_.small_pt, kWeightLabel, TextAlign::Left, m_.stat_pt);
    const float uw = text_advance("ms", static_cast<float>(m_.small_pt));
    const float ug = std::round(4.0f * m_.k);
    b_.text("id_osc_ft_u", x0_, iw_, y_, ch, "ms", kLabel, m_.small_pt,
            kWeightLabel, TextAlign::Right, m_.stat_pt);
    b_.text("id_osc_ft_v", x0_, iw_ - uw - ug, y_, ch,
            fmt_ms(1000.0f / r.fps), kText, m_.stat_pt, kWeightValue,
            TextAlign::Right, m_.stat_pt);
    y_ += ch + std::round(4.0f * m_.k);

    graph(r, h);
  }

  /**
   * Frame-time columns on a fixed 0 .. 2×target scale, newest at the right,
   * a 1 px line at the target and stutters in red.
   */
  void graph(const Readings &r, const Histories &h) {
    const float gh = m_.graph_h;
    const float gy = y_;
    b_.rect("id_osc_graph_bg", x0_, gy, iw_, gh, kGraphWell);
    const float target_ms = 1000.0f / static_cast<float>(r.fps_target);
    const float top_ms = 2.0f * target_ms;
    const float avg = r.fps_avg > 0.0f ? r.fps_avg : r.fps;
    const float stutter_ms = 1000.0f / (kStutterRatio * avg);

    const Ring<kGraphPoints> &ring = h.frame_ms;
    const int n = std::min(kGraphPoints, ring.size());
    const int first = ring.size() - n;
    const float gap = m_.graph_gap;
    const float col_w = std::max(
        1.0f, std::floor((iw_ - gap * (kGraphPoints - 1)) / kGraphPoints));
    /* Columns are right-anchored; leftover pixels stay on the left edge. */
    const float right = x0_ + iw_;
    for (int i = 0; i < n; ++i) {
      const float ms = ring.at(first + i);
      const float frac = std::clamp(ms / top_ms, 0.0f, 1.0f);
      const float ch = std::max(2.0f, std::round(frac * gh));
      const int slot_from_right = n - 1 - i;
      const float cx = right - col_w - slot_from_right * (col_w + gap);
      const bool stutter = ms > stutter_ms;
      b_.rect("id_osc_col" + std::to_string(i), cx, gy + gh - ch, col_w, ch,
              stutter ? kBad : kGraphColumn);
    }
    b_.rect("id_osc_target", x0_, gy + std::round(gh * 0.5f), iw_, 1.0f,
            kTargetLine);
    y_ += gh;
  }

  /** label · value [unit] ............ [right] / usage bar */
  void line(const char *key, const char *label, const std::string &value,
            Rgba value_color, const std::string &unit, const std::string &right,
            Rgba right_color, float bar_pct) {
    if (last_block_ != 2)
      block_break();
    if (last_block_ == 2)
      y_ += std::round(6.0f * m_.k);
    last_block_ = 2;
    const std::string base = std::string("id_osc_") + key;
    const float lh = m_.small_h + std::round(4.0f * m_.k);
    b_.text(base + "_l", x0_, m_.value_col, y_, lh, label, kLabel, m_.label_pt,
            kWeightLabel, TextAlign::Left, m_.stat_pt);
    const float vx = x0_ + m_.value_col;
    b_.text(base + "_v", vx, Builder::left_w(value, m_.stat_pt), y_, lh, value,
            value_color, m_.stat_pt, kWeightValue, TextAlign::Left, m_.stat_pt);
    if (!unit.empty()) {
      const float ux = vx +
                       text_advance(value.c_str(),
                                    static_cast<float>(m_.stat_pt)) +
                       std::round(5.0f * m_.k);
      b_.text(base + "_u", ux, Builder::left_w(unit, m_.small_pt), y_, lh,
              unit, kLabel, m_.small_pt, kWeightLabel, TextAlign::Left,
              m_.stat_pt);
    }
    if (!right.empty())
      b_.text(base + "_r", x0_, iw_, y_, lh, right, right_color, m_.stat_pt,
              kWeightValue, TextAlign::Right, m_.stat_pt);
    y_ += lh;
    if (bar_pct >= 0.0f) {
      y_ += m_.bar_gap;
      b_.rect(base + "_track", x0_, y_, iw_, m_.bar_h, kTrack);
      const float p = std::clamp(bar_pct, 0.0f, 100.0f);
      b_.rect(base + "_fill", x0_, y_, std::round(iw_ * p / 100.0f), m_.bar_h,
              p > kBarWarnPct ? kWarn : kFill);
      y_ += m_.bar_h;
    }
  }

private:
  const Metrics &m_;
  Builder &b_;
  float x0_, iw_, y_;
  bool started_ = false;
  int last_block_ = 0; /* 0 none, 1 FPS block, 2 metric lines */
  int divs_ = 0;
};

inline Scene build_panel(const Options &opt, const Readings &r,
                         const Histories &h, bool full) {
  const Metrics m(opt.font_pt);
  Scene content{};
  Builder b(m, content);
  PanelBuilder p(m, b);
  p.header(r, opt);

  const bool show_ip = opt.show_ip || full;
  const bool show_fan = opt.show_fan || full;
  const bool per_core = full || opt.per_core_cpu;

  std::array<int, kMetricCount> order = opt.order;
  normalize_metric_order(order);
  int sections = 0;
  for (int metric : order) {
    switch (metric) {
    case kMetricFps:
      if (!opt.show_fps)
        break;
      ++sections;
      p.fps_block(r, h);
      break;
    case kMetricCpu:
      if (!opt.show_cpu && !opt.per_core_cpu && !full)
        break;
      ++sections;
      p.line("cpu", "CPU", r.cpu_valid ? fmt_pct(r.cpu_avg) : kDash, kText, "",
             r.cpu_temp_valid ? fmt_temp(r.cpu_temp) : kDash,
             r.cpu_temp_valid ? temp_color(r.cpu_temp, kCpuTempScale) : kText,
             r.cpu_valid ? r.cpu_avg : 0.0f);
      if (per_core) {
        for (int half = 0; half < 2; ++half) {
          std::string cores;
          if (r.cpu_valid) {
            for (int c = 0; c < 4; ++c) {
              if (c)
                cores += "  ";
              cores += fmt("%.0f", std::clamp(r.cpu_core[half * 4 + c], 0.0f,
                                              100.0f));
            }
          } else {
            cores = kDash;
          }
          p.line(half ? "cpu_c1" : "cpu_c0", half ? "C4-7 %" : "C0-3 %",
                 cores, kText, "", "", kText, -1.0f);
        }
      }
      break;
    case kMetricGpu:
      if (!opt.show_gpu)
        break;
      ++sections;
      p.line("gpu", "GPU", r.gpu_valid ? fmt_gb(r.vram_used_mb) : kDash, kText,
             r.gpu_valid ? "/ " + fmt_gb_total(r.vram_total_mb) + " GB" : "",
             r.soc_temp_valid ? fmt_temp(r.soc_temp) : kDash,
             r.soc_temp_valid ? temp_color(r.soc_temp, kGpuTempScale) : kText,
             r.gpu_valid ? pct(r.vram_used_mb, r.vram_total_mb) : 0.0f);
      break;
    case kMetricMemory:
      if (!opt.show_ram)
        break;
      ++sections;
      p.line("ram", "RAM", r.ram_valid ? fmt_gb(r.ram_used_mb) : kDash, kText,
             r.ram_valid ? "/ " + fmt_gb_total(r.ram_total_mb) + " GB" : "",
             "", kText, r.ram_valid ? pct(r.ram_used_mb, r.ram_total_mb) : 0.0f);
      break;
    case kMetricIp:
      if (!show_ip)
        break;
      ++sections;
      p.line("ip", "IP", r.ip.empty() ? kDash : r.ip, kText, "", "", kText,
             -1.0f);
      break;
    case kMetricFan:
      if (!show_fan)
        break;
      ++sections;
      p.line("fan", "FAN", r.fan_valid ? fmt_pct(r.fan_pct) : kDash, kText, "",
             "", kText, r.fan_valid ? r.fan_pct : 0.0f);
      break;
    default:
      break;
    }
  }
  if (sections == 0)
    return Scene{};
  return finish(opt, std::move(content), m.panel_w, p.height());
}

} // namespace detail

inline bool any_metric(const Options &o) {
  return o.show_fps || o.show_cpu || o.per_core_cpu || o.show_gpu ||
         o.show_ram || o.show_ip || o.show_fan;
}

/** Build one frame. Returns an empty scene when nothing is enabled or the
 *  screen size is unknown. */
inline Scene build_scene(const Options &opt, const Readings &readings,
                         const Histories &hist) {
  const Readings r = detail::sanitized(readings);
  if (!valid_screen_dimension(opt.screen_w) ||
      !valid_screen_dimension(opt.screen_h))
    return Scene{};
  Scene s{};
  switch (opt.detail) {
  case Detail::Minimal:
    /* The pill is the FPS counter; metric toggles do not apply. */
    return detail::build_minimal(opt, r);
  case Detail::Panel:
    if (any_metric(opt))
      s = detail::build_panel(opt, r, hist, false);
    break;
  case Detail::Full:
    s = detail::build_panel(opt, r, hist, true);
    break;
  case Detail::Compact:
  default:
    if (any_metric(opt))
      s = detail::build_compact(opt, r);
    break;
  }
  if (opt.fps_pill) {
    if (s.rects.empty() && s.texts.empty()) {
      s.screen_w = opt.screen_w;
      s.screen_h = opt.screen_h;
    }
    detail::add_fps_pill(s, opt, r);
  }
  return s;
}

} // namespace onion::overlay::scene
