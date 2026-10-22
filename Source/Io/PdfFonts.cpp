#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include "Rellu/Geometry/Path.hpp"
#include "PdfFonts.hpp"

using namespace Rellu;

namespace {
  constexpr auto PDF_SYMBOLIC = 4;
  constexpr auto FIXED_PITCH_STYLE = 1;
  constexpr auto SERIF_STYLE = 2;
  constexpr auto SCRIPT_STYLE = 8;
  constexpr auto ITALIC_STYLE = 64;

  std::uint32_t make_tag(std::string_view tag) {
    return (static_cast<std::uint32_t>(tag[0]) << 24) |
      (static_cast<std::uint32_t>(tag[1]) << 16) |
      (static_cast<std::uint32_t>(tag[2]) << 8) |
      static_cast<std::uint32_t>(tag[3]);
  }

  class Reader {
    public:
      explicit Reader(std::string_view data)
        : m_data(data) {}

      bool has(std::size_t offset, std::size_t size) const {
        return offset <= m_data.size() && size <= m_data.size() - offset;
      }

      std::uint8_t u8(std::size_t offset) const {
        if(!has(offset, 1)) {
          return 0;
        }
        return static_cast<std::uint8_t>(m_data[offset]);
      }

      std::uint16_t u16(std::size_t offset) const {
        return static_cast<std::uint16_t>((u8(offset) << 8) | u8(offset + 1));
      }

      std::int16_t s16(std::size_t offset) const {
        return static_cast<std::int16_t>(u16(offset));
      }

      std::uint32_t u32(std::size_t offset) const {
        return (static_cast<std::uint32_t>(u16(offset)) << 16) |
          u16(offset + 2);
      }

    private:
      std::string_view m_data;
  };

  struct Table {
    std::size_t m_offset = 0;
    std::size_t m_length = 0;
    bool m_is_present = false;
  };

  struct Sfnt {
    int m_units_per_em = 1000;
    int m_glyph_count = 0;
    bool m_is_true_type = false;
    bool m_is_variable = false;
    std::uint16_t m_embedding = 0;
    int m_ascent = 0;
    int m_descent = 0;
    int m_cap_height = 0;
    int m_style = 0;
    int m_italic_angle = 0;
    int m_x_min = 0;
    int m_y_min = 0;
    int m_x_max = 0;
    int m_y_max = 0;
    std::vector<std::uint16_t> m_advances;
    std::vector<char32_t> m_unicodes;
  };

  void read_cmap(const Reader& reader, const Table& table, Sfnt& sfnt) {
    if(!table.m_is_present) {
      return;
    }
    auto base = table.m_offset;
    auto count = reader.u16(base + 2);
    auto best = std::size_t(0);
    auto best_rank = 0;
    for(auto i = 0; i != count; ++i) {
      auto record = base + 4 + 8 * static_cast<std::size_t>(i);
      auto platform = reader.u16(record);
      auto encoding = reader.u16(record + 2);
      auto offset = base + reader.u32(record + 4);
      auto format = reader.u16(offset);
      auto rank = 0;
      if(platform == 3 && encoding == 10 && format == 12) {
        rank = 4;
      } else if(platform == 0 && format == 12) {
        rank = 3;
      } else if(platform == 3 && encoding == 1 && format == 4) {
        rank = 2;
      } else if(platform == 0 && format == 4) {
        rank = 1;
      }
      if(rank > best_rank) {
        best_rank = rank;
        best = offset;
      }
    }
    if(best_rank == 0) {
      return;
    }
    auto assign = [&] (char32_t character, std::uint32_t glyph) {
      if(glyph != 0 && glyph < sfnt.m_unicodes.size() &&
          (sfnt.m_unicodes[glyph] == 0 ||
            character < sfnt.m_unicodes[glyph])) {
        sfnt.m_unicodes[glyph] = character;
      }
    };
    auto format = reader.u16(best);
    if(format == 12) {
      auto groups = reader.u32(best + 12);
      for(auto i = std::uint32_t(0); i != groups; ++i) {
        auto group = best + 16 + 12 * static_cast<std::size_t>(i);
        if(!reader.has(group, 12)) {
          break;
        }
        auto start = reader.u32(group);
        auto end = std::min(reader.u32(group + 4), std::uint32_t(0x10FFFF));
        auto glyph = reader.u32(group + 8);
        for(auto character = start; character <= end; ++character) {
          assign(character, glyph + (character - start));
          if(character == end) {
            break;
          }
        }
      }
    } else {
      auto segments = reader.u16(best + 6) / 2;
      auto ends = best + 14;
      auto starts = ends + 2 * static_cast<std::size_t>(segments) + 2;
      auto deltas = starts + 2 * static_cast<std::size_t>(segments);
      auto offsets = deltas + 2 * static_cast<std::size_t>(segments);
      for(auto i = 0; i != segments; ++i) {
        auto index = 2 * static_cast<std::size_t>(i);
        auto end = reader.u16(ends + index);
        auto start = reader.u16(starts + index);
        auto delta = reader.u16(deltas + index);
        auto range = reader.u16(offsets + index);
        for(auto character = std::uint32_t(start); character <= end &&
            character != 0xFFFF; ++character) {
          auto glyph = std::uint32_t(0);
          if(range == 0) {
            glyph = (character + delta) & 0xFFFF;
          } else {
            auto address = offsets + index + range + 2 * (character - start);
            glyph = reader.u16(address);
            if(glyph != 0) {
              glyph = (glyph + delta) & 0xFFFF;
            }
          }
          assign(character, glyph);
        }
      }
    }
  }

