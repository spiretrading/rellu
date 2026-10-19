#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>
#include <QHashFunctions>
#include "Rellu/Geometry/SvgPath.hpp"
#include "Rellu/Io/XdImporters.hpp"
#include "Rellu/Io/XdValues.hpp"

using namespace Qt::StringLiterals;
using namespace Rellu;

namespace {
  struct StringHash {
    std::size_t operator ()(const QString& text) const {
      return qHash(text);
    }
  };

  struct PathCache {
    std::mutex m_mutex;
    std::unordered_map<QString, Shared<Path>, StringHash> m_paths;
  };

  std::optional<Shared<Path>> parse_path(const QString& data,
      XdImport& import) {
    auto& cache = import.get_states().get<PathCache>();
    {
      auto lock = std::lock_guard(cache.m_mutex);
      if(auto entry = cache.m_paths.find(data); entry != cache.m_paths.end()) {
        return entry->second;
      }
    }
    auto bytes = data.toLatin1();
    auto path = parse_svg_path(
      std::string_view(bytes.constData(), static_cast<std::size_t>(
        bytes.size())));
    if(!path) {
      return std::nullopt;
    }
    auto lock = std::lock_guard(cache.m_mutex);
    return cache.m_paths.emplace(data, Shared(std::move(*path))).first->second;
  }

  std::optional<ShapeGeometry> read_rectangle(XdObject& shape) {
    auto x = shape.get_number("x"_L1, 0);
    auto y = shape.get_number("y"_L1, 0);
    auto width = shape.get_number("width"_L1);
    auto height = shape.get_number("height"_L1);
    if(!width || !height) {
      return std::nullopt;
    }
    auto rectangle = RectangleShape();
    rectangle.m_bounds = Rect(x, y, *width, *height);
    auto& json = shape.get_json();
    auto r = json.value("r"_L1);
    if(r.isDouble()) {
      auto radius = shape.get_number("r"_L1, 0);
      rectangle.m_radii = CornerRadii(radius, radius, radius, radius);
    } else if(r.isArray()) {
      auto radii = r.toArray();
      if(radii.size() == 4 && radii[0].isDouble() && radii[1].isDouble() &&
          radii[2].isDouble() && radii[3].isDouble()) {
        rectangle.m_radii = CornerRadii(radii[0].toDouble(),
          radii[1].toDouble(), radii[2].toDouble(), radii[3].toDouble());
        shape.consume("r"_L1);
      }
    }
    return rectangle;
  }

  std::optional<ShapeGeometry> read_circle(XdObject& shape) {
    auto cx = shape.get_number("cx"_L1);
    auto cy = shape.get_number("cy"_L1);
    auto r = shape.get_number("r"_L1);
    if(!cx || !cy || !r) {
      return std::nullopt;
    }
    return EllipseShape(Point(*cx, *cy), *r, *r);
  }

  std::optional<ShapeGeometry> read_ellipse(XdObject& shape) {
    auto cx = shape.get_number("cx"_L1);
    auto cy = shape.get_number("cy"_L1);
    auto rx = shape.get_number("rx"_L1);
    auto ry = shape.get_number("ry"_L1);
    if(!cx || !cy || !rx || !ry) {
      return std::nullopt;
    }
    return EllipseShape(Point(*cx, *cy), *rx, *ry);
  }

  std::optional<ShapeGeometry> read_line(XdObject& shape) {
    auto x1 = shape.get_number("x1"_L1);
    auto y1 = shape.get_number("y1"_L1);
    auto x2 = shape.get_number("x2"_L1);
    auto y2 = shape.get_number("y2"_L1);
    if(!x1 || !y1 || !x2 || !y2) {
      return std::nullopt;
    }
    return LineShape(Point(*x1, *y1), Point(*x2, *y2));
  }

  std::optional<ShapeGeometry> read_polygon(XdObject& shape) {
    auto count = shape.get_number("uxdesign#cornerCount"_L1);
    auto width = shape.get_number("uxdesign#width"_L1);
    auto height = shape.get_number("uxdesign#height"_L1);
    if(!count || !width || !height) {
      return std::nullopt;
    }
    auto polygon = PolygonShape();
    polygon.m_corner_count = static_cast<int>(*count);
    polygon.m_size = Size(*width, *height);
    polygon.m_corner_radius = shape.get_number("uxdesign#cornerRadius"_L1, 0);
    polygon.m_star_ratio = shape.get_number("uxdesign#starRatio"_L1, 100);
    if(auto points = shape.get_array("points"_L1)) {
      polygon.m_vertices.reserve(points->get_size());
      for(auto i = std::size_t(0); i != points->get_size(); ++i) {
        auto point = points->get_object(i);
        if(!point) {
          continue;
        }
        polygon.m_vertices.push_back(Point(point->get_number("x"_L1, 0),
          point->get_number("y"_L1, 0)));
      }
    }
    return polygon;
  }

