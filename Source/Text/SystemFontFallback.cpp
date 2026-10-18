#include <cmath>
#include <string>
#include <utility>
#include "SystemFontFallback.hpp"
#ifdef _WIN32
#include <windows.h>
#include <dwrite_3.h>
#endif

using namespace Rellu;
using namespace Rellu::Details;

#ifdef _WIN32
namespace {
  constexpr auto LOCALE = L"en-us";

  template<typename T>
  void release(T*& object) {
    if(object) {
      object->Release();
      object = nullptr;
    }
  }

  class TextSource : public IDWriteTextAnalysisSource {
    public:
      explicit TextSource(std::wstring text)
        : m_text(std::move(text)) {}

      HRESULT __stdcall QueryInterface(REFIID id, void** object) override {
        if(id == __uuidof(IUnknown) ||
            id == __uuidof(IDWriteTextAnalysisSource)) {
          *object = this;
          return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
      }

      ULONG __stdcall AddRef() override {
        return 1;
      }

      ULONG __stdcall Release() override {
        return 1;
      }

      HRESULT __stdcall GetTextAtPosition(UINT32 position,
          const WCHAR** text, UINT32* length) override {
        if(position >= m_text.size()) {
          *text = nullptr;
          *length = 0;
        } else {
          *text = m_text.data() + position;
          *length = static_cast<UINT32>(m_text.size() - position);
        }
        return S_OK;
      }

      HRESULT __stdcall GetTextBeforePosition(UINT32 position,
          const WCHAR** text, UINT32* length) override {
        if(position == 0 || position > m_text.size()) {
          *text = nullptr;
          *length = 0;
        } else {
          *text = m_text.data();
          *length = position;
        }
        return S_OK;
      }

      DWRITE_READING_DIRECTION __stdcall
          GetParagraphReadingDirection() override {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
      }

      HRESULT __stdcall GetLocaleName(UINT32 position, UINT32* length,
          const WCHAR** locale) override {
        *length = static_cast<UINT32>(m_text.size()) - position;
        *locale = LOCALE;
        return S_OK;
      }

      HRESULT __stdcall GetNumberSubstitution(UINT32 position,
          UINT32* length, IDWriteNumberSubstitution** substitution) override {
        *length = static_cast<UINT32>(m_text.size()) - position;
        *substitution = nullptr;
        return S_OK;
      }

    private:
      std::wstring m_text;
  };

  std::wstring to_wide(const std::string& text) {
    auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
      static_cast<int>(text.size()), nullptr, 0);
    auto result = std::wstring(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(),
      static_cast<int>(text.size()), result.data(), size);
    return result;
  }

  std::wstring to_utf16(char32_t character) {
    if(character < 0x10000) {
      return std::wstring(1, static_cast<wchar_t>(character));
    }
    auto offset = character - 0x10000;
    return std::wstring({static_cast<wchar_t>(0xD800 + (offset >> 10)),
      static_cast<wchar_t>(0xDC00 + (offset & 0x3FF))});
  }

  std::optional<std::filesystem::path> get_path(IDWriteFontFace& face) {
    auto count = UINT32(1);
    auto file = static_cast<IDWriteFontFile*>(nullptr);
    if(FAILED(face.GetFiles(&count, &file)) || !file) {
      return std::nullopt;
    }
    auto path = std::optional<std::filesystem::path>();
    auto loader = static_cast<IDWriteFontFileLoader*>(nullptr);
    auto local = static_cast<IDWriteLocalFontFileLoader*>(nullptr);
    auto key = static_cast<const void*>(nullptr);
    auto size = UINT32(0);
    if(SUCCEEDED(file->GetLoader(&loader)) &&
        SUCCEEDED(loader->QueryInterface(IID_PPV_ARGS(&local))) &&
        SUCCEEDED(file->GetReferenceKey(&key, &size))) {
      auto length = UINT32(0);
      if(SUCCEEDED(local->GetFilePathLengthFromKey(key, size, &length))) {
        auto name = std::wstring(length + 1, L'\0');
        if(SUCCEEDED(local->GetFilePathFromKey(
            key, size, name.data(), length + 1))) {
          name.resize(length);
          path = std::filesystem::path(name);
        }
      }
    }
    release(local);
    release(loader);
    release(file);
    return path;
  }

  IDWriteFontFallback* get_fallback() {
    static auto fallback = [] {
      auto factory = static_cast<IDWriteFactory2*>(nullptr);
      auto fallback = static_cast<IDWriteFontFallback*>(nullptr);
      if(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
          __uuidof(IDWriteFactory2),
          reinterpret_cast<IUnknown**>(&factory))) && factory) {
        if(FAILED(factory->GetSystemFontFallback(&fallback))) {
          fallback = nullptr;
        }
      }
      release(factory);
      return fallback;
    }();
    return fallback;
  }
}

std::optional<SystemFace> Details::find_system_fallback(char32_t character,
    const std::string& family, double weight, bool is_italic) {
  auto fallback = get_fallback();
  if(!fallback) {
    return std::nullopt;
  }
  auto source = TextSource(to_utf16(character));
  auto name = to_wide(family);
  auto length = UINT32(0);
  auto font = static_cast<IDWriteFont*>(nullptr);
  auto scale = FLOAT(1);
  auto style = DWRITE_FONT_STYLE_NORMAL;
  if(is_italic) {
    style = DWRITE_FONT_STYLE_ITALIC;
  }
  if(FAILED(fallback->MapCharacters(&source, 0,
      static_cast<UINT32>(to_utf16(character).size()), nullptr,
      name.empty() ? nullptr : name.c_str(),
      static_cast<DWRITE_FONT_WEIGHT>(std::lround(weight)), style,
      DWRITE_FONT_STRETCH_NORMAL, &length, &font, &scale)) || !font) {
    return std::nullopt;
  }
  auto result = std::optional<SystemFace>();
  auto face = static_cast<IDWriteFontFace*>(nullptr);
  if(SUCCEEDED(font->CreateFontFace(&face))) {
    if(auto path = get_path(*face)) {
      result = SystemFace(std::move(*path), face->GetIndex(),
        (font->GetSimulations() & DWRITE_FONT_SIMULATIONS_BOLD) != 0);
    }
  }
  release(face);
  release(font);
  return result;
}
#else
std::optional<SystemFace> Details::find_system_fallback(char32_t character,
    const std::string& family, double weight, bool is_italic) {
  return std::nullopt;
}
#endif
