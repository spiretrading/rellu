#include <algorithm>
#include <array>
#include <doctest/doctest.h>
#include "Rellu/Prototype/Easing.hpp"

using namespace Rellu;

namespace {
  const auto EASINGS = std::to_array({Easing::LINEAR, Easing::EASE_IN,
    Easing::EASE_OUT, Easing::EASE_IN_OUT, Easing::WIND_UP, Easing::BOUNCE,
    Easing::SNAP});
}

TEST_SUITE("Easing") {
  TEST_CASE("bezier") {
    REQUIRE(evaluate_bezier(0.25, 0.1, 0.25, 1, 0.5) ==
      doctest::Approx(0.8024034).epsilon(1e-6));
    REQUIRE(evaluate_bezier(0, 0, 1, 1, 0.3) == doctest::Approx(0.3));
    REQUIRE(evaluate_bezier(0.42, 0, 1, 1, 0) == 0);
    REQUIRE(evaluate_bezier(0.42, 0, 1, 1, 1) == 1);
  }

  TEST_CASE("end_points") {
    for(auto easing : EASINGS) {
      REQUIRE(ease(easing, 0) == 0);
      REQUIRE(ease(easing, 1) == 1);
      REQUIRE(ease(easing, -1) == 0);
      REQUIRE(ease(easing, 2) == 1);
    }
  }

  TEST_CASE("linear") {
    REQUIRE(ease(Easing::LINEAR, 0.25) == doctest::Approx(0.25));
    REQUIRE(ease(Easing::LINEAR, 0.75) == doctest::Approx(0.75));
  }

  TEST_CASE("curves") {
    REQUIRE(ease(Easing::EASE_IN, 0.5) == doctest::Approx(0.125));
    REQUIRE(ease(Easing::EASE_OUT, 0.5) == doctest::Approx(0.875));
    REQUIRE(ease(Easing::EASE_OUT, 0.25) == doctest::Approx(0.578125));
    REQUIRE(ease(Easing::EASE_IN_OUT, 0.25) == doctest::Approx(0.0625));
    REQUIRE(ease(Easing::BOUNCE, 0.5) == doctest::Approx(0.765625));
    REQUIRE(ease(Easing::SNAP, 0.6) == doctest::Approx(1.02).epsilon(0.01));
    REQUIRE(ease(Easing::EASE_IN_OUT, 0.5) == doctest::Approx(0.5));
    REQUIRE(ease(Easing::EASE_IN_OUT, 0.25) ==
      doctest::Approx(1 - ease(Easing::EASE_IN_OUT, 0.75)));
    for(auto easing :
        {Easing::EASE_IN, Easing::EASE_OUT, Easing::EASE_IN_OUT}) {
      auto previous = 0.0;
      for(auto i = 1; i <= 100; ++i) {
        auto value = ease(easing, i / 100.0);
        REQUIRE(value >= previous);
        previous = value;
      }
    }
  }

  TEST_CASE("overshoot") {
    auto lowest = 1.0;
    auto bounce_range = true;
    auto highest = 0.0;
    for(auto i = 1; i < 100; ++i) {
      auto time = i / 100.0;
      lowest = std::min(lowest, ease(Easing::WIND_UP, time));
      highest = std::max(highest, ease(Easing::SNAP, time));
      auto bounce = ease(Easing::BOUNCE, time);
      bounce_range = bounce_range && bounce >= 0 && bounce <= 1;
    }
    REQUIRE(lowest < 0);
    REQUIRE(highest > 1);
    REQUIRE(bounce_range);
  }
}