  Sfnt read_sfnt(std::string_view data, unsigned int index) {
    auto sfnt = Sfnt();
    auto reader = Reader(data);
    auto start = std::size_t(0);
    if(reader.u32(0) == make_tag("ttcf")) {
      if(index >= reader.u32(8)) {
        return sfnt;
      }
      start = reader.u32(12 + 4 * static_cast<std::size_t>(index));
    }
    auto find = [&] (std::string_view tag) {
      auto table = Table();
      auto count = reader.u16(start + 4);
      for(auto i = 0; i != count; ++i) {
        auto record = start + 12 + 16 * static_cast<std::size_t>(i);
        if(reader.u32(record) == make_tag(tag)) {
          table.m_offset = reader.u32(record + 8);
          table.m_length = reader.u32(record + 12);
          table.m_is_present = reader.has(table.m_offset, table.m_length);
        }
      }
      return table;
    };
    auto head = find("head");
    if(head.m_is_present) {
      sfnt.m_units_per_em = std::max<int>(1, reader.u16(head.m_offset + 18));
      sfnt.m_x_min = reader.s16(head.m_offset + 36);
      sfnt.m_y_min = reader.s16(head.m_offset + 38);
      sfnt.m_x_max = reader.s16(head.m_offset + 40);
      sfnt.m_y_max = reader.s16(head.m_offset + 42);
    }
    auto maxp = find("maxp");
    if(maxp.m_is_present) {
      sfnt.m_glyph_count = reader.u16(maxp.m_offset + 4);
    }
    sfnt.m_is_true_type = find("glyf").m_is_present;
    sfnt.m_is_variable = find("fvar").m_is_present;
    auto hhea = find("hhea");
    auto hmtx = find("hmtx");
    sfnt.m_advances.assign(static_cast<std::size_t>(sfnt.m_glyph_count), 0);
    if(hhea.m_is_present && hmtx.m_is_present) {
      auto metrics = reader.u16(hhea.m_offset + 34);
      auto last = std::uint16_t(0);
      for(auto i = 0; i != sfnt.m_glyph_count; ++i) {
        if(i < metrics) {
          last = reader.u16(hmtx.m_offset + 4 * static_cast<std::size_t>(i));
        }
        sfnt.m_advances[static_cast<std::size_t>(i)] = last;
      }
      auto post = find("post");
      if(post.m_is_present) {
        sfnt.m_italic_angle =
          static_cast<std::int32_t>(reader.u32(post.m_offset + 4)) >> 16;
        if(reader.u32(post.m_offset + 12) != 0 && metrics == 1) {
          sfnt.m_style |= FIXED_PITCH_STYLE;
        }
      }
    }
    auto os2 = find("OS/2");
    if(os2.m_is_present) {
      auto base = os2.m_offset;
      auto version = reader.u16(base);
      sfnt.m_embedding = reader.u16(base + 8);
      auto family = reader.u8(base + 32);
      auto serif = reader.u8(base + 33);
      if(family == 2 && serif >= 2 && serif <= 10) {
        sfnt.m_style |= SERIF_STYLE;
      } else if(family == 3) {
        sfnt.m_style |= SCRIPT_STYLE;
      }
      auto selection = reader.u16(base + 62);
      if(selection & 1) {
        sfnt.m_style |= ITALIC_STYLE;
      }
      if((selection & 0x80) && version >= 4) {
        sfnt.m_ascent = reader.s16(base + 68);
        sfnt.m_descent = -reader.s16(base + 70);
      } else {
        sfnt.m_ascent = reader.u16(base + 74);
        sfnt.m_descent = reader.u16(base + 76);
      }
      if(version >= 2) {
        sfnt.m_cap_height = reader.s16(base + 88);
      }
    }
    sfnt.m_unicodes.assign(static_cast<std::size_t>(sfnt.m_glyph_count), 0);
    read_cmap(reader, find("cmap"), sfnt);
    return sfnt;
  }

