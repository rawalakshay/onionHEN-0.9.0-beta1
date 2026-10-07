/* Copyright (C) 2025 OnionHEN
 *
 * Mono side of the "rows" overlay style: reconciles an overlay_rows.hpp Scene
 * into UI2.Panel / UI2.Label widgets on the Game scene RootWidget. Widgets are
 * created on first use, updated only when a property changed, moved off-screen
 * when a frame no longer mentions them, and destroyed by remove_all().
 * UI thread only.
 */
#pragma once

#include "overlay_rows.hpp"

/** Apply one frame. Returns false if the RootWidget is not available. */
bool overlay_rows_apply(const onion::overlay::rows::Scene &scene);

/** Destroy every rows widget (style switch, overlay off, font change). */
void overlay_rows_remove_all();

/** Drop trend history and FPS session stats (prx_overlay.cpp), so switching
 *  back to the rows style does not show stale points across the gap. */
void overlay_rows_reset_state();