  std::optional<ShapeGeometry> read_path(XdObject& shape, XdImport& import) {
    auto data = shape.get_string("path"_L1);
    if(!data) {
      return std::nullopt;
    }
    auto path = parse_path(*data, import);
    if(!path) {
      import.warn("Malformed path data");
      path = Shared(Path());
    }
    auto result = PathShape();
    result.m_path = std::move(*path);
    auto winding = shape.get_json().value("winding"_L1).toString();
    if(winding == "evenodd"_L1) {
      result.m_fill_rule = FillRule::EVEN_ODD;
      shape.consume("winding"_L1);
    } else if(winding == "nonzero"_L1) {
      shape.consume("winding"_L1);
    }
    return result;
  }

  std::optional<ShapeGeometry> read_compound(XdObject& shape,
      XdImport& import) {
    auto operation = shape.get_json().value("operation"_L1).toString();
    auto result = CompoundShape();
    if(operation == "add"_L1) {
      result.m_operation = CompoundOperation::UNION;
    } else if(operation == "subtract"_L1) {
      result.m_operation = CompoundOperation::SUBTRACT;
    } else if(operation == "intersect"_L1) {
      result.m_operation = CompoundOperation::INTERSECT;
    } else if(operation == "exclude"_L1) {
      result.m_operation = CompoundOperation::EXCLUDE;
    } else {
      return std::nullopt;
    }
    shape.consume("operation"_L1);
    if(auto data = shape.get_string("path"_L1)) {
      if(auto path = parse_path(*data, import)) {
        result.m_path = std::move(*path);
      } else {
        import.warn("Malformed path data");
      }
    }
    return result;
  }

  std::optional<Video> read_video(XdObject& ux, XdImport& import) {
    auto media = ux.get_object("mediaFill"_L1);
    auto guid = ux.get_string("mediaFileGuid"_L1);
    if(!media || !guid) {
      import.warn("Video without media");
      return std::nullopt;
    }
    auto poster = read_fill(*media, import);
    if(!poster || !std::holds_alternative<ImageFill>(*poster)) {
      import.warn("Video without a poster image");
      return std::nullopt;
    }
    ux.consume("subtype"_L1);
    auto video = Video();
    video.m_poster = std::get<ImageFill>(std::move(*poster));
    video.m_media = to_node_id(*guid);
    video.m_is_looping = ux.get_bool("loop"_L1, false);
    return video;
  }
}

std::optional<ShapeGeometry> Rellu::read_geometry(
    XdObject& shape, XdImport& import) {
  auto type = shape.get_json().value("type"_L1).toString();
  auto geometry = [&] () -> std::optional<ShapeGeometry> {
    if(type == "rect"_L1) {
      return read_rectangle(shape);
    } else if(type == "circle"_L1) {
      return read_circle(shape);
    } else if(type == "ellipse"_L1) {
      return read_ellipse(shape);
    } else if(type == "line"_L1) {
      return read_line(shape);
    } else if(type == "polygon"_L1) {
      return read_polygon(shape);
    } else if(type == "path"_L1) {
      return read_path(shape, import);
    } else if(type == "compound"_L1) {
      return read_compound(shape, import);
    }
    return std::nullopt;
  }();
  if(geometry) {
    shape.consume("type"_L1);
  }
  return geometry;
}

void Rellu::add_shape_importer(XdImporter& importer) {
  importer.add_document_importer(XdPhase::RESOURCES, [] (XdImport& import) {
    import.get_states().get<PathCache>();
  });
  importer.set_node_importer("shape"_L1, [] (XdNode& node) {
    auto shape = node.m_json->get_object("shape"_L1);
    if(!shape) {
      node.m_import->warn("Shape without geometry");
      return;
    }
    auto geometry = read_geometry(*shape, *node.m_import);
    if(!geometry) {
      node.m_import->warn("Unsupported shape geometry");
      return;
    }
    if(std::holds_alternative<CompoundShape>(*geometry)) {
      node.m_children = shape->get_array("children"_L1);
    }
    auto style = Style();
    auto video = std::optional<Video>();
    if(auto ux = get_ux(*shape)) {
      if(ux->get_json().value("subtype"_L1).toString() == "video"_L1) {
        video = read_video(*ux, *node.m_import);
      } else if(auto media = ux->get_object("mediaFill"_L1)) {
        if(auto fill = read_fill(*media, *node.m_import)) {
          style.m_fill = std::move(*fill);
        }
      }
    }
    if(auto group = node.m_json->get_object("group"_L1)) {
      if(auto children = group->get_array("children"_L1)) {
        if(children->get_size() != 0) {
          node.m_import->warn("Shape with group children");
        }
      }
    }
    node.m_payload =
      Shape(std::move(*geometry), std::move(style), std::move(video));
  });
}