  double from_font_units(double value, int units_per_em) {
    auto scaled = static_cast<float>(value);
    if(units_per_em == 1000) {
      return scaled;
    }
    return scaled * 1000.0f / static_cast<float>(units_per_em);
  }

  std::string to_utf16_hex(char32_t character) {
    if(character >= 0x10000) {
      auto value = character - 0x10000;
      return to_pdf_hex(static_cast<std::uint16_t>(0xD800 + (value >> 10))) +
        to_pdf_hex(static_cast<std::uint16_t>(0xDC00 + (value & 0x3FF)));
    }
    return to_pdf_hex(static_cast<std::uint16_t>(character));
  }

  std::string to_code(std::uint16_t code, bool is_multibyte) {
    if(is_multibyte) {
      return to_pdf_hex(code);
    }
    return std::format("{:02X}", code);
  }

  std::string make_to_unicode(const std::vector<char32_t>& unicodes,
      const std::vector<bool>& usage, bool is_multibyte,
      std::uint16_t first, std::uint16_t last) {
    struct Range {
      int m_start;
      int m_end;
      char32_t m_unicode;
    };
    auto offset = is_multibyte ? 0 : first - 1;
    auto chars = std::vector<Range>();
    auto ranges = std::vector<Range>();
    auto current = Range(0, 0, 0);
    auto is_empty = true;
    auto limit = static_cast<int>(last) + 1 - offset;
    auto get_unicode = [&] (int glyph) {
      if(glyph < 0 || static_cast<std::size_t>(glyph) >= unicodes.size()) {
        return char32_t(0);
      }
      return unicodes[static_cast<std::size_t>(glyph)];
    };
    auto is_used = [&] (int glyph) {
      auto code = is_multibyte ? glyph : (glyph == 0 ? 0 : glyph - first + 1);
      return code >= 0 && static_cast<std::size_t>(code) < usage.size() &&
        usage[static_cast<std::size_t>(code)];
    };
    for(auto i = static_cast<int>(first) - offset; i < limit + 1; ++i) {
      auto glyph = i + offset;
      auto is_in = i < limit && is_used(glyph);
      if(!is_empty) {
        auto is_in_range = i == current.m_end + 1 &&
          (i >> 8) == (current.m_start >> 8) && i < limit &&
          get_unicode(glyph) == current.m_unicode +
            static_cast<char32_t>(i - current.m_start);
        if(!is_in || !is_in_range) {
          if(current.m_end > current.m_start) {
            ranges.push_back(current);
          } else {
            chars.push_back(current);
          }
          is_empty = true;
        }
      }
      if(is_in) {
        current.m_end = i;
        if(is_empty) {
          current.m_start = i;
          current.m_unicode = get_unicode(glyph);
          is_empty = false;
        }
      }
    }
    auto cmap = std::string("/CIDInit /ProcSet findresource begin\n"
      "12 dict begin\nbegincmap\n/CIDSystemInfo\n<<  /Registry (Adobe)\n"
      "/Ordering (UCS)\n/Supplement 0\n>> def\n"
      "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
      "1 begincodespacerange\n");
    cmap += is_multibyte ? "<0000> <FFFF>\n" : "<00> <FF>\n";
    cmap += "endcodespacerange\n";
    for(auto i = std::size_t(0); i < chars.size(); i += 100) {
      auto count = std::min<std::size_t>(chars.size() - i, 100);
      cmap += std::format("{} beginbfchar\n", count);
      for(auto j = i; j != i + count; ++j) {
        cmap += "<" + to_code(static_cast<std::uint16_t>(chars[j].m_start),
          is_multibyte) + "> <" + to_utf16_hex(chars[j].m_unicode) + ">\n";
      }
      cmap += "endbfchar\n";
    }
    for(auto i = std::size_t(0); i < ranges.size(); i += 100) {
      auto count = std::min<std::size_t>(ranges.size() - i, 100);
      cmap += std::format("{} beginbfrange\n", count);
      for(auto j = i; j != i + count; ++j) {
        cmap += "<" + to_code(static_cast<std::uint16_t>(ranges[j].m_start),
          is_multibyte) + "> <" + to_code(
            static_cast<std::uint16_t>(ranges[j].m_end), is_multibyte) +
          "> <" + to_utf16_hex(ranges[j].m_unicode) + ">\n";
      }
      cmap += "endbfrange\n";
    }
    cmap += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend";
    return cmap;
  }

