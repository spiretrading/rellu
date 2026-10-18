#ifndef RELLU_MODEL_BOX_HPP
#define RELLU_MODEL_BOX_HPP
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

namespace Rellu {

  /**
   * A value held on the heap behind a single pointer and shared by copies,
   * so that copying it never copies the value; a copy is made only when
   * a shared value is modified. Two Box compare equal when their values do.
   * @param <T> The type of value held.
   */
  template<typename T>
  class Box {
    public:

      /** The type of value held. */
      using Type = T;

      /** Constructs a Box holding a default constructed value. */
      Box();

      /**
       * Constructs a Box holding a value.
       * @param value The value to hold.
       */
      Box(T value);

      Box(const Box& box) noexcept;

      ~Box();

      /**
       * Returns the value for modification, copying it first if another Box
       * shares it.
       */
      T& edit();

      const T& operator *() const;
      const T* operator ->() const;
      Box& operator =(const Box& box) noexcept;
      bool operator ==(const Box& box) const;

    private:
      struct Block {
        std::atomic<std::size_t> m_count;
        T m_value;
      };
      Block* m_block;
  };

  /**
   * An optional value held on the heap behind a single pointer, null when
   * empty, and shared by copies, so that copying it never copies the value;
   * a copy is made only when a shared value is accessed for modification.
   * Two OptionalBox compare equal when both are empty or their values are
   * equal.
   * @param <T> The type of value held.
   */
  template<typename T>
  class OptionalBox {
    public:

      /** The type of value held. */
      using Type = T;

      /** Constructs an empty OptionalBox. */
      OptionalBox() noexcept;

      /** Constructs an empty OptionalBox. */
      OptionalBox(std::nullopt_t) noexcept;

      /**
       * Constructs an OptionalBox holding a value.
       * @param value The value to hold.
       */
      OptionalBox(T value);

      OptionalBox(const OptionalBox& box) noexcept;
      OptionalBox(OptionalBox&& box) noexcept;

      ~OptionalBox();

      explicit operator bool() const;
      const T& operator *() const;
      T& operator *();
      const T* operator ->() const;
      T* operator ->();
      OptionalBox& operator =(std::nullopt_t) noexcept;
      OptionalBox& operator =(T value);
      OptionalBox& operator =(const OptionalBox& box) noexcept;
      OptionalBox& operator =(OptionalBox&& box) noexcept;
      bool operator ==(const OptionalBox& box) const;
      bool operator ==(std::nullopt_t) const;

    private:
      struct Block {
        std::atomic<std::size_t> m_count;
        T m_value;
      };
      Block* m_block;

      T& edit();
      void release() noexcept;
  };

  template<typename T>
  Box<T>::Box()
    : m_block(new Block(1, T())) {}

  template<typename T>
  Box<T>::Box(T value)
    : m_block(new Block(1, std::move(value))) {}

  template<typename T>
  Box<T>::Box(const Box& box) noexcept
      : m_block(box.m_block) {
    m_block->m_count.fetch_add(1, std::memory_order_relaxed);
  }

  template<typename T>
  Box<T>::~Box() {
    if(m_block->m_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete m_block;
    }
  }

  template<typename T>
  T& Box<T>::edit() {
    if(m_block->m_count.load(std::memory_order_acquire) != 1) {
      *this = Box(m_block->m_value);
    }
    return m_block->m_value;
  }

  template<typename T>
  const T& Box<T>::operator *() const {
    return m_block->m_value;
  }

  template<typename T>
  const T* Box<T>::operator ->() const {
    return &m_block->m_value;
  }

  template<typename T>
  Box<T>& Box<T>::operator =(const Box& box) noexcept {
    box.m_block->m_count.fetch_add(1, std::memory_order_relaxed);
    if(m_block->m_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete m_block;
    }
    m_block = box.m_block;
    return *this;
  }

  template<typename T>
  bool Box<T>::operator ==(const Box& box) const {
    return m_block == box.m_block || m_block->m_value == box.m_block->m_value;
  }

  template<typename T>
  OptionalBox<T>::OptionalBox() noexcept
    : m_block(nullptr) {}

  template<typename T>
  OptionalBox<T>::OptionalBox(std::nullopt_t) noexcept
    : m_block(nullptr) {}

  template<typename T>
  OptionalBox<T>::OptionalBox(T value)
    : m_block(new Block(1, std::move(value))) {}

  template<typename T>
  OptionalBox<T>::OptionalBox(const OptionalBox& box) noexcept
      : m_block(box.m_block) {
    if(m_block) {
      m_block->m_count.fetch_add(1, std::memory_order_relaxed);
    }
  }

  template<typename T>
  OptionalBox<T>::OptionalBox(OptionalBox&& box) noexcept
    : m_block(std::exchange(box.m_block, nullptr)) {}

  template<typename T>
  OptionalBox<T>::~OptionalBox() {
    release();
  }

  template<typename T>
  OptionalBox<T>::operator bool() const {
    return m_block != nullptr;
  }

  template<typename T>
  const T& OptionalBox<T>::operator *() const {
    return m_block->m_value;
  }

  template<typename T>
  T& OptionalBox<T>::operator *() {
    return edit();
  }

  template<typename T>
  const T* OptionalBox<T>::operator ->() const {
    return &m_block->m_value;
  }

  template<typename T>
  T* OptionalBox<T>::operator ->() {
    return &edit();
  }

  template<typename T>
  OptionalBox<T>& OptionalBox<T>::operator =(std::nullopt_t) noexcept {
    release();
    return *this;
  }

  template<typename T>
  OptionalBox<T>& OptionalBox<T>::operator =(T value) {
    auto block = new Block(1, std::move(value));
    release();
    m_block = block;
    return *this;
  }

  template<typename T>
  OptionalBox<T>& OptionalBox<T>::operator =(const OptionalBox& box) noexcept {
    if(box.m_block) {
      box.m_block->m_count.fetch_add(1, std::memory_order_relaxed);
    }
    release();
    m_block = box.m_block;
    return *this;
  }

  template<typename T>
  OptionalBox<T>& OptionalBox<T>::operator =(OptionalBox&& box) noexcept {
    if(this != &box) {
      release();
      m_block = std::exchange(box.m_block, nullptr);
    }
    return *this;
  }

  template<typename T>
  bool OptionalBox<T>::operator ==(const OptionalBox& box) const {
    if(m_block == box.m_block) {
      return true;
    } else if(!m_block || !box.m_block) {
      return false;
    }
    return m_block->m_value == box.m_block->m_value;
  }

  template<typename T>
  bool OptionalBox<T>::operator ==(std::nullopt_t) const {
    return !m_block;
  }

  template<typename T>
  T& OptionalBox<T>::edit() {
    if(m_block->m_count.load(std::memory_order_acquire) != 1) {
      *this = OptionalBox(m_block->m_value);
    }
    return m_block->m_value;
  }

  template<typename T>
  void OptionalBox<T>::release() noexcept {
    if(m_block &&
        m_block->m_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete m_block;
    }
    m_block = nullptr;
  }
}

#endif
