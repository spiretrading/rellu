#ifndef RELLU_MODEL_OPAQUE_DATA_HPP
#define RELLU_MODEL_OPAQUE_DATA_HPP
#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace Rellu {

  /**
   * Data a document format keeps with a node or a document without the model
   * interpreting it, such as what an Adobe XD file holds beyond the model. It
   * is an immutable string of bytes shared on copy, taking a single pointer
   * that is null when empty; two OpaqueData compare equal when their bytes
   * do.
   */
  class OpaqueData {
    public:

      /** Constructs an empty OpaqueData. */
      OpaqueData() noexcept;

      /**
       * Constructs an OpaqueData holding bytes.
       * @param bytes The bytes to hold, empty for none.
       */
      explicit OpaqueData(std::string bytes);

      OpaqueData(const OpaqueData& opaque) noexcept;
      OpaqueData(OpaqueData&& opaque) noexcept;

      ~OpaqueData();

      /** Returns the bytes held. */
      std::string_view get_bytes() const;

      /** Returns <code>true</code> iff no bytes are held. */
      bool is_empty() const;

      OpaqueData& operator =(const OpaqueData& opaque) noexcept;
      OpaqueData& operator =(OpaqueData&& opaque) noexcept;
      bool operator ==(const OpaqueData& opaque) const;

    private:
      struct Block {
        std::atomic<std::size_t> m_count;
        std::string m_bytes;
      };
      Block* m_block;

      void release() noexcept;
  };

  inline OpaqueData::OpaqueData() noexcept
    : m_block(nullptr) {}

  inline OpaqueData::OpaqueData(std::string bytes)
      : m_block(nullptr) {
    if(!bytes.empty()) {
      m_block = new Block(1, std::move(bytes));
    }
  }

  inline OpaqueData::OpaqueData(const OpaqueData& opaque) noexcept
      : m_block(opaque.m_block) {
    if(m_block) {
      m_block->m_count.fetch_add(1, std::memory_order_relaxed);
    }
  }

  inline OpaqueData::OpaqueData(OpaqueData&& opaque) noexcept
    : m_block(std::exchange(opaque.m_block, nullptr)) {}

  inline OpaqueData::~OpaqueData() {
    release();
  }

  inline std::string_view OpaqueData::get_bytes() const {
    if(!m_block) {
      return std::string_view();
    }
    return m_block->m_bytes;
  }

  inline bool OpaqueData::is_empty() const {
    return !m_block;
  }

  inline OpaqueData& OpaqueData::operator =(
      const OpaqueData& opaque) noexcept {
    if(opaque.m_block) {
      opaque.m_block->m_count.fetch_add(1, std::memory_order_relaxed);
    }
    release();
    m_block = opaque.m_block;
    return *this;
  }

  inline OpaqueData& OpaqueData::operator =(OpaqueData&& opaque) noexcept {
    if(this != &opaque) {
      release();
      m_block = std::exchange(opaque.m_block, nullptr);
    }
    return *this;
  }

  inline bool OpaqueData::operator ==(const OpaqueData& opaque) const {
    if(m_block == opaque.m_block) {
      return true;
    }
    return get_bytes() == opaque.get_bytes();
  }

  inline void OpaqueData::release() noexcept {
    if(m_block &&
        m_block->m_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete m_block;
    }
    m_block = nullptr;
  }
}

#endif
