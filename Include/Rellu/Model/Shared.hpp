#ifndef RELLU_MODEL_SHARED_HPP
#define RELLU_MODEL_SHARED_HPP
#include <memory>
#include <utility>

namespace Rellu {

  /**
   * An immutable value held through a std::shared_ptr&lt;const T&gt;, so that
   * copying it never copies the value. Two Shared values compare equal when
   * their values do.
   * @param <T> The type of value held.
   */
  template<typename T>
  class Shared {
    public:

      /** The type of value held. */
      using Type = T;

      /** Constructs a Shared holding a default constructed value. */
      Shared();

      /**
       * Constructs a Shared holding a value.
       * @param value The value to hold.
       */
      explicit Shared(T value);

      /**
       * Constructs a Shared from an existing pointer.
       * @param pointer The non-null pointer to the value to hold.
       */
      explicit Shared(std::shared_ptr<const T> pointer) noexcept;

      /** Returns the pointer to the value. */
      const std::shared_ptr<const T>& get_pointer() const;

      const T& operator *() const;
      const T* operator ->() const;
      bool operator ==(const Shared& shared) const;

    private:
      std::shared_ptr<const T> m_pointer;
  };

  template<typename T>
  Shared<T>::Shared()
    : m_pointer(std::make_shared<const T>()) {}

  template<typename T>
  Shared<T>::Shared(T value)
    : m_pointer(std::make_shared<const T>(std::move(value))) {}

  template<typename T>
  Shared<T>::Shared(std::shared_ptr<const T> pointer) noexcept
    : m_pointer(std::move(pointer)) {}

  template<typename T>
  const std::shared_ptr<const T>& Shared<T>::get_pointer() const {
    return m_pointer;
  }

  template<typename T>
  const T& Shared<T>::operator *() const {
    return *m_pointer;
  }

  template<typename T>
  const T* Shared<T>::operator ->() const {
    return m_pointer.get();
  }

  template<typename T>
  bool Shared<T>::operator ==(const Shared& shared) const {
    if(m_pointer == shared.m_pointer) {
      return true;
    } else if(!m_pointer || !shared.m_pointer) {
      return false;
    }
    return *m_pointer == *shared.m_pointer;
  }
}

#endif
