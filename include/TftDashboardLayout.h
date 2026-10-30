#pragma once

// The installed ST7796S uses a 320 x 480 portrait viewport.
namespace tft_dashboard_layout {
struct Rect {
  int x, y, width, height;
  constexpr int right() const { return x + width; }
  constexpr int bottom() const { return y + height; }
  constexpr bool inside() const {
    return x >= 0 && y >= 0 && right() <= 320 && bottom() <= 480;
  }
};
constexpr Rect header{8, 8, 304, 44};
constexpr Rect boiler{8, 58, 149, 148};
constexpr Rect solar{163, 58, 149, 148};
constexpr Rect relay{8, 212, 149, 94};
constexpr Rect led_valve{163, 212, 149, 94};
constexpr Rect controller{8, 312, 304, 73};
constexpr Rect footer{8, 391, 304, 81};
constexpr bool separate(Rect a, Rect b) {
  return a.right() <= b.x || b.right() <= a.x ||
         a.bottom() <= b.y || b.bottom() <= a.y;
}
static_assert(header.inside() && boiler.inside() && solar.inside() &&
              relay.inside() && led_valve.inside() && controller.inside() &&
              footer.inside(), "TFT panels must fit the 320 x 480 viewport");
static_assert(separate(header, boiler) && separate(boiler, solar) &&
              separate(boiler, relay) && separate(solar, led_valve) &&
              separate(relay, led_valve) && separate(relay, controller) &&
              separate(controller, footer), "TFT panels must not overlap");
} // namespace tft_dashboard_layout
