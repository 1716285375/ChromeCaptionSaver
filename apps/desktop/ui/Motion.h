#pragma once
#include "imgui.h"
#include <algorithm>
#include <cmath>
namespace ccs::ui {
inline float Approach(float value, float target, float dt, float speed = 18) {
  return value +
         (target - value) * (1 - std::exp(-speed * std::clamp(dt, 0.f, .1f)));
}
struct Motion {
  bool reduced = false;
  float page = 1, caption = 1, toast = 0;
  void Tick(bool changed, bool new_caption, bool has_toast) {
    if (changed)
      page = 0;
    if (new_caption)
      caption = 0;
    const auto dt = ImGui::GetIO().DeltaTime;
    page = reduced ? 1 : Approach(page, 1, dt);
    caption = reduced ? 1 : Approach(caption, 1, dt);
    toast = reduced ? (has_toast ? 1.f : 0.f)
                    : Approach(toast, has_toast ? 1.f : 0.f, dt);
  }
};
} // namespace ccs::ui
