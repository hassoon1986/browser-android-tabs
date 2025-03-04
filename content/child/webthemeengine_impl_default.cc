// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/child/webthemeengine_impl_default.h"

#include "build/build_config.h"
#include "skia/ext/platform_canvas.h"
#include "third_party/blink/public/platform/web_rect.h"
#include "third_party/blink/public/platform/web_size.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/overlay_scrollbar_constants_aura.h"

using blink::WebColorScheme;
using blink::WebRect;
using blink::WebScrollbarOverlayColorTheme;
using blink::WebThemeEngine;

namespace content {
namespace {

#if defined(OS_WIN)
// The width of a vertical scroll bar in dips.
int32_t g_vertical_scroll_bar_width;

// The height of a horizontal scroll bar in dips.
int32_t g_horizontal_scroll_bar_height;

// The height of the arrow bitmap on a vertical scroll bar in dips.
int32_t g_vertical_arrow_bitmap_height;

// The width of the arrow bitmap on a horizontal scroll bar in dips.
int32_t g_horizontal_arrow_bitmap_width;
#endif

}  // namespace

// TODO(https://crbug.com/988434): The mapping functions below are duplicated
// inside Blink and in the Android implementation of WebThemeEngine. They should
// be implemented in one place where dependencies between Blink and
// ui::NativeTheme make sense.
static ui::NativeTheme::Part NativeThemePart(
    WebThemeEngine::Part part) {
  switch (part) {
    case WebThemeEngine::kPartScrollbarDownArrow:
      return ui::NativeTheme::kScrollbarDownArrow;
    case WebThemeEngine::kPartScrollbarLeftArrow:
      return ui::NativeTheme::kScrollbarLeftArrow;
    case WebThemeEngine::kPartScrollbarRightArrow:
      return ui::NativeTheme::kScrollbarRightArrow;
    case WebThemeEngine::kPartScrollbarUpArrow:
      return ui::NativeTheme::kScrollbarUpArrow;
    case WebThemeEngine::kPartScrollbarHorizontalThumb:
      return ui::NativeTheme::kScrollbarHorizontalThumb;
    case WebThemeEngine::kPartScrollbarVerticalThumb:
      return ui::NativeTheme::kScrollbarVerticalThumb;
    case WebThemeEngine::kPartScrollbarHorizontalTrack:
      return ui::NativeTheme::kScrollbarHorizontalTrack;
    case WebThemeEngine::kPartScrollbarVerticalTrack:
      return ui::NativeTheme::kScrollbarVerticalTrack;
    case WebThemeEngine::kPartScrollbarCorner:
      return ui::NativeTheme::kScrollbarCorner;
    case WebThemeEngine::kPartCheckbox:
      return ui::NativeTheme::kCheckbox;
    case WebThemeEngine::kPartRadio:
      return ui::NativeTheme::kRadio;
    case WebThemeEngine::kPartButton:
      return ui::NativeTheme::kPushButton;
    case WebThemeEngine::kPartTextField:
      return ui::NativeTheme::kTextField;
    case WebThemeEngine::kPartMenuList:
      return ui::NativeTheme::kMenuList;
    case WebThemeEngine::kPartSliderTrack:
      return ui::NativeTheme::kSliderTrack;
    case WebThemeEngine::kPartSliderThumb:
      return ui::NativeTheme::kSliderThumb;
    case WebThemeEngine::kPartInnerSpinButton:
      return ui::NativeTheme::kInnerSpinButton;
    case WebThemeEngine::kPartProgressBar:
      return ui::NativeTheme::kProgressBar;
    default:
      return ui::NativeTheme::kScrollbarDownArrow;
  }
}

static ui::NativeTheme::ScrollbarOverlayColorTheme
NativeThemeScrollbarOverlayColorTheme(WebScrollbarOverlayColorTheme theme) {
  switch (theme) {
    case WebScrollbarOverlayColorTheme::kWebScrollbarOverlayColorThemeLight:
      return ui::NativeTheme::ScrollbarOverlayColorThemeLight;
    case WebScrollbarOverlayColorTheme::kWebScrollbarOverlayColorThemeDark:
      return ui::NativeTheme::ScrollbarOverlayColorThemeDark;
    default:
      return ui::NativeTheme::ScrollbarOverlayColorThemeDark;
  }
}

static ui::NativeTheme::State NativeThemeState(
    WebThemeEngine::State state) {
  switch (state) {
    case WebThemeEngine::kStateDisabled:
      return ui::NativeTheme::kDisabled;
    case WebThemeEngine::kStateHover:
      return ui::NativeTheme::kHovered;
    case WebThemeEngine::kStateNormal:
      return ui::NativeTheme::kNormal;
    case WebThemeEngine::kStatePressed:
      return ui::NativeTheme::kPressed;
    default:
      return ui::NativeTheme::kDisabled;
  }
}

static ui::NativeTheme::ColorScheme NativeColorScheme(
    WebColorScheme color_scheme) {
  switch (color_scheme) {
    case WebColorScheme::kLight:
      return ui::NativeTheme::ColorScheme::kLight;
    case WebColorScheme::kDark:
      return ui::NativeTheme::ColorScheme::kDark;
  }
}

static void GetNativeThemeExtraParams(
    WebThemeEngine::Part part,
    WebThemeEngine::State state,
    const WebThemeEngine::ExtraParams* extra_params,
    ui::NativeTheme::ExtraParams* native_theme_extra_params) {
  if (!extra_params)
    return;

  switch (part) {
    case WebThemeEngine::kPartScrollbarHorizontalTrack:
    case WebThemeEngine::kPartScrollbarVerticalTrack:
      native_theme_extra_params->scrollbar_track.track_x =
          extra_params->scrollbar_track.track_x;
      native_theme_extra_params->scrollbar_track.track_y =
          extra_params->scrollbar_track.track_y;
      native_theme_extra_params->scrollbar_track.track_width =
          extra_params->scrollbar_track.track_width;
      native_theme_extra_params->scrollbar_track.track_height =
          extra_params->scrollbar_track.track_height;
      break;
    case WebThemeEngine::kPartCheckbox:
      native_theme_extra_params->button.checked = extra_params->button.checked;
      native_theme_extra_params->button.indeterminate =
          extra_params->button.indeterminate;
      break;
    case WebThemeEngine::kPartRadio:
      native_theme_extra_params->button.checked = extra_params->button.checked;
      break;
    case WebThemeEngine::kPartButton:
      native_theme_extra_params->button.has_border =
          extra_params->button.has_border;
      // Native buttons have a different focus style.
      native_theme_extra_params->button.is_focused = false;
      native_theme_extra_params->button.background_color =
          extra_params->button.background_color;
      break;
    case WebThemeEngine::kPartTextField:
      native_theme_extra_params->text_field.is_text_area =
          extra_params->text_field.is_text_area;
      native_theme_extra_params->text_field.is_listbox =
          extra_params->text_field.is_listbox;
      native_theme_extra_params->text_field.background_color =
          extra_params->text_field.background_color;
      break;
    case WebThemeEngine::kPartMenuList:
      native_theme_extra_params->menu_list.has_border =
          extra_params->menu_list.has_border;
      native_theme_extra_params->menu_list.has_border_radius =
          extra_params->menu_list.has_border_radius;
      native_theme_extra_params->menu_list.arrow_x =
          extra_params->menu_list.arrow_x;
      native_theme_extra_params->menu_list.arrow_y =
          extra_params->menu_list.arrow_y;
      native_theme_extra_params->menu_list.arrow_size =
          extra_params->menu_list.arrow_size;
      native_theme_extra_params->menu_list.arrow_color =
          extra_params->menu_list.arrow_color;
      native_theme_extra_params->menu_list.background_color =
          extra_params->menu_list.background_color;
      break;
    case WebThemeEngine::kPartSliderTrack:
      native_theme_extra_params->slider.thumb_x = extra_params->slider.thumb_x;
      native_theme_extra_params->slider.thumb_y = extra_params->slider.thumb_y;
      FALLTHROUGH;
      // vertical and in_drag properties are used by both slider track and
      // slider thumb.
    case WebThemeEngine::kPartSliderThumb:
      native_theme_extra_params->slider.vertical =
          extra_params->slider.vertical;
      native_theme_extra_params->slider.in_drag = extra_params->slider.in_drag;
      break;
    case WebThemeEngine::kPartInnerSpinButton:
      native_theme_extra_params->inner_spin.spin_up =
          extra_params->inner_spin.spin_up;
      native_theme_extra_params->inner_spin.read_only =
          extra_params->inner_spin.read_only;
      break;
    case WebThemeEngine::kPartProgressBar:
      native_theme_extra_params->progress_bar.determinate =
          extra_params->progress_bar.determinate;
      native_theme_extra_params->progress_bar.value_rect_x =
          extra_params->progress_bar.value_rect_x;
      native_theme_extra_params->progress_bar.value_rect_y =
          extra_params->progress_bar.value_rect_y;
      native_theme_extra_params->progress_bar.value_rect_width =
          extra_params->progress_bar.value_rect_width;
      native_theme_extra_params->progress_bar.value_rect_height =
          extra_params->progress_bar.value_rect_height;
      break;
    case WebThemeEngine::kPartScrollbarHorizontalThumb:
    case WebThemeEngine::kPartScrollbarVerticalThumb:
      native_theme_extra_params->scrollbar_thumb.scrollbar_theme =
          NativeThemeScrollbarOverlayColorTheme(
              extra_params->scrollbar_thumb.scrollbar_theme);
      break;
    case WebThemeEngine::kPartScrollbarDownArrow:
    case WebThemeEngine::kPartScrollbarLeftArrow:
    case WebThemeEngine::kPartScrollbarRightArrow:
    case WebThemeEngine::kPartScrollbarUpArrow:
      native_theme_extra_params->scrollbar_arrow.zoom =
          extra_params->scrollbar_button.zoom;
      native_theme_extra_params->scrollbar_arrow.right_to_left =
          extra_params->scrollbar_button.right_to_left;
      break;
    default:
      break;  // Parts that have no extra params get here.
  }
}

WebThemeEngineDefault::~WebThemeEngineDefault() = default;

blink::WebSize WebThemeEngineDefault::GetSize(WebThemeEngine::Part part) {
  ui::NativeTheme::ExtraParams extra;
  ui::NativeTheme::Part native_theme_part = NativeThemePart(part);
#if defined(OS_WIN)
  switch (native_theme_part) {
    case ui::NativeTheme::kScrollbarDownArrow:
    case ui::NativeTheme::kScrollbarLeftArrow:
    case ui::NativeTheme::kScrollbarRightArrow:
    case ui::NativeTheme::kScrollbarUpArrow:
    case ui::NativeTheme::kScrollbarHorizontalThumb:
    case ui::NativeTheme::kScrollbarVerticalThumb:
    case ui::NativeTheme::kScrollbarHorizontalTrack:
    case ui::NativeTheme::kScrollbarVerticalTrack: {
      return gfx::Size(g_vertical_scroll_bar_width,
                       g_vertical_scroll_bar_width);
    }

    default:
      break;
  }
#endif
  return ui::NativeTheme::GetInstanceForWeb()->GetPartSize(
      native_theme_part, ui::NativeTheme::kNormal, extra);
}

void WebThemeEngineDefault::Paint(
    cc::PaintCanvas* canvas,
    WebThemeEngine::Part part,
    WebThemeEngine::State state,
    const blink::WebRect& rect,
    const WebThemeEngine::ExtraParams* extra_params,
    blink::WebColorScheme color_scheme) {
  ui::NativeTheme::ExtraParams native_theme_extra_params;
  GetNativeThemeExtraParams(
      part, state, extra_params, &native_theme_extra_params);
  ui::NativeTheme::GetInstanceForWeb()->Paint(
      canvas, NativeThemePart(part), NativeThemeState(state), gfx::Rect(rect),
      native_theme_extra_params, NativeColorScheme(color_scheme));
}

void WebThemeEngineDefault::GetOverlayScrollbarStyle(ScrollbarStyle* style) {
  style->fade_out_delay = ui::kOverlayScrollbarFadeDelay;
  style->fade_out_duration = ui::kOverlayScrollbarFadeDuration;
  // The other fields in this struct are used only on Android to draw solid
  // color scrollbars. On other platforms the scrollbars are painted in
  // NativeTheme so these fields are unused.
}

bool WebThemeEngineDefault::SupportsNinePatch(Part part) const {
  return ui::NativeTheme::GetInstanceForWeb()->SupportsNinePatch(
      NativeThemePart(part));
}

blink::WebSize WebThemeEngineDefault::NinePatchCanvasSize(Part part) const {
  return ui::NativeTheme::GetInstanceForWeb()->GetNinePatchCanvasSize(
      NativeThemePart(part));
}

blink::WebRect WebThemeEngineDefault::NinePatchAperture(Part part) const {
  return ui::NativeTheme::GetInstanceForWeb()->GetNinePatchAperture(
      NativeThemePart(part));
}

#if defined(OS_WIN)
// static
void WebThemeEngineDefault::cacheScrollBarMetrics(
    int32_t vertical_scroll_bar_width,
    int32_t horizontal_scroll_bar_height,
    int32_t vertical_arrow_bitmap_height,
    int32_t horizontal_arrow_bitmap_width) {
  g_vertical_scroll_bar_width = vertical_scroll_bar_width;
  g_horizontal_scroll_bar_height = horizontal_scroll_bar_height;
  g_vertical_arrow_bitmap_height = vertical_arrow_bitmap_height;
  g_horizontal_arrow_bitmap_width = horizontal_arrow_bitmap_width;
}
#endif

}  // namespace content
