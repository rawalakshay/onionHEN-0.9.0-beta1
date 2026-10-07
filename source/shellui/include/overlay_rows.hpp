/* Copyright (C) 2025 OnionHEN
 *
 * "Rows" overlay style: an Afterburner-style corner panel (the pc-monitor
 * OSD "rows" preset) — one section per metric, label left / value right,
 * thin usage bars and small trend graphs.
 *
 * Pure layout: readings + history in, a flat list of rectangles and text runs
 * out (absolute RootWidget coordinates, back-to-front). The ShellUI renderer
 * reconciles that list into UI2.Panel / UI2.Label widgets by id, so this file
 * has no Mono dependency and is covered by host tests.
 */
#pragma once

#include "overlay_layout.hpp"
#include "overlay_text_metrics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace onion::overlay::rows {

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

/* Palette — pc-monitor tokens.css (OSD section hues + thermal scale). */
inline constexpr Rgba kFps = hex(0xFFD36A);
inline constexpr Rgba kCpu = hex(0x3D9BFF);
inline constexpr Rgba kGpu = hex(0x55D76C);
inline constexpr Rgba kRam = hex(0xE0709E);
inline constexpr Rgba kIp = hex(0xC1CADD);
inline constexpr Rgba kFan = hex(0xEC835A);
inline constexpr Rgba kTextPrimary = hex(0xF7F9FF);
inline constexpr Rgba kTextSecondary = hex(0xC1CADD);
inline constexpr Rgba kTextDim = hex(0x8492A6);
inline constexpr Rgba kAccent = hex(0x3D9BFF);
inline constexpr float kSurfaceAlpha = 0.65f;
inline constexpr Rgba kSurface = {7.0f / 255.0f, 11.0f / 255.0f, 18.0f / 255.0f,
                                  kSurfaceAlpha};
inline constexpr Rgba kHairline = hex(0x8EA4C2, 0.36f * kSurfaceAlpha);
inline constexpr Rgba kDivider = hex(0x8EA4C2, 0.14f);
inline constexpr Rgba kHeaderDivider = hex(0x8EA4C2, 0.16f);
inline constexpr Rgba kTrack = hex(0x8EA4C2, 0.16f);
inline constexpr Rgba kTempCool = hex(0x3D9BFF);
inline constexpr Rgba kTempWarm = hex(0xFFD36A);
inline constexpr Rgba kTempHot = hex(0xEC835A);
inline constexpr Rgba kTempCrit = hex(0xFF7F93);

/* Shown in the header, as in the pc-monitor overlay title bar. */
inline constexpr const char *kBrandText = "4aLse Benchmarks";

/** Thermal thresholds [warm, hot, critical] in °C. */
struct TempScale {
  int warm;
  int hot;
  int crit;
};
inline constexpr TempScale kCpuTempScale = {45, 65, 80};
inline constexpr TempScale kGpuTempScale = {45, 65, 85};

inline Rgba temp_color(int celsius, TempScale scale) {
  if (celsius >= scale.crit)
    return kTempCrit;
  if (celsius >= scale.hot)
    return kTempHot;
  if (celsius >= scale.warm)
    return kTempWarm;
  return kTempCool;
}

/* Trend points: 1 sample per metrics refresh (~1 s). */
inline constexpr int kHistoryCap = 30;
inline constexpr int kSparkPoints = 12;
inline constexpr int kGraphPoints = kHistoryCap;

/** Fixed-size ring of the most recent samples, read oldest → newest. */
class History {
public:
  void push(float v) {
    if (!std::isfinite(v))
      return;
    buf_[head_] = v;
    head_ = (head_ + 1) % kHistoryCap;
    if (count_ < kHistoryCap)
      ++count_;
  }
  void clear() {
    head_ = 0;
    count_ = 0;
  }
  int size() const { return count_; }
  /** i = 0 is the oldest retained sample. */
  float at(int i) const {
    const int start = (head_ - count_ + kHistoryCap) % kHistoryCap;
    return buf_[(start + i) % kHistoryCap];
  }

private:
  std::array<float, kHistoryCap> buf_{};
  int head_ = 0;
  int count_ = 0;
};