  std::string make_widths(const std::vector<std::pair<int, double>>& glyphs) {
    auto mode = 0.0;
    auto result = std::vector<std::string>();
    for(auto i = std::size_t(0); i < glyphs.size(); ++i) {
      auto advance = glyphs[i].second;
      if(advance == mode) {
        continue;
      }
      auto j = i + 1;
      while(j < glyphs.size() && glyphs[j].second == advance) {
        ++j;
      }
      if(j - i >= 2) {
        result.push_back(std::to_string(glyphs[i].first));
        result.push_back(std::to_string(glyphs[j - 1].first));
        result.push_back(to_pdf_number(advance));
        i = j - 1;
        continue;
      }
      result.push_back(std::to_string(glyphs[i].first));
      auto advances = std::vector<std::string>();
      advances.push_back(to_pdf_number(advance));
      j = i + 1;
      for(; j < glyphs.size(); ++j) {
        advance = glyphs[j].second;
        if(advance == mode) {
          break;
        }
        auto skipped = glyphs[j].first - glyphs[j - 1].first - 1;
        if(skipped >= 4) {
          break;
        }
        auto next = 0.0;
        if(j + 1 < glyphs.size()) {
          next = glyphs[j + 1].second;
          auto next_skipped = glyphs[j + 1].first - glyphs[j].first - 1;
          if(advance == next && skipped + next_skipped >= 4) {
            break;
          }
        }
        if(j + 2 < glyphs.size() && advance == next) {
          next = glyphs[j + 2].second;
          if(advance == next) {
            break;
          }
        }
        while(skipped-- > 0) {
          advances.push_back("0");
        }
        advances.push_back(to_pdf_number(advance));
      }
      result.push_back(to_pdf_array(advances));
      i = j - 1;
    }
    return to_pdf_array(result);
  }

  Rect get_glyph_bounds(const Typeface& typeface, std::uint32_t index,
      double scale) {
    auto& outline = typeface.get_outline(index);
    if(outline.is_empty()) {
      return Rect(0, 0, 0, 0);
    }
    return get_bounds(outline, make_scale(scale, scale));
  }
}

struct PdfFonts::Face {
  std::shared_ptr<const Typeface> m_typeface;
  Sfnt m_sfnt;
  bool m_is_type3;
  int m_stem;
  int m_cap_height;
};

PdfFonts::PdfFonts(PdfFile& file)
  : m_file(&file) {}

PdfFonts::~PdfFonts() = default;

