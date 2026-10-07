/* Copyright (C) 2025 OnionHEN — "rows" overlay style widget reconciler. */

#include "overlay_rows_renderer.hpp"

#include "external_symbols.hpp"
#include "hooked_funcs.hpp"
#include "ipc.hpp"

#include <string>
#include <unordered_map>

extern MonoObject *Game;

MonoObject *CreateLabel(const char *name, float x, float y, const char *text,
                        MonoObject *fontObj, int horzAlign, int vertAlign,
                        float r, float g, float b, float a);
void Widget_Append_Child(MonoObject *widget, MonoObject *child);
MonoObject *CreateUIFont(int size, int style, int weight);
MonoObject *CreateUIColor(float r, float g, float b, float a);

namespace {

using onion::overlay::rows::Font;
using onion::overlay::rows::Rect;
using onion::overlay::rows::Rgba;
using onion::overlay::rows::Scene;
using onion::overlay::rows::Text;
using onion::overlay::rows::TextAlign;

constexpr float kOff = onion::overlay::kOffscreen;
constexpr int kFontStyleBold = 1;
constexpr int kAlignTop = 0;

struct Widget {
  MonoObject *obj = nullptr;   /* Panel (rect) or transparent cell (text) */
  MonoObject *label = nullptr; /* text only */
  uint32_t obj_handle = 0;
  uint32_t label_handle = 0;
  float x = kOff, y = 0.0f, w = -1.0f, h = -1.0f, margin_top = -1.0f;
  Rgba color{-1.0f, -1.0f, -1.0f, -1.0f};
  std::string text;
  bool seen = false;
};

/* Pinned gchandles keep every cached widget alive and unmoved while native
 * code holds the raw pointer. */
std::unordered_map<std::string, Widget> g_widgets;

/* Two full-screen transparent containers fix the z-order once: anything in
 * the rect layer draws under anything in the text layer, however late it was
 * created (trend columns filling in, a bar appearing, FPS rows returning). */
struct Layer {
  MonoObject *obj = nullptr;
  uint32_t handle = 0;
};
Layer g_rect_layer;
Layer g_text_layer;
/* RootWidget the layers hang off; a different root drops the cache. */
MonoObject *g_layer_root = nullptr;

MonoClass *widget_class() {
  static MonoClass *cls =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
  return cls;
}

MonoClass *panel_class() {
  static MonoClass *cls =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Panel");
  return cls;
}

MonoClass *label_class() {
  static MonoClass *cls =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");
  return cls;
}

MonoObject *find_root_widget() {
  if (!Game)
    return nullptr;
  return Get_Property<MonoObject *>(pui_img, "Sce.PlayStation.PUI.UI2", "Scene",
                                    Game, "RootWidget");
}

/* 0 = not pinned; the object must not be cached then. */
uint32_t pin(MonoObject *obj) {
  return (obj && mono_gchandle_new) ? mono_gchandle_new(obj, /*pinned=*/1) : 0;
}

void unpin(uint32_t handle) {
  if (handle && mono_gchandle_free)
    mono_gchandle_free(handle);
}

bool same_color(const Rgba &a, const Rgba &b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

int font_pt(const Scene &scene, Font font) {
  switch (font) {
  case Font::Brand:
    return scene.brand_pt;
  case Font::Label:
    return scene.label_pt;
  case Font::Value:
  default:
    return scene.value_pt;
  }
}

int font_weight(Font font) { return font == Font::Value ? 700 : 800; }

/* Shared by rects and text cells: position + size of a Panel. */
void place_panel(Widget &w, float x, float y, float width, float height) {
  MonoClass *cls = panel_class();
  if (w.x != x)
    Set_Property(cls, w.obj, "X", x);
  if (w.y != y)
    Set_Property(cls, w.obj, "Y", y);
  if (w.w != width)
    Set_Property(cls, w.obj, "Width", width);
  if (w.h != height)
    Set_Property(cls, w.obj, "Height", height);
}

MonoObject *new_panel(const std::string &name, bool background) {
  MonoClass *cls = panel_class();
  if (!cls)
    return nullptr;
  MonoObject *panel = New_Object(cls);
  if (!panel)
    return nullptr;
  mono_runtime_object_init(panel);
  Set_Property(cls, panel, "Name", mono_string_new(Root_Domain, name.c_str()));
  Set_Property(cls, panel, "BackgroundVisibility", background);
  if (background) {
    Set_Property(cls, panel, "BackgroundOpacity", 1.0f);
    Set_Property(cls, panel, "BackgroundStyle", 1);
  }
  return panel;
}

Widget *create_rect(MonoObject *layer, const Rect &r) {
  MonoObject *panel = new_panel(r.id, true);
  if (!panel)
    return nullptr;
  const uint32_t handle = pin(panel);
  if (!handle)
    return nullptr;
  Widget_Append_Child(layer, panel);
  Widget w{};
  w.obj = panel;
  w.obj_handle = handle;
  return &(g_widgets[r.id] = w);
}

Widget *create_text(MonoObject *layer, const Scene &scene, const Text &t) {
  /* Font first: a failure here must not strand a fresh cell every tick. */
  MonoObject *font =
      CreateUIFont(font_pt(scene, t.font), kFontStyleBold, font_weight(t.font));
  if (!font)
    return nullptr;
  MonoObject *cell = new_panel(t.id + "_cell", false);
  if (!cell)
    return nullptr;
  const int halign = t.align == TextAlign::Right
                         ? onion::overlay::rows::kHAlignRight
                         : onion::overlay::rows::kHAlignLeft;
  MonoObject *label =
      CreateLabel(t.id.c_str(), 0.0f, t.margin_top, t.text.c_str(), font,
                  halign, kAlignTop, t.color.r, t.color.g, t.color.b, t.color.a);
  if (!label)
    return nullptr;
  const uint32_t cell_handle = pin(cell);
  const uint32_t label_handle = pin(label);
  if (!cell_handle || !label_handle) {
    unpin(cell_handle);
    unpin(label_handle);
    return nullptr;
  }
  MonoClass *lbl = label_class();
  Set_Property(lbl, label, "PositionType", 1);
  Set_Property(lbl, label, "MarginLeft", 0.0f);
  Set_Property(lbl, label, "Width", t.w);
  Widget_Append_Child(cell, label);
  Widget_Append_Child(layer, cell);

  Widget w{};
  w.obj = cell;
  w.label = label;
  w.obj_handle = cell_handle;
  w.label_handle = label_handle;
  w.margin_top = t.margin_top;
  w.text = t.text;
  w.color = t.color;
  return &(g_widgets[t.id] = w);
}

void apply_rect(MonoObject *layer, const Rect &r) {
  auto it = g_widgets.find(r.id);
  Widget *w = it != g_widgets.end() ? &it->second : create_rect(layer, r);
  if (!w)
    return;
  w->seen = true;
  place_panel(*w, r.x, r.y, r.w, r.h);
  w->x = r.x;
  w->y = r.y;
  w->w = r.w;
  w->h = r.h;
  if (!same_color(w->color, r.color)) {
    MonoObject *c = CreateUIColor(r.color.r, r.color.g, r.color.b, r.color.a);
    if (c)
      Set_Property_Invoke(panel_class(), w->obj, "BackgroundColor", c);
    w->color = r.color;
  }
}

void apply_text(MonoObject *layer, const Scene &scene, const Text &t) {
  auto it = g_widgets.find(t.id);
  Widget *w =
      it != g_widgets.end() ? &it->second : create_text(layer, scene, t);
  if (!w)
    return;
  w->seen = true;
  MonoClass *lbl = label_class();
  if (w->w != t.w)
    Set_Property(lbl, w->label, "Width", t.w);
  place_panel(*w, t.x, t.y, t.w, t.h);
  w->x = t.x;
  w->y = t.y;
  w->w = t.w;
  w->h = t.h;
  if (w->margin_top != t.margin_top) {
    Set_Property(lbl, w->label, "MarginTop", t.margin_top);
    w->margin_top = t.margin_top;
  }
  if (w->text != t.text) {
    Set_Property(lbl, w->label, "Text",
                 mono_string_new(Root_Domain, t.text.c_str()));
    w->text = t.text;
  }
  if (!same_color(w->color, t.color)) {
    MonoObject *c = CreateUIColor(t.color.r, t.color.g, t.color.b, t.color.a);
    if (c)
      Set_Property_Invoke(lbl, w->label, "TextColor", c);
    w->color = t.color;
  }
}

bool ensure_layer(Layer &layer, MonoObject *root, const char *name,
                  const Scene &scene) {
  if (layer.obj)
    return true;
  MonoObject *panel = new_panel(name, false);
  if (!panel)
    return false;
  const uint32_t handle = pin(panel);
  if (!handle) {
    LOG_ERROR("overlay rows: cannot pin %s; panel disabled", name);
    return false;
  }
  MonoClass *cls = panel_class();
  Set_Property(cls, panel, "X", 0.0f);
  Set_Property(cls, panel, "Y", 0.0f);
  Set_Property(cls, panel, "Width", scene.screen_w);
  Set_Property(cls, panel, "Height", scene.screen_h);
  Widget_Append_Child(root, panel);
  layer.obj = panel;
  layer.handle = handle;
  return true;
}

void release_layer(Layer &layer) {
  if (layer.obj && widget_class())
    Invoke<void>(pui_img, widget_class(), layer.obj, "RemoveFromParent");
  unpin(layer.handle);
  layer = Layer{};
}

} // namespace

bool overlay_rows_apply(const Scene &scene) {
  MonoObject *root = find_root_widget();
  if (!root || !widget_class() || !panel_class() || !label_class())
    return false;

  /* Game scene rebuilt under us: the cached widgets hang off the old root. */
  if (g_layer_root && g_layer_root != root)
    overlay_rows_remove_all();
  if (!ensure_layer(g_rect_layer, root, "id_orow_layer_rects", scene) ||
      !ensure_layer(g_text_layer, root, "id_orow_layer_texts", scene))
    return false;
  g_layer_root = root;

  for (auto &entry : g_widgets)
    entry.second.seen = false;

  for (const Rect &r : scene.rects)
    apply_rect(g_rect_layer.obj, r);
  for (const Text &t : scene.texts)
    apply_text(g_text_layer.obj, scene, t);

  /* Anything this frame did not mention (e.g. FPS detail rows after a game
   * exits, trend columns not yet filled) parks off-screen. */
  for (auto &entry : g_widgets) {
    Widget &w = entry.second;
    if (w.seen || w.x == kOff)
      continue;
    Set_Property(panel_class(), w.obj, "X", kOff);
    w.x = kOff;
  }
  return true;
}

void overlay_rows_remove_all() {
  if (g_widgets.empty() && !g_rect_layer.obj && !g_text_layer.obj)
    return;
  /* Detaching a layer takes all of its children along; they only need their
   * pins released. */
  release_layer(g_text_layer);
  release_layer(g_rect_layer);
  for (auto &entry : g_widgets) {
    unpin(entry.second.label_handle);
    unpin(entry.second.obj_handle);
  }
  LOG_DEBUG("overlay rows: removed %zu widgets", g_widgets.size());
  g_widgets.clear();
  g_layer_root = nullptr;
}