/**
 * Average and 1% low over a sliding window of FPS samples. The daemon
 * publishes an already-smoothed rate (not per-frame times), so "1% low" here
 * is the 1st percentile of sampled rates — the closest stand-in available.
 */
class FpsStats {
public:
  static constexpr int kCap = 1800; /* ~60 s at the 2-frame read cadence */

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

/** Latest sensor values; *_valid = false renders a dash. */
struct Readings {
  bool fps_valid = false;
  float fps = 0.0f;
  float fps_avg = 0.0f;
  float fps_low = 0.0f;
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
  History fps;
  History cpu;
  History cpu_temp;
  History vram;
  History soc_temp;
  History ram;
};

struct Options {
  float screen_w = 0.0f;
  float screen_h = 0.0f;
  BarEdge edge = BarEdge::Top;
  BarAlign align = BarAlign::Left;
  int font_pt = static_cast<int>(kFontH);
  bool background = true;
  /** Gap from the screen edges the panel is anchored to (overlay.margin). */
  float margin = 0.0f;
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

enum class Font : uint8_t { Brand, Label, Value };
enum class TextAlign : uint8_t { Left, Right };

struct Rect {
  std::string id;
  float x, y, w, h;
  Rgba color;
};

/** A text run: the renderer places a transparent cell at x/y/w/h and the
 *  label inside it at margin_top. */
struct Text {
  std::string id;
  float x, y, w, h;
  float margin_top;
  std::string text;
  Rgba color;
  Font font;
  TextAlign align;
};

struct Scene {
  float screen_w = 0.0f, screen_h = 0.0f;
  float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
  int label_pt = 0, value_pt = 0, brand_pt = 0;
  std::vector<Rect> rects; /* back to front */
  std::vector<Text> texts;
};

/**
 * true: values use the label's own right alignment over the full row width.
 * false: values are left-aligned at an estimated x (fallback if
 * HorizontalAlignment=Right ever misbehaves in ShellUI).
 */
inline constexpr bool kNativeRightAlign = true;
inline constexpr int kHAlignLeft = 0;
inline constexpr int kHAlignRight = 2;

/** Every pc-monitor 1x pixel length scales by font_pt / 12.5 (its value size). */
struct Metrics {
  float k;
  int value_pt, label_pt, brand_pt;
  float row_h, pad_x, pad_y, panel_w, rail_w, rail_inset;
  float section_gap, header_gap, bar_gap, bar_h;
  float spark_w, spark_h, spark_gap, value_reserve, graph_h, mark;

  explicit Metrics(int font_pt) {
    value_pt = font_pt > 0 ? font_pt : static_cast<int>(kFontH);
    k = static_cast<float>(value_pt) / 12.5f;
    label_pt = static_cast<int>(std::lround(11.0f * k));
    brand_pt = static_cast<int>(std::lround(10.5f * k));
    row_h = std::round(static_cast<float>(value_pt) * 1.4f);
    pad_x = std::round(10.0f * k);
    pad_y = std::round(8.0f * k);
    panel_w = std::round(250.0f * k);
    rail_w = std::max(3.0f, std::round(2.5f * k));
    rail_inset = std::round(6.0f * k);
    section_gap = std::round(3.5f * k);
    header_gap = std::round(3.0f * k);
    bar_gap = std::max(1.0f, std::round(1.0f * k));
    bar_h = std::max(2.0f, std::round(2.0f * k));
    spark_w = std::round(30.0f * k);
    spark_h = std::round(12.0f * k);
    spark_gap = std::round(5.0f * k);
    value_reserve = std::round(62.0f * k);
    graph_h = std::round(26.0f * k);
    mark = std::round(9.0f * k);
  }

  /** Top inset that puts a run of `pt` on the row's shared baseline. */
  float text_inset(int pt) const {
    const float box = static_cast<float>(value_pt) * 1.25f;
    const float top = (row_h - box) * 0.5f;
    return std::round(top + static_cast<float>(value_pt - pt) * 0.85f);
  }
};

namespace detail {

inline std::string fmt(const char *format, double v) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), format, v);
  return buf;
}