PdfFonts::Face& PdfFonts::get_face(
    const std::shared_ptr<const Typeface>& typeface) {
  auto& face = m_faces[typeface.get()];
  if(face) {
    return *face;
  }
  face = std::make_unique<Face>();
  face->m_typeface = typeface;
  face->m_sfnt = read_sfnt(typeface->get_data(), typeface->get_index());
  auto& sfnt = face->m_sfnt;
  auto is_restricted = sfnt.m_embedding != 0 &&
    ((sfnt.m_embedding & 0x0002) || (sfnt.m_embedding & 0x0200));
  face->m_is_type3 = !sfnt.m_is_true_type || sfnt.m_is_variable ||
    is_restricted || !typeface->get_variations().empty();
  auto glyph_of = [&] (char32_t character) {
    for(auto i = std::size_t(0); i != sfnt.m_unicodes.size(); ++i) {
      if(sfnt.m_unicodes[i] == character) {
        return static_cast<std::uint32_t>(i);
      }
    }
    return std::uint32_t(0);
  };
  auto stem = std::numeric_limits<int>::max();
  for(auto character : {U'i', U'I', U'!', U'1'}) {
    auto bounds = get_glyph_bounds(*typeface, glyph_of(character), 1000);
    stem = std::min(stem, static_cast<int>(std::lround(bounds.m_width)));
  }
  face->m_stem = stem == std::numeric_limits<int>::max() ? 0 : stem;
  face->m_cap_height = sfnt.m_cap_height;
  if(face->m_cap_height == 0) {
    auto height = 0.0;
    for(auto character : {U'M', U'X'}) {
      height +=
        get_glyph_bounds(*typeface, glyph_of(character), 1000).m_height;
    }
    face->m_cap_height = static_cast<int>(std::lround(height / 2));
  }
  return *face;
}

std::optional<PdfGlyph> PdfFonts::use(
    const std::shared_ptr<const Typeface>& typeface, std::uint32_t index) {
  auto& face = get_face(typeface);
  auto& sfnt = face.m_sfnt;
  if(index >= static_cast<std::uint32_t>(sfnt.m_glyph_count)) {
    return std::nullopt;
  }
  auto glyph = static_cast<std::uint16_t>(index);
  auto subset = std::uint16_t(0);
  if(face.m_is_type3) {
    subset = glyph != 0 ?
      static_cast<std::uint16_t>(glyph - (glyph - 1) % 255) : 1;
  }
  auto key = std::pair(typeface.get(), subset);
  auto entry = m_fonts.find(key);
  if(entry == m_fonts.end()) {
    auto font = Font();
    font.m_id = m_file->reserve();
    font.m_face = &face;
    font.m_is_type3 = face.m_is_type3;
    font.m_last = static_cast<std::uint16_t>(sfnt.m_glyph_count - 1);
    if(face.m_is_type3) {
      font.m_first = subset;
      font.m_last = static_cast<std::uint16_t>(
        std::min<int>(font.m_last, 254 + subset));
      font.m_usage.assign(static_cast<std::size_t>(font.m_last) -
        font.m_first + 2, false);
    } else {
      font.m_first = 1;
      font.m_usage.assign(static_cast<std::size_t>(sfnt.m_glyph_count), false);
    }
    font.m_usage[0] = true;
    entry = m_fonts.emplace(key, std::move(font)).first;
  }
  auto& font = entry->second;
  auto code = glyph;
  if(font.m_is_type3 && glyph != 0) {
    code = static_cast<std::uint16_t>(glyph - font.m_first + 1);
  }
  font.m_usage[code] = true;
  auto advance = static_cast<double>(
    sfnt.m_advances[glyph]) / sfnt.m_units_per_em;
  return PdfGlyph(font.m_id, code, !font.m_is_type3, advance,
    !font.m_is_type3 || sfnt.m_units_per_em == 1000);
}

void PdfFonts::finish() {
  auto fonts = std::vector<const Font*>();
  for(auto& entry : m_fonts) {
    fonts.push_back(&entry.second);
  }
  std::sort(fonts.begin(), fonts.end(), [] (const auto* left,
      const auto* right) {
    return left->m_id < right->m_id;
  });
  for(auto font : fonts) {
    if(font->m_is_type3) {
      write_type3(*font);
    } else {
      write_type0(*font);
    }
  }
}

