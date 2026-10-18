#include <cmath>
#include <unordered_map>
#include <utility>
#include "GlyphRenderer.hpp"
#ifdef _WIN32
#include <windows.h>
#include <dwrite_3.h>
#endif

using namespace Rellu;
using namespace Rellu::Details;

#ifdef _WIN32
namespace {
  template<typename T>
  void release(T*& object) {
    if(object) {
      object->Release();
      object = nullptr;
    }
  }

  class DirectWriteRenderer : public GlyphRenderer {
    public:
      explicit DirectWriteRenderer(IDWriteFactory2* factory)
        : m_factory(factory) {}

      ~DirectWriteRenderer() override {
        for(auto& face : m_faces) {
          release(face.second.second);
        }
        release(m_factory);
      }

      bool render(const std::shared_ptr<const Typeface>& typeface,
          std::uint32_t glyph, double size, const Matrix& transform,
          CoverageMask& mask) override {
        auto face = get_face(typeface);
        if(!face) {
          return false;
        }
        mask = CoverageMask();
        auto index = static_cast<UINT16>(glyph);
        auto advance = FLOAT(0);
        auto offset = DWRITE_GLYPH_OFFSET();
        auto run = DWRITE_GLYPH_RUN();
        run.fontFace = face;
        run.fontEmSize = static_cast<FLOAT>(size);
        run.glyphCount = 1;
        run.glyphIndices = &index;
        run.glyphAdvances = &advance;
        run.glyphOffsets = &offset;
        auto matrix = DWRITE_MATRIX(static_cast<FLOAT>(transform.m_a),
          static_cast<FLOAT>(transform.m_b), static_cast<FLOAT>(transform.m_c),
          static_cast<FLOAT>(transform.m_d), 0, 0);
        auto is_linear = transform.m_a != 1 || transform.m_b != 0 ||
          transform.m_c != 0 || transform.m_d != 1;
        auto mode = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
        auto grid = DWRITE_GRID_FIT_MODE_DEFAULT;
        auto recommended = static_cast<IDWriteFontFace2*>(nullptr);
        if(SUCCEEDED(face->QueryInterface(IID_PPV_ARGS(&recommended)))) {
          auto recommended_mode = DWRITE_RENDERING_MODE_DEFAULT;
          auto recommended_grid = DWRITE_GRID_FIT_MODE_DEFAULT;
          if(SUCCEEDED(recommended->GetRecommendedRenderingMode(
              static_cast<FLOAT>(size), 96, 96, nullptr, FALSE,
              DWRITE_OUTLINE_THRESHOLD_ANTIALIASED,
              DWRITE_MEASURING_MODE_NATURAL, nullptr, &recommended_mode,
              &recommended_grid)) &&
              recommended_mode == DWRITE_RENDERING_MODE_NATURAL &&
              recommended_grid == DWRITE_GRID_FIT_MODE_DISABLED) {
            mode = recommended_mode;
            grid = recommended_grid;
          }
          recommended->Release();
        }
        auto analysis = static_cast<IDWriteGlyphRunAnalysis*>(nullptr);
        if(FAILED(m_factory->CreateGlyphRunAnalysis(&run,
            is_linear ? &matrix : nullptr, mode,
            DWRITE_MEASURING_MODE_NATURAL, grid,
            DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
            static_cast<FLOAT>(transform.m_tx),
            static_cast<FLOAT>(transform.m_ty), &analysis)) || !analysis) {
          return true;
        }
        auto bounds = RECT();
        if(SUCCEEDED(analysis->GetAlphaTextureBounds(
            DWRITE_TEXTURE_ALIASED_1x1, &bounds)) &&
            bounds.right > bounds.left && bounds.bottom > bounds.top) {
          mask.m_left = bounds.left;
          mask.m_top = bounds.top;
          mask.m_width = bounds.right - bounds.left;
          mask.m_height = bounds.bottom - bounds.top;
          mask.m_coverage.resize(mask.m_width * mask.m_height);
          if(SUCCEEDED(analysis->CreateAlphaTexture(
              DWRITE_TEXTURE_ALIASED_1x1, &bounds, mask.m_coverage.data(),
              static_cast<UINT32>(mask.m_coverage.size())))) {
            for(auto& coverage : mask.m_coverage) {
              coverage = static_cast<std::uint8_t>(
                std::lround(coverage * 16 / 255.0));
            }
          } else {
            mask = CoverageMask();
          }
        }
        analysis->Release();
        return true;
      }

