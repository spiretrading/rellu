#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QImageReader>
#include <QSize>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>
#include "Rellu/Geometry/SvgPath.hpp"
#include "Rellu/Io/SvgImport.hpp"
#include "Rellu/Localization/Localization.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  const auto XLINK = u"http://www.w3.org/1999/xlink"_s;
  const auto TEXT_NODE = u"#text"_s;
  const auto DEFAULT_FAMILY = u"Helvetica Neue"_s;
  constexpr auto DEFAULT_SIZE = 300.0;
  constexpr auto WINDOW_SIZE = 1.0;
  constexpr auto LAYOUT_UNITS = 100.0;
  constexpr auto FONT_SIZE = 20.0;
  constexpr auto PRECISION = 1e5;
  constexpr auto TOLERANCE = 0.01;
  constexpr auto FRAME_TOLERANCE = 0.15;
  constexpr auto SCRIPT_SCALE = 0.5833;
  constexpr auto MAXIMUM_NAME_LENGTH = 256;

  struct Declaration {
    QString m_name;
    QString m_value;
    bool m_is_important = false;
  };

  struct Element {
    QString m_name;
    QHash<QString, QString> m_attributes;
    std::vector<Declaration> m_declarations;
    QString m_text;
    std::vector<Element> m_children;
  };

  struct Item {
    NodeHeader m_header;
    Payload m_payload;
    std::vector<Item> m_children;
    std::vector<Effect> m_effects;
    double m_fill_alpha = 1;
    bool m_is_image = false;
    bool m_is_failed = false;
  };

  struct Compound {
    QString m_type;
    QString m_id;
    QStringList m_classes;
  };

  struct Selector {
    std::vector<Compound> m_compounds;
    std::vector<bool> m_is_child;
    int m_specificity = 0;
  };

  struct Rule {
    Selector m_selector;
    std::vector<Declaration> m_declarations;
  };

  using Properties = QHash<QString, QString>;

  const auto INHERITED = std::unordered_set<std::u16string>({u"fill",
    u"fill-opacity", u"fill-rule", u"stroke", u"stroke-width",
    u"stroke-linecap", u"stroke-linejoin", u"stroke-miterlimit",
    u"stroke-dasharray", u"stroke-dashoffset", u"stroke-opacity",
    u"visibility", u"color", u"font-family", u"font-size", u"font-style",
    u"font-weight", u"font-variant", u"letter-spacing", u"word-spacing",
    u"text-anchor"});

  const auto GRAPHICS = std::unordered_set<std::u16string>({u"path",
    u"circle", u"ellipse", u"rect", u"polygon", u"polyline", u"line", u"g",
    u"svg", u"text", u"image", u"use", u"switch"});

  const auto GEOMETRY = std::unordered_set<std::u16string>({u"x", u"y",
    u"cx", u"cy", u"r", u"rx", u"ry", u"width", u"height", u"x1", u"x2",
    u"y1", u"y2", u"dx", u"dy", u"offset", u"transform", u"d", u"points"});

  const auto SKETCH_SHADOW = QRegularExpression(
    uR"(^filter/(?:(?:femorphology\+)?feoffset\+fegaussianblur\+)"
    uR"((?:fecomposite\+)?fecolormatrix\+)+femerge/(?:femergenode\+)+)"
    uR"(femergenode$)"_s);

  const auto SKETCH_INNER_SHADOW = QRegularExpression(
    uR"(^filter/(fegaussianblur\+feoffset\+fecomposite\+fecolormatrix\+?)+)"
    uR"((femerge/femergenode(\+femergenode)+)?$)"_s);

  const auto WRITER_SHADOW = QRegularExpression(
    uR"(^filter/feoffset\+fegaussianblur\+feflood\+fecomposite)"
    uR"((\+fecomposite)?$)"_s);

  const auto WRITER_INNER_SHADOW = QRegularExpression(
    uR"(^filter/feoffset\+fegaussianblur\+feflood\+fecomposite\+)"
    uR"(fecomposite\+fecomposite$)"_s);

  const auto PLAIN_NUMBER = QRegularExpression(
    uR"(^\s*[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?\s*$)"_s);

  const auto PIXELS = QRegularExpression(uR"(px\s*$)"_s);

  const auto LENGTH = QRegularExpression(
    uR"(^\s*([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?))"
    uR"((em|ex|px|in|cm|mm|pt|pc|%)?\s*$)"_s,
    QRegularExpression::CaseInsensitiveOption);

  const auto IMAGE_DATA = QRegularExpression(
    uR"(^data:(?:image|img)/([^;]+);base64,)"_s);

  double round(double value) {
    return std::round(value * PRECISION) / PRECISION;
  }

  double to_single(double value) {
    return static_cast<double>(static_cast<float>(value));
  }

  double to_layout_units(double value) {
    return to_single(std::round(value * LAYOUT_UNITS) / LAYOUT_UNITS);
  }

  Matrix to_single(const Matrix& matrix) {
    return Matrix(to_single(round(matrix.m_a)), to_single(round(matrix.m_b)),
      to_single(round(matrix.m_c)), to_single(round(matrix.m_d)),
      to_single(round(matrix.m_tx)), to_single(round(matrix.m_ty)));
  }

  double quantize(double alpha) {
    return std::round(std::clamp(alpha, 0.0, 1.0) * 255) / 255;
  }

  bool is_close(double left, double right, double tolerance) {
    return std::abs(left - right) <= tolerance;
  }

  bool is_truthy(double value) {
    return value != 0 && !std::isnan(value);
  }

  void to_bounding_box(GradientFill& fill, const Rect& bounds, Point offset) {
    if(!fill.m_transform.is_identity() || bounds.m_width <= 0 ||
        bounds.m_height <= 0) {
      fill.m_transform =
        make_translation(-offset.m_x, -offset.m_y) * fill.m_transform;
      return;
    }
    auto map = [&] (Point point) {
      return Point(round((point.m_x - bounds.m_x) / bounds.m_width),
        round((point.m_y - bounds.m_y) / bounds.m_height));
    };
    if(auto linear = std::get_if<LinearGradient>(&fill.m_geometry)) {
      linear->m_start = map(linear->m_start);
      linear->m_end = map(linear->m_end);
    } else if(auto radial = std::get_if<RadialGradient>(&fill.m_geometry)) {
      auto single = [] (Point point) {
        return Point(to_single(point.m_x), to_single(point.m_y));
      };
      radial->m_center = single(map(radial->m_center));
      radial->m_focus = single(map(radial->m_focus));
      radial->m_radius =
        to_single(round(radial->m_radius / bounds.m_height));
      auto scale = bounds.m_height / bounds.m_width;
      fill.m_transform = Matrix(to_single(scale), 0, 0, 1,
        to_single((1 - scale) / 2), 0);
    }
    fill.m_units = GradientUnits::OBJECT_BOUNDING_BOX;
  }

  Path close_explicitly(const Path& path) {
    auto& segments = path.get_segments();
    auto is_needed = false;
    auto start = Point(0, 0);
    auto current = Point(0, 0);
    auto end_of = [] (const Segment& segment) {
      if(segment.m_type == SegmentType::QUADRATIC) {
        return segment.m_points[1];
      } else if(segment.m_type == SegmentType::CUBIC) {
        return segment.m_points[2];
      }
      return segment.m_points[0];
    };
    for(auto& segment : segments) {
      if(segment.m_type == SegmentType::MOVE) {
        start = segment.m_points[0];
        current = start;
      } else if(segment.m_type == SegmentType::CLOSE) {
        is_needed |= current != start;
        current = start;
      } else {
        current = end_of(segment);
      }
    }
    if(!is_needed) {
      return path;
    }
    auto result = Path(path.get_fill_rule());
    for(auto& segment : segments) {
      auto& points = segment.m_points;
      if(segment.m_type == SegmentType::MOVE) {
        start = points[0];
        current = start;
        result.move_to(start);
      } else if(segment.m_type == SegmentType::LINE) {
        current = points[0];
        result.line_to(current);
      } else if(segment.m_type == SegmentType::QUADRATIC) {
        current = points[1];
        result.quadratic_to(points[0], current);
      } else if(segment.m_type == SegmentType::CUBIC) {
        current = points[2];
        result.cubic_to(points[0], points[1], current);
      } else {
        if(current != start) {
          result.line_to(start);
        }
        result.close();
        current = start;
      }
    }
    return result;
  }

  std::vector<Declaration> parse_declarations(QStringView text) {
    auto declarations = std::vector<Declaration>();
    for(auto entry : text.split(u';')) {
      auto colon = entry.indexOf(u':');
      if(colon < 0) {
        continue;
      }
      auto declaration = Declaration();
      declaration.m_name = entry.left(colon).trimmed().toString().toLower();
      declaration.m_value = entry.mid(colon + 1).trimmed().toString();
      if(auto bang = declaration.m_value.lastIndexOf(u'!'); bang >= 0 &&
          declaration.m_value.mid(bang + 1).trimmed().compare(
            u"important"_s, Qt::CaseInsensitive) == 0) {
        declaration.m_value = declaration.m_value.left(bang).trimmed();
        declaration.m_is_important = true;
      }
      if(!declaration.m_name.isEmpty()) {
        declarations.push_back(std::move(declaration));
      }
    }
    return declarations;
  }

  bool is_name_character(QChar character) {
    return character.isLetterOrNumber() || character == u'-' ||
      character == u'_' || character.unicode() > 127;
  }

  std::optional<Compound> parse_compound(QStringView text) {
    auto compound = Compound();
    auto position = qsizetype(0);
    auto read_name = [&] {
      auto start = position;
      while(position < text.size() && is_name_character(text[position])) {
        ++position;
      }
      return text.mid(start, position - start).toString();
    };
    if(text.isEmpty()) {
      return std::nullopt;
    }
    if(text[0] == u'*') {
      ++position;
    } else if(is_name_character(text[0])) {
      compound.m_type = read_name();
    }
    while(position < text.size()) {
      auto marker = text[position];
      ++position;
      auto name = read_name();
      if(name.isEmpty()) {
        return std::nullopt;
      }
      if(marker == u'.') {
        compound.m_classes.push_back(name);
      } else if(marker == u'#') {
        compound.m_id = name;
      } else {
        return std::nullopt;
      }
    }
    return compound;
  }

  std::optional<Selector> parse_selector(QStringView text) {
    auto spaced = text.toString();
    spaced.replace(u">"_s, u" > "_s);
    auto selector = Selector();
    auto is_child = false;
    for(auto& token : spaced.split(QRegularExpression(u"\\s+"_s),
        Qt::SkipEmptyParts)) {
      if(token == u">"_s) {
        if(selector.m_compounds.empty() || is_child) {
          return std::nullopt;
        }
        is_child = true;
        continue;
      }
      auto compound = parse_compound(token);
      if(!compound) {
        return std::nullopt;
      }
      selector.m_specificity += (compound->m_id.isEmpty() ? 0 : 10000) +
        100 * static_cast<int>(compound->m_classes.size()) +
        (compound->m_type.isEmpty() ? 0 : 1);
      selector.m_is_child.push_back(is_child);
      selector.m_compounds.push_back(std::move(*compound));
      is_child = false;
    }
    if(selector.m_compounds.empty() || is_child) {
      return std::nullopt;
    }
    return selector;
  }

  QString strip_comments(const QString& text) {
    auto result = QString();
    auto position = qsizetype(0);
    while(true) {
      auto open = text.indexOf(u"/*"_s, position);
      if(open < 0) {
        result += text.mid(position);
        break;
      }
      result += text.mid(position, open - position);
      auto close = text.indexOf(u"*/"_s, open + 2);
      if(close < 0) {
        break;
      }
      position = close + 2;
    }
    result.replace(u"<!--"_s, u" "_s);
    result.replace(u"-->"_s, u" "_s);
    return result;
  }

  qsizetype skip_block(const QString& text, qsizetype open) {
    auto depth = 0;
    for(auto i = open; i < text.size(); ++i) {
      if(text[i] == u'{') {
        ++depth;
      } else if(text[i] == u'}') {
        --depth;
        if(depth == 0) {
          return i + 1;
        }
      }
    }
    return text.size();
  }

  void parse_stylesheet(const QString& source, std::vector<Rule>& rules) {
    auto text = strip_comments(source);
    auto position = qsizetype(0);
    while(position < text.size()) {
      while(position < text.size() && text[position].isSpace()) {
        ++position;
      }
      if(position >= text.size()) {
        break;
      }
      if(text[position] == u'@') {
        auto open = text.indexOf(u'{', position);
        auto end = text.indexOf(u';', position);
        if(end >= 0 && (open < 0 || end < open)) {
          position = end + 1;
        } else if(open >= 0) {
          position = skip_block(text, open);
        } else {
          break;
        }
        continue;
      }
      auto open = text.indexOf(u'{', position);
      if(open < 0) {
        break;
      }
      auto close = text.indexOf(u'}', open);
      if(close < 0) {
        close = text.size();
      }
      auto declarations =
        parse_declarations(QStringView(text).mid(open + 1, close - open - 1));
      for(auto part :
          QStringView(text).mid(position, open - position).split(u',')) {
        if(auto selector = parse_selector(part.trimmed())) {
          rules.push_back(Rule(std::move(*selector), declarations));
        }
      }
      position = close + 1;
    }
  }

  bool matches(const Compound& compound, const Element& element) {
    if(!compound.m_type.isEmpty() && compound.m_type != element.m_name) {
      return false;
    }
    if(!compound.m_id.isEmpty() &&
        element.m_attributes.value(u"id"_s) != compound.m_id) {
      return false;
    }
    if(compound.m_classes.isEmpty()) {
      return true;
    }
    auto classes = element.m_attributes.value(u"class"_s).split(
      QRegularExpression(u"\\s+"_s), Qt::SkipEmptyParts);
    for(auto& name : compound.m_classes) {
      if(!classes.contains(name)) {
        return false;
      }
    }
    return true;
  }

  bool matches(const Selector& selector, std::size_t index,
      std::size_t position, const std::vector<const Element*>& path) {
    if(!matches(selector.m_compounds[index], *path[position])) {
      return false;
    }
    if(index == 0) {
      return true;
    }
    if(selector.m_is_child[index]) {
      return position != 0 && matches(selector, index - 1, position - 1, path);
    }
    for(auto i = position; i != 0; --i) {
      if(matches(selector, index - 1, i - 1, path)) {
        return true;
      }
    }
    return false;
  }

  void apply(Element& element, const Declaration& declaration) {
    if(!GEOMETRY.contains(declaration.m_name.toStdU16String())) {
      element.m_attributes.insert(declaration.m_name, declaration.m_value);
    }
  }

  void cascade(Element& element, const std::vector<Rule>& rules,
      std::vector<const Element*>& path) {
    if(element.m_name == TEXT_NODE) {
      return;
    }
    path.push_back(&element);
    struct Match {
      const Declaration* m_declaration;
      int m_specificity;
    };
    auto found = std::vector<Match>();
    for(auto& rule : rules) {
      auto& selector = rule.m_selector;
      if(matches(selector, selector.m_compounds.size() - 1, path.size() - 1,
          path)) {
        for(auto& declaration : rule.m_declarations) {
          found.push_back(Match(&declaration, selector.m_specificity));
        }
      }
    }
    std::ranges::stable_sort(found, [] (const auto& left, const auto& right) {
      return left.m_specificity < right.m_specificity;
    });
    for(auto is_important : {false, true}) {
      for(auto& match : found) {
        if(match.m_declaration->m_is_important == is_important) {
          apply(element, *match.m_declaration);
        }
      }
      for(auto& declaration : element.m_declarations) {
        if(declaration.m_is_important == is_important) {
          apply(element, declaration);
        }
      }
    }
    element.m_declarations.clear();
    for(auto& child : element.m_children) {
      cascade(child, rules, path);
    }
    path.pop_back();
  }

  void collect_stylesheets(const Element& element, std::vector<Rule>& rules) {
    if(element.m_name == u"style"_s) {
      auto text = QString();
      for(auto& child : element.m_children) {
        if(child.m_name == TEXT_NODE) {
          text += child.m_text;
        }
      }
      parse_stylesheet(text, rules);
      return;
    }
    for(auto& child : element.m_children) {
      collect_stylesheets(child, rules);
    }
  }

  Element read_element(QXmlStreamReader& reader) {
    auto element = Element();
    element.m_name = reader.name().toString();
    for(auto& attribute : reader.attributes()) {
      auto name = attribute.name().toString();
      auto space = attribute.namespaceUri();
      if(space == XLINK && name == u"href"_s) {
        element.m_attributes.insert(name, attribute.value().toString());
      } else if(space.isEmpty() && name == u"href"_s) {
        continue;
      } else if(space.isEmpty() && name == u"style"_s) {
        element.m_declarations = parse_declarations(attribute.value());
      } else if(space.isEmpty()) {
        element.m_attributes.insert(name, attribute.value().toString());
      } else {
        element.m_attributes.insert(
          attribute.qualifiedName().toString(), attribute.value().toString());
      }
    }
    while(!reader.atEnd()) {
      auto token = reader.readNext();
      if(token == QXmlStreamReader::StartElement) {
        element.m_children.push_back(read_element(reader));
      } else if(token == QXmlStreamReader::Characters) {
        if(!element.m_children.empty() &&
            element.m_children.back().m_name == TEXT_NODE) {
          element.m_children.back().m_text += reader.text();
        } else {
          auto text = Element();
          text.m_name = TEXT_NODE;
          text.m_text = reader.text().toString();
          element.m_children.push_back(std::move(text));
        }
      } else if(token == QXmlStreamReader::EndElement) {
        break;
      }
    }
    return element;
  }

  std::optional<Element> parse(const QString& text) {
    auto reader = QXmlStreamReader(text);
    auto root = std::optional<Element>();
    while(!reader.atEnd()) {
      auto token = reader.readNext();
      if(token == QXmlStreamReader::StartElement) {
        if(root) {
          return std::nullopt;
        }
        root = read_element(reader);
      }
    }
    if(reader.hasError() || !root || root->m_name != u"svg"_s) {
      return std::nullopt;
    }
    return root;
  }

  void index(const Element& element, QHash<QString, const Element*>& ids) {
    if(auto id = element.m_attributes.value(u"id"_s); !id.isEmpty() &&
        !ids.contains(id)) {
      ids.insert(id, &element);
    }
    for(auto& child : element.m_children) {
      index(child, ids);
    }
  }

  std::vector<const Element*> get_elements(const Element& element) {
    auto elements = std::vector<const Element*>();
    for(auto& child : element.m_children) {
      if(child.m_name != TEXT_NODE) {
        elements.push_back(&child);
      }
    }
    return elements;
  }

  QString describe(const Element& element) {
    auto text = element.m_name.toLower();
    auto children = QStringList();
    for(auto child : get_elements(element)) {
      children.push_back(describe(*child));
    }
    if(!children.isEmpty()) {
      text += u'/' + children.join(u'+');
    }
    return text;
  }

  void find_all(const Element& element, const QString& name,
      std::vector<std::pair<const Element*, const Element*>>& found) {
    auto elements = get_elements(element);
    for(auto i = std::size_t(0); i != elements.size(); ++i) {
      if(elements[i]->m_name == name) {
        found.emplace_back(elements[i],
          i + 1 < elements.size() ? elements[i + 1] : nullptr);
      }
      find_all(*elements[i], name, found);
    }
  }

  class NumberReader {
    public:
      explicit NumberReader(QStringView text)
        : m_text(text),
          m_position(0) {}

      std::optional<double> read() {
        skip();
        auto start = m_position;
        auto is_digit = [&] (qsizetype i) {
          return i < m_text.size() && m_text[i] >= u'0' && m_text[i] <= u'9';
        };
        if(m_position < m_text.size() &&
            (m_text[m_position] == u'+' || m_text[m_position] == u'-')) {
          ++m_position;
        }
        auto digits = false;
        while(is_digit(m_position)) {
          ++m_position;
          digits = true;
        }
        if(m_position < m_text.size() && m_text[m_position] == u'.') {
          ++m_position;
          while(is_digit(m_position)) {
            ++m_position;
            digits = true;
          }
        }
        if(!digits) {
          m_position = start;
          return std::nullopt;
        }
        if(m_position < m_text.size() &&
            (m_text[m_position] == u'e' || m_text[m_position] == u'E')) {
          auto exponent = m_position + 1;
          if(exponent < m_text.size() &&
              (m_text[exponent] == u'+' || m_text[exponent] == u'-')) {
            ++exponent;
          }
          if(is_digit(exponent)) {
            m_position = exponent;
            while(is_digit(m_position)) {
              ++m_position;
            }
          }
        }
        auto value = false;
        auto number =
          m_text.mid(start, m_position - start).toDouble(&value);
        if(!value) {
          return std::nullopt;
        }
        return number;
      }

      QStringView read_unit() {
        auto start = m_position;
        while(m_position < m_text.size() && (m_text[m_position].isLetter() ||
            m_text[m_position] == u'%')) {
          ++m_position;
        }
        return m_text.mid(start, m_position - start);
      }

      bool is_done() {
        skip();
        return m_position >= m_text.size();
      }

    private:
      QStringView m_text;
      qsizetype m_position;

      void skip() {
        while(m_position < m_text.size() && (m_text[m_position].isSpace() ||
            m_text[m_position] == u',')) {
          ++m_position;
        }
      }
  };

  std::vector<double> parse_numbers(QStringView text) {
    auto reader = NumberReader(text);
    auto numbers = std::vector<double>();
    while(auto number = reader.read()) {
      numbers.push_back(*number);
    }
    return numbers;
  }

  double parse_float(QStringView text) {
    auto value = text.trimmed();
    if(value.isEmpty() || value.front() == u',') {
      return std::numeric_limits<double>::quiet_NaN();
    }
    auto reader = NumberReader(value);
    return reader.read().value_or(std::numeric_limits<double>::quiet_NaN());
  }

  std::optional<double> parse_length(QStringView text, double reference,
      double font_size = FONT_SIZE, std::optional<double> ex = std::nullopt) {
    auto reader = NumberReader(text.trimmed());
    auto number = reader.read();
    if(!number) {
      return std::nullopt;
    }
    auto unit = reader.read_unit().toString().toLower();
    if(unit.isEmpty() || unit == u"px"_s) {
      return *number;
    } else if(unit == u"%"_s) {
      return *number * reference / 100;
    } else if(unit == u"pt"_s) {
      return *number * 4 / 3;
    } else if(unit == u"pc"_s) {
      return *number * 16;
    } else if(unit == u"in"_s) {
      return *number * 96;
    } else if(unit == u"cm"_s) {
      return *number * 96 / 2.54;
    } else if(unit == u"mm"_s) {
      return *number * 96 / 25.4;
    } else if(unit == u"em"_s) {
      return *number * font_size;
    } else if(unit == u"ex"_s) {
      return *number * ex.value_or(font_size / 2);
    }
    return std::nullopt;
  }

  double parse_fraction(QStringView text, double fallback) {
    auto reader = NumberReader(text.trimmed());
    auto number = reader.read();
    if(!number) {
      return fallback;
    }
    if(reader.read_unit() == u"%"_s) {
      return *number / 100;
    }
    return *number;
  }

  double resolve_font_size(
      QStringView text, double parent, std::optional<double> ex) {
    auto value = text.trimmed().toString().toLower();
    static const auto KEYWORDS = std::array<std::pair<const char16_t*,
      double>, 7>({std::pair(u"xx-small", 9.0), std::pair(u"x-small", 10.0),
      std::pair(u"small", 13.0), std::pair(u"medium", 16.0),
      std::pair(u"large", 18.0), std::pair(u"x-large", 24.0),
      std::pair(u"xx-large", 32.0)});
    for(auto& keyword : KEYWORDS) {
      if(value == QStringView(keyword.first)) {
        return keyword.second;
      }
    }
    if(value == u"larger"_s) {
      return parent * 1.2;
    } else if(value == u"smaller"_s) {
      return parent / 1.2;
    }
    return parse_length(value, parent, parent, ex).value_or(parent);
  }

  double to_js_number(QStringView text) {
    if(text.trimmed().isEmpty()) {
      return 0;
    } else if(!PLAIN_NUMBER.matchView(text).hasMatch()) {
      return std::numeric_limits<double>::quiet_NaN();
    }
    return text.trimmed().toDouble();
  }

  double to_json(double value) {
    if(std::isnan(value)) {
      return 0;
    }
    return value;
  }

  double to_radians(double degrees) {
    return degrees * std::numbers::pi / 180;
  }

  Matrix parse_transform(QStringView text) {
    auto matrix = Matrix();
    auto position = qsizetype(0);
    while(true) {
      auto open = text.indexOf(u'(', position);
      if(open < 0) {
        break;
      }
      auto close = text.indexOf(u')', open);
      if(close < 0) {
        break;
      }
      auto name = text.mid(position, open - position).trimmed();
      while(!name.isEmpty() &&
          (name.front() == u',' || name.front().isSpace())) {
        name = name.mid(1);
      }
      auto arguments = parse_numbers(text.mid(open + 1, close - open - 1));
      auto size = arguments.size();
      auto argument = [&] (std::size_t i, double fallback) {
        return i < size ? arguments[i] : fallback;
      };
      auto step = Matrix();
      if(name == u"matrix" && size == 6) {
        step = Matrix(arguments[0], arguments[1], arguments[2], arguments[3],
          arguments[4], arguments[5]);
      } else if(name == u"translate" && size >= 1) {
        step = make_translation(arguments[0], argument(1, 0));
      } else if(name == u"scale" && size >= 1) {
        step = make_scale(arguments[0], argument(1, arguments[0]));
      } else if(name == u"rotate" && size >= 1) {
        step = make_rotation(to_radians(arguments[0]),
          Point(argument(1, 0), argument(2, 0)));
      } else if(name == u"skewX" && size == 1) {
        step = Matrix(1, 0, std::tan(to_radians(arguments[0])), 1, 0, 0);
      } else if(name == u"skewY" && size == 1) {
        step = Matrix(1, std::tan(to_radians(arguments[0])), 0, 1, 0, 0);
      } else {
        return Matrix();
      }
      matrix = matrix * step;
      position = close + 1;
    }
    return matrix;
  }

  std::optional<Color> parse_hex(QStringView digits) {
    auto value = false;
    auto number = digits.toUInt(&value, 16);
    if(!value) {
      return std::nullopt;
    }
    auto expand = [] (unsigned int nibble) {
      return static_cast<int>(nibble * 17);
    };
    if(digits.size() == 3) {
      return make_color(expand((number >> 8) & 0xF),
        expand((number >> 4) & 0xF), expand(number & 0xF));
    } else if(digits.size() == 4) {
      return make_color(expand((number >> 12) & 0xF),
        expand((number >> 8) & 0xF), expand((number >> 4) & 0xF),
        expand(number & 0xF) / 255.0);
    } else if(digits.size() == 6) {
      return make_color(static_cast<int>((number >> 16) & 0xFF),
        static_cast<int>((number >> 8) & 0xFF),
        static_cast<int>(number & 0xFF));
    } else if(digits.size() == 8) {
      return make_color(static_cast<int>((number >> 24) & 0xFF),
        static_cast<int>((number >> 16) & 0xFF),
        static_cast<int>((number >> 8) & 0xFF), (number & 0xFF) / 255.0);
    }
    return std::nullopt;
  }

  std::optional<Color> parse_function_color(QStringView text) {
    auto open = text.indexOf(u'(');
    auto close = text.lastIndexOf(u')');
    if(open < 0 || close < open) {
      return std::nullopt;
    }
    auto name = text.left(open).trimmed().toString().toLower();
    if(name != u"rgb"_s && name != u"rgba"_s) {
      return std::nullopt;
    }
    auto arguments = text.mid(open + 1, close - open - 1).toString();
    arguments.replace(u'/', u' ');
    auto reader = NumberReader(arguments);
    auto channels = std::vector<double>();
    while(auto number = reader.read()) {
      auto is_percent = reader.read_unit() == u"%";
      if(channels.size() < 3) {
        channels.push_back(is_percent ? *number * 255 / 100 : *number);
      } else {
        channels.push_back(is_percent ? *number / 100 : *number);
      }
    }
    if(channels.size() != 3 && channels.size() != 4) {
      return std::nullopt;
    }
    auto channel = [&] (std::size_t i) {
      return static_cast<int>(
        std::round(std::clamp(channels[i], 0.0, 255.0)));
    };
    auto alpha = channels.size() == 4 ? std::clamp(channels[3], 0.0, 1.0) : 1;
    return make_color(channel(0), channel(1), channel(2), alpha);
  }

  std::optional<Color> parse_color(QStringView text) {
    auto value = text.trimmed();
    if(value.startsWith(u'#')) {
      return parse_hex(value.mid(1));
    } else if(value.contains(u'(')) {
      return parse_function_color(value);
    } else if(value.compare(u"transparent", Qt::CaseInsensitive) == 0) {
      return Color(0, 0, 0, 0);
    }
    auto color = QColor::fromString(value.toString().toLower());
    if(!color.isValid()) {
      return std::nullopt;
    }
    return make_color(color.red(), color.green(), color.blue());
  }

  QString strip_url(QStringView text) {
    auto value = text.trimmed();
    if(!value.startsWith(u"url(")) {
      return QString();
    }
    auto close = value.indexOf(u')');
    if(close < 0) {
      return QString();
    }
    auto reference = value.mid(4, close - 4).trimmed();
    if(reference.size() >= 2 && (reference.front() == u'"' ||
        reference.front() == u'\'')) {
      reference = reference.mid(1, reference.size() - 2);
    }
    if(reference.startsWith(u'#')) {
      reference = reference.mid(1);
    }
    return reference.toString();
  }

  QString to_reference(QStringView text) {
    if(auto reference = strip_url(text); !reference.isEmpty()) {
      return reference;
    }
    auto value = text.trimmed();
    if(value.startsWith(u'#')) {
      value = value.mid(1);
    }
    return value.toString();
  }

  QString to_style_name(QStringView style, QStringView weight) {
    static const auto NAMES = std::array<const char*, 9>({"Thin",
      "ExtraLight", "Light", "Regular", "Medium", "SemiBold", "Bold",
      "ExtraBold", "Black"});
    auto value = weight.trimmed().toString().toLower();
    auto number = 400.0;
    if(value == u"bold"_s || value == u"bolder"_s) {
      number = 700;
    } else if(value == u"lighter"_s) {
      number = 300;
    } else if(auto parsed = NumberReader(value).read()) {
      number = *parsed;
    }
    auto index = static_cast<std::size_t>(
      std::clamp(std::round(number / 100), 1.0, 9.0)) - 1;
    auto is_italic = style.trimmed().compare(u"italic", Qt::CaseInsensitive) ==
      0 || style.trimmed().compare(u"oblique", Qt::CaseInsensitive) == 0;
    if(!is_italic) {
      return QString::fromLatin1(NAMES[index]);
    } else if(index == 3) {
      return u"Italic"_s;
    }
    return QString::fromLatin1(NAMES[index]) + u" Italic"_s;
  }

  QString to_family(QStringView text) {
    auto value = text.toString();
    value.remove(u'\'');
    value.remove(u'"');
    auto family = value.section(u',', 0, 0).trimmed();
    if(family.isEmpty()) {
      return DEFAULT_FAMILY;
    }
    return family;
  }

  QStringList merge_decorations(
      const QStringList& inherited, const QStringList& own) {
    if(own.isEmpty()) {
      return inherited;
    }
    if(inherited.isEmpty()) {
      return own;
    }
    auto result = inherited;
    for(auto& entry : own) {
      if(!result.contains(entry)) {
        result.push_back(entry);
      }
    }
    if(auto none = result.indexOf(u"none"_s); none >= 0) {
      for(auto entry : {u"underline"_s, u"overline"_s, u"line-through"_s,
          u"blink"_s, u"inherit"_s}) {
        if(result.contains(entry)) {
          result.removeAt(none);
          break;
        }
      }
    }
    return result;
  }

  bool is_same_set(QStringList left, QStringList right) {
    if(left.size() != right.size()) {
      return false;
    }
    for(auto& entry : right) {
      auto index = left.indexOf(entry);
      if(index < 0) {
        return false;
      }
      left.removeAt(index);
    }
    return true;
  }

  Style* find_style(Item& item) {
    if(auto shape = std::get_if<Box<Shape>>(&item.m_payload)) {
      return &shape->edit().m_style;
    } else if(auto text = std::get_if<Box<Text>>(&item.m_payload)) {
      return &text->edit().m_style;
    }
    return nullptr;
  }

  std::vector<Effect>& get_effects(Item& item) {
    if(auto style = find_style(item)) {
      return style->m_effects;
    }
    return item.m_effects;
  }

  bool is_group(const Item& item) {
    return std::holds_alternative<Group>(item.m_payload);
  }

  bool has_inner_shadow(Item& item) {
    return std::ranges::any_of(get_effects(item), [] (const auto& effect) {
      return std::holds_alternative<InnerShadow>(effect);
    });
  }

  void hide_fill(Item& item) {
    if(item.m_is_image) {
      return;
    }
    if(auto shape = std::get_if<Box<Shape>>(&item.m_payload)) {
      auto& style = shape->edit().m_style;
      style.m_fill = disable_fill(style.m_fill);
    }
  }

  void collect_mask(const Item& item, const Matrix& transform,
      std::vector<MaskShape>& shapes) {
    if(item.m_is_failed) {
      return;
    }
    auto matrix = transform * item.m_header.m_transform;
    if(auto shape = std::get_if<Box<Shape>>(&item.m_payload)) {
      auto mask = MaskShape();
      mask.m_transform = matrix;
      mask.m_geometry = (*shape)->m_geometry;
      shapes.push_back(std::move(mask));
    } else if(is_group(item)) {
      for(auto& child : item.m_children) {
        collect_mask(child, matrix, shapes);
      }
    }
  }

  std::string make_content_name(const QString& content) {
    auto name = content;
    name.truncate(MAXIMUM_NAME_LENGTH);
    if(!name.isEmpty() && name.back().isHighSurrogate()) {
      name.chop(1);
    }
    return name.toStdString();
  }

  class Converter {
    public:
      Converter(const Element& root, SvgMeasurer measurer)
          : m_root(&root),
            m_measurer(std::move(measurer)) {
        index(root, m_ids);
        auto box = parse_numbers(root.m_attributes.value(u"viewBox"_s));
        auto width_value = root.m_attributes.value(u"width"_s);
        auto height_value = root.m_attributes.value(u"height"_s);
        auto width = get_root_length(width_value);
        auto height = get_root_length(height_value);
        if(box.size() == 4) {
          m_width = box[2];
          m_height = box[3];
        } else {
          m_width = width != 0 ? width : DEFAULT_SIZE;
          m_height = height != 0 ? height : DEFAULT_SIZE;
        }
      }

      std::vector<Item> convert() {
        auto defaults = Properties();
        defaults.insert(u"font-family"_s, DEFAULT_FAMILY);
        defaults.insert(u"font-size"_s, QString::number(FONT_SIZE));
        m_root_properties = inherit(*m_root, defaults);
        m_root_properties.insert(u"font-family"_s, DEFAULT_FAMILY);
        m_root_properties.insert(u"font-size"_s, QString::number(FONT_SIZE));
        m_root_properties.insert(u"font-style"_s, u"normal"_s);
        m_root_properties.insert(u"font-weight"_s, u"400"_s);
        m_root_properties.insert(u"font-variant"_s, u"normal"_s);
        if(parse_length(m_root_properties.value(u"stroke-width"_s), 0) ==
            0.01) {
          m_root_properties.insert(u"stroke-width"_s, u"1"_s);
        }
        index_font_sizes(*m_root, m_root_properties);
        auto items = std::vector<Item>();
        convert_children(*m_root, m_root_properties, true, items);
        return items;
      }

      std::vector<ImageResource>& get_images() {
        return m_images;
      }



    private:
      struct Run {
        std::size_t m_from = 0;
        std::size_t m_to = 0;
        TextStyle m_style;
        TextAlignment m_alignment = TextAlignment::LEFT;
        QStringList m_decorations;
        double m_fill_alpha = 1;
        double m_alpha = 1;
        double m_opacity = 1;
        bool m_has_stroke = false;
        Stroke m_stroke;
        std::optional<double> m_x;
        std::optional<double> m_y;
        double m_dx = 0;
        double m_dy = 0;
      };

      struct Position {
        std::optional<double> m_x;
        std::optional<double> m_y;
        std::optional<double> m_dx;
        std::optional<double> m_dy;
      };

      struct Scope {
        double m_dx = 0;
        double m_dy = 0;
        bool m_has_x = false;
        bool m_has_y = false;
        bool m_is_first = false;
      };

      struct Cell {
        std::vector<const Element*> m_path;
        double m_baseline = 0;
        Font m_font;
        std::optional<std::size_t> m_row;
      };

      struct TextState {
        const Element* m_element = nullptr;
        std::vector<std::vector<Run>> m_lines;
        bool m_has_line = false;
        QString m_raw;
        bool m_is_last_space = true;
        bool m_is_last_collapsed = false;
        bool m_is_multiline = true;
        std::optional<double> m_x;
        std::optional<double> m_y;
        double m_start_x = 0;
        std::optional<double> m_line_spacing;
        bool m_is_irregular = false;
        bool m_has_position = false;
        std::optional<Point> m_reset;
        std::optional<QString> m_stroke;
        bool m_is_stroke_same = true;
        std::size_t m_row = 0;
        std::vector<const Element*> m_path;
        std::vector<Cell> m_cells;
      };

      struct Extent {
        double m_top = std::numeric_limits<double>::infinity();
        double m_bottom = 0;
      };

      const Element* m_root;
      SvgMeasurer m_measurer;
      QHash<QString, const Element*> m_ids;
      double m_width;
      double m_height;
      QHash<const Element*, double> m_font_sizes;
      Properties m_root_properties;
      std::vector<const Element*> m_uses;
      std::vector<const Element*> m_clips;
      std::vector<const Element*> m_patterns;
      std::vector<ImageResource> m_images;
      QHash<QString, std::pair<ImageId, Size>> m_image_ids;

      const Element* find(const QString& id) const {
        return m_ids.value(id, nullptr);
      }

      Properties inherit(const Element& element,
          const Properties& inherited) const {
        auto properties = inherited;
        auto parent = inherited.value(u"font-size"_s).toDouble();
        if(parent <= 0) {
          parent = FONT_SIZE;
        }
        for(auto entry = element.m_attributes.begin();
            entry != element.m_attributes.end(); ++entry) {
          if(INHERITED.contains(entry.key().toStdU16String()) &&
              entry.value().trimmed() != u"inherit"_s) {
            if(entry.key() == u"font-size"_s) {
              properties.insert(entry.key(), QString::number(
                resolve_font_size(entry.value(), parent,
                  get_ex(entry.value(), make_font(inherited))), 'g', 17));
            } else {
              properties.insert(entry.key(), entry.value().trimmed());
            }
          }
        }
        return properties;
      }

      static QString get(const Properties& properties, const QString& name,
          const QString& fallback) {
        return properties.value(name, fallback);
      }

      static QString get_own(const Element& element, const QString& name,
          const QString& fallback) {
        auto value = element.m_attributes.value(name);
        if(value.isEmpty()) {
          return fallback;
        }
        return value.trimmed();
      }

      static bool is_set(const Element& element, const QString& name) {
        auto value = get_own(element, name, u"none"_s);
        return value != u"none"_s;
      }

      void index_font_sizes(
          const Element& element, const Properties& properties) {
        m_font_sizes.insert(&element, get_font_size(properties));
        for(auto child : get_elements(element)) {
          index_font_sizes(*child, inherit(*child, properties));
        }
      }

      double get_root_length(const QString& value) const {
        auto length = parse_length(value, WINDOW_SIZE, FONT_SIZE,
          get_ex(value, make_root_font()));
        if(!length || *length < 0) {
          return WINDOW_SIZE;
        }
        return *length;
      }

      static Font make_root_font() {
        auto font = Font();
        font.m_family = DEFAULT_FAMILY.toStdString();
        font.m_style = "Regular";
        font.m_size = FONT_SIZE;
        return font;
      }

      static Font make_font(const Properties& properties) {
        auto font = Font();
        font.m_family = to_family(
          get(properties, u"font-family"_s, DEFAULT_FAMILY)).toStdString();
        font.m_style = to_style_name(
          get(properties, u"font-style"_s, u"normal"_s),
          get(properties, u"font-weight"_s, u"400"_s)).toStdString();
        font.m_size = round(get_font_size(properties));
        return font;
      }

      double get_x_height(const Font& font) const {
        if(m_measurer.m_extent) {
          if(auto extent = m_measurer.m_extent(font); extent &&
              extent->m_x_height > 0) {
            return extent->m_x_height;
          }
        }
        return 0.5;
      }

      double get_ex(const Font& font) const {
        return font.m_size * get_x_height(font);
      }

      std::optional<double> get_ex(
          QStringView value, const Font& font) const {
        if(!value.trimmed().endsWith(u"ex", Qt::CaseInsensitive)) {
          return std::nullopt;
        }
        return get_ex(font);
      }

      static QString get_default(const QString& type, const QString& name) {
        if((type == u"use"_s || type == u"svg"_s) &&
            (name == u"width"_s || name == u"height"_s)) {
          return u"100%"_s;
        } else if(type == u"radialGradient"_s && (name == u"cx"_s ||
            name == u"cy"_s || name == u"r"_s)) {
          return u"50%"_s;
        } else if(type == u"linearGradient"_s && name == u"x2"_s) {
          return u"100%"_s;
        }
        return u"0"_s;
      }

      static bool is_vertical(const QString& type, const QString& name) {
        if(type == u"rect"_s) {
          return name == u"y"_s || name == u"ry"_s || name == u"height"_s;
        } else if(type == u"image"_s) {
          return name == u"y"_s || name == u"height"_s;
        } else if(type == u"text"_s) {
          return name == u"y"_s;
        } else if(type == u"circle"_s) {
          return name == u"cy"_s;
        } else if(type == u"ellipse"_s) {
          return name == u"cy"_s || name == u"ry"_s;
        } else if(type == u"line"_s) {
          return name == u"y1"_s || name == u"y2"_s;
        } else if(type == u"tspan"_s) {
          return name == u"y"_s || name == u"dy"_s;
        }
        return false;
      }

      double to_px(
          const QString& value, bool is_vertical, const Font& font) const {
        auto number = parse_float(value);
        if(PLAIN_NUMBER.match(value).hasMatch() ||
            PIXELS.match(value).hasMatch()) {
          return round(number);
        }
        if(std::isnan(number)) {
          return number;
        }
        auto match = LENGTH.match(value);
        if(!match.hasMatch()) {
          return 0;
        }
        auto amount = match.captured(1).toDouble();
        auto unit = match.captured(2).toLower();
        auto result = amount;
        if(unit == u"%"_s) {
          result = amount * (is_vertical ? m_height : m_width) / 100;
        } else if(unit == u"em"_s) {
          result = amount * font.m_size;
        } else if(unit == u"ex"_s) {
          result = amount * get_ex(font);
        } else if(unit == u"in"_s) {
          result = amount * 96;
        } else if(unit == u"cm"_s) {
          result = amount * 96 / 2.54;
        } else if(unit == u"mm"_s) {
          result = amount * 96 / 25.4;
        } else if(unit == u"pt"_s) {
          result = amount * 4 / 3;
        } else if(unit == u"pc"_s) {
          result = amount * 16;
        }
        return round(result);
      }

      double get_length(const Element& element, const QString& name) const {
        auto value = element.m_attributes.contains(name) ?
          element.m_attributes.value(name) :
          get_default(element.m_name, name);
        auto font = make_root_font();
        font.m_size = m_font_sizes.value(&element, FONT_SIZE);
        auto length =
          to_px(value, is_vertical(element.m_name, name), font);
        if(!std::isnan(length)) {
          return length;
        }
        auto fallback = get_default(element.m_name, name);
        if(PLAIN_NUMBER.match(fallback).hasMatch()) {
          return parse_float(fallback);
        }
        return 0;
      }

      double get_stroke_width(const Properties& properties) const {
        auto width = to_px(get(properties, u"stroke-width"_s, u"1"_s), false,
          make_font(properties));
        if(std::isnan(width)) {
          return 1;
        }
        return width;
      }

      static double get_font_size(const Properties& properties) {
        auto size = properties.value(u"font-size"_s).toDouble();
        if(size <= 0) {
          return FONT_SIZE;
        }
        return size;
      }

      void convert_children(const Element& element,
          const Properties& inherited, bool is_top, std::vector<Item>& items) {
        for(auto child : get_elements(element)) {
          if(!GRAPHICS.contains(child->m_name.toStdU16String()) ||
              get_own(*child, u"display"_s, u"inline"_s) == u"none"_s) {
            continue;
          }
          convert(*child, inherited, is_top, items);
        }
      }

      void convert(const Element& element, const Properties& inherited,
          bool is_top, std::vector<Item>& items) {
        if(element.m_name == u"switch"_s && is_top &&
            is_editing_switch(element)) {
          auto properties = inherit(element, inherited);
          auto& group = *get_elements(element)[1];
          auto group_properties = inherit(group, properties);
          for(auto child : get_elements(group)) {
            if(GRAPHICS.contains(child->m_name.toStdU16String())) {
              convert(*child, group_properties, false, items);
            }
          }
          return;
        }
        if(auto item = convert(element, inherited)) {
          items.push_back(std::move(*item));
        }
      }

      std::optional<Item> convert(
          const Element& element, const Properties& inherited) {
        auto properties = inherit(element, inherited);
        if(element.m_name == u"g"_s) {
          if(get_elements(element).empty()) {
            return std::nullopt;
          }
          if(auto item = apply_group_rules(element, properties)) {
            return item;
          }
        } else if(element.m_name == u"switch"_s) {
          auto item = Item();
          item.m_header = make_header(element, properties);
          item.m_header.m_transform = to_single(
            parse_transform(element.m_attributes.value(u"transform"_s)));
          item.m_payload = Group();
          return item;
        }
        return make_item(element, properties);
      }

      static bool is_editing_switch(const Element& element) {
        auto children = get_elements(element);
        return children.size() == 2 &&
          children[0]->m_name.toLower() == u"foreignobject"_s &&
          children[1]->m_name == u"g"_s &&
          children[1]->m_attributes.value(u"i:extraneous"_s) == u"self"_s;
      }

      NodeHeader make_header(
          const Element& element, const Properties& properties) const {
        auto header = NodeHeader();
        auto name = element.m_attributes.value(u"data-name"_s);
        if(name.isEmpty()) {
          name = element.m_attributes.value(u"id"_s);
        }
        header.m_name = name.toStdString();
        header.m_has_custom_name = !name.isEmpty();
        auto visibility = get(properties, u"visibility"_s, u"visible"_s);
        header.m_is_visible =
          visibility != u"hidden"_s && visibility != u"collapse"_s;
        header.m_opacity = to_single(std::clamp(round(parse_fraction(
          get_own(element, u"opacity"_s, u"1"_s), 1)), 0.0, 1.0));
        return header;
      }

      std::optional<Item> make_item(
          const Element& element, const Properties& properties) {
        auto transform = Matrix();
        auto clip = std::optional<Mask>();
        auto item = make_local(element, properties, transform, clip);
        if(!item) {
          return std::nullopt;
        }
        if(element.m_name == u"g"_s && element.m_attributes.value(
            u"data-type"_s) == u"innerShadowGroup"_s) {
          if(auto child = collapse_inner_shadows(*item)) {
            return child;
          }
        }
        apply_filter(element, *item);
        if(item->m_is_failed) {
          return item;
        }
        if(!clip) {
          clip = make_clip(element);
        }
        if(clip) {
          auto local = item->m_header.m_transform;
          if(auto inverse = invert(local)) {
            for(auto& shape : clip->m_shapes) {
              shape.m_transform = *inverse * shape.m_transform;
            }
            item->m_header.m_transform = Matrix();
          } else {
            local = Matrix();
          }
          auto group = Item();
          group.m_header.m_name = localize("name", "Group");
          group.m_payload = Group();
          std::get<Group>(group.m_payload).m_mask = std::move(*clip);
          group.m_header.m_transform = transform * local;
          group.m_fill_alpha = item->m_fill_alpha;
          group.m_children.push_back(std::move(*item));
          return group;
        }
        item->m_header.m_transform = transform * item->m_header.m_transform;
        return item;
      }

      std::optional<Item> make_local(const Element& element,
          const Properties& properties, Matrix& transform,
          std::optional<Mask>& clip) {
        auto item = Item();
        item.m_header = make_header(element, properties);
        item.m_fill_alpha = get_paint_alpha(properties);
        transform = to_single(
          parse_transform(element.m_attributes.value(u"transform"_s)));
        auto& type = element.m_name;
        if(type == u"use"_s) {
          return convert_use(element, properties, std::move(item), transform,
            clip);
        } else if(type == u"g"_s || type == u"svg"_s ||
            type == u"symbol"_s) {
          convert_children(element, properties, false, item.m_children);
          if(item.m_children.empty()) {
            return std::nullopt;
          }
          item.m_payload = Group();
          return item;
        } else if(type == u"text"_s) {
          return make_text(element, properties, std::move(item));
        } else if(type == u"image"_s) {
          return make_image(element, std::move(item));
        }
        auto geometry = make_geometry(element, properties);
        if(!geometry) {
          return std::nullopt;
        }
        auto bounds = get_bounds(to_path(*geometry));
        auto offset = normalize(*geometry);
        item.m_header.m_transform = make_translation(offset.m_x, offset.m_y);
        auto shape = Shape();
        shape.m_geometry = std::move(*geometry);
        shape.m_style = make_style(properties, bounds, offset);
        item.m_payload = Box<Shape>(std::move(shape));
        return item;
      }

      static Point normalize(ShapeGeometry& geometry) {
        if(auto rectangle = std::get_if<RectangleShape>(&geometry)) {
          auto offset = rectangle->m_bounds.get_top_left();
          rectangle->m_bounds.m_x = 0;
          rectangle->m_bounds.m_y = 0;
          return offset;
        } else if(auto ellipse = std::get_if<EllipseShape>(&geometry)) {
          auto offset = Point(ellipse->m_center.m_x - ellipse->m_x_radius,
            ellipse->m_center.m_y - ellipse->m_y_radius);
          ellipse->m_center =
            Point(ellipse->m_x_radius, ellipse->m_y_radius);
          return offset;
        } else if(auto line = std::get_if<LineShape>(&geometry)) {
          auto offset = Point(std::min(line->m_start.m_x, line->m_end.m_x),
            std::min(line->m_start.m_y, line->m_end.m_y));
          line->m_start = Point(line->m_start.m_x - offset.m_x,
            line->m_start.m_y - offset.m_y);
          line->m_end = Point(
            line->m_end.m_x - offset.m_x, line->m_end.m_y - offset.m_y);
          return offset;
        }
        return Point(0, 0);
      }

      std::optional<Item> convert_use(const Element& element,
          const Properties& properties, Item item, Matrix& transform,
          std::optional<Mask>& clip) {
        auto reference = element.m_attributes.value(u"href"_s);
        if(reference.isEmpty()) {
          throw std::runtime_error("A use element has no xlink:href.");
        } else if(!reference.startsWith(u'#')) {
          return std::nullopt;
        }
        auto target = find(reference.mid(1));
        if(!target || std::ranges::contains(m_uses, target)) {
          return std::nullopt;
        }
        m_uses.push_back(target);
        auto converted = std::optional<Item>();
        auto offset = make_translation(
          get_length(element, u"x"_s), get_length(element, u"y"_s));
        if(target->m_name == u"symbol"_s) {
          auto symbol = Item();
          auto symbol_properties = inherit(*target, properties);
          convert_children(*target, symbol_properties, false,
            symbol.m_children);
          if(!symbol.m_children.empty()) {
            symbol.m_payload = Group();
            symbol.m_header = make_header(*target, symbol_properties);
            auto viewport = make_viewport(element, *target);
            transform = transform * offset * viewport;
            if(get_own(*target, u"overflow"_s, u"hidden"_s) != u"visible"_s) {
              if(auto inverse = invert(viewport)) {
                auto size = get_use_size(element);
                auto area = inverse->map(
                  Rect(0, 0, size.m_width, size.m_height));
                auto shape = MaskShape();
                shape.m_geometry = RectangleShape(Rect(round(area.m_x),
                  round(area.m_y), round(area.m_width), round(area.m_height)));
                clip.emplace().m_shapes.push_back(std::move(shape));
              }
            }
            converted = std::move(symbol);
          }
        } else if(GRAPHICS.contains(target->m_name.toStdU16String())) {
          auto target_transform = Matrix();
          auto ignored = std::optional<Mask>();
          auto target_properties = inherit(*target, properties);
          converted = make_local(
            *target, target_properties, target_transform, ignored);
          transform = transform * offset * target_transform;
        }
        m_uses.pop_back();
        if(!converted) {
          return std::nullopt;
        }
        auto opacity = converted->m_header.m_opacity;
        converted->m_header.m_name = item.m_header.m_name;
        converted->m_header.m_has_custom_name =
          item.m_header.m_has_custom_name;
        converted->m_header.m_is_visible = item.m_header.m_is_visible;
        converted->m_header.m_blend_mode = item.m_header.m_blend_mode;
        converted->m_header.m_is_isolated = item.m_header.m_is_isolated;
        converted->m_header.m_opacity =
          opacity != 1 ? opacity : item.m_header.m_opacity;
        return converted;
      }

      Size get_use_size(const Element& use) const {
        auto size = Size(get_length(use, u"width"_s),
          get_length(use, u"height"_s));
        if(size.m_width == 1 && size.m_height == 1) {
          return Size(DEFAULT_SIZE, DEFAULT_SIZE);
        }
        return size;
      }

      Matrix make_viewport(const Element& use, const Element& symbol) const {
        auto box = parse_numbers(symbol.m_attributes.value(u"viewBox"_s));
        if(box.size() != 4 || box[2] <= 0 || box[3] <= 0) {
          return Matrix();
        }
        auto size = get_use_size(use);
        auto width = size.m_width;
        auto height = size.m_height;
        auto ratio = symbol.m_attributes.value(u"preserveAspectRatio"_s).
          split(u' ', Qt::SkipEmptyParts);
        auto align = ratio.isEmpty() ? u"xMidYMid"_s : ratio[0];
        if(align == u"none"_s) {
          return make_scale(width / box[2], height / box[3]) *
            make_translation(-box[0], -box[1]);
        }
        auto is_slice = ratio.size() > 1 && ratio[1] == u"slice"_s;
        auto scale = is_slice ? std::max(width / box[2], height / box[3]) :
          std::min(width / box[2], height / box[3]);
        auto factor = [] (QChar code) {
          if(code == u'i') {
            return 0.0;
          } else if(code == u'a') {
            return 1.0;
          }
          return 0.5;
        };
        auto x = align.size() > 3 ? factor(align[3]) : 0.5;
        auto y = align.size() > 7 ? factor(align[7]) : 0.5;
        return make_translation(x * (width - box[2] * scale),
          y * (height - box[3] * scale)) * make_scale(scale, scale) *
          make_translation(-box[0], -box[1]);
      }

      std::optional<Mask> make_clip(const Element& element) {
        auto value = get_own(element, u"clip-path"_s, u"none"_s);
        if(value == u"none"_s) {
          return std::nullopt;
        }
        auto target = find(to_reference(value));
        if(!target || std::ranges::contains(m_clips, target)) {
          return std::nullopt;
        }
        m_clips.push_back(target);
        auto items = std::vector<Item>();
        auto properties = inherit(*target, m_root_properties);
        if(target->m_name == u"clipPath"_s) {
          auto children = std::vector<Item>();
          convert_children(*target, properties, false, children);
          auto group = Item();
          group.m_payload = Group();
          group.m_header.m_transform = to_single(
            parse_transform(target->m_attributes.value(u"transform"_s)));
          group.m_children = std::move(children);
          items.push_back(std::move(group));
        } else if(GRAPHICS.contains(target->m_name.toStdU16String())) {
          convert(*target, m_root_properties, false, items);
        }
        m_clips.pop_back();
        auto mask = Mask();
        for(auto& item : items) {
          collect_mask(item, Matrix(), mask.m_shapes);
        }
        if(mask.m_shapes.empty()) {
          return std::nullopt;
        }
        return mask;
      }

      static double read_number(const Element& element, const QString& name) {
        return round(to_js_number(element.m_attributes.value(name, u"0"_s)));
      }

      std::vector<Effect> make_sketch_shadows(
          const Element& filter, bool is_inner) const {
        auto offsets = std::vector<std::pair<const Element*, const Element*>>();
        auto blurs = std::vector<std::pair<const Element*, const Element*>>();
        auto matrices =
          std::vector<std::pair<const Element*, const Element*>>();
        find_all(filter, u"feOffset"_s, offsets);
        find_all(filter, u"feGaussianBlur"_s, blurs);
        find_all(filter, u"feColorMatrix"_s, matrices);
        auto effects = std::vector<Effect>();
        for(auto i = std::size_t(0); i < offsets.size(); ++i) {
          if(i >= blurs.size() || i >= matrices.size() || (blurs[i].second &&
              blurs[i].second->m_name == u"feComposite"_s)) {
            continue;
          }
          auto values = matrices[i].first->m_attributes.value(u"values"_s).
            split(QRegularExpression(u"\\s+"_s));
          auto value = [&] (qsizetype index) {
            if(index >= values.size()) {
              return std::numeric_limits<double>::quiet_NaN();
            }
            return to_js_number(values[index]);
          };
          auto channel = [&] (qsizetype index) {
            return std::clamp(to_json(round(255 * value(index))), 0.0,
              255.0) / 255;
          };
          auto color = Color(channel(4), channel(9), channel(14),
            std::clamp(to_json(round(value(18))), 0.0, 1.0));
          auto& offset = *offsets[i].first;
          auto shift = Point(read_number(offset, u"dx"_s),
            read_number(offset, u"dy"_s));
          auto blur =
            to_json(read_number(*blurs[i].first, u"stdDeviation"_s));
          if(is_inner) {
            effects.push_back(InnerShadow(shift, blur, color));
          } else {
            effects.push_back(DropShadow(shift, blur, color));
          }
        }
        return effects;
      }

      std::optional<Effect> make_writer_shadow(
          const Element& filter, bool is_inner) const {
        auto offsets = std::vector<std::pair<const Element*, const Element*>>();
        auto blurs = std::vector<std::pair<const Element*, const Element*>>();
        auto floods = std::vector<std::pair<const Element*, const Element*>>();
        find_all(filter, u"feOffset"_s, offsets);
        find_all(filter, u"feGaussianBlur"_s, blurs);
        find_all(filter, u"feFlood"_s, floods);
        if(offsets.empty() || blurs.empty() || floods.empty()) {
          return std::nullopt;
        }
        auto& flood = *floods.front().first;
        auto color = parse_color(
          get_own(flood, u"flood-color"_s, u"#000"_s)).value_or(
            Color(0, 0, 0, 1));
        if(color.m_alpha == 0) {
          color.m_alpha = 1;
        }
        color.m_alpha *= round(
          parse_fraction(get_own(flood, u"flood-opacity"_s, u"1"_s), 1));
        auto& offset = *offsets.front().first;
        auto shift =
          Point(read_number(offset, u"dx"_s), read_number(offset, u"dy"_s));
        auto blur =
          to_json(read_number(*blurs.front().first, u"stdDeviation"_s));
        if(is_inner) {
          return InnerShadow(shift, blur, color);
        }
        return DropShadow(shift, blur, color);
      }

      const Element* find_filter(const Element& element) const {
        auto value = get_own(element, u"filter"_s, u"none"_s);
        if(value == u"none"_s) {
          return nullptr;
        }
        auto target = find(to_reference(value));
        if(!target || target->m_name != u"filter"_s) {
          return nullptr;
        }
        return target;
      }

      void apply_filter(const Element& element, Item& item) const {
        auto filter = find_filter(element);
        if(!filter) {
          return;
        }
        auto description = describe(*filter);
        auto& effects = get_effects(item);
        auto add = [&] (std::vector<Effect> shadows) {
          effects.insert(effects.end(), shadows.begin(), shadows.end());
        };
        if(SKETCH_SHADOW.match(description).hasMatch()) {
          add(make_sketch_shadows(*filter, false));
        }
        if(SKETCH_INNER_SHADOW.match(description).hasMatch()) {
          add(make_sketch_shadows(*filter, true));
        }
        if(WRITER_SHADOW.match(description).hasMatch()) {
          if(auto shadow = make_writer_shadow(*filter, false)) {
            effects.push_back(*shadow);
          }
          if(!description.endsWith(u"fecomposite+fecomposite"_s)) {
            hide_fill(item);
          }
        }
        if(WRITER_INNER_SHADOW.match(description).hasMatch()) {
          if(auto shadow = make_writer_shadow(*filter, true)) {
            effects.push_back(*shadow);
          }
        }
        if(has_inner_shadow(item)) {
          hide_fill(item);
        }
        for(auto& effect : effects) {
          std::visit([&] (const auto& shadow) {
            if constexpr(requires { shadow.m_offset; }) {
              if(std::isnan(shadow.m_offset.m_x) ||
                  std::isnan(shadow.m_offset.m_y)) {
                item.m_is_failed = true;
              }
            }
          }, effect);
        }
      }

      std::optional<Item> collapse_inner_shadows(Item& group) {
        auto& children = group.m_children;
        auto merge = [&] {
          if(children.size() < 2 || get_effects(children[1]).empty()) {
            return;
          }
          auto effect = get_effects(children[1]).front();
          if(auto inner = std::get_if<InnerShadow>(&effect)) {
            auto alpha = children[0].m_fill_alpha;
            if(alpha != 0) {
              inner->m_color.m_alpha =
                std::min(inner->m_color.m_alpha / alpha, 1.0);
            } else {
              inner->m_color.m_alpha = 1;
            }
          }
          get_effects(children[0]).push_back(effect);
          children.erase(children.begin() + 1);
        };
        auto count = children.size();
        if(count < 1 || count > 4) {
          return std::nullopt;
        }
        if(count == 4) {
          if(!get_effects(children[0]).empty()) {
            get_effects(children[1]).push_back(
              get_effects(children[0]).front());
          }
          children.erase(children.begin());
        }
        if(count >= 3 && children.size() >= 2) {
          auto& first = children[0];
          if(is_group(first) && !first.m_effects.empty() &&
              std::holds_alternative<DropShadow>(first.m_effects.front())) {
            get_effects(children[1]).push_back(first.m_effects.front());
            children.erase(children.begin());
          } else {
            merge();
          }
        }
        if(count >= 2 && children.size() >= 2) {
          if(is_group(children[1])) {
            return std::nullopt;
          }
          if(has_inner_shadow(children[0])) {
            auto source = find_style(children[1]);
            if(auto target = find_style(children[0]); target && source) {
              target->m_stroke = source->m_stroke;
            }
            children.erase(children.begin() + 1);
          } else {
            merge();
          }
        }
        if(children.empty()) {
          return std::nullopt;
        }
        return std::move(children.front());
      }

      Rect get_box(const Element& element, const Properties& properties) const {
        auto geometry = make_geometry(element, properties);
        if(!geometry) {
          return Rect(0, 0, 0, 0);
        }
        return get_bounds(to_path(*geometry));
      }

      bool has_effects(const Element& element) const {
        return is_set(element, u"filter"_s) || is_set(element, u"mask"_s) ||
          is_set(element, u"clip-path"_s) ||
          !element.m_attributes.value(u"transform"_s).trimmed().isEmpty();
      }

      Element move_group_style(const Element& group, const Element& child,
          bool is_transformed) const {
        auto copy = child;
        auto& attributes = copy.m_attributes;
        attributes.remove(u"stroke"_s);
        for(auto name : {u"filter"_s, u"mask"_s, u"clip-path"_s}) {
          attributes.insert(name, get_own(group, name, u"none"_s));
        }
        attributes.insert(
          u"opacity"_s, get_own(group, u"opacity"_s, u"1"_s));
        for(auto name : {u"mix-blend-mode"_s, u"isolation"_s}) {
          if(auto value = get_own(group, name, QString());
              !value.isEmpty()) {
            attributes.insert(name, value);
          }
        }
        if(is_transformed) {
          attributes.insert(u"transform"_s,
            group.m_attributes.value(u"transform"_s));
        }
        return copy;
      }

      std::optional<Item> apply_group_rules(
          const Element& group, const Properties& properties) {
        auto name = group.m_attributes.value(u"data-name"_s);
        if(name.isEmpty()) {
          name = group.m_attributes.value(u"id"_s);
        }
        auto item = std::optional<Item>();
        if(auto alignment = match_aligned_stroke(group, properties)) {
          auto& inner = *get_elements(group)[0];
          item = convert(move_group_style(group, inner, true), properties);
          if(item) {
            if(auto style = find_style(*item); style && style->m_stroke) {
              style->m_stroke->m_alignment = *alignment;
            }
          }
        } else if(auto stroke = match_framed_stroke(group, properties)) {
          auto& inner = *get_elements(group)[0];
          item = convert(move_group_style(group, inner, false), properties);
          if(item) {
            if(auto style = find_style(*item)) {
              auto merged = style->m_stroke.value_or(Stroke());
              merged.m_color = stroke->m_color;
              merged.m_width = stroke->m_width;
              merged.m_alignment = StrokeAlignment::CENTER;
              merged.m_is_enabled = true;
              style->m_stroke = std::move(merged);
            }
          }
        } else if(auto child = match_shadow_element(group)) {
          item = convert(*child, properties);
        }
        if(item && !name.isEmpty()) {
          item->m_header.m_name = name.toStdString();
          item->m_header.m_has_custom_name = true;
        }
        return item;
      }

      std::optional<StrokeAlignment> match_aligned_stroke(
          const Element& group, const Properties& properties) const {
        auto children = get_elements(group);
        if(get(properties, u"stroke"_s, u"none"_s) == u"none"_s ||
            children.size() != 2 ||
            children[0]->m_name != children[1]->m_name ||
            (children[0]->m_name != u"rect"_s &&
              children[0]->m_name != u"circle"_s &&
              children[0]->m_name != u"ellipse"_s)) {
          return std::nullopt;
        }
        auto& inner = *children[0];
        auto& outer = *children[1];
        auto inner_properties = inherit(inner, properties);
        auto outer_properties = inherit(outer, properties);
        if(get(inner_properties, u"stroke"_s, u"none"_s) != u"none"_s ||
            get(outer_properties, u"fill"_s, u"#000"_s) != u"none"_s ||
            has_effects(inner)) {
          return std::nullopt;
        }
        auto width = get_stroke_width(properties);
        auto first = get_box(inner, inner_properties);
        auto second = get_box(outer, outer_properties);
        auto& larger = first.m_width > second.m_width ? first : second;
        auto& smaller = first.m_width > second.m_width ? second : first;
        if(!is_close(larger.m_width - smaller.m_width, width, TOLERANCE) ||
            !is_close(smaller.m_x - larger.m_x, width / 2, TOLERANCE) ||
            !is_close(smaller.m_y - larger.m_y, width / 2, TOLERANCE)) {
          return std::nullopt;
        }
        if(second.m_width > first.m_width) {
          return StrokeAlignment::OUTSIDE;
        }
        return StrokeAlignment::INSIDE;
      }

      std::optional<Stroke> match_framed_stroke(
          const Element& group, const Properties& properties) const {
        auto children = get_elements(group);
        if(children.size() != 2 || children[0]->m_name != u"rect"_s ||
            children[1]->m_name != u"path"_s) {
          return std::nullopt;
        }
        auto& rectangle = *children[0];
        auto& frame = *children[1];
        auto rectangle_properties = inherit(rectangle, properties);
        auto frame_properties = inherit(frame, properties);
        if(get(rectangle_properties, u"stroke"_s, u"none"_s) != u"none"_s ||
            get(frame_properties, u"fill"_s, u"#000"_s) == u"none"_s ||
            get(frame_properties, u"stroke"_s, u"none"_s) != u"none"_s) {
          return std::nullopt;
        }
        auto data = frame.m_attributes.value(u"d"_s);
        auto letters = data;
        letters.remove(QRegularExpression(u"[^a-zA-Z]"_s));
        letters.replace(u"path"_s, QString());
        auto upper = letters.toUpper();
        if(upper != u"MVHVHMHVHVZ"_s && upper != u"MVHVHMHVHVLZ"_s) {
          return std::nullopt;
        }
        struct Corner {
          std::optional<double> m_x;
          std::optional<double> m_y;
        };
        auto corners = std::array<Corner, 8>();
        auto inside_top_right = std::size_t(0);
        auto inside_bottom_right = std::size_t(1);
        auto inside_bottom_left = std::size_t(2);
        auto inside_top_left = std::size_t(3);
        auto outside_top_right = std::size_t(4);
        auto outside_top_left = std::size_t(5);
        auto outside_bottom_left = std::size_t(6);
        auto outside_bottom_right = std::size_t(7);
        auto order = std::array<std::size_t, 10>({inside_top_right,
          inside_bottom_right, inside_bottom_left, inside_top_left,
          inside_top_right, outside_top_right, outside_top_left,
          outside_bottom_left, outside_bottom_right, outside_top_right});
        auto previous = std::optional<Point>();
        auto start = qsizetype(-1);
        for(auto i = qsizetype(0); i + 1 < letters.size(); ++i) {
          if(letters[i] == u'L' || letters[i] == u'l') {
            continue;
          }
          start = data.indexOf(letters[i], start + 1);
          auto end = data.indexOf(letters[i + 1], start + 1);
          auto command = data.mid(start, end < 0 ? -1 : end - start).trimmed();
          auto& corner = corners[order[static_cast<std::size_t>(i)]];
          auto point = read_frame_point(command, previous);
          if(!point) {
            return std::nullopt;
          }
          if(corner.m_x && corner.m_y && is_truthy(*corner.m_x) &&
              is_truthy(*corner.m_y) &&
              (std::abs(point->m_x - *corner.m_x) > FRAME_TOLERANCE ||
              std::abs(point->m_y - *corner.m_y) > FRAME_TOLERANCE)) {
            return std::nullopt;
          }
          corner.m_x = point->m_x;
          corner.m_y = point->m_y;
          previous = point;
        }
        for(auto& corner : corners) {
          if(!corner.m_x || !corner.m_y) {
            return std::nullopt;
          }
        }
        auto x = [&] (std::size_t index) {
          return *corners[index].m_x;
        };
        auto y = [&] (std::size_t index) {
          return *corners[index].m_y;
        };
        auto width = x(inside_top_left) - x(outside_top_left);
        auto differences = std::array<double, 7>({
          y(inside_top_left) - y(outside_top_left),
          x(outside_top_right) - x(inside_top_right),
          y(inside_top_right) - y(outside_top_right),
          x(outside_bottom_right) - x(inside_bottom_right),
          y(outside_bottom_right) - y(inside_bottom_right),
          x(inside_bottom_left) - x(outside_bottom_left),
          y(outside_bottom_left) - y(inside_bottom_left)});
        for(auto difference : differences) {
          if(std::abs(difference - width) > FRAME_TOLERANCE) {
            return std::nullopt;
          }
        }
        auto translation =
          parse_transform(frame.m_attributes.value(u"transform"_s));
        auto box = Rect(x(inside_top_left) - width / 2 + translation.m_tx,
          y(inside_top_left) - width / 2 + translation.m_ty,
          x(outside_top_right) - x(outside_top_left) - width,
          y(outside_bottom_right) - y(outside_top_right) - width);
        if(has_effects(rectangle)) {
          return std::nullopt;
        }
        auto bounds = get_box(rectangle, rectangle_properties);
        auto& larger = bounds.m_width > box.m_width ? bounds : box;
        auto& smaller = bounds.m_width > box.m_width ? box : bounds;
        if(!is_close(larger.m_width, smaller.m_width, FRAME_TOLERANCE) ||
            !is_close(smaller.m_x, larger.m_x, FRAME_TOLERANCE) ||
            !is_close(smaller.m_y, larger.m_y, FRAME_TOLERANCE)) {
          return std::nullopt;
        }
        auto stroke = Stroke();
        stroke.m_width = round(width);
        auto color = resolve_color(get(frame_properties, u"fill"_s, u"#000"_s),
          frame_properties).value_or(Color(0, 0, 0, 1));
        color.m_alpha = quantize(color.m_alpha * std::clamp(parse_fraction(
          get_own(frame, u"opacity"_s, u"1"_s), 1), 0.0, 1.0));
        stroke.m_color = color;
        return stroke;
      }

      static std::optional<Point> read_frame_point(
          QStringView command, std::optional<Point> previous) {
        if(command.isEmpty()) {
          return std::nullopt;
        }
        auto letter = command[0];
        auto rest = command.mid(1).trimmed().toString();
        if(letter == u'V' || letter == u'v') {
          if(!previous) {
            return std::nullopt;
          }
          auto y = parse_float(rest);
          if(letter == u'v') {
            y += previous->m_y;
          }
          return Point(previous->m_x, y);
        } else if(letter == u'H' || letter == u'h') {
          if(!previous) {
            return std::nullopt;
          }
          auto x = parse_float(rest);
          if(letter == u'h') {
            x += previous->m_x;
          }
          return Point(x, previous->m_y);
        } else if(letter == u'M' || letter == u'm') {
          if(letter == u'm' && !previous) {
            return std::nullopt;
          }
          if(auto space = rest.indexOf(u' '); space >= 0) {
            rest[space] = u',';
          }
          if(!rest.contains(u',')) {
            if(auto minus = rest.indexOf(u'-'); minus >= 0) {
              rest.insert(minus, u',');
            }
          }
          auto comma = rest.indexOf(u',');
          auto point = Point(parse_float(comma < 0 ? QString() :
            rest.left(comma)), parse_float(rest.mid(comma + 1)));
          if(letter == u'm') {
            point = Point(point.m_x + previous->m_x, point.m_y + previous->m_y);
          }
          return point;
        }
        return std::nullopt;
      }

      std::optional<Element> match_shadow_element(const Element& group) const {
        auto children = get_elements(group);
        if(children.size() != 1) {
          return std::nullopt;
        }
        auto filter = find_filter(group);
        if(!filter) {
          return std::nullopt;
        }
        auto description = describe(*filter);
        if(!WRITER_SHADOW.match(description).hasMatch() &&
            !WRITER_INNER_SHADOW.match(description).hasMatch()) {
          return std::nullopt;
        }
        auto& child = *children[0];
        auto has_mask = is_set(group, u"mask"_s);
        auto has_clip = is_set(group, u"clip-path"_s);
        auto child_transform = child.m_attributes.value(u"transform"_s);
        if((has_mask || has_clip) && (is_set(child, u"mask"_s) ||
            is_set(child, u"clip-path"_s) ||
            !child_transform.trimmed().isEmpty()) ||
            is_set(child, u"filter"_s)) {
          return std::nullopt;
        }
        auto copy = child;
        copy.m_attributes.insert(
          u"filter"_s, group.m_attributes.value(u"filter"_s));
        if(has_mask) {
          copy.m_attributes.insert(
            u"mask"_s, group.m_attributes.value(u"mask"_s));
        }
        if(has_clip) {
          copy.m_attributes.insert(
            u"clip-path"_s, group.m_attributes.value(u"clip-path"_s));
        }
        auto opacity = parse_fraction(get_own(group, u"opacity"_s, u"1"_s), 1);
        if(opacity != 1) {
          copy.m_attributes.insert(u"opacity"_s, QString::number(opacity *
            parse_fraction(get_own(child, u"opacity"_s, u"1"_s), 1), 'g', 17));
        }
        auto transform = group.m_attributes.value(u"transform"_s);
        if(!transform.trimmed().isEmpty()) {
          copy.m_attributes.insert(
            u"transform"_s, transform + u' ' + child_transform);
        }
        return copy;
      }

      std::optional<Item> make_image(const Element& element, Item item) {
        auto width = get_length(element, u"width"_s);
        auto height = get_length(element, u"height"_s);
        if(width < 0 || height < 0) {
          return std::nullopt;
        }
        auto shape = Shape();
        shape.m_geometry = RectangleShape(Rect(0, 0, width, height));
        auto reference = element.m_attributes.value(u"href"_s).trimmed();
        if(auto image = load_image(reference)) {
          auto fill = ImageFill();
          fill.m_image = image->first;
          fill.m_size = image->second;
          shape.m_style.m_fill = fill;
          item.m_is_image = true;
        }
        item.m_header.m_transform =
          make_translation(get_length(element, u"x"_s),
            get_length(element, u"y"_s));
        item.m_payload = Box<Shape>(std::move(shape));
        return item;
      }

      std::optional<std::pair<ImageId, Size>> load_image(
          const QString& reference) {
        if(auto entry = m_image_ids.find(reference);
            entry != m_image_ids.end()) {
          return *entry;
        }
        auto bytes = QByteArray();
        auto subtype = QString();
        if(auto match = IMAGE_DATA.match(reference); match.hasMatch()) {
          bytes = QByteArray::fromBase64(
            reference.mid(match.capturedLength()).toLatin1());
          subtype = match.captured(1).toLower();
        } else {
          auto url = QUrl(reference);
          auto path = url.isLocalFile() ? url.toLocalFile() : reference;
          if(path.isEmpty() || !QDir::isAbsolutePath(path) ||
              !QFile::exists(path)) {
            return std::nullopt;
          }
          auto file = QFile(path);
          if(!file.open(QIODevice::ReadOnly)) {
            return std::nullopt;
          }
          bytes = file.readAll();
        }
        auto buffer = QBuffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        auto reader = QImageReader(&buffer);
        auto dimensions = reader.size();
        auto format = reader.format();
        if(!dimensions.isValid() || format.isEmpty()) {
          if(subtype.isEmpty()) {
            return std::nullopt;
          }
          dimensions = QSize(0, 0);
          format = subtype.toLatin1();
        }
        auto resource = ImageResource();
        resource.m_id = generate_node_id();
        auto type = QString::fromLatin1(format).toLower();
        if(type == u"jpg"_s) {
          type = u"jpeg"_s;
        }
        resource.m_media_type = "image/" + type.toStdString();
        auto data = std::vector<std::byte>(static_cast<std::size_t>(
          bytes.size()));
        std::memcpy(data.data(), bytes.constData(), data.size());
        resource.m_data = Shared(std::move(data));
        auto result = std::pair(resource.m_id,
          Size(dimensions.width(), dimensions.height()));
        m_images.push_back(std::move(resource));
        m_image_ids.insert(reference, result);
        return result;
      }

      std::optional<Item> make_text(
          const Element& element, const Properties& properties, Item item) {
        auto state = TextState();
        state.m_element = &element;
        auto x_attribute = element.m_attributes.value(u"x"_s);
        auto y_attribute = element.m_attributes.value(u"y"_s);
        if(element.m_attributes.contains(u"x"_s)) {
          state.m_x = parse_numbers(x_attribute).empty() ? 0 :
            parse_numbers(x_attribute).front();
        }
        if(element.m_attributes.contains(u"y"_s)) {
          state.m_y = parse_numbers(y_attribute).empty() ? 0 :
            parse_numbers(y_attribute).front();
        }
        state.m_has_position = state.m_x || state.m_y;
        state.m_start_x = state.m_x.value_or(0);
        auto frame = Point(get_length(element, u"x"_s),
          get_length(element, u"y"_s));
        auto parent = find_parent(*m_root, element);
        auto is_first = parent && get_elements(*parent).front() == &element;
        collect_runs(
          element, properties, true, true, is_first, QStringList(), state);
        if(!state.m_is_last_collapsed && state.m_raw.endsWith(u' ') &&
            !state.m_lines.empty() && !state.m_lines.back().empty()) {
          --state.m_lines.back().back().m_to;
          state.m_raw.chop(1);
        }
        if(state.m_raw.isEmpty()) {
          return std::nullopt;
        }
        auto opacity = std::optional<double>();
        auto is_opacity_same = true;
        for(auto& line : state.m_lines) {
          for(auto& run : line) {
            if(!opacity) {
              opacity = run.m_opacity;
            } else if(*opacity != run.m_opacity) {
              is_opacity_same = false;
            }
          }
        }
        auto is_split = state.m_is_multiline && state.m_is_stroke_same &&
          (!is_opacity_same || opacity == 0.0);
        auto is_point = true;
        if(!is_opacity_same || is_split) {
          for(auto& line : state.m_lines) {
            for(auto& run : line) {
              if(is_split && run.m_opacity != 1 && run.m_has_stroke) {
                is_point = false;
              }
              run.m_style.m_color.m_alpha =
                quantize(run.m_alpha * run.m_opacity);
            }
          }
        }
        if(is_opacity_same && opacity && *opacity < 1) {
          item.m_header.m_opacity = to_single(round(*opacity));
        }
        if(is_point && m_measurer.m_extent && state.m_is_multiline &&
            state.m_is_stroke_same &&
            (!state.m_line_spacing || state.m_is_irregular)) {
          is_point = space_lines(state);
        }
        if(!state.m_is_multiline || !state.m_is_stroke_same || !is_point) {
          return split_text(state, frame, std::move(item));
        }
        if(state.m_reset) {
          frame = *state.m_reset;
        }
        auto text = Text();
        text.m_frame = PositionedFrame();
        auto content = QString();
        auto runs = std::vector<TextRun>();
        auto append_run = [&] (const TextStyle& style, std::size_t length) {
          if(length == 0) {
            return;
          }
          if(!runs.empty() && runs.back().m_style == style) {
            runs.back().m_length += length;
          } else {
            auto run = TextRun();
            run.m_length = length;
            run.m_style = style;
            runs.push_back(std::move(run));
          }
        };
        auto first = static_cast<const Run*>(nullptr);
        auto last = static_cast<const Run*>(nullptr);
        for(auto i = std::size_t(0); i != state.m_lines.size(); ++i) {
          if(i != 0) {
            content += u'\n';
            if(auto& before = state.m_lines[i - 1]; !before.empty()) {
              append_run(before.back().m_style, 1);
            } else if(!runs.empty()) {
              ++runs.back().m_length;
            }
          }
          for(auto& run : state.m_lines[i]) {
            if(run.m_to <= run.m_from) {
              continue;
            }
            if(!first) {
              first = &run;
            }
            last = &run;
            content += state.m_raw.mid(static_cast<qsizetype>(run.m_from),
              static_cast<qsizetype>(run.m_to - run.m_from));
            append_run(run.m_style, run.m_to - run.m_from);
          }
        }
        if(!first) {
          return std::nullopt;
        }
        item.m_fill_alpha = first->m_fill_alpha;
        text.m_content = Shared(content.toStdU16String());
        text.m_base_style = last->m_style;
        text.m_paragraph_style.m_alignment = first->m_alignment;
        if(state.m_is_multiline && state.m_is_stroke_same &&
            state.m_line_spacing && !state.m_is_irregular) {
          text.m_paragraph_style.m_line_height = *state.m_line_spacing;
        }
        if(runs.size() > 1) {
          text.m_runs = std::move(runs);
        }
        text.m_style.m_fill = SolidFill(text.m_base_style.m_color);
        if(get(properties, u"stroke"_s, u"none"_s) != u"none"_s) {
          text.m_style.m_stroke =
            make_stroke(properties, Rect(0, 0, 0, 0), Point(0, 0));
        } else {
          auto stroke = Stroke();
          stroke.m_color = Color(0, 0, 0, 0);
          stroke.m_is_enabled = false;
          text.m_style.m_stroke = std::move(stroke);
        }
        item.m_header.m_transform = make_translation(frame.m_x, frame.m_y);
        if(!item.m_header.m_has_custom_name) {
          item.m_header.m_name = make_content_name(content);
        }
        item.m_payload = Box<Text>(std::move(text));
        return item;
      }

      std::optional<Item> split_text(
          const TextState& state, Point pen, Item item) const {
        auto texts = std::vector<Item>();
        for(auto& line : state.m_lines) {
          for(auto& run : line) {
            if(run.m_to <= run.m_from) {
              continue;
            }
            auto content = state.m_raw.mid(static_cast<qsizetype>(run.m_from),
              static_cast<qsizetype>(run.m_to - run.m_from));
            auto position =
              Point(to_single(run.m_x.value_or(pen.m_x) + run.m_dx),
                to_single(run.m_y.value_or(pen.m_y) + run.m_dy));
            auto text = Text();
            text.m_frame = PositionedFrame();
            text.m_content = Shared(content.toStdU16String());
            text.m_base_style = run.m_style;
            text.m_paragraph_style.m_alignment = run.m_alignment;
            text.m_style.m_fill = SolidFill(run.m_style.m_color);
            text.m_style.m_stroke = run.m_stroke;
            auto child = Item();
            child.m_header.m_name = make_content_name(content);
            child.m_header.m_transform =
              make_translation(position.m_x, position.m_y);
            child.m_payload = Box<Text>(std::move(text));
            child.m_fill_alpha = run.m_fill_alpha;
            texts.push_back(std::move(child));
            auto width = 0.0;
            if(m_measurer.m_width) {
              width = m_measurer.m_width(run.m_style.m_font,
                content.toStdU16String()).value_or(0);
            }
            pen = Point(to_single(position.m_x + std::round(width)),
              position.m_y);
          }
        }
        if(texts.empty()) {
          return std::nullopt;
        } else if(texts.size() == 1) {
          auto& text = texts.front();
          text.m_header.m_transform =
            item.m_header.m_transform * text.m_header.m_transform;
          if(item.m_header.m_has_custom_name) {
            text.m_header.m_name = item.m_header.m_name;
            text.m_header.m_has_custom_name = true;
          }
          text.m_header.m_opacity = item.m_header.m_opacity;
          text.m_header.m_is_visible = item.m_header.m_is_visible;
          return std::move(text);
        }
        item.m_payload = Group();
        item.m_children = std::move(texts);
        return item;
      }

      void collect_runs(const Element& element, const Properties& properties,
          bool is_last, bool is_collapsed, bool is_first,
          QStringList decorations, TextState& state) {
        decorations = merge_decorations(decorations, get_own(element,
          u"text-decoration"_s, u"none"_s).split(QRegularExpression(
            u"\\s+"_s), Qt::SkipEmptyParts));
        auto space = element.m_attributes.value(u"xml:space"_s);
        auto collapse = space.isEmpty() ? is_collapsed : space != u"preserve"_s;
        auto scope = Scope();
        scope.m_dx = get_length(element, u"dx"_s);
        scope.m_dy = get_length(element, u"dy"_s);
        scope.m_has_x = !element.m_attributes.value(u"x"_s).isEmpty();
        scope.m_has_y = !element.m_attributes.value(u"y"_s).isEmpty();
        scope.m_is_first = is_first;
        state.m_path.push_back(&element);
        auto& children = element.m_children;
        auto first = static_cast<const Element*>(nullptr);
        for(auto& child : children) {
          if(child.m_name != TEXT_NODE) {
            first = &child;
            break;
          }
        }
        for(auto i = std::size_t(0); i != children.size(); ++i) {
          auto& child = children[i];
          auto is_child_last = is_last && i + 1 == children.size();
          if(child.m_name != TEXT_NODE) {
            collect_runs(child, inherit(child, properties), is_child_last,
              collapse, &child == first, decorations, state);
            continue;
          }
          auto text = child.m_text;
          if(collapse) {
            text.remove(u'\n');
            text.replace(QRegularExpression(u"\\s+"_s), u" "_s);
            if(state.m_is_last_space && text.startsWith(u' ')) {
              text.remove(0, 1);
            }
            if(is_child_last && text.endsWith(u' ')) {
              text.chop(1);
            }
          } else {
            text.remove(u'\n');
            text.replace(u'\t', u' ');
          }
          state.m_is_last_collapsed = collapse;
          if(text.isEmpty()) {
            continue;
          }
          state.m_is_last_space = text.back().isSpace();
          auto from = static_cast<std::size_t>(state.m_raw.size());
          state.m_raw += text;
          if(text == u" "_s && state.m_has_line &&
              !state.m_lines.back().empty() && is_same_set(decorations,
                state.m_lines.back().back().m_decorations)) {
            ++state.m_lines.back().back().m_to;
            auto position = Position();
            place(element, scope, position, state);
            state.m_cells.push_back(Cell(state.m_path, state.m_y.value_or(0),
              make_font(properties), std::nullopt));
            continue;
          }
          auto run = Run();
          run.m_from = from;
          run.m_to = from + static_cast<std::size_t>(text.size());
          run.m_decorations = decorations;
          auto position = Position();
          place(element, scope, position, state);
          run.m_x = position.m_x;
          run.m_y = position.m_y;
          run.m_dx = position.m_dx.value_or(0);
          run.m_dy = position.m_dy.value_or(0);
          fill_run(element, properties, run, state);
          state.m_cells.push_back(Cell(state.m_path, state.m_y.value_or(0),
            make_font(properties), state.m_row));
          if(!state.m_has_line) {
            state.m_lines.emplace_back();
            state.m_has_line = true;
          }
          state.m_lines.back().push_back(std::move(run));
        }
        state.m_path.pop_back();
      }

      std::optional<Extent> measure(
          const Element& element, const TextState& state) const {
        auto extent = std::optional<Extent>();
        for(auto& cell : state.m_cells) {
          if(!std::ranges::contains(cell.m_path, &element)) {
            continue;
          }
          auto metrics = m_measurer.m_extent(cell.m_font);
          if(!metrics) {
            continue;
          }
          auto top = to_single(cell.m_baseline -
            to_layout_units(metrics->m_ascent * cell.m_font.m_size));
          auto bottom = to_single(cell.m_baseline +
            to_layout_units(metrics->m_descent * cell.m_font.m_size));
          if(!extent) {
            extent = Extent(top, bottom);
          } else {
            extent->m_top = std::min(extent->m_top, top);
            extent->m_bottom = std::max(extent->m_bottom, bottom);
          }
        }
        return extent;
      }

      bool space_lines(TextState& state) const {
        auto rows = std::vector<Extent>(state.m_row + 1);
        auto proportions = std::unordered_map<std::string, double>();
        auto height = 0.0;
        for(auto& cell : state.m_cells) {
          if(!cell.m_row) {
            continue;
          }
          auto extent = measure(*cell.m_path.back(), state);
          if(!extent) {
            continue;
          }
          auto& row = rows[*cell.m_row];
          row.m_top = std::min(row.m_top, extent->m_top);
          row.m_bottom = std::max(row.m_bottom, extent->m_bottom);
          height = extent->m_bottom - extent->m_top;
          if(cell.m_font.m_size != 0) {
            proportions[cell.m_font.m_family] = height / cell.m_font.m_size;
          }
        }
        auto tolerance = std::sqrt(height) / 1.35;
        auto minimum = height / 2;
        auto previous = rows.front().m_bottom;
        auto inserted = std::size_t(0);
        for(auto i = std::size_t(1); i < rows.size(); ++i) {
          auto gap = rows[i].m_top - previous;
          if(gap < -tolerance || (gap > tolerance && gap < minimum)) {
            return false;
          }
          auto index = i + inserted - 1;
          if(gap > tolerance && std::isfinite(gap) &&
              index < state.m_lines.size() && !state.m_lines[index].empty()) {
            auto& last = state.m_lines[index].back();
            if(auto proportion = proportions.find(last.m_style.m_font.m_family);
                proportion != proportions.end() && proportion->second != 0) {
              auto spacer = last;
              spacer.m_from = last.m_to;
              spacer.m_style.m_font.m_size =
                to_single(round(gap / proportion->second));
              state.m_lines.insert(state.m_lines.begin() +
                static_cast<std::ptrdiff_t>(index + 1),
                std::vector<Run>({std::move(spacer)}));
              ++inserted;
            }
          }
          previous = rows[i].m_bottom;
        }
        return true;
      }

      void place(const Element& element, Scope& scope, Position& position,
          TextState& state) {
        auto capture = false;
        if(element.m_name == u"tspan"_s) {
          if(scope.m_dx != 0) {
            position.m_dx = scope.m_dx;
            scope.m_dx = 0;
          }
          if(scope.m_dy != 0) {
            position.m_dy = scope.m_dy;
            scope.m_dy = 0;
          }
          if(!state.m_has_position && scope.m_has_x && scope.m_has_y) {
            capture = scope.m_is_first;
            state.m_has_position = true;
          }
          if(scope.m_has_x) {
            position.m_x = get_length(element, u"x"_s);
            scope.m_has_x = false;
          }
          if(scope.m_has_y) {
            position.m_y = get_length(element, u"y"_s);
            if(!state.m_y) {
              state.m_y = position.m_y;
            }
            scope.m_has_y = false;
          }
          if(capture) {
            state.m_reset = Point(*position.m_x, *position.m_y);
          }
        }
        auto x = round(position.m_x.value_or(state.m_x.value_or(0)) +
          position.m_dx.value_or(0));
        auto y = round(position.m_y.value_or(state.m_y.value_or(0)) +
          position.m_dy.value_or(0));
        if(!state.m_x) {
          state.m_start_x = x;
          state.m_x = x;
        }
        if(!state.m_y) {
          state.m_y = y;
        }
        if(state.m_is_multiline && y != round(*state.m_y)) {
          auto is_first = scope.m_is_first;
          if(x == state.m_start_x && (!element.m_attributes.value(
              u"x"_s).isEmpty() || is_first)) {
            state.m_lines.emplace_back();
            state.m_has_line = true;
            ++state.m_row;
            if(!is_first) {
              auto spacing = std::round(100 * (y - *state.m_y)) / 100;
              if(!state.m_line_spacing && !state.m_is_irregular) {
                state.m_line_spacing = spacing;
              }
              auto count = 1.0;
              if(state.m_is_irregular || *state.m_line_spacing <= 0 ||
                  std::fmod(spacing, *state.m_line_spacing) > TOLERANCE ||
                  spacing < 0) {
                state.m_is_irregular = true;
              } else {
                count = spacing / *state.m_line_spacing;
              }
              for(auto i = 1; i < count; ++i) {
                state.m_lines.emplace_back();
              }
            }
          } else {
            state.m_is_multiline = false;
            auto merged = std::vector<Run>();
            for(auto& line : state.m_lines) {
              merged.insert(merged.end(), line.begin(), line.end());
            }
            state.m_lines.clear();
            state.m_lines.push_back(std::move(merged));
            state.m_has_line = true;
            state.m_is_irregular = true;
          }
        }
        state.m_x = x;
        state.m_y = y;
        if(&element == state.m_element) {
          state.m_has_position = true;
        }
      }

      static const Element* find_parent(
          const Element& root, const Element& element) {
        for(auto& child : root.m_children) {
          if(&child == &element) {
            return &root;
          }
          if(auto parent = find_parent(child, element)) {
            return parent;
          }
        }
        return nullptr;
      }

      void fill_run(const Element& element, const Properties& properties,
          Run& run, TextState& state) {
        auto& style = run.m_style;
        auto size = round(get_font_size(properties));
        style.m_font.m_family =
          to_family(get(properties, u"font-family"_s, DEFAULT_FAMILY)).
            toStdString();
        style.m_font.m_style = to_style_name(
          get(properties, u"font-style"_s, u"normal"_s),
          get(properties, u"font-weight"_s, u"400"_s)).toStdString();
        if(auto spacing = get(properties, u"letter-spacing"_s, u"normal"_s);
            spacing != u"normal"_s && !spacing.endsWith(u'%') &&
            size != 0) {
          if(auto length = parse_length(
              spacing, size, size, get_ex(spacing, make_font(properties)))) {
            style.m_letter_spacing = round(*length / size * 1000);
          }
        }
        auto shift = get_own(element, u"baseline-shift"_s, u"baseline"_s);
        if(shift == u"super"_s) {
          style.m_script = TextScript::SUPERSCRIPT;
        } else if(shift == u"sub"_s) {
          style.m_script = TextScript::SUBSCRIPT;
        }
        if(style.m_script != TextScript::NORMAL) {
          size = round(size / SCRIPT_SCALE);
        }
        style.m_font.m_size = size;
        style.m_is_underlined = run.m_decorations.contains(u"underline"_s);
        style.m_is_struck_through =
          run.m_decorations.contains(u"line-through"_s);
        auto anchor = get(properties, u"text-anchor"_s, u"start"_s);
        if(anchor == u"middle"_s) {
          run.m_alignment = TextAlignment::CENTER;
        } else if(anchor == u"end"_s) {
          run.m_alignment = TextAlignment::RIGHT;
        }
        auto fill = get(properties, u"fill"_s, u"#000"_s);
        auto color = Color(0, 0, 0, 1);
        if(fill == u"none"_s) {
          color.m_alpha = 0;
        } else if(strip_url(fill).isEmpty()) {
          color = resolve_color(fill, properties).value_or(Color(0, 0, 0, 1));
          run.m_fill_alpha = color.m_alpha;
          color.m_alpha *= std::clamp(parse_fraction(
            get(properties, u"fill-opacity"_s, u"1"_s), 1), 0.0, 1.0);
        }
        run.m_alpha = color.m_alpha;
        run.m_opacity = std::clamp(
          parse_fraction(get_own(element, u"opacity"_s, u"1"_s), 1), 0.0, 1.0);
        run.m_has_stroke = get(properties, u"stroke"_s, u"none"_s) != u"none"_s;
        run.m_stroke = make_stroke(properties, Rect(0, 0, 0, 0), Point(0, 0));
        color.m_alpha = quantize(color.m_alpha);
        style.m_color = color;
        auto stroke = QStringList();
        for(auto name : {u"stroke"_s, u"stroke-width"_s, u"stroke-dasharray"_s,
            u"stroke-dashoffset"_s, u"stroke-linecap"_s, u"stroke-linejoin"_s,
            u"stroke-miterlimit"_s, u"stroke-opacity"_s}) {
          stroke.push_back(get(properties, name, QString()));
        }
        auto key = stroke.join(u',');
        if(!state.m_stroke) {
          state.m_stroke = key;
        } else if(*state.m_stroke != key) {
          state.m_is_stroke_same = false;
        }
      }

      std::optional<ShapeGeometry> make_geometry(
          const Element& element, const Properties& properties) const {
        auto& type = element.m_name;
        auto rule = get(properties, u"fill-rule"_s, u"nonzero"_s) ==
          u"evenodd"_s ? FillRule::EVEN_ODD : FillRule::NON_ZERO;
        if(type == u"rect"_s) {
          auto rectangle = RectangleShape();
          rectangle.m_bounds = Rect(get_length(element, u"x"_s),
            get_length(element, u"y"_s), get_length(element, u"width"_s),
            get_length(element, u"height"_s));
          auto radius = get_length(element, u"rx"_s);
          if(radius == 0) {
            radius = get_length(element, u"ry"_s);
          }
          rectangle.m_radii = CornerRadii(radius, radius, radius, radius);
          return rectangle;
        } else if(type == u"circle"_s) {
          auto radius = get_length(element, u"r"_s);
          return EllipseShape(
            Point(get_length(element, u"cx"_s), get_length(element, u"cy"_s)),
            radius, radius);
        } else if(type == u"ellipse"_s) {
          return EllipseShape(
            Point(get_length(element, u"cx"_s), get_length(element, u"cy"_s)),
            get_length(element, u"rx"_s), get_length(element, u"ry"_s));
        } else if(type == u"line"_s) {
          return LineShape(
            Point(get_length(element, u"x1"_s), get_length(element, u"y1"_s)),
            Point(get_length(element, u"x2"_s), get_length(element, u"y2"_s)));
        } else if(type == u"polygon"_s || type == u"polyline"_s) {
          auto numbers = parse_numbers(element.m_attributes.value(u"points"_s));
          if(numbers.size() < 2) {
            return std::nullopt;
          }
          auto path = Path();
          path.move_to(Point(numbers[0], numbers[1]));
          for(auto i = std::size_t(2); i + 1 < numbers.size(); i += 2) {
            path.line_to(Point(numbers[i], numbers[i + 1]));
          }
          if(type == u"polygon"_s) {
            path.close();
          }
          return PathShape(Shared(close_explicitly(path)), rule);
        } else if(type == u"path"_s) {
          auto data = element.m_attributes.value(u"d"_s).toLatin1();
          auto path = parse_svg_path(std::string_view(data.constData(),
            static_cast<std::size_t>(data.size())));
          if(!path) {
            return std::nullopt;
          }
          return PathShape(Shared(close_explicitly(*path)), rule);
        }
        return std::nullopt;
      }

      double get_paint_alpha(const Properties& properties) const {
        auto fill = get(properties, u"fill"_s, u"#000"_s);
        if(fill == u"none"_s || !strip_url(fill).isEmpty()) {
          return 1;
        }
        if(auto color = resolve_color(fill, properties)) {
          return color->m_alpha;
        }
        return 1;
      }

      std::optional<Color> resolve_color(
          QStringView value, const Properties& properties) const {
        if(value.trimmed().compare(u"currentColor", Qt::CaseInsensitive) ==
            0) {
          return parse_color(get(properties, u"color"_s, u"#000"_s));
        }
        return parse_color(value);
      }

      std::optional<FillPaint> make_paint(const QString& value,
          double opacity, const Properties& properties, const Rect& bounds,
          Point offset) {
        if(value == u"none"_s) {
          return std::nullopt;
        }
        if(auto reference = strip_url(value); !reference.isEmpty()) {
          if(auto target = find(reference);
              target && target->m_name == u"pattern"_s) {
            if(auto pattern = make_pattern(*target, bounds)) {
              return *pattern;
            }
            return SolidFill(Color(0, 0, 0, quantize(opacity)));
          }
          if(auto gradient =
              make_gradient(reference, opacity, bounds, offset)) {
            return *gradient;
          }
          auto fallback = value.mid(value.indexOf(u')') + 1).trimmed();
          if(fallback.isEmpty() || fallback == u"none"_s) {
            return std::nullopt;
          }
          return make_paint(fallback, opacity, properties, bounds, offset);
        }
        auto color = resolve_color(value, properties);
        if(!color) {
          return std::nullopt;
        }
        color->m_alpha = quantize(color->m_alpha * opacity);
        return SolidFill(*color);
      }

      std::optional<ImageFill> make_pattern(
          const Element& pattern, const Rect& bounds) {
        if(std::ranges::contains(m_patterns, &pattern)) {
          return std::nullopt;
        }
        m_patterns.push_back(&pattern);
        auto children = std::vector<const Element*>();
        auto image_count = m_images.size();
        auto image_ids = m_image_ids;
        auto properties = inherit(pattern, m_root_properties);
        for(auto child : get_elements(pattern)) {
          if(GRAPHICS.contains(child->m_name.toStdU16String()) &&
              get_own(*child, u"display"_s, u"inline"_s) != u"none"_s) {
            auto items = std::vector<Item>();
            convert(*child, properties, false, items);
            if(!items.empty()) {
              children.push_back(child);
            }
          }
        }
        m_images.erase(m_images.begin() +
          static_cast<std::ptrdiff_t>(image_count), m_images.end());
        m_image_ids = std::move(image_ids);
        m_patterns.pop_back();
        if(children.size() != 1) {
          return std::nullopt;
        }
        auto element_pointer = children.front();
        auto transform =
          parse_transform(element_pointer->m_attributes.value(u"transform"_s));
        if(element_pointer->m_name == u"use"_s) {
          auto reference = element_pointer->m_attributes.value(u"href"_s);
          auto target = reference.startsWith(u'#') ?
            find(reference.mid(1)) : nullptr;
          if(!target) {
            return std::nullopt;
          }
          transform = transform * make_translation(
            get_length(*element_pointer, u"x"_s),
            get_length(*element_pointer, u"y"_s)) *
            parse_transform(target->m_attributes.value(u"transform"_s));
          element_pointer = target;
        }
        if(element_pointer->m_name != u"image"_s) {
          return std::nullopt;
        }
        auto& element = *element_pointer;
        auto image =
          load_image(element.m_attributes.value(u"href"_s).trimmed());
        if(!image) {
          return std::nullopt;
        }
        auto fill = ImageFill();
        fill.m_image = image->first;
        fill.m_size = image->second;
        auto image_width = get_length(element, u"width"_s);
        auto image_height = get_length(element, u"height"_s);
        auto width = image_width * transform.m_a;
        auto height = image_height * transform.m_d;
        auto box = parse_numbers(get_linked(pattern, u"viewBox"_s));
        auto pattern_transform = get_linked(pattern, u"patternTransform"_s);
        fill.m_scale_behavior = ScaleBehavior::COVER;
        if(box.size() != 4) {
          fill.m_scale_behavior = ScaleBehavior::FILL;
          return fill;
        }
        for(auto& value : box) {
          value = round(value);
        }
        if(is_close(box[2], image_width, TOLERANCE) &&
            is_close(box[3], image_height, TOLERANCE) &&
            is_close(box[0], 0, TOLERANCE) && is_close(box[1], 0, TOLERANCE) &&
            pattern_transform.isEmpty()) {
          return fill;
        }
        auto shape_width = round(bounds.m_width);
        auto shape_height = round(bounds.m_height);
        auto horizontal = shape_width / box[2];
        auto vertical = shape_height / box[3];
        auto is_flipped_horizontally = false;
        auto is_flipped_vertically = false;
        if(!pattern_transform.isEmpty()) {
          if(pattern_transform.contains(
              QRegularExpression(uR"(matrix\(-1(,|\s))"_s))) {
            is_flipped_horizontally = true;
          } else if(pattern_transform.contains(QRegularExpression(
              uR"(matrix\(1(,|\s)\s*0(,|\s)\s*0(,|\s)\s*-1(,|\s))"_s))) {
            is_flipped_vertically = true;
          } else if(pattern_transform.contains(u"rotate(-180)"_s)) {
            is_flipped_horizontally = true;
            is_flipped_vertically = true;
          }
        }
        auto covered_width = 0.0;
        auto covered_height = 0.0;
        if(1 / vertical < 1 / horizontal) {
          covered_width = width;
          covered_height = height * horizontal / vertical;
        } else {
          covered_height = height;
          covered_width = width * vertical / horizontal;
        }
        auto top = (covered_height * vertical - shape_height) / (2 * vertical);
        auto middle = (height - covered_height) / 2;
        auto offset_x = horizontal * ((covered_width * horizontal -
          shape_width) / (2 * horizontal) + (width - covered_width) / 2 -
          box[0]) / width;
        auto offset_y = vertical * (top + middle - box[1]) / height;
        if(!std::isfinite(horizontal) || !std::isfinite(vertical) ||
            !std::isfinite(offset_x) || !std::isfinite(offset_y)) {
          fill.m_scale_behavior = ScaleBehavior::FILL;
          return fill;
        }
        fill.m_offset = Point(offset_x, offset_y);
        fill.m_is_flipped_horizontally = is_flipped_horizontally;
        fill.m_is_flipped_vertically = is_flipped_vertically;
        if(is_close(horizontal, vertical, TOLERANCE)) {
          if(!is_close(horizontal, 1, 1e-6)) {
            fill.m_scale = horizontal;
          }
        } else {
          fill.m_scale_behavior = ScaleBehavior::FILL;
          if(!is_close(horizontal, 1, 1e-6) || !is_close(vertical, 1, 1e-6)) {
            fill.m_horizontal_scale = horizontal;
            fill.m_vertical_scale = vertical;
          }
        }
        return fill;
      }

      QString get_linked(const Element& element, const QString& name) const {
        auto current = &element;
        auto visited = std::vector<const Element*>();
        while(current && !std::ranges::contains(visited, current)) {
          if(auto value = current->m_attributes.value(name); !value.isEmpty()) {
            return value;
          }
          visited.push_back(current);
          auto reference = current->m_attributes.value(u"href"_s);
          current = reference.startsWith(u'#') ? find(reference.mid(1)) :
            nullptr;
        }
        return QString();
      }

      std::vector<GradientStop> get_stops(const Element& element) const {
        auto current = &element;
        auto visited = std::vector<const Element*>();
        while(current && !std::ranges::contains(visited, current)) {
          auto stops = std::vector<GradientStop>();
          for(auto child : get_elements(*current)) {
            if(child->m_name != u"stop"_s) {
              continue;
            }
            auto color = parse_color(
              get_own(*child, u"stop-color"_s, u"#000"_s)).value_or(
                Color(0, 0, 0, 1));
            color.m_alpha = color.m_alpha * parse_fraction(
              get_own(*child, u"stop-opacity"_s, u"1"_s), 1);
            auto offset = to_single(std::clamp(round(parse_fraction(
              get_own(*child, u"offset"_s, u"0"_s), 0)), 0.0, 1.0));
            if(!stops.empty()) {
              offset = std::max(offset, stops.back().m_offset);
            }
            stops.push_back(GradientStop(offset, color));
          }
          if(!stops.empty()) {
            return stops;
          }
          visited.push_back(current);
          auto reference = current->m_attributes.value(u"href"_s);
          current = reference.startsWith(u'#') ? find(reference.mid(1)) :
            nullptr;
        }
        return {};
      }

      std::optional<GradientFill> make_gradient(const QString& id,
          double opacity, const Rect& bounds, Point offset) const {
        auto element = find(id);
        if(!element || (element->m_name != u"linearGradient"_s &&
            element->m_name != u"radialGradient"_s)) {
          return std::nullopt;
        }
        auto fill = GradientFill();
        auto is_user = get_linked(*element, u"gradientUnits"_s) ==
          u"userSpaceOnUse"_s;
        fill.m_units = is_user ? GradientUnits::USER_SPACE :
          GradientUnits::OBJECT_BOUNDING_BOX;
        auto font = make_root_font();
        font.m_size = m_font_sizes.value(element, FONT_SIZE);
        auto read = [&] (const QString& name, const QString& fallback) {
          auto value = get_linked(*element, name);
          if(value.isEmpty()) {
            value = fallback;
          }
          if(!is_user) {
            return round(parse_fraction(value, 0));
          }
          auto length = to_px(value, false, font);
          if(length == 0 || std::isnan(length)) {
            return round(parse_fraction(fallback, 0));
          }
          return length;
        };
        auto read_focus = [&] (const QString& name, double center) {
          auto value = get_linked(*element, name);
          if(value.isEmpty()) {
            return center;
          } else if(!is_user) {
            return round(parse_fraction(value, 0));
          }
          auto length = to_px(value, false, font);
          if(length == 0 || std::isnan(length)) {
            return center;
          }
          return length;
        };
        if(element->m_name == u"linearGradient"_s) {
          fill.m_geometry = LinearGradient(
            Point(read(u"x1"_s, u"0"_s), read(u"y1"_s, u"0"_s)),
            Point(read(u"x2"_s, u"100%"_s), read(u"y2"_s, u"0"_s)));
        } else {
          auto center = Point(read(u"cx"_s, u"50%"_s), read(u"cy"_s, u"50%"_s));
          auto focus = Point(read_focus(u"fx"_s, center.m_x),
            read_focus(u"fy"_s, center.m_y));
          fill.m_geometry =
            RadialGradient(center, read(u"r"_s, u"50%"_s), focus);
        }
        fill.m_transform =
          parse_transform(get_linked(*element, u"gradientTransform"_s));
        fill.m_stops = get_stops(*element);
        if(fill.m_stops.empty()) {
          return std::nullopt;
        }
        for(auto& stop : fill.m_stops) {
          stop.m_color.m_alpha = quantize(stop.m_color.m_alpha * opacity);
        }
        if(is_user) {
          to_bounding_box(fill, bounds, offset);
        }
        return fill;
      }

      void apply_stroke_attributes(
          const Properties& properties, Stroke& stroke) const {
        stroke.m_width = get_stroke_width(properties);
        auto cap = get(properties, u"stroke-linecap"_s, u"butt"_s);
        if(cap == u"round"_s) {
          stroke.m_cap = StrokeCap::ROUND;
        } else if(cap == u"square"_s) {
          stroke.m_cap = StrokeCap::SQUARE;
        }
        auto join = get(properties, u"stroke-linejoin"_s, u"miter"_s);
        if(join == u"round"_s) {
          stroke.m_join = StrokeJoin::ROUND;
        } else if(join == u"bevel"_s) {
          stroke.m_join = StrokeJoin::BEVEL;
        }
        stroke.m_miter_limit = round(parse_fraction(
          get(properties, u"stroke-miterlimit"_s, u"4"_s), 4));
        auto dashes = get(properties, u"stroke-dasharray"_s, u"none"_s);
        if(dashes != u"none"_s) {
          stroke.m_dashes = parse_dashes(dashes, make_font(properties));
        }
        auto offset = get(properties, u"stroke-dashoffset"_s, u"0"_s);
        if(auto number = parse_float(offset);
            offset.endsWith(u'%') && !std::isnan(number)) {
          stroke.m_dash_offset = round(number);
        } else {
          auto font = make_font(properties);
          stroke.m_dash_offset = round(parse_length(
            offset, 0, font.m_size, get_ex(offset, font)).value_or(0));
        }
      }

      std::vector<double> parse_dashes(
          const QString& value, const Font& font) const {
        auto dashes = std::vector<double>();
        for(auto& token : value.split(
            QRegularExpression(u"[,\\s]+"_s), Qt::SkipEmptyParts)) {
          auto length = std::optional<double>();
          if(token.endsWith(u'%')) {
            if(auto number = parse_float(token); !std::isnan(number)) {
              length = number;
            }
          } else {
            length =
              parse_length(token, 0, font.m_size, get_ex(token, font));
          }
          if(!length) {
            return {};
          }
          dashes.push_back(*length);
        }
        return dashes;
      }

      Style make_style(const Properties& properties, const Rect& bounds,
          Point offset) {
        auto style = Style();
        auto fill_opacity = std::clamp(parse_fraction(
          get(properties, u"fill-opacity"_s, u"1"_s), 1), 0.0, 1.0);
        if(auto paint = make_paint(get(properties, u"fill"_s, u"#000"_s),
            fill_opacity, properties, bounds, offset)) {
          style.m_fill = std::visit([] (auto& value) -> Fill {
            return value;
          }, *paint);
        } else {
          style.m_fill = NoFill(SolidFill(Color(0, 0, 0, 0)));
        }
        style.m_stroke = make_stroke(properties, bounds, offset);
        return style;
      }

      Stroke make_stroke(
          const Properties& properties, const Rect& bounds, Point offset) {
        auto opacity = std::clamp(parse_fraction(
          get(properties, u"stroke-opacity"_s, u"1"_s), 1), 0.0, 1.0);
        auto paint = make_paint(get(properties, u"stroke"_s, u"none"_s),
          opacity, properties, bounds, offset);
        auto stroke = Stroke();
        if(!paint) {
          stroke.m_color = make_color(112, 112, 112);
          stroke.m_is_enabled = false;
          return stroke;
        }
        if(auto solid = std::get_if<SolidFill>(&*paint)) {
          stroke.m_color = solid->m_color;
        } else {
          stroke.m_color = make_color(112, 112, 112);
        }
        apply_stroke_attributes(properties, stroke);
        return stroke;
      }
  };

  void finish_effects(std::vector<Effect>& effects) {
    auto has_drop_shadow = false;
    std::erase_if(effects, [&] (const auto& effect) {
      if(!std::holds_alternative<DropShadow>(effect)) {
        return false;
      }
      if(has_drop_shadow) {
        return true;
      }
      has_drop_shadow = true;
      return false;
    });
  }

  void append(Transaction& transaction, const NodeId& parent, Item item,
      std::vector<NodeId>& anchored) {
    if(item.m_is_failed) {
      return;
    }
    auto is_anchored = is_group(item) && !item.m_children.empty() &&
      std::ranges::all_of(item.m_children, [] (const auto& child) {
        return child.m_is_failed;
      });
    if(item.m_header.m_id.is_nil()) {
      item.m_header.m_id = generate_node_id();
    }
    if(auto style = find_style(item)) {
      finish_effects(style->m_effects);
    }
    item.m_header.m_rotation = derive_rotation(item.m_header.m_transform);
    auto id = transaction.append(
      parent, std::move(item.m_header), std::move(item.m_payload));
    if(is_anchored) {
      anchored.push_back(id);
    }
    for(auto& child : item.m_children) {
      append(transaction, id, std::move(child), anchored);
    }
  }
}