inline std::string fmt_int(const char *format, int v) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), format, v);
  return buf;
}

inline constexpr const char *kDash = "\xE2\x80\x94";  /* — */
inline constexpr const char *kDegC = "\xC2\xB0" "C";  /* °C */

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

struct Domain {
  bool fixed;
  float lo;
  float hi;
};
inline constexpr Domain kAuto = {false, 0.0f, 0.0f};
inline constexpr Domain kPct = {true, 0.0f, 100.0f};
inline constexpr Domain kTemp = {true, 20.0f, 100.0f};

class Builder {
public:
  Builder(const Metrics &m, Scene &scene) : m_(m), scene_(scene) {
    inner_x_ = m_.pad_x;
    inner_w_ = m_.panel_w - 2.0f * m_.pad_x;
    y_ = m_.pad_y;
  }

  float y() const { return y_; }
  void advance(float dy) { y_ += dy; }

  void rect(std::string id, float x, float y, float w, float h, Rgba c) {
    /* negated so NaN sizes are dropped too */
    if (!(w > 0.0f) || !(h > 0.0f))
      return;
    scene_.rects.push_back({std::move(id), x, y, w, h, c});
  }

  void text(std::string id, float x, float w, std::string s, Rgba c, Font f,
            TextAlign align, int pt) {
    if (align == TextAlign::Right && !kNativeRightAlign) {
      const float est = estimate_text_width(s.c_str(), static_cast<float>(pt));
      x = x + w - est;
      w = est;
      align = TextAlign::Left;
    }
    scene_.texts.push_back({std::move(id), x, y_, w, m_.row_h,
                            m_.text_inset(pt), std::move(s), c, f, align});
  }

  /** Brand left; running title + session time right. The title comes from
   *  the FPS sample, so with FPS off the right side stays empty rather than
   *  claiming there is no game. */
  void header(const Readings &r, bool fps_enabled) {
    const float mark_y = y_ + std::round((m_.row_h - m_.mark) * 0.5f);
    rect("id_orow_mark", inner_x_, mark_y, m_.mark, m_.mark, kAccent);
    const float brand_x = inner_x_ + m_.mark + std::round(4.0f * m_.k);
    text("id_orow_brand", brand_x, inner_w_ * 0.6f, kBrandText, kTextPrimary,
         Font::Brand, TextAlign::Left, m_.brand_pt);

    if (fps_enabled) {
      std::string session = "NO GAME";
      if (r.fps_valid) {
        const std::string t = format_session(r.session_s);
        /* Past an hour the clock grows to h:mm:ss and title + clock would
         * run into the brand, so the clock alone is kept. */
        session = r.session_s >= 3600 ? std::string() : r.title_id;
        if (!t.empty())
          session += session.empty() ? t : "  " + t;
      }
      if (!session.empty())
        text("id_orow_session", inner_x_, inner_w_, session, kTextDim,
             Font::Label, TextAlign::Right, m_.label_pt);
    }
    y_ += m_.row_h + m_.header_gap;
    rect("id_orow_head_div", inner_x_, y_, inner_w_, 1.0f, kHeaderDivider);
    y_ += 1.0f + m_.header_gap;
  }

  /** 1px rule between sections (not before the first). */
  void section_break() {
    if (sections_++ == 0)
      return;
    y_ += m_.section_gap;
    rect("id_orow_div" + std::to_string(sections_ - 1), inner_x_, y_, inner_w_,
         1.0f, kDivider);
    y_ += 1.0f + m_.section_gap;
  }

