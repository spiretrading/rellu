#ifndef RELLU_IO_XD_VALUE_HPP
#define RELLU_IO_XD_VALUE_HPP
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <QJsonValue>

namespace Rellu {
  struct XdMember;

  /**
   * A JSON value whose object members keep the order they were added in, as
   * the Adobe XD writer produces them. Numbers are doubles and strings are
   * UTF-8.
   */
  class XdValue {
    public:

      /** Lists the types of value. */
      enum class Type : unsigned char {

        /** The null value. */
        NUL,

        /** A boolean. */
        BOOL,

        /** A number. */
        NUMBER,

        /** A string. */
        STRING,

        /** An array of values. */
        ARRAY,

        /** An object of named members. */
        OBJECT
      };

      /** Constructs a null value. */
      XdValue() noexcept = default;

      /**
       * Constructs a boolean.
       * @param value The boolean.
       */
      XdValue(bool value) noexcept;

      /**
       * Constructs a number.
       * @param value The number.
       */
      XdValue(double value) noexcept;

      /**
       * Constructs a number.
       * @param value The number.
       */
      XdValue(int value) noexcept;

      /**
       * Constructs a number.
       * @param value The number.
       */
      XdValue(std::size_t value) noexcept;

      /**
       * Constructs a string.
       * @param value The UTF-8 string.
       */
      XdValue(std::string value) noexcept;

      /**
       * Constructs a string.
       * @param value The UTF-8 string.
       */
      XdValue(const char* value);

      /**
       * Constructs a string.
       * @param value The UTF-8 string.
       */
      XdValue(std::string_view value);

      /** Returns an empty array. */
      static XdValue make_array();

      /** Returns an empty object. */
      static XdValue make_object();

      /** Returns the type of value. */
      Type get_type() const;

      /** Returns <code>true</code> iff this is an object. */
      bool is_object() const;

      /** Returns <code>true</code> iff this is an array. */
      bool is_array() const;

      /** Returns the boolean, or <code>false</code> if this is not one. */
      bool to_bool() const;

      /** Returns the number, or 0 if this is not one. */
      double to_number() const;

      /** Returns the string, or an empty string if this is not one. */
      std::string_view to_string() const;

      /** Returns the elements of an array, empty if this is not one. */
      const std::vector<XdValue>& get_elements() const;

      /** Returns the elements of an array, making this an array if needed. */
      std::vector<XdValue>& get_elements();

      /** Returns the members of an object, empty if this is not one. */
      const std::vector<XdMember>& get_members() const;

      /**
       * Returns the members of an object, making this an object if needed.
       */
      std::vector<XdMember>& get_members();

      /**
       * Finds a member of an object.
       * @param key The member's name.
       * @return The member's value, or <code>nullptr</code> if there is none.
       */
      const XdValue* find(std::string_view key) const;

      /**
       * Finds a member of an object.
       * @param key The member's name.
       * @return The member's value, or <code>nullptr</code> if there is none.
       */
      XdValue* find(std::string_view key);

      /**
       * Sets a member of an object, replacing any member of the same name in
       * place and otherwise appending it, making this an object if needed.
       * @param key The member's name.
       * @param value The member's value.
       * @return The member's value.
       */
      XdValue& set(std::string_view key, XdValue value);

      /**
       * Removes a member of an object.
       * @param key The member's name.
       * @return <code>true</code> iff the member was present.
       */
      bool erase(std::string_view key);

      /**
       * Appends an element to an array, making this an array if needed.
       * @param value The element.
       * @return The element.
       */
      XdValue& append(XdValue value);

      bool operator ==(const XdValue& value) const;

    private:
      std::variant<std::monostate, bool, double, std::string,
        std::vector<XdValue>, std::vector<XdMember>> m_value;
  };

  /** A named member of an object. */
  struct XdMember {

    /** The member's name. */
    std::string m_key;

    /** The member's value. */
    XdValue m_value;

    bool operator ==(const XdMember&) const = default;
  };

  /**
   * Converts a Qt JSON value, keeping the order of the object members Qt
   * provides.
   * @param value The value to convert.
   */
  XdValue to_xd_value(const QJsonValue& value);

  /**
   * Converts a value to a Qt JSON value.
   * @param value The value to convert.
   */
  QJsonValue to_json_value(const XdValue& value);

  /**
   * Formats a number the way JavaScript does, which is how Adobe XD writes
   * numbers: the shortest digits that read back as the same double.
   * @param value The number.
   * @param out The string the formatted number is appended to.
   */
  void append_number(double value, std::string& out);

  /**
   * Writes a value as compact JSON text the way JavaScript's
   * JSON.stringify does.
   * @param value The value to write.
   * @param out The string the text is appended to.
   */
  void append_json(const XdValue& value, std::string& out);

  /**
   * Writes a value as compact JSON text.
   * @param value The value to write.
   * @return The UTF-8 JSON text.
   */
  std::string to_json(const XdValue& value);

  /**
   * Returns a 64-bit hash of a value's content, the same for equal values.
   * @param value The value to hash.
   */
  std::uint64_t hash_value(const XdValue& value);

  inline XdValue::XdValue(bool value) noexcept
    : m_value(value) {}

  inline XdValue::XdValue(double value) noexcept
    : m_value(value) {}

  inline XdValue::XdValue(int value) noexcept
    : m_value(static_cast<double>(value)) {}

  inline XdValue::XdValue(std::size_t value) noexcept
    : m_value(static_cast<double>(value)) {}

  inline XdValue::XdValue(std::string value) noexcept
    : m_value(std::move(value)) {}

  inline XdValue::XdValue(const char* value)
    : m_value(std::string(value)) {}

  inline XdValue::XdValue(std::string_view value)
    : m_value(std::string(value)) {}

  inline XdValue XdValue::make_array() {
    auto value = XdValue();
    value.m_value = std::vector<XdValue>();
    return value;
  }

  inline XdValue XdValue::make_object() {
    auto value = XdValue();
    value.m_value = std::vector<XdMember>();
    return value;
  }

  inline XdValue::Type XdValue::get_type() const {
    return static_cast<Type>(m_value.index());
  }

  inline bool XdValue::is_object() const {
    return get_type() == Type::OBJECT;
  }

  inline bool XdValue::is_array() const {
    return get_type() == Type::ARRAY;
  }
}

#endif
