#include <doctest/doctest.h>
#include "Rellu/Text/FontLibrary.hpp"

using namespace Rellu;

namespace {
  const FontLibrary& get_library() {
    static auto library = FontLibrary(get_system_font_directories());
    return library;
  }

  Font make_font(std::string family, std::string style,
      std::string postscript_name = std::string()) {
    return Font(std::move(family), std::move(style),
      std::move(postscript_name), 16);
  }
}

TEST_SUITE("FontLibrary") {
  TEST_CASE("postscript_name") {
    auto resolution =
      get_library().resolve(make_font("Unknown", "Regular", "ArialMT"));
    REQUIRE(resolution.m_match == FontMatch::POSTSCRIPT_NAME);
    REQUIRE(resolution.m_typeface->get_names().m_postscript_name ==
      "ArialMT");
  }

  TEST_CASE("family_and_style") {
    auto resolution = get_library().resolve(make_font("Arial", "Bold"));
    REQUIRE(resolution.m_match == FontMatch::FAMILY_AND_STYLE);
    REQUIRE(resolution.m_typeface->get_names().m_postscript_name ==
      "Arial-BoldMT");
  }

  TEST_CASE("nearest_style") {
    auto resolution = get_library().resolve(make_font("Arial", "SemiBold"));
    REQUIRE(resolution.m_match == FontMatch::FAMILY);
    REQUIRE(resolution.m_typeface->get_names().m_postscript_name ==
      "Arial-BoldMT");
  }

  TEST_CASE("family_as_postscript_name") {
    auto resolution = get_library().resolve(make_font("Arial-BoldMT", ""));
    REQUIRE(resolution.m_match == FontMatch::POSTSCRIPT_NAME);
    REQUIRE(resolution.m_typeface->get_names().m_postscript_name ==
      "Arial-BoldMT");
  }

  TEST_CASE("substitute") {
    auto resolution = get_library().resolve(
      make_font("Rellu Helvetica Neue", "Bold", "RelluHelveticaNeue-Bold"));
    REQUIRE(resolution.m_match == FontMatch::FALLBACK);
    resolution = get_library().resolve(
      make_font("Helvetica Neue", "Bold", "HelveticaNeue-Bold"));
    REQUIRE(resolution.m_match == FontMatch::SUBSTITUTE);
    REQUIRE(resolution.m_typeface->get_names().m_postscript_name ==
      "Arial-BoldMT");
  }

  TEST_CASE("fallback") {
    auto resolution =
      get_library().resolve(make_font("No Such Family", "Regular"));
    REQUIRE(resolution.m_match == FontMatch::FALLBACK);
    REQUIRE(resolution.m_typeface);
  }

  TEST_CASE("empty") {
    auto library = FontLibrary({});
    REQUIRE(library.get_faces().empty());
    auto resolution = library.resolve(make_font("Arial", "Regular"));
    REQUIRE(resolution.m_match == FontMatch::NONE);
    REQUIRE(!resolution.m_typeface);
  }

  TEST_CASE("bundled_fonts") {
    auto resolution = get_default_font_library().resolve(
      make_font("Roboto", "Regular", "Roboto-Regular"));
    REQUIRE(resolution.m_match == FontMatch::POSTSCRIPT_NAME);
    REQUIRE(resolution.m_typeface->get_path().parent_path() ==
      std::filesystem::path(RELLU_FONTS_DIRECTORY));
  }

  TEST_CASE("character_fallback") {
    auto typeface = get_library().find_fallback(U'\u2192');
    REQUIRE(typeface);
    REQUIRE(typeface->has_glyph(U'\u2192'));
  }
}