  /**
   * One metric row: label · [spark] · value, then an optional usage bar.
   * bar_pct < 0 = no bar; spark == nullptr = no trend.
   */
  void row(const char *key, const char *label, Rgba label_color,
           const std::string &value, Rgba value_color, float bar_pct = -1.0f,
           Rgba bar_color = {}, const History *spark = nullptr,
           Domain domain = kAuto) {
    const std::string base = std::string("id_orow_") + key;
    const bool has_spark = spark && spark->size() > 1;
    const float label_w =
        inner_w_ - (has_spark ? m_.value_reserve + m_.spark_gap + m_.spark_w
                              : m_.value_reserve);
    text(base + "_l", inner_x_, label_w, label, label_color, Font::Label,
         TextAlign::Left, m_.label_pt);
    text(base + "_v", inner_x_, inner_w_, value, value_color, Font::Value,
         TextAlign::Right, m_.value_pt);
    if (has_spark) {
      const float sx =
          inner_x_ + inner_w_ - m_.value_reserve - m_.spark_gap - m_.spark_w;
      const float sy = y_ + std::round((m_.row_h - m_.spark_h) * 0.5f);
      columns(base + "_sp", *spark, kSparkPoints, sx, sy, m_.spark_w,
              m_.spark_h, label_color, domain, false);
    }
    y_ += m_.row_h;
    if (bar_pct >= 0.0f) {
      y_ += m_.bar_gap;
      rect(base + "_track", inner_x_, y_, inner_w_, m_.bar_h, kTrack);
      const float fill = std::round(inner_w_ * std::clamp(bar_pct, 0.0f, 100.0f) /
                                    100.0f);
      rect(base + "_fill", inner_x_, y_, fill, m_.bar_h, bar_color);
      y_ += m_.bar_h + m_.bar_gap;
    }
  }

  /** Full-width trend graph on its own line (pc-monitor frame-time graph). */
  void graph(const char *key, const History &h, Rgba color) {
    if (h.size() < 2)
      return;
    y_ += m_.bar_gap;
    columns(std::string("id_orow_") + key, h, kGraphPoints, inner_x_, y_,
            inner_w_, m_.graph_h, color, kAuto, true);
    y_ += m_.graph_h + m_.bar_gap;
  }

  float panel_h() const { return y_ + m_.pad_y; }

private:
  /**
   * Area-under-line approximated with columns: a faint body plus a 2px cap
   * at the data point, newest at the right edge.
   */
  void columns(const std::string &base, const History &h, int points, float x,
               float y, float w, float height, Rgba color, Domain domain,
               bool graph) {
    const int n = std::min(points, h.size());
    if (n < 2)
      return;
    const int first = h.size() - n;
    float lo = domain.lo, hi = domain.hi;
    if (!domain.fixed) {
      lo = hi = h.at(first);
      for (int i = first; i < h.size(); ++i) {
        lo = std::min(lo, h.at(i));
        hi = std::max(hi, h.at(i));
      }
      /* Leave headroom so a flat line sits mid-graph instead of on the floor. */
      const float pad = std::max((hi - lo) * 0.15f, 1.0f);
      lo = std::max(0.0f, lo - pad);
      hi += pad;
    }
    const float span = (hi - lo) > 0.0f ? (hi - lo) : 1.0f;
    const float gap = 1.0f;
    const float col_w =
        std::max(1.0f, std::floor((w - gap * (points - 1)) / points));
    const float cap_h = std::max(1.0f, std::round(height * 0.12f));
    const float body_a = graph ? 0.22f : 0.25f;
    for (int i = 0; i < n; ++i) {
      const float v = std::clamp(h.at(first + i), lo, hi);
      const float colh = std::max(1.0f, std::round((v - lo) / span * height));
      /* right-anchor: slot index counts back from the newest sample */
      const int slot = points - n + i;
      const float cx = x + w - (points - slot) * (col_w + gap) + gap;
      const float cy = y + height - colh;
      const std::string id = base + std::to_string(i);
      rect(id + "b", cx, cy, col_w, colh, with_alpha(color, body_a));
      rect(id + "c", cx, cy, col_w, std::min(cap_h, colh),
           with_alpha(color, 0.9f));
    }
  }

  const Metrics &m_;
  Scene &scene_;
  float inner_x_ = 0.0f;
  float inner_w_ = 0.0f;
  float y_ = 0.0f;
  int sections_ = 0;
};