    private:
      IDWriteFactory2* m_factory;
      std::unordered_map<const Typeface*,
        std::pair<std::shared_ptr<const Typeface>, IDWriteFontFace*>> m_faces;

      IDWriteFontFace* get_face(
          const std::shared_ptr<const Typeface>& typeface) {
        auto& entry = m_faces[typeface.get()];
        if(!entry.first) {
          entry.first = typeface;
          entry.second = load(*typeface);
        }
        return entry.second;
      }

      IDWriteFontFace* load(const Typeface& typeface) {
        if(typeface.get_path().empty()) {
          return nullptr;
        }
        auto file = static_cast<IDWriteFontFile*>(nullptr);
        if(FAILED(m_factory->CreateFontFileReference(
            typeface.get_path().wstring().c_str(), nullptr, &file))) {
          return nullptr;
        }
        auto face = static_cast<IDWriteFontFace*>(nullptr);
        if(typeface.get_instance()) {
          auto factory = static_cast<IDWriteFactory6*>(nullptr);
          auto resource = static_cast<IDWriteFontResource*>(nullptr);
          if(SUCCEEDED(m_factory->QueryInterface(IID_PPV_ARGS(&factory))) &&
              SUCCEEDED(factory->CreateFontResource(
                file, typeface.get_index(), &resource))) {
            auto values = std::vector<DWRITE_FONT_AXIS_VALUE>();
            for(auto& variation : typeface.get_variations()) {
              values.push_back(DWRITE_FONT_AXIS_VALUE(
                static_cast<DWRITE_FONT_AXIS_TAG>(
                  ((variation.first >> 24) & 0xFF) |
                  ((variation.first >> 8) & 0xFF00) |
                  ((variation.first << 8) & 0xFF0000) |
                  ((variation.first << 24) & 0xFF000000)),
                static_cast<FLOAT>(variation.second)));
            }
            auto variable = static_cast<IDWriteFontFace5*>(nullptr);
            if(SUCCEEDED(resource->CreateFontFace(
                DWRITE_FONT_SIMULATIONS_NONE, values.data(),
                static_cast<UINT32>(values.size()), &variable))) {
              face = variable;
            }
          }
          release(resource);
          release(factory);
        } else {
          auto is_supported = BOOL(FALSE);
          auto file_type = DWRITE_FONT_FILE_TYPE();
          auto face_type = DWRITE_FONT_FACE_TYPE();
          auto count = UINT32(0);
          if(SUCCEEDED(file->Analyze(
              &is_supported, &file_type, &face_type, &count)) &&
              is_supported) {
            if(FAILED(m_factory->CreateFontFace(face_type, 1, &file,
                typeface.get_index(), DWRITE_FONT_SIMULATIONS_NONE, &face))) {
              face = nullptr;
            }
          }
        }
        release(file);
        return face;
      }
  };
}

std::unique_ptr<GlyphRenderer> Details::make_direct_write_renderer() {
  auto factory = static_cast<IDWriteFactory2*>(nullptr);
  if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
      __uuidof(IDWriteFactory2), reinterpret_cast<IUnknown**>(&factory))) ||
      !factory) {
    return nullptr;
  }
  return std::make_unique<DirectWriteRenderer>(factory);
}
#else
std::unique_ptr<GlyphRenderer> Details::make_direct_write_renderer() {
  return nullptr;
}
#endif
