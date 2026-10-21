#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include <QColor>
#include <QHash>
#include <QXmlStreamReader>
#include "Rellu/Geometry/SvgPath.hpp"
#include "Rellu/Io/SvgImport.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  const auto XLINK = u"http://www.w3.org/1999/xlink"_s;
  constexpr auto DEFAULT_SIZE = 300.0;
  constexpr auto FONT_SIZE = 20.0;
  constexpr auto PRECISION = 1e5;

  struct Element {
    QString m_name;
    QHash<QString, QString> m_attributes;
    std::vector<Element> m_children;
  };

  struct Item {
    NodeHeader m_header;
    Payload m_payload;
    std::vector<Item> m_children;
  };

  using Properties = QHash<QString, QString>;

  const auto INHERITED = std::unordered_set<std::u16string>({u"fill",
    u"fill-opacity", u"fill-rule", u"stroke", u"stroke-width",
    u"stroke-linecap", u"stroke-linejoin", u"stroke-miterlimit",
    u"stroke-dasharray", u"stroke-dashoffset", u"stroke-opacity",
    u"visibility", u"color"});

  const auto GRAPHICS = std::unordered_set<std::u16string>({u"path",
    u"circle", u"ellipse", u"rect", u"polygon", u"polyline", u"line", u"g",
    u"svg", u"use", u"switch"});

  double round(double value) {
    return std::round(value * PRECISION) / PRECISION;
  }

  double to_single(double value) {
    return static_cast<double>(static_cast<float>(value));
  }

  Matrix to_single(const Matrix& matrix) {
    return Matrix(to_single(round(matrix.m_a)), to_single(round(matrix.m_b)),
      to_single(round(matrix.m_c)), to_single(round(matrix.m_d)),
      to_single(round(matrix.m_tx)), to_single(round(matrix.m_ty)));
  }

  double quantize(double alpha) {
    return std::round(std::clamp(alpha, 0.0, 1.0) * 255) / 255;
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
      radial->m_center = map(radial->m_center);
      radial->m_focus = map(radial->m_focus);
      radial->m_radius = round(radial->m_radius / bounds.m_width);
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

  void parse_style(QStringView style, QHash<QString, QString>& attributes) {
    for(auto declaration : style.split(u';')) {
      auto colon = declaration.indexOf(u':');
      if(colon < 0) {
        continue;
      }
      auto name = declaration.left(colon).trimmed().toString().toLower();
      auto value = declaration.mid(colon + 1).trimmed().toString();
      if(value.endsWith(u"!important"_s)) {
        value.chop(10);
        value = value.trimmed();
      }
      if(!name.isEmpty()) {
        attributes.insert(name, value);
      }
    }
  }

  Element read_element(QXmlStreamReader& reader) {
    auto element = Element();
    element.m_name = reader.name().toString();
    auto style = QString();
    for(auto& attribute : reader.attributes()) {
      auto name = attribute.name().toString();
      if(attribute.namespaceUri() == XLINK && name == u"href"_s) {
        if(!element.m_attributes.contains(u"href"_s)) {
          element.m_attributes.insert(name, attribute.value().toString());
        }
      } else if(name == u"style"_s) {
        style = attribute.value().toString();
      } else if(attribute.namespaceUri().isEmpty()) {
        element.m_attributes.insert(name, attribute.value().toString());
      }
    }
    parse_style(style, element.m_attributes);
    while(!reader.atEnd()) {
      auto token = reader.readNext();
      if(token == QXmlStreamReader::StartElement) {
        element.m_children.push_back(read_element(reader));
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

  std::optional<double> parse_length(QStringView text, double reference) {
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
      return *number * FONT_SIZE;
    } else if(unit == u"ex"_s) {
      return *number * FONT_SIZE / 2;
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

  class Converter {
    public:
      explicit Converter(const Element& root)
          : m_root(&root) {
        index(root, m_ids);
        auto box = parse_numbers(root.m_attributes.value(u"viewBox"_s));
        auto width = parse_length(root.m_attributes.value(u"width"_s), 0);
        auto height = parse_length(root.m_attributes.value(u"height"_s), 0);
        m_width = width && *width > 0 ? *width :
          box.size() == 4 ? box[2] : DEFAULT_SIZE;
        m_height = height && *height > 0 ? *height :
          box.size() == 4 ? box[3] : DEFAULT_SIZE;
      }

      std::vector<Item> convert() {
        auto items = std::vector<Item>();
        convert_children(*m_root, Properties(), items);
        return items;
      }

    private:
      const Element* m_root;
      QHash<QString, const Element*> m_ids;
      double m_width;
      double m_height;
      std::vector<const Element*> m_uses;

      const Element* find(const QString& id) const {
        return m_ids.value(id, nullptr);
      }

      static Properties inherit(const Element& element,
          const Properties& inherited) {
        auto properties = inherited;
        for(auto entry = element.m_attributes.begin();
            entry != element.m_attributes.end(); ++entry) {
          if(INHERITED.contains(entry.key().toStdU16String()) &&
              entry.value().trimmed() != u"inherit"_s) {
            properties.insert(entry.key(), entry.value().trimmed());
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

      double get_length(const Element& element, const QString& name,
          double reference) const {
        return parse_length(
          element.m_attributes.value(name), reference).value_or(0);
      }

      double get_x(const Element& element, const QString& name) const {
        return round(get_length(element, name, m_width));
      }

      double get_y(const Element& element, const QString& name) const {
        return round(get_length(element, name, m_height));
      }

      double get_diagonal() const {
        return std::hypot(m_width, m_height) / std::numbers::sqrt2;
      }

      void convert_children(const Element& element,
          const Properties& inherited, std::vector<Item>& items) {
        for(auto& child : element.m_children) {
          if(!GRAPHICS.contains(child.m_name.toStdU16String()) ||
              get_own(child, u"display"_s, u"inline"_s) == u"none"_s) {
            continue;
          }
          if(auto item = convert(child, inherited)) {
            items.push_back(std::move(*item));
          }
        }
      }

      std::optional<Item> convert(const Element& element,
          const Properties& inherited) {
        auto properties = inherit(element, inherited);
        auto item = Item();
        auto& header = item.m_header;
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
        header.m_transform = to_single(
          parse_transform(element.m_attributes.value(u"transform"_s)));
        if(element.m_name == u"use"_s) {
          return convert_use(element, properties, std::move(item));
        }
        if(element.m_name == u"g"_s || element.m_name == u"svg"_s ||
            element.m_name == u"switch"_s) {
          convert_children(element, properties, item.m_children);
          if(item.m_children.empty()) {
            return std::nullopt;
          }
          item.m_payload = Group();
          return item;
        }
        auto geometry = make_geometry(element, properties);
        if(!geometry) {
          return std::nullopt;
        }
        auto bounds = get_bounds(to_path(*geometry));
        auto offset = normalize(*geometry);
        header.m_transform =
          header.m_transform * make_translation(offset.m_x, offset.m_y);
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
          const Properties& properties, Item item) {
        auto reference = element.m_attributes.value(u"href"_s);
        if(!reference.startsWith(u'#')) {
          return std::nullopt;
        }
        auto target = find(reference.mid(1));
        if(!target || std::ranges::contains(m_uses, target)) {
          return std::nullopt;
        }
        m_uses.push_back(target);
        auto converted = std::optional<Item>();
        auto offset = make_translation(
          get_x(element, u"x"_s), get_y(element, u"y"_s));
        if(target->m_name == u"symbol"_s) {
          auto symbol = Item();
          convert_children(*target, inherit(*target, properties),
            symbol.m_children);
          if(!symbol.m_children.empty()) {
            symbol.m_payload = Group();
            symbol.m_header.m_transform = make_viewport(element, *target);
            converted = std::move(symbol);
          }
        } else if(GRAPHICS.contains(target->m_name.toStdU16String())) {
          converted = convert(*target, properties);
        }
        m_uses.pop_back();
        if(!converted) {
          return std::nullopt;
        }
        converted->m_header.m_name = item.m_header.m_name;
        converted->m_header.m_has_custom_name =
          item.m_header.m_has_custom_name;
        converted->m_header.m_transform = item.m_header.m_transform *
          offset * converted->m_header.m_transform;
        converted->m_header.m_opacity *= item.m_header.m_opacity;
        if(!item.m_header.m_is_visible) {
          converted->m_header.m_is_visible = false;
        }
        return converted;
      }

      Matrix make_viewport(const Element& use, const Element& symbol) const {
        auto box = parse_numbers(symbol.m_attributes.value(u"viewBox"_s));
        if(box.size() != 4 || box[2] <= 0 || box[3] <= 0) {
          return Matrix();
        }
        auto width = parse_length(use.m_attributes.value(u"width"_s), m_width).
          value_or(DEFAULT_SIZE);
        auto height = parse_length(
          use.m_attributes.value(u"height"_s), m_height).value_or(DEFAULT_SIZE);
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

      std::optional<ShapeGeometry> make_geometry(
          const Element& element, const Properties& properties) const {
        auto& type = element.m_name;
        auto rule = get(properties, u"fill-rule"_s, u"nonzero"_s) ==
          u"evenodd"_s ? FillRule::EVEN_ODD : FillRule::NON_ZERO;
        if(type == u"rect"_s) {
          auto rectangle = RectangleShape();
          rectangle.m_bounds = Rect(get_x(element, u"x"_s),
            get_y(element, u"y"_s), get_x(element, u"width"_s),
            get_y(element, u"height"_s));
          auto radius = get_x(element, u"rx"_s);
          if(radius == 0) {
            radius = get_y(element, u"ry"_s);
          }
          rectangle.m_radii = CornerRadii(radius, radius, radius, radius);
          return rectangle;
        } else if(type == u"circle"_s) {
          auto radius = round(get_length(element, u"r"_s, get_diagonal()));
          return EllipseShape(
            Point(get_x(element, u"cx"_s), get_y(element, u"cy"_s)), radius,
            radius);
        } else if(type == u"ellipse"_s) {
          return EllipseShape(
            Point(get_x(element, u"cx"_s), get_y(element, u"cy"_s)),
            get_x(element, u"rx"_s), get_y(element, u"ry"_s));
        } else if(type == u"line"_s) {
          return LineShape(
            Point(get_x(element, u"x1"_s), get_y(element, u"y1"_s)),
            Point(get_x(element, u"x2"_s), get_y(element, u"y2"_s)));
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
          Point offset) const {
        if(value == u"none"_s) {
          return std::nullopt;
        }
        if(auto reference = strip_url(value); !reference.isEmpty()) {
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
          for(auto& child : current->m_children) {
            if(child.m_name != u"stop"_s) {
              continue;
            }
            auto color = parse_color(
              get_own(child, u"stop-color"_s, u"#000"_s)).value_or(
                Color(0, 0, 0, 1));
            color.m_alpha = color.m_alpha * parse_fraction(
              get_own(child, u"stop-opacity"_s, u"1"_s), 1);
            auto offset = to_single(std::clamp(round(parse_fraction(
              get_own(child, u"offset"_s, u"0"_s), 0)), 0.0, 1.0));
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
        auto read = [&] (const QString& name, const QString& fallback,
            double reference) {
          auto value = get_linked(*element, name);
          if(value.isEmpty()) {
            value = fallback;
          }
          if(is_user) {
            return round(parse_length(value, reference).value_or(0));
          }
          return round(parse_fraction(value, 0));
        };
        if(element->m_name == u"linearGradient"_s) {
          fill.m_geometry = LinearGradient(
            Point(read(u"x1"_s, u"0"_s, m_width),
              read(u"y1"_s, u"0"_s, m_height)),
            Point(read(u"x2"_s, u"100%"_s, m_width),
              read(u"y2"_s, u"0"_s, m_height)));
        } else {
          auto center = Point(read(u"cx"_s, u"50%"_s, m_width),
            read(u"cy"_s, u"50%"_s, m_height));
          auto focus = center;
          if(!get_linked(*element, u"fx"_s).isEmpty()) {
            focus.m_x = read(u"fx"_s, u"50%"_s, m_width);
          }
          if(!get_linked(*element, u"fy"_s).isEmpty()) {
            focus.m_y = read(u"fy"_s, u"50%"_s, m_height);
          }
          fill.m_geometry = RadialGradient(
            center, read(u"r"_s, u"50%"_s, get_diagonal()), focus);
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

      Style make_style(const Properties& properties, const Rect& bounds,
          Point offset) const {
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
        auto stroke_opacity = std::clamp(parse_fraction(
          get(properties, u"stroke-opacity"_s, u"1"_s), 1), 0.0, 1.0);
        auto paint = make_paint(get(properties, u"stroke"_s, u"none"_s),
          stroke_opacity, properties, bounds, offset);
        auto stroke = Stroke();
        if(!paint) {
          stroke.m_color = make_color(112, 112, 112);
          stroke.m_is_enabled = false;
          style.m_stroke = std::move(stroke);
          return style;
        }
        if(auto solid = std::get_if<SolidFill>(&*paint)) {
          stroke.m_color = solid->m_color;
        } else if(auto gradient = std::get_if<GradientFill>(&*paint)) {
          stroke.m_color = gradient->m_stops.front().m_color;
        }
        stroke.m_width = round(parse_length(
          get(properties, u"stroke-width"_s, u"1"_s), get_diagonal()).value_or(
            1));
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
          stroke.m_dashes = parse_numbers(dashes);
        }
        style.m_stroke = std::move(stroke);
        return style;
      }
  };

  void append(Transaction& transaction, const NodeId& parent, Item item) {
    if(item.m_header.m_id.is_nil()) {
      item.m_header.m_id = generate_node_id();
    }
    item.m_header.m_rotation = derive_rotation(item.m_header.m_transform);
    auto id = transaction.append(
      parent, std::move(item.m_header), std::move(item.m_payload));
    for(auto& child : item.m_children) {
      append(transaction, id, std::move(child));
    }
  }
}

bool Rellu::is_svg(const QString& text) {
  if(!text.contains(u"<svg"_s)) {
    return false;
  }
  return static_cast<bool>(parse(text));
}

void Rellu::read_svg(const QString& text, Document& document) {
  auto root = parse(text);
  if(!root) {
    throw std::invalid_argument("The text is not SVG markup.");
  }
  auto items = Converter(*root).convert();
  if(items.empty()) {
    return;
  }
  auto transaction = Transaction(document);
  if(items.size() == 1) {
    append(transaction, NodeId(), std::move(items.front()));
  } else {
    auto group = Item();
    group.m_payload = Group();
    group.m_children = std::move(items);
    append(transaction, NodeId(), std::move(group));
  }
  transaction.commit();
}