inline History frame_times(const History &fps) {
  History out;
  for (int i = 0; i < fps.size(); ++i) {
    const float f = fps.at(i);
    out.push(f > 0.0f ? 1000.0f / f : 0.0f);
  }
  return out;
}

/** A NaN/Inf reading renders as missing ("—", empty bar), never "nan %". */
inline Readings sanitized(const Readings &in) {
  Readings r = in;
  auto ok = [](float v) { return std::isfinite(v); };
  r.fps_valid = r.fps_valid && ok(r.fps) && ok(r.fps_avg) && ok(r.fps_low);
  r.cpu_valid = r.cpu_valid && ok(r.cpu_avg);
  for (float &core : r.cpu_core)
    if (!ok(core))
      core = 0.0f;
  r.fan_valid = r.fan_valid && ok(r.fan_pct);
  return r;
}

} // namespace detail

inline bool any_metric(const Options &o) {
  return o.show_fps || o.show_cpu || o.per_core_cpu || o.show_gpu ||
         o.show_ram || o.show_ip || o.show_fan;
}

/** Build the panel. Returns an empty scene when nothing is enabled or the
 *  screen size is unknown. */
inline Scene build_scene(const Options &opt, const Readings &readings,
                         const Histories &hist) {
  using namespace detail;
  const Readings r = sanitized(readings);
  Scene scene{};
  if (!valid_screen_dimension(opt.screen_w) ||
      !valid_screen_dimension(opt.screen_h) || !any_metric(opt))
    return scene;

  scene.screen_w = opt.screen_w;
  scene.screen_h = opt.screen_h;
  const Metrics m(opt.font_pt);
  scene.value_pt = m.value_pt;
  scene.label_pt = m.label_pt;
  scene.brand_pt = m.brand_pt;

  /* Content first, in panel-local coordinates; chrome is prepended after the
   * height is known so it stays at the back. */
  Scene content{};
  Builder b(m, content);
  b.header(r, opt.show_fps);

  std::array<int, kMetricCount> order = opt.order;
  normalize_metric_order(order);
  for (int metric : order) {
    switch (metric) {
    case kMetricFps:
      if (!opt.show_fps)
        break;
      b.section_break();
      if (r.fps_valid) {
        b.row("fps", "FPS", kFps, fmt("%.0f", r.fps), kTextPrimary, -1.0f, {},
              &hist.fps, kAuto);
        b.row("fps_avg", "AVERAGE FPS", kFps, fmt("%.0f", r.fps_avg),
              kTextPrimary);
        b.row("fps_low", "1% LOW FPS", kFps, fmt("%.0f", r.fps_low),
              kTextPrimary);
        b.row("fps_ft", "FRAME TIME", kFps,
              fmt("%.1f ms", r.fps > 0.0f ? 1000.0 / r.fps : 0.0),
              kTextPrimary);
        b.graph("fps_ftg", frame_times(hist.fps), kTextSecondary);
      } else {
        b.row("fps", "FPS", kFps, "NO GAME DETECTED", kTextDim);
      }
      break;
    case kMetricCpu: {
      if (!opt.show_cpu && !opt.per_core_cpu)
        break;
      b.section_break();
      b.row("cpu", "CPU", kCpu,
            r.cpu_valid ? fmt("%.0f %%", r.cpu_avg) : kDash, kTextPrimary,
            r.cpu_valid ? r.cpu_avg : 0.0f, kCpu, &hist.cpu, kPct);
      b.row("cpu_temp", "CPU TEMP", kCpu,
            r.cpu_temp_valid ? fmt_int("%d ", r.cpu_temp) + kDegC : kDash,
            r.cpu_temp_valid ? temp_color(r.cpu_temp, kCpuTempScale)
                             : kTextPrimary,
            -1.0f, {}, &hist.cpu_temp, kTemp);
      if (opt.per_core_cpu) {
        for (int half = 0; half < 2; ++half) {
          std::string cores;
          if (r.cpu_valid) {
            for (int c = 0; c < 4; ++c) {
              if (c)
                cores += "  ";
              cores += fmt("%.0f", r.cpu_core[half * 4 + c]);
            }
          } else {
            cores = kDash;
          }
          b.row(half ? "cpu_c1" : "cpu_c0", half ? "CORES 4-7" : "CORES 0-3",
                kCpu, cores, kTextSecondary);
        }
      }
      break;
    }
    case kMetricGpu:
      if (!opt.show_gpu)
        break;
      b.section_break();
      b.row("gpu", "GPU VRAM", kGpu,
            r.gpu_valid ? fmt_int("%d MB", r.vram_used_mb) : kDash,
            kTextPrimary,
            r.gpu_valid ? pct(r.vram_used_mb, r.vram_total_mb) : 0.0f, kGpu,
            &hist.vram, kPct);
      b.row("gpu_temp", "GPU TEMP", kGpu,
            r.soc_temp_valid ? fmt_int("%d ", r.soc_temp) + kDegC : kDash,
            r.soc_temp_valid ? temp_color(r.soc_temp, kGpuTempScale)
                             : kTextPrimary,
            -1.0f, {}, &hist.soc_temp, kTemp);
      break;
    case kMetricMemory:
      if (!opt.show_ram)
        break;
      b.section_break();
      b.row("ram", "RAM USAGE", kRam,
            r.ram_valid ? fmt_int("%d MB", r.ram_used_mb) : kDash,
            kTextPrimary,
            r.ram_valid ? pct(r.ram_used_mb, r.ram_total_mb) : 0.0f, kRam,
            &hist.ram, kPct);
      break;
    case kMetricIp:
      if (!opt.show_ip)
        break;
      b.section_break();
      b.row("ip", "IP ADDRESS", kIp, r.ip.empty() ? kDash : r.ip,
            kTextPrimary);
      break;
    case kMetricFan:
      if (!opt.show_fan)
        break;
      b.section_break();
      b.row("fan", "FAN", kFan,
            r.fan_valid ? fmt("%.0f %%", r.fan_pct) : kDash, kTextPrimary,
            r.fan_valid ? r.fan_pct : 0.0f, kFan);
      break;
    default:
      break;
    }
  }

  scene.w = m.panel_w;
  scene.h = std::round(b.panel_h());
  const float margin = std::max(0.0f, opt.margin);
  switch (opt.align) {
  case BarAlign::Left:
    scene.x = margin;
    break;
  case BarAlign::Right:
    scene.x = std::round(opt.screen_w - margin - scene.w);
    break;
  case BarAlign::Center:
  default:
    scene.x = std::round((opt.screen_w - scene.w) * 0.5f);
    break;
  }
  scene.y = opt.edge == BarEdge::Bottom
                ? std::round(opt.screen_h - margin - scene.h)
                : margin;

  if (opt.background) {
    const float w = scene.w, h = scene.h;
    scene.rects.push_back({"id_orow_bg", 0.0f, 0.0f, w, h, kSurface});
    scene.rects.push_back({"id_orow_edge_t", 0.0f, 0.0f, w, 1.0f, kHairline});
    scene.rects.push_back({"id_orow_edge_b", 0.0f, h - 1.0f, w, 1.0f, kHairline});
    scene.rects.push_back({"id_orow_edge_l", 0.0f, 1.0f, 1.0f, h - 2.0f, kHairline});
    scene.rects.push_back(
        {"id_orow_edge_r", w - 1.0f, 1.0f, 1.0f, h - 2.0f, kHairline});
    scene.rects.push_back({"id_orow_rail", 0.0f, m.rail_inset, m.rail_w,
                           h - 2.0f * m.rail_inset,
                           with_alpha(kAccent, 0.35f + 0.65f * kSurfaceAlpha)});
  }
  for (Rect &rc : content.rects)
    scene.rects.push_back(std::move(rc));
  scene.texts = std::move(content.texts);

  for (Rect &rc : scene.rects) {
    rc.x += scene.x;
    rc.y += scene.y;
  }
  for (Text &t : scene.texts) {
    t.x += scene.x;
    t.y += scene.y;
  }
  return scene;
}

} // namespace onion::overlay::rows
