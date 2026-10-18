#ifndef RELLU_IO_XD_OBJECT_HPP
#define RELLU_IO_XD_OBJECT_HPP
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include "Rellu/Io/ImportReport.hpp"

namespace Rellu {
  class XdArray;

  /**
   * A JSON object read by an import, recording which of its keys are
   * consumed. Reading a key through any accessor consumes it, except that an
   * accessor finding a value of the wrong type leaves the key unconsumed. A
   * root object reports every key left unconsumed, in itself and in the
   * objects and arrays read through it, when it is destroyed.
   */
  class XdObject {
    public:

      /**
       * Constructs a root object.
       * @param json The JSON object to read.
       * @param path The normalized path of the object, such as agc or
       *        &lt;shape&gt;.
       * @param report The report receiving the unconsumed keys.
       */
      XdObject(QJsonObject json, std::string path,
        ImportReport& report) noexcept;

      /** Reports the unconsumed keys if this is a root object. */
      ~XdObject();

      /** Returns the JSON object read. */
      const QJsonObject& get_json() const;

      /**
       * Tests whether the object has a key, without consuming it.
       * @param key The key.
       * @return <code>true</code> iff the key is present.
       */
      bool contains(QLatin1StringView key) const;

      /**
       * Consumes a key.
       * @param key The key.
       * @return The key's value, undefined if the key is absent.
       */
      QJsonValue consume(QLatin1StringView key);

      /**
       * Consumes a key.
       * @param key The key.
       * @return The key's value, undefined if the key is absent.
       */
      QJsonValue consume(const QString& key);

      /**
       * Consumes a number.
       * @param key The key.
       * @return The number, or <code>std::nullopt</code> if it is absent or
       *         not a number.
       */
      std::optional<double> get_number(QLatin1StringView key);

      /**
       * Consumes a number.
       * @param key The key.
       * @param fallback The value returned if the number is absent.
       * @return The number, or the <i>fallback</i>.
       */
      double get_number(QLatin1StringView key, double fallback);

      /**
       * Consumes a boolean.
       * @param key The key.
       * @return The boolean, or <code>std::nullopt</code> if it is absent or
       *         not a boolean.
       */
      std::optional<bool> get_bool(QLatin1StringView key);

      /**
       * Consumes a boolean.
       * @param key The key.
       * @param fallback The value returned if the boolean is absent.
       * @return The boolean, or the <i>fallback</i>.
       */
      bool get_bool(QLatin1StringView key, bool fallback);

      /**
       * Consumes a string.
       * @param key The key.
       * @return The string, or <code>std::nullopt</code> if it is absent or
       *         not a string.
       */
      std::optional<QString> get_string(QLatin1StringView key);

      /**
       * Consumes an object, whose own keys are tracked separately.
       * @param key The key.
       * @return The object, or <code>nullptr</code> if it is absent or not
       *         an object, valid as long as this object.
       */
      XdObject* get_object(QLatin1StringView key);

      /**
       * Consumes an object, whose own keys are tracked separately.
       * @param key The key.
       * @return The object, or <code>nullptr</code> if it is absent or not
       *         an object, valid as long as this object.
       */
      XdObject* get_object(const QString& key);

      /**
       * Consumes an array, whose scalar elements count as consumed and whose
       * object elements are tracked once read.
       * @param key The key.
       * @return The array, or <code>nullptr</code> if it is absent or not an
       *         array, valid as long as this object.
       */
      XdArray* get_array(QLatin1StringView key);

      /**
       * Consumes an array, whose scalar elements count as consumed and whose
       * object elements are tracked once read.
       * @param key The key.
       * @return The array, or <code>nullptr</code> if it is absent or not an
       *         array, valid as long as this object.
       */
      XdArray* get_array(const QString& key);

      /**
       * Consumes keys whose values are deliberately not imported.
       * @param keys The keys.
       */
      void ignore(std::initializer_list<QLatin1StringView> keys);

      /** Consumes every key. */
      void consume_all();

    private:
      friend class XdArray;
      struct Child {
        qsizetype m_index;
        std::unique_ptr<XdObject> m_object;
        std::unique_ptr<XdArray> m_array;
      };
      QJsonObject m_json;
      std::string m_path;
      ImportReport* m_report;
      std::uint64_t m_mask;
      std::vector<bool> m_overflow;
      bool m_is_consumed;
      std::vector<Child> m_children;

      explicit XdObject(QJsonObject json) noexcept;
      XdObject(const XdObject&) = delete;
      XdObject& operator =(const XdObject&) = delete;
      bool is_consumed(qsizetype index) const;
      void mark(qsizetype index);
      XdObject* get_object(QJsonObject::const_iterator entry);
      XdArray* get_array(QJsonObject::const_iterator entry);
      void flush(const std::string& root,
        std::vector<std::pair<const QJsonObject*, qsizetype>>& segments,
        ImportReport& report) const;
  };

  /**
   * A JSON array read through an XdObject. Its scalar elements count as
   * consumed; its object elements track their keys once read.
   */
  class XdArray {
    public:

      /** Returns the number of elements. */
      std::size_t get_size() const;

      /** Returns the JSON array read. */
      const QJsonArray& get_json() const;

      /**
       * Returns an element holding an object.
       * @param index The element's index.
       * @return The object, or <code>nullptr</code> if the element is not an
       *         object, valid as long as this array.
       */
      XdObject* get_object(std::size_t index);

      /**
       * Returns an element holding an array.
       * @param index The element's index.
       * @return The array, or <code>nullptr</code> if the element is not an
       *         array, valid as long as this array.
       */
      XdArray* get_array(std::size_t index);

    private:
      friend class XdObject;
      QJsonArray m_json;
      std::vector<std::unique_ptr<XdObject>> m_objects;
      std::vector<std::unique_ptr<XdArray>> m_arrays;

      explicit XdArray(QJsonArray json) noexcept;
      XdArray(const XdArray&) = delete;
      XdArray& operator =(const XdArray&) = delete;
      void flush(const std::string& root,
        std::vector<std::pair<const QJsonObject*, qsizetype>>& segments,
        ImportReport& report) const;
  };
}

#endif