void PdfFonts::write_type0(const Font& font) {
  auto& face = *font.m_face;
  auto& sfnt = face.m_sfnt;
  auto& names = face.m_typeface->get_names();
  auto name = to_pdf_name(names.m_postscript_name);
  auto em = sfnt.m_units_per_em;
  auto descriptor = PdfDictionary("FontDescriptor");
  descriptor.add("FontName", name);
  descriptor.add("Flags", std::to_string(sfnt.m_style | PDF_SYMBOLIC));
  descriptor.add("Ascent", to_pdf_number(from_font_units(sfnt.m_ascent, em)));
  descriptor.add(
    "Descent", to_pdf_number(from_font_units(sfnt.m_descent, em)));
  descriptor.add("StemV", to_pdf_number(from_font_units(face.m_stem, em)));
  descriptor.add(
    "CapHeight", to_pdf_number(from_font_units(face.m_cap_height, em)));
  descriptor.add("ItalicAngle", std::to_string(sfnt.m_italic_angle));
  descriptor.add("FontBBox", to_pdf_array({
    to_pdf_number(from_font_units(sfnt.m_x_min, em)),
    to_pdf_number(from_font_units(sfnt.m_y_min, em)),
    to_pdf_number(from_font_units(sfnt.m_x_max, em)),
    to_pdf_number(from_font_units(sfnt.m_y_max, em))}));
  auto data = face.m_typeface->get_data();
  if(!data.empty()) {
    auto file = PdfDictionary();
    file.add("Length1", std::to_string(data.size()));
    auto file_id = m_file->reserve();
    m_file->write_stream(file_id, std::move(file), std::string(data));
    descriptor.add("FontFile2", to_pdf_reference(file_id));
  }
  auto descriptor_id = m_file->reserve();
  m_file->write(descriptor_id, descriptor.to_string());
  auto cid = PdfDictionary("Font");
  cid.add("FontDescriptor", to_pdf_reference(descriptor_id));
  cid.add("BaseFont", name);
  cid.add("Subtype", "/CIDFontType2");
  cid.add("CIDToGIDMap", "/Identity");
  cid.add("CIDSystemInfo",
    "<</Registry (Adobe)\n/Ordering (Identity)\n/Supplement 0>>");
  auto glyphs = std::vector<std::pair<int, double>>();
  for(auto i = std::size_t(0); i != font.m_usage.size(); ++i) {
    if(font.m_usage[i]) {
      glyphs.emplace_back(static_cast<int>(i),
        from_font_units(sfnt.m_advances[i], em));
    }
  }
  auto widths = make_widths(glyphs);
  if(widths != "[]") {
    cid.add("W", widths);
  }
  cid.add("DW", "0");
  auto cid_id = m_file->reserve();
  m_file->write(cid_id, cid.to_string());
  auto unicode_id = m_file->reserve();
  m_file->write_stream(unicode_id, PdfDictionary(), make_to_unicode(
    sfnt.m_unicodes, font.m_usage, true, font.m_first, font.m_last));
  auto type0 = PdfDictionary("Font");
  type0.add("Subtype", "/Type0");
  type0.add("BaseFont", name);
  type0.add("Encoding", "/Identity-H");
  type0.add("DescendantFonts", to_pdf_array({to_pdf_reference(cid_id)}));
  type0.add("ToUnicode", to_pdf_reference(unicode_id));
  m_file->write(font.m_id, type0.to_string());
}

