#ifndef RELLU_PROTOTYPE_KEY_NAMES_HPP
#define RELLU_PROTOTYPE_KEY_NAMES_HPP
#include <string>
#include <string_view>
#include <Qt>

namespace Rellu {

  /**
   * A key pressed with modifiers, as Adobe XD's key triggers name it, such
   * as Ctrl+Shift+K: the modifiers joined by + before the key, which is a
   * capital letter, a digit, a function key such as F5, a name such as Esc,
   * PgUp and PgDown, or a symbol such as the arrows, the return symbol for
   * Enter and the tab symbol for Tab.
   */
  struct KeyChord {

    /** The key's name, in UTF-8. */
    std::string m_key;

    /** Whether Ctrl is held. */
    bool m_has_control = false;

    /** Whether Alt is held. */
    bool m_has_alt = false;

    /** Whether Shift is held. */
    bool m_has_shift = false;

    /** Whether the Meta key is held. */
    bool m_has_meta = false;

    bool operator ==(const KeyChord&) const = default;
  };

  /**
   * Parses the keys of a key trigger.
   * @param keys The keys, such as Ctrl+Shift+K.
   * @return The chord, whose key is empty if the <i>keys</i> name none.
   */
  KeyChord parse_key_chord(std::string_view keys);

  /**
   * Returns the name of a chord as a key trigger records it, its modifiers
   * in the order Ctrl, Alt, Shift and Meta.
   * @param chord The chord.
   */
  std::string to_string(const KeyChord& chord);

  /**
   * Returns the name Adobe XD gives a key.
   * @param key The key, a Qt::Key.
   * @param text The characters the key produces, in UTF-16.
   * @return The name, or empty for a modifier or a key without a name.
   */
  std::string to_key_name(int key, std::u16string_view text);

  /**
   * Makes the chord of a key press.
   * @param key The key, a Qt::Key.
   * @param modifiers The modifier keys held.
   * @param text The characters the key produces, in UTF-16.
   * @return The chord, whose key is empty for a modifier or a key without
   *         a name.
   */
  KeyChord make_key_chord(
    int key, Qt::KeyboardModifiers modifiers, std::u16string_view text);
}

#endif
