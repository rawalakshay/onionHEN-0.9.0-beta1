/* Copyright (C) 2025 OnionHEN / LightningMods — OnPress overlay domain */
#include "onpress.hpp"
#include "overlay_scene_renderer.hpp"
#include <cstdlib>
#include <unistd.h>

void RemoveGameWidget(RemoveWidget widget);
bool CreateGameWidget(CreateWidget widget);

/** Tear down every overlay widget. The scene renderer re-creates what it
 *  needs on its next render tick; the legacy bar is rebuilt here. */
static void rebuild_overlay_bar() {
  RemoveGameWidget(REMOVE_ALL_OVERLAYS);
  overlay_scene_remove_all();
  apply_overlay_layout();
  if (!g_settings.overlay_enabled || !kOverlayLegacyBarFallback)
    return;
  if (g_settings.overlay_fps)
    CreateGameWidget(CREATE_FPS_OVERLAY);
  if (g_settings.overlay_cpu || g_settings.all_cpu_usage)
    CreateGameWidget(CREATE_CPU_OVERLAY);
  if (g_settings.overlay_gpu)
    CreateGameWidget(CREATE_GPU_OVERLAY);
  if (g_settings.overlay_ram)
    CreateGameWidget(CREATE_RAM_OVERLAY);
  if (g_settings.overlay_ip)
    CreateGameWidget(CREATE_IP_OVERLAY);
  if (g_settings.overlay_fan)
    CreateGameWidget(CREATE_FAN_OVERLAY);
}

static OnPressResult toggle_overlay_flag(OnPressContext &ctx, bool &flag) {
  if (atoi(ctx.value.c_str()) == flag) {
    return OnPressResult::EarlyReturn;
  }
  flag = !flag;
  rebuild_overlay_bar();
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_overlay_enabled(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_enabled);
}

static OnPressResult id_overlay_brand(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_brand);
}

static OnPressResult id_overlay_fps_pill(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_fps_pill);
}

static OnPressResult id_overlay_system(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_system);
}

static OnPressResult id_overlay_opacity(OnPressContext &ctx) {
  const int pct = atoi(ctx.value.c_str());
  if (pct == g_settings.overlay_opacity || !onion::overlay_opacity_valid(pct)) {
    return OnPressResult::EarlyReturn;
  }
  g_settings.overlay_opacity = pct;
  rebuild_overlay_bar();
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_overlay_gpu(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_gpu);
}

static OnPressResult id_overlay_cpu(OnPressContext &ctx) {
  if (atoi(ctx.value.c_str()) == g_settings.overlay_cpu) {
    return OnPressResult::EarlyReturn;
  }
  if (!atoi(ctx.value.c_str()) && g_settings.all_cpu_usage) {
    notify("notify.overlay.disable_cpu_first");
    return OnPressResult::EarlyReturn;
  }
  g_settings.overlay_cpu = !g_settings.overlay_cpu;
  rebuild_overlay_bar();
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_overlay_fps(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_fps);
}

static OnPressResult id_overlay_ram(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_ram);
}

static OnPressResult id_overlay_ip(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_ip);
}

static OnPressResult id_overlay_fan(OnPressContext &ctx) {
  return toggle_overlay_flag(ctx, g_settings.overlay_fan);
}