bool Rellu::is_svg(const QString& text) {
  if(!text.contains(u"<svg"_s)) {
    return false;
  }
  return static_cast<bool>(parse(text));
}

std::vector<NodeId> Rellu::read_svg(const QString& text, Document& document,
    const SvgMeasurer& measurer) {
  auto root = parse(text);
  if(!root) {
    throw std::invalid_argument("The text is not SVG markup.");
  }
  auto rules = std::vector<Rule>();
  collect_stylesheets(*root, rules);
  auto path = std::vector<const Element*>();
  cascade(*root, rules, path);
  auto converter = Converter(*root, measurer);
  auto items = converter.convert();
  auto anchored = std::vector<NodeId>();
  if(items.empty()) {
    return anchored;
  }
  auto transaction = Transaction(document);
  for(auto& image : converter.get_images()) {
    transaction.add_image(std::move(image));
  }
  auto survivors = std::ranges::count_if(items, [] (const auto& item) {
    return !item.m_is_failed;
  });
  if(survivors == 1) {
    for(auto& item : items) {
      append(transaction, NodeId(), std::move(item), anchored);
    }
  } else {
    auto group = Item();
    group.m_payload = Group();
    group.m_children = std::move(items);
    append(transaction, NodeId(), std::move(group), anchored);
  }
  transaction.commit();
  return anchored;
}