void PdfFonts::write_type3(const Font& font) {
  auto& face = *font.m_face;
  auto& sfnt = face.m_sfnt;
  auto& typeface = *face.m_typeface;
  auto first = font.m_first;
  auto last = font.m_last;
  auto is_used = [&] (std::uint16_t glyph) {
    auto code = glyph == 0 ? 0 : glyph - first + 1;
    return font.m_usage[static_cast<std::size_t>(code)];
  };
  while(last > first && !is_used(last)) {
    --last;
  }
  auto em = static_cast<double>(sfnt.m_units_per_em);
  auto dictionary = PdfDictionary("Font");
  dictionary.add("Subtype", "/Type3");
  dictionary.add("FontMatrix", to_pdf_array({to_pdf_number(1 / em), "0", "0",
    to_pdf_number(-1 / em), "0", "0"}));
  auto procedures = PdfDictionary();
  auto differences = std::vector<std::string>({"0"});
  auto widths = std::vector<std::string>();
  auto box = std::optional<std::array<int, 4>>();
  auto glyphs = std::vector<std::uint16_t>({0});
  for(auto glyph = first; glyph <= last; ++glyph) {
    glyphs.push_back(glyph);
    if(glyph == std::numeric_limits<std::uint16_t>::max()) {
      break;
    }
  }
  for(auto glyph : glyphs) {
    if(glyph != 0 && !is_used(glyph)) {
      differences.push_back("/g0");
      widths.push_back("0");
      continue;
    }
    auto name = std::format("g{:X}", glyph);
    auto advance = static_cast<double>(sfnt.m_advances[glyph]);
    auto outline = transform(typeface.get_outline(glyph), make_scale(em, em));
    auto glyph_box = std::array<int, 4>({0, 0, 0, 0});
    if(!outline.is_empty()) {
      auto bounds = get_bounds(outline);
      glyph_box = std::array<int, 4>({
        static_cast<int>(std::floor(static_cast<float>(bounds.m_x))) - 1,
        static_cast<int>(std::floor(static_cast<float>(bounds.m_y))) - 1,
        static_cast<int>(std::ceil(
          static_cast<float>(bounds.get_right()))) + 1,
        static_cast<int>(std::ceil(
          static_cast<float>(bounds.get_bottom()))) + 1});
      if(!box) {
        box = glyph_box;
      } else {
        (*box)[0] = std::min((*box)[0], glyph_box[0]);
        (*box)[1] = std::min((*box)[1], glyph_box[1]);
        (*box)[2] = std::max((*box)[2], glyph_box[2]);
        (*box)[3] = std::max((*box)[3], glyph_box[3]);
      }
    }
    auto content = std::format("{} 0 {} {} {} {} d1\n", to_pdf_number(advance),
      glyph_box[0], glyph_box[1], glyph_box[2], glyph_box[3]);
    if(!outline.is_empty()) {
      content += to_pdf_path(outline);
      content += get_pdf_fill(outline);
    }
    auto id = m_file->reserve();
    m_file->write_stream(id, PdfDictionary(), std::move(content));
    procedures.add(name, to_pdf_reference(id));
    differences.push_back(to_pdf_name(name));
    widths.push_back(to_pdf_number(advance));
  }
  dictionary.add("FirstChar", "0");
  dictionary.add("LastChar", std::to_string(last - first + 1));
  auto bounds = box.value_or(std::array<int, 4>({0, 0, 0, 0}));
  dictionary.add("FontBBox", to_pdf_array({std::to_string(bounds[0]),
    std::to_string(bounds[3]), std::to_string(bounds[2]),
    std::to_string(bounds[1])}));
  dictionary.add("CIDToGIDMap", "/Identity");
  auto unicode_id = m_file->reserve();
  m_file->write_stream(unicode_id, PdfDictionary(), make_to_unicode(
    sfnt.m_unicodes, font.m_usage, false, first, last));
  dictionary.add("ToUnicode", to_pdf_reference(unicode_id));
  dictionary.add(
    "FontDescriptor", to_pdf_reference(get_type3_descriptor(face)));
  dictionary.add("Widths", to_pdf_array(widths));
  auto encoding = PdfDictionary("Encoding");
  encoding.add("Differences", to_pdf_array(differences));
  dictionary.add("Encoding", encoding.to_string());
  dictionary.add("CharProcs", procedures.to_string());
  m_file->write(font.m_id, dictionary.to_string());
}

int PdfFonts::get_type3_descriptor(Face& face) {
  if(auto descriptor = m_descriptors.find(&face);
      descriptor != m_descriptors.end()) {
    return descriptor->second;
  }
  auto& sfnt = face.m_sfnt;
  auto descriptor = PdfDictionary("FontDescriptor");
  descriptor.add(
    "FontName", to_pdf_name(face.m_typeface->get_names().m_postscript_name));
  descriptor.add("ItalicAngle", std::to_string(sfnt.m_italic_angle));
  if(face.m_cap_height != 0) {
    descriptor.add("CapHeight", std::to_string(face.m_cap_height));
  }
  if(face.m_stem != 0) {
    descriptor.add("StemV", std::to_string(face.m_stem));
  }
  auto x_height = face.m_typeface->get_metrics().m_x_height *
    sfnt.m_units_per_em;
  if(x_height != 0) {
    descriptor.add("XHeight", to_pdf_number(x_height));
  }
  descriptor.add("Flags", std::to_string(sfnt.m_style | PDF_SYMBOLIC));
  auto id = m_file->reserve();
  m_file->write(id, descriptor.to_string());
  m_descriptors.emplace(&face, id);
  return id;
}
