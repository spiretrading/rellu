#include <algorithm>
#include <cmath>
#include "Rellu/Prototype/Easing.hpp"

using namespace Rellu;

namespace {
  double evaluate_cubic(double first, double second, double u) {
    auto v = 1 - u;
    return 3 * v * v * u * first + 3 * v * u * u * second + u * u * u;
  }

  double evaluate_slope(double first, double second, double u) {
    auto v = 1 - u;
    return 3 * v * v * first + 6 * v * u * (second - first) +
      3 * u * u * (1 - second);
  }

  double bounce(double time) {
    constexpr auto STRENGTH = 7.5625;
    constexpr auto SPAN = 2.75;
    if(time < 1 / SPAN) {
      return STRENGTH * time * time;
    } else if(time < 2 / SPAN) {
      time -= 1.5 / SPAN;
      return STRENGTH * time * time + 0.75;
    } else if(time < 2.5 / SPAN) {
      time -= 2.25 / SPAN;
      return STRENGTH * time * time + 0.9375;
    }
    time -= 2.625 / SPAN;
    return STRENGTH * time * time + 0.984375;
  }
}

double Rellu::evaluate_bezier(
    double x1, double y1, double x2, double y2, double time) {
  if(time <= 0) {
    return 0;
  } else if(time >= 1) {
    return 1;
  }
  auto u = time;
  for(auto i = 0; i != 8; ++i) {
    auto error = evaluate_cubic(x1, x2, u) - time;
    if(std::abs(error) < 1e-9) {
      return evaluate_cubic(y1, y2, u);
    }
    auto slope = evaluate_slope(x1, x2, u);
    if(std::abs(slope) < 1e-9) {
      break;
    }
    u -= error / slope;
  }
  auto low = 0.0;
  auto high = 1.0;
  u = time;
  for(auto i = 0; i != 64; ++i) {
    auto x = evaluate_cubic(x1, x2, u);
    if(std::abs(x - time) < 1e-9) {
      break;
    } else if(x < time) {
      low = u;
    } else {
      high = u;
    }
    u = (low + high) / 2;
  }
  return evaluate_cubic(y1, y2, u);
}

double Rellu::ease(Easing easing, double time) {
  time = std::clamp(time, 0.0, 1.0);
  if(time == 0 || time == 1) {
    return time;
  }
  switch(easing) {
    case Easing::EASE_IN:
      return time * time * time;
    case Easing::EASE_OUT:
      return 1 - (1 - time) * (1 - time) * (1 - time);
    case Easing::EASE_IN_OUT:
      if(time < 0.5) {
        return 4 * time * time * time;
      }
      return 1 - 4 * (1 - time) * (1 - time) * (1 - time);
    case Easing::WIND_UP:
      return evaluate_bezier(0.564, -0.205, 0.676, 0.275, time);
    case Easing::BOUNCE:
      return bounce(time);
    case Easing::SNAP:
      return evaluate_bezier(0.424, 1.272, 0.354, 0.996, time);
    default:
      return time;
  }
}
