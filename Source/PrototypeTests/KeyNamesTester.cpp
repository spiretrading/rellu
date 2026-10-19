#include <doctest/doctest.h>
#include "Rellu/Prototype/KeyNames.hpp"

using namespace Rellu;

TEST_SUITE("KeyNames") {
  TEST_CASE("parse") {
    auto chord = parse_key_chord("Ctrl+Shift+K");
    REQUIRE(chord.m_key == "K");
    REQUIRE(chord.m_has_control);
    REQUIRE(chord.m_has_shift);
    REQUIRE(!chord.m_has_alt);
    REQUIRE(!chord.m_has_meta);
    REQUIRE(parse_key_chord("a") == KeyChord("A"));
    REQUIRE(parse_key_chord("PgDown") == KeyChord("PgDown"));
    REQUIRE(parse_key_chord("Ctrl++") == KeyChord("+", true));
    REQUIRE(parse_key_chord("Shift+\xE2\x86\x91") ==
      KeyChord("\xE2\x86\x91", false, false, true));
    REQUIRE(parse_key_chord("").m_key.empty());
  }

  TEST_CASE("to_string") {
    REQUIRE(to_string(KeyChord("K", true, true, true, true)) ==
      "Ctrl+Alt+Shift+Meta+K");
    REQUIRE(to_string(KeyChord("F5")) == "F5");
    REQUIRE(parse_key_chord(to_string(KeyChord("K", true, false, true))) ==
      KeyChord("K", true, false, true));
  }

  TEST_CASE("key_names") {
    REQUIRE(to_key_name(Qt::Key_Return, u"\r") == "\xE2\x86\xA9");
    REQUIRE(to_key_name(Qt::Key_Enter, u"\r") == "\xE2\x86\xA9");
    REQUIRE(to_key_name(Qt::Key_Tab, u"\t") == "\xE2\x87\xA5");
    REQUIRE(to_key_name(Qt::Key_Backspace, u"") == "\xE2\x8C\xAB");
    REQUIRE(to_key_name(Qt::Key_Escape, u"") == "Esc");
    REQUIRE(to_key_name(Qt::Key_PageDown, u"") == "PgDown");
    REQUIRE(to_key_name(Qt::Key_Down, u"") == "\xE2\x86\x93");
    REQUIRE(to_key_name(Qt::Key_F5, u"") == "F5");
    REQUIRE(to_key_name(Qt::Key_A, u"a") == "A");
    REQUIRE(to_key_name(Qt::Key_5, u"5") == "5");
    REQUIRE(to_key_name(Qt::Key_Shift, u"").empty());
    REQUIRE(to_key_name(Qt::Key_Exclam, u"!") == "!");
  }

  TEST_CASE("key_press") {
    REQUIRE(make_key_chord(Qt::Key_K,
      Qt::ControlModifier | Qt::ShiftModifier, u"\x0b") ==
      parse_key_chord("Ctrl+Shift+K"));
    REQUIRE(make_key_chord(Qt::Key_Backtab, Qt::ShiftModifier, u"") ==
      parse_key_chord("Shift+\xE2\x87\xA5"));
  }
}
