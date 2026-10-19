#include <array>
#include <cctype>
#include <utility>
#include <QString>
#include "Rellu/Prototype/KeyNames.hpp"

using namespace Rellu;

namespace {
  const auto RETURN = std::string("\xE2\x86\xA9");
  const auto TAB = std::string("\xE2\x87\xA5");
  const auto SPACE = std::string("\xE2\x90\xA3");
  const auto BACKSPACE = std::string("\xE2\x8C\xAB");
  const auto DELETE_KEY = std::string("\xE2\x8C\xA6");
  const auto CAPS_LOCK = std::string("\xE2\x87\xAA");
  const auto DOWN = std::string("\xE2\x86\x93");
  const auto LEFT = std::string("\xE2\x86\x90");
  const auto RIGHT = std::string("\xE2\x86\x92");
  const auto UP = std::string("\xE2\x86\x91");

  bool consume(std::string_view& keys, std::string_view prefix) {
    if(keys.size() > prefix.size() && keys.starts_with(prefix)) {
      keys.remove_prefix(prefix.size());
      return true;
    }
    return false;
  }
}

KeyChord Rellu::parse_key_chord(std::string_view keys) {
  auto chord = KeyChord();
  while(true) {
    if(consume(keys, "Ctrl+")) {
      chord.m_has_control = true;
    } else if(consume(keys, "Alt+")) {
      chord.m_has_alt = true;
    } else if(consume(keys, "Shift+")) {
      chord.m_has_shift = true;
    } else if(consume(keys, "Meta+") || consume(keys, "Cmd+")) {
      chord.m_has_meta = true;
    } else {
      break;
    }
  }
  chord.m_key = std::string(keys);
  if(chord.m_key.size() == 1) {
    chord.m_key[0] = static_cast<char>(
      std::toupper(static_cast<unsigned char>(chord.m_key[0])));
  }
  return chord;
}

std::string Rellu::to_string(const KeyChord& chord) {
  auto name = std::string();
  if(chord.m_has_control) {
    name += "Ctrl+";
  }
  if(chord.m_has_alt) {
    name += "Alt+";
  }
  if(chord.m_has_shift) {
    name += "Shift+";
  }
  if(chord.m_has_meta) {
    name += "Meta+";
  }
  return name + chord.m_key;
}

std::string Rellu::to_key_name(int key, std::u16string_view text) {
  switch(key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Meta:
    case Qt::Key_unknown:
      return std::string();
    case Qt::Key_Return:
    case Qt::Key_Enter:
      return RETURN;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
      return TAB;
    case Qt::Key_Space:
      return SPACE;
    case Qt::Key_Backspace:
      return BACKSPACE;
    case Qt::Key_Delete:
      return DELETE_KEY;
    case Qt::Key_Insert:
      return "Ins";
    case Qt::Key_Escape:
      return "Esc";
    case Qt::Key_CapsLock:
      return CAPS_LOCK;
    case Qt::Key_Home:
      return "Home";
    case Qt::Key_End:
      return "End";
    case Qt::Key_PageUp:
      return "PgUp";
    case Qt::Key_PageDown:
      return "PgDown";
    case Qt::Key_Down:
      return DOWN;
    case Qt::Key_Left:
      return LEFT;
    case Qt::Key_Right:
      return RIGHT;
    case Qt::Key_Up:
      return UP;
    default:
      break;
  }
  if(key >= Qt::Key_F1 && key <= Qt::Key_F35) {
    return "F" + std::to_string(key - Qt::Key_F1 + 1);
  } else if(key >= Qt::Key_A && key <= Qt::Key_Z) {
    return std::string(1, static_cast<char>('A' + (key - Qt::Key_A)));
  } else if(key >= Qt::Key_0 && key <= Qt::Key_9) {
    return std::string(1, static_cast<char>('0' + (key - Qt::Key_0)));
  }
  auto characters = QString::fromStdU16String(std::u16string(text));
  if(characters.isEmpty() || !characters.at(0).isPrint()) {
    return std::string();
  }
  return characters.toUpper().toStdString();
}

KeyChord Rellu::make_key_chord(
    int key, Qt::KeyboardModifiers modifiers, std::u16string_view text) {
  auto chord = KeyChord();
  chord.m_key = to_key_name(key, text);
  chord.m_has_control = modifiers.testFlag(Qt::ControlModifier);
  chord.m_has_alt = modifiers.testFlag(Qt::AltModifier);
  chord.m_has_shift = modifiers.testFlag(Qt::ShiftModifier);
  chord.m_has_meta = modifiers.testFlag(Qt::MetaModifier);
  return chord;
}