static OnPressResult id_all_cpu_usage(OnPressContext &ctx) {
  if (g_settings.all_cpu_usage == atoi(ctx.value.c_str())) {
    return OnPressResult::EarlyReturn;
  }
  if (!g_settings.overlay_cpu) {
    notify("notify.overlay.enable_cpu_first");
    return OnPressResult::EarlyReturn;
  }
  /* Persisted via settings_commit as overlay.cpu_usage_mode in config.ini. */
  g_settings.all_cpu_usage = !g_settings.all_cpu_usage;
  rebuild_overlay_bar();
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_overlay_change_pos(OnPressContext &ctx) {
  if ((overlay_positions)atoi(ctx.value.c_str()) == g_settings.overlay_pos) {
    return OnPressResult::EarlyReturn;
  }
  g_settings.overlay_pos = atoi(ctx.value.c_str());
  rebuild_overlay_bar();
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_overlay_align(OnPressContext &ctx) {
  const int align = atoi(ctx.value.c_str());
  if (align == g_settings.overlay_align) {
    return OnPressResult::EarlyReturn;
  }
  g_settings.overlay_align = align;
  rebuild_overlay_bar();
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_overlay_detail(OnPressContext &ctx) {
  const int detail = atoi(ctx.value.c_str());
  if (detail == g_settings.overlay_detail ||
      !onion::overlay_detail_valid(detail)) {
    return OnPressResult::EarlyReturn;
  }
  g_settings.overlay_detail = detail;
  overlay_scene_reset_state();
  rebuild_overlay_bar();
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_overlay_margin(OnPressContext &ctx) {
  const int px = atoi(ctx.value.c_str());
  if (px == g_settings.overlay_margin || !onion::overlay_margin_valid(px)) {
    return OnPressResult::EarlyReturn;
  }
  /* The overlay re-places itself on its next render tick. */
  g_settings.overlay_margin = px;
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_overlay_font_size(OnPressContext &ctx) {
  const int size = atoi(ctx.value.c_str());
  if (size == g_settings.overlay_font_size) {
    return OnPressResult::EarlyReturn;
  }
  if (size != onion::kOverlayFontSmall && size != onion::kOverlayFontMedium &&
      size != onion::kOverlayFontLarge) {
    return OnPressResult::EarlyReturn;
  }
  g_settings.overlay_font_size = size;
  rebuild_overlay_bar();
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult overlay_set_order(OnPressContext &ctx, int metric) {
  const int position = atoi(ctx.value.c_str());
  const int current =
      onion::overlay_metric_position(g_settings.overlay_order, metric);
  if (position == current) {
    return OnPressResult::EarlyReturn;
  }
  if (position < 1 || position > onion::kOverlayMetricCount) {
    return OnPressResult::EarlyReturn;
  }
  onion::overlay_move_metric(g_settings.overlay_order, metric, position);
  rebuild_overlay_bar();
  ctx.reload_main = true;
  return OnPressResult::Handled;
}

static OnPressResult id_overlay_fps_order(OnPressContext &ctx) {
  return overlay_set_order(ctx, onion::kOverlayMetricFps);
}

static OnPressResult id_overlay_cpu_order(OnPressContext &ctx) {
  return overlay_set_order(ctx, onion::kOverlayMetricCpu);
}

static OnPressResult id_overlay_gpu_order(OnPressContext &ctx) {
  return overlay_set_order(ctx, onion::kOverlayMetricGpu);
}

static OnPressResult id_overlay_ram_order(OnPressContext &ctx) {
  return overlay_set_order(ctx, onion::kOverlayMetricMemory);
}

static OnPressResult id_overlay_ip_order(OnPressContext &ctx) {
  return overlay_set_order(ctx, onion::kOverlayMetricIp);
}

static OnPressResult id_overlay_fan_order(OnPressContext &ctx) {
  return overlay_set_order(ctx, onion::kOverlayMetricFan);
}

static const OnPressExactEntry kExact[] = {
    {"id_overlay_enabled", id_overlay_enabled},
    {"id_overlay_opacity", id_overlay_opacity},
    {"id_overlay_brand", id_overlay_brand},
    {"id_overlay_fps_pill", id_overlay_fps_pill},
    {"id_overlay_system", id_overlay_system},
    {"id_overlay_gpu", id_overlay_gpu},
    {"id_overlay_cpu", id_overlay_cpu},
    {"id_overlay_fps", id_overlay_fps},
    {"id_overlay_ram", id_overlay_ram},
    {"id_overlay_ip", id_overlay_ip},
    {"id_overlay_fan", id_overlay_fan},
    {"id_all_cpu_usage", id_all_cpu_usage},
    {"id_overlay_change_pos", id_overlay_change_pos},
    {"id_overlay_align", id_overlay_align},
    {"id_overlay_detail", id_overlay_detail},
    {"id_overlay_margin", id_overlay_margin},
    {"id_overlay_font_size", id_overlay_font_size},
    {"id_overlay_fps_order", id_overlay_fps_order},
    {"id_overlay_cpu_order", id_overlay_cpu_order},
    {"id_overlay_gpu_order", id_overlay_gpu_order},
    {"id_overlay_ram_order", id_overlay_ram_order},
    {"id_overlay_ip_order", id_overlay_ip_order},
    {"id_overlay_fan_order", id_overlay_fan_order},
};

const OnPressExactEntry *onpress_overlay_exact(size_t *count) {
  *count = sizeof(kExact) / sizeof(kExact[0]);
  return kExact;
}
