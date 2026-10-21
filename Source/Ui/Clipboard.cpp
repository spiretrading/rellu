#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QUuid>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Commands/ArtboardPlacement.hpp"
#include "Rellu/Commands/CopyNames.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/EffectCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/StyleCommands.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Io/SvgImport.hpp"
#include "Rellu/Io/XdClipboard.hpp"
#include "Rellu/Localization/Localization.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Ui/Clipboard.hpp"

using namespace Rellu;

namespace {
  using Mapping = std::unordered_map<NodeId, NodeId>;

  const auto MARK_TYPE = QStringLiteral("application/x-rellu-clipboard");
  const auto XD_TYPE = QStringLiteral(
    "application/x-qt-windows-mime;value=\"%1\"").arg(QString::fromLatin1(
      XD_CLIPBOARD_FORMAT.data(),
      static_cast<qsizetype>(XD_CLIPBOARD_FORMAT.size())));
  const auto SVG_TYPE = QStringLiteral(
    "application/x-qt-windows-mime;value=\"Scalable Vector Graphics\"");
  const auto IMAGE_DIRECTORY = QStringLiteral("Rellu Clipboard");
  constexpr auto TEXT_FRAME_SIZE = 300.0;
  constexpr auto SEQUENCE_PADDING = 10.0;
  constexpr auto MINIMUM_OVERLAP = 10.0;
  constexpr auto ROUNDING_TOLERANCE = 0.001;

  void remap(NodeId& id, const Mapping& mapping) {
    if(auto entry = mapping.find(id); entry != mapping.end()) {
      id = entry->second;
    }
  }

  void remap(std::vector<NodeId>& ids, const Mapping& mapping) {
    for(auto& id : ids) {
      remap(id, mapping);
    }
  }

  template<typename V>
  void remap_keys(std::map<NodeId, V>& map, const Mapping& mapping) {
    auto remapped = std::map<NodeId, V>();
    for(auto& entry : map) {
      auto key = entry.first;
      remap(key, mapping);
      remapped.emplace(key, std::move(entry.second));
    }
    map = std::move(remapped);
  }

  void remap(std::map<NodeId, NodeId>& map, const Mapping& mapping) {
    remap_keys(map, mapping);
    for(auto& entry : map) {
      remap(entry.second, mapping);
    }
  }

  void remap(Overrides& overrides, const Mapping& mapping) {
    remap(overrides.m_added, mapping);
    remap(overrides.m_removed, mapping);
    remap(overrides.m_reordered, mapping);
    remap_keys(overrides.m_properties, mapping);
  }

  void remap(Payload& payload, const Mapping& mapping) {
    if(auto reference = std::get_if<Reference>(&payload)) {
      remap(reference->m_source, mapping);
    } else if(auto group = std::get_if<Group>(&payload)) {
      if(group->m_stack) {
        remap_keys(group->m_stack->m_spacing, mapping);
      }
      if(auto& instance = group->m_component_instance) {
        remap(instance->m_sync_map, mapping);
        remap(instance->m_overrides, mapping);
        for(auto& state : instance->m_states) {
          remap(state.m_tree, mapping);
          remap(state.m_sync_map, mapping);
          remap(state.m_overrides, mapping);
        }
      }
    }
  }

  NodeId* get_destination(Action& action) {
    if(auto transition = std::get_if<ArtboardTransition>(&action)) {
      return &transition->m_destination;
    } else if(auto overlay = std::get_if<Overlay>(&action)) {
      return &overlay->m_destination;
    } else if(auto playback = std::get_if<VideoPlayback>(&action)) {
      return &playback->m_target;
    } else if(auto scroll = std::get_if<ScrollTransition>(&action)) {
      return &scroll->m_destination;
    }
    return nullptr;
  }

  QString decode_text(const QByteArray& bytes) {
    auto text = QString();
    if(bytes.size() >= 2 && bytes[1] == '\0') {
      text = QString::fromUtf16(reinterpret_cast<const char16_t*>(
        bytes.constData()), bytes.size() / 2);
    } else {
      text = QString::fromUtf8(bytes);
    }
    while(!text.isEmpty() && text.back() == QChar(0)) {
      text.chop(1);
    }
    return text;
  }

  QString find_svg(const QMimeData& data) {
    if(data.hasFormat(SVG_TYPE)) {
      if(auto text = decode_text(data.data(SVG_TYPE)); is_svg(text)) {
        return text;
      }
    }
    if(data.hasText()) {
      if(auto text = data.text(); is_svg(text)) {
        return text;
      }
    }
    return QString();
  }

  std::vector<std::filesystem::path>& get_image_folders() {
    static auto folders = std::vector<std::filesystem::path>();
    return folders;
  }

  std::filesystem::path to_folder_name(const QByteArray& mark) {
    auto id = QUuid::fromString(QString::fromLatin1(mark));
    return std::filesystem::path(
      id.toString(QUuid::WithoutBraces).toStdU16String());
  }

  bool can_copy(const Document& document, const NodeId& id) {
    auto& node = document.get(id);
    return !node.get_reference() &&
      (!node.m_parent.is_nil() || document.is_on_canvas(id));
  }

  std::vector<NodeId> get_copyable(
      const Document& document, const std::vector<NodeId>& nodes) {
    auto roots = get_top_level(document, nodes);
    std::erase_if(roots, [&] (const auto& id) {
      return !can_copy(document, id);
    });
    return roots;
  }

  const ImageId* get_image(const Payload& payload) {
    auto fill = static_cast<const Fill*>(nullptr);
    if(auto shape = std::get_if<Box<Shape>>(&payload)) {
      fill = &(*shape)->m_style.m_fill;
    } else if(auto artboard = std::get_if<Box<Artboard>>(&payload)) {
      fill = &(*artboard)->m_background;
    }
    if(fill) {
      if(auto image = std::get_if<ImageFill>(fill)) {
        return &image->m_image;
      }
    }
    return nullptr;
  }

  bool has_image_fill(const Payload& payload) {
    if(auto shape = std::get_if<Box<Shape>>(&payload)) {
      return std::holds_alternative<ImageFill>((*shape)->m_style.m_fill);
    }
    return false;
  }

  std::optional<Rect> unite_bounds(
      const std::optional<Rect>& left, const std::optional<Rect>& right) {
    if(!left) {
      return right;
    } else if(!right) {
      return left;
    }
    return Rellu::unite(*left, *right);
  }

  double round_delta(double delta) {
    auto floor = std::floor(delta);
    if(delta < floor + ROUNDING_TOLERANCE) {
      return floor;
    }
    return std::ceil(delta);
  }

  bool overlaps_view(const std::optional<Rect>& view, const Rect& region) {
    return !view || view->intersects(region);
  }

  Rect clip_to_view(const std::optional<Rect>& view, const Rect& region) {
    if(!view) {
      return region;
    } else if(!view->intersects(region)) {
      return Rect(region.m_x, region.m_y, 0, 0);
    }
    return intersect(region, *view);
  }

  bool is_visible(const std::optional<Rect>& view, const Rect& region) {
    if(!view || view->contains(region)) {
      return true;
    }
    auto overlap = intersect(region, *view);
    return overlap.m_width >= MINIMUM_OVERLAP &&
      overlap.m_height >= MINIMUM_OVERLAP;
  }

  enum class AppearanceKind : unsigned char {
    NONE,
    GROUP,
    SHAPE,
    RECTANGLE,
    LINE,
    TEXT,
    ARTBOARD
  };

  struct Appearance {
    AppearanceKind m_kind = AppearanceKind::NONE;
    double m_opacity = 1;
    BlendMode m_blend_mode = BlendMode::NORMAL;
    std::optional<Fill> m_fill;
    bool m_is_strokeless = false;
    StrokeEdit m_stroke;
    std::optional<std::vector<Effect>> m_effects;
    std::optional<CornerRadii> m_radii;
    std::optional<Text> m_text;
    bool m_is_image = false;
    std::vector<ImageResource> m_images;
  };

  template<typename T>
  struct Uniform {
    std::optional<T> m_value;
    bool m_is_uniform = false;

    void add(const T& value) {
      if(!m_value) {
        m_value = value;
        m_is_uniform = true;
      } else if(m_is_uniform && !(*m_value == value)) {
        m_is_uniform = false;
      }
    }

    bool is_set() const {
      return m_value && m_is_uniform;
    }
  };

  struct LeafStyles {
    Uniform<Fill> m_fill;
    Uniform<Color> m_color;
    Uniform<double> m_width;
    Uniform<StrokeAlignment> m_alignment;
    Uniform<std::vector<double>> m_dashes;
    Uniform<StrokeJoin> m_join;
    Uniform<StrokeCap> m_cap;

    bool is_any_set() const {
      return m_fill.is_set() || m_color.is_set() || m_width.is_set() ||
        m_alignment.is_set() || m_dashes.is_set() || m_join.is_set() ||
        m_cap.is_set();
    }
  };

  AppearanceKind get_appearance_kind(const Payload& payload) {
    if(std::holds_alternative<Group>(payload)) {
      return AppearanceKind::GROUP;
    } else if(std::holds_alternative<Box<Artboard>>(payload)) {
      return AppearanceKind::ARTBOARD;
    } else if(std::holds_alternative<Box<Text>>(payload)) {
      return AppearanceKind::TEXT;
    } else if(auto shape = std::get_if<Box<Shape>>(&payload)) {
      auto& geometry = (*shape)->m_geometry;
      if(std::holds_alternative<RectangleShape>(geometry)) {
        return AppearanceKind::RECTANGLE;
      } else if(std::holds_alternative<LineShape>(geometry)) {
        return AppearanceKind::LINE;
      }
      return AppearanceKind::SHAPE;
    }
    return AppearanceKind::NONE;
  }

  bool has_fill(AppearanceKind kind) {
    return kind == AppearanceKind::SHAPE ||
      kind == AppearanceKind::RECTANGLE || kind == AppearanceKind::TEXT ||
      kind == AppearanceKind::ARTBOARD;
  }

  bool has_stroke(AppearanceKind kind) {
    return kind == AppearanceKind::SHAPE ||
      kind == AppearanceKind::RECTANGLE || kind == AppearanceKind::LINE ||
      kind == AppearanceKind::TEXT;
  }

  bool has_inner_shadow(AppearanceKind kind) {
    return has_stroke(kind) && kind != AppearanceKind::LINE;
  }

  bool has_opacity(AppearanceKind kind) {
    return kind != AppearanceKind::NONE && kind != AppearanceKind::ARTBOARD;
  }

  bool is_enabled(const Fill& fill) {
    return !std::holds_alternative<NoFill>(fill);
  }

  bool is_open(const Payload& payload) {
    if(auto shape = std::get_if<Box<Shape>>(&payload)) {
      if(auto path = std::get_if<PathShape>(&(*shape)->m_geometry)) {
        auto& segments = path->m_path->get_segments();
        return !segments.empty() &&
          segments.back().m_type != SegmentType::CLOSE;
      }
    }
    return false;
  }

  const Fill* find_fill(const Payload& payload) {
    if(auto shape = std::get_if<Box<Shape>>(&payload)) {
      return &(*shape)->m_style.m_fill;
    } else if(auto text = std::get_if<Box<Text>>(&payload)) {
      return &(*text)->m_style.m_fill;
    } else if(auto artboard = std::get_if<Box<Artboard>>(&payload)) {
      return &(*artboard)->m_background;
    }
    return nullptr;
  }

  const Style* find_style(const Payload& payload) {
    if(auto shape = std::get_if<Box<Shape>>(&payload)) {
      return &(*shape)->m_style;
    } else if(auto text = std::get_if<Box<Text>>(&payload)) {
      return &(*text)->m_style;
    }
    return nullptr;
  }

  Style* find_editable_style(Payload& payload) {
    if(auto shape = std::get_if<Box<Shape>>(&payload)) {
      return &shape->edit().m_style;
    } else if(auto text = std::get_if<Box<Text>>(&payload)) {
      return &text->edit().m_style;
    }
    return nullptr;
  }

  std::optional<Fill> get_text_fill(const Text& text) {
    if(!std::holds_alternative<SolidFill>(text.m_style.m_fill)) {
      return text.m_style.m_fill;
    }
    auto runs = get_runs(text);
    auto color = runs.front().m_style.m_color;
    if(std::any_of(runs.begin(), runs.end(), [&] (const auto& run) {
        return run.m_style.m_color != color;
      })) {
      return std::nullopt;
    }
    return Fill(SolidFill(color));
  }

  StrokeEdit make_stroke_edit(const Stroke& stroke) {
    auto edit = StrokeEdit();
    edit.m_color = stroke.m_color;
    edit.m_width = stroke.m_width;
    edit.m_alignment = stroke.m_alignment;
    edit.m_cap = stroke.m_cap;
    edit.m_join = stroke.m_join;
    edit.m_miter_limit = stroke.m_miter_limit;
    edit.m_dashes = stroke.m_dashes;
    edit.m_is_enabled = stroke.m_is_enabled;
    return edit;
  }

  bool is_effect(const Effect& effect, EffectKind kind) {
    if(kind == EffectKind::DROP_SHADOW) {
      return std::holds_alternative<DropShadow>(effect);
    } else if(kind == EffectKind::INNER_SHADOW) {
      return std::holds_alternative<InnerShadow>(effect);
    }
    return std::holds_alternative<Blur>(effect);
  }

  void replace_effects(std::vector<Effect>& effects,
      const std::vector<Effect>& source, EffectKind kind) {
    std::erase_if(effects, [&] (const auto& effect) {
      return is_effect(effect, kind);
    });
    for(auto& effect : source) {
      if(is_effect(effect, kind)) {
        effects.push_back(effect);
      }
    }
  }

  TextStyleEdit make_text_edit(const Text& text) {
    auto runs = get_runs(text);
    auto is_uniform = [&] (auto get) {
      return std::all_of(runs.begin(), runs.end(), [&] (const auto& run) {
        return get(run) == get(runs.front());
      });
    };
    auto& style = runs.front().m_style;
    auto edit = TextStyleEdit();
    auto family = [] (const auto& run) { return run.m_style.m_font.m_family; };
    auto face = [] (const auto& run) { return run.m_style.m_font.m_style; };
    auto size = [] (const auto& run) { return run.m_style.m_font.m_size; };
    auto spacing = [] (const auto& run) {
      return run.m_style.m_letter_spacing;
    };
    auto underline = [] (const auto& run) {
      return run.m_style.m_is_underlined;
    };
    auto strikethrough = [] (const auto& run) {
      return run.m_style.m_is_struck_through;
    };
    auto transform = [] (const auto& run) { return run.m_style.m_transform; };
    auto script = [] (const auto& run) { return run.m_style.m_script; };
    auto hyperlink = [] (const auto& run) { return run.m_hyperlink; };
    if(is_uniform(family)) {
      edit.m_family = style.m_font.m_family;
      if(is_uniform(face)) {
        edit.m_style = style.m_font.m_style;
        edit.m_postscript_name = style.m_font.m_postscript_name;
      }
    }
    if(is_uniform(size)) {
      edit.m_size = style.m_font.m_size;
    }
    if(is_uniform(spacing)) {
      edit.m_letter_spacing = style.m_letter_spacing;
    }
    if(is_uniform(underline)) {
      edit.m_is_underlined = style.m_is_underlined;
    }
    if(is_uniform(strikethrough)) {
      edit.m_is_struck_through = style.m_is_struck_through;
    }
    if(is_uniform(transform)) {
      edit.m_transform = style.m_transform;
    }
    if(is_uniform(script)) {
      edit.m_script = style.m_script;
    }
    if(is_uniform(hyperlink)) {
      edit.m_hyperlink = runs.front().m_hyperlink;
    }
    return edit;
  }

  NodeId find_artboard_at(const Document& document, const Rect& bounds) {
    auto& canvas = document.get_canvas();
    for(auto i = canvas.rbegin(); i != canvas.rend(); ++i) {
      if(auto artboard = document.get(*i).get_artboard()) {
        if(artboard->m_frame.intersects(bounds) ||
            artboard->m_frame.contains(bounds.get_top_left())) {
          return *i;
        }
      }
    }
    return NodeId();
  }
}

struct Clipboard::Content {
  struct Entry {
    NodeHeader m_header;
    Payload m_payload;
    NodeId m_parent;
  };

  struct Tree {
    NodeId m_artboard;
    Matrix m_transform;
    std::optional<Point> m_offset;
    std::optional<Rect> m_bounds;
    std::vector<Entry> m_nodes;
  };

  std::vector<Tree> m_trees;
  std::vector<std::vector<Entry>> m_states;
  std::vector<Component> m_components;
  std::vector<std::vector<Entry>> m_masters;
  std::vector<ImageResource> m_images;
  std::vector<Interaction> m_interactions;
  std::unordered_set<NodeId> m_instanced;
  bool m_is_sequenced = false;
  bool m_is_foreign = false;
  bool m_is_image = false;
  bool m_is_text = false;
  std::string m_prefix;
  QByteArray m_mark;
  QByteArray m_xd;

  static std::vector<Entry> collect(const Document& document,
      const NodeId& root, std::unordered_set<NodeId>& visited,
      std::vector<NodeId>& states, std::vector<ComponentId>& components) {
    auto entries = std::vector<Entry>();
    auto pending = std::vector<NodeId>();
    pending.push_back(root);
    while(!pending.empty()) {
      auto id = pending.back();
      pending.pop_back();
      if(!visited.insert(id).second) {
        continue;
      }
      auto& node = document.get(id);
      entries.push_back(Entry(node.m_header, node.m_payload, node.m_parent));
      if(auto group = node.get_group(); group && group->m_component_instance) {
        components.push_back(group->m_component_instance->m_component);
        for(auto& state : group->m_component_instance->m_states) {
          if(document.find(state.m_tree)) {
            states.push_back(state.m_tree);
          }
        }
      }
      pending.insert(
        pending.end(), node.m_children.rbegin(), node.m_children.rend());
    }
    return entries;
  }

  void add_resources(const Document& document, std::vector<ComponentId> ids) {
    auto& resources = document.get_resources();
    auto added = std::unordered_set<ComponentId>();
    auto visited = std::unordered_set<NodeId>();
    for(auto i = std::size_t(0); i != ids.size(); ++i) {
      auto component = resources.m_components.find(ids[i]);
      if(component == resources.m_components.end() ||
          !added.insert(ids[i]).second) {
        continue;
      }
      m_components.push_back(component->second);
      auto trees = std::vector<NodeId>({component->second.m_master});
      for(auto& state : component->second.m_states) {
        trees.push_back(state.m_tree);
      }
      for(auto& tree : trees) {
        if(document.find(tree) && !visited.contains(tree)) {
          auto states = std::vector<NodeId>();
          m_masters.push_back(collect(document, tree, visited, states, ids));
        }
      }
    }
    auto images = std::unordered_set<ImageId>();
    auto add_images = [&] (const std::vector<Entry>& entries) {
      for(auto& entry : entries) {
        if(auto image = get_image(entry.m_payload);
            image && images.insert(*image).second) {
          if(auto resource = resources.m_images.find(*image);
              resource != resources.m_images.end()) {
            m_images.push_back(resource->second);
          }
        }
      }
    };
    for(auto& tree : m_trees) {
      add_images(tree.m_nodes);
    }
    for(auto& tree : m_states) {
      add_images(tree);
    }
    for(auto& tree : m_masters) {
      add_images(tree);
    }
  }

  static std::shared_ptr<Content> make(const Document& document,
      const std::vector<NodeId>& roots, const TextMeasurer& measurer) {
    auto content = std::make_shared<Content>();
    auto visited = std::unordered_set<NodeId>();
    auto states = std::vector<NodeId>();
    auto components = std::vector<ComponentId>();
    for(auto& root : roots) {
      auto& node = document.get(root);
      auto tree = Tree();
      tree.m_transform = get_world_transform(document, root);
      tree.m_bounds =
        get_world_bounds(document, root, BoundsType::GEOMETRY, measurer);
      if(!node.get_artboard()) {
        tree.m_artboard = find_artboard(document, root);
        if(!tree.m_artboard.is_nil()) {
          auto& frame = document.get(tree.m_artboard).get_artboard()->m_frame;
          auto origin = tree.m_bounds ? tree.m_bounds->get_top_left() :
            Point(tree.m_transform.m_tx, tree.m_transform.m_ty);
          tree.m_offset =
            Point(origin.m_x - frame.m_x, origin.m_y - frame.m_y);
        }
      }
      tree.m_nodes = collect(document, root, visited, states, components);
      content->m_trees.push_back(std::move(tree));
    }
    for(auto i = std::size_t(0); i != states.size(); ++i) {
      auto state = states[i];
      if(!visited.contains(state)) {
        content->m_states.push_back(
          collect(document, state, visited, states, components));
      }
    }
    for(auto& interaction : document.get_prototype().m_interactions) {
      if(visited.contains(interaction.m_owner)) {
        content->m_interactions.push_back(interaction);
      }
    }
    for(auto& tree : content->m_trees) {
      for(auto& entry : tree.m_nodes) {
        auto group = std::get_if<Group>(&entry.m_payload);
        if((group && group->m_component_instance) ||
            content->m_instanced.contains(entry.m_parent)) {
          content->m_instanced.insert(entry.m_header.m_id);
        }
      }
    }
    for(auto& tree : content->m_states) {
      for(auto& entry : tree) {
        content->m_instanced.insert(entry.m_header.m_id);
      }
    }
    content->add_resources(document, std::move(components));
    content->m_mark = QUuid::createUuid().toByteArray();
    return content;
  }

  static std::shared_ptr<Content> make_node(
      NodeHeader header, Payload payload, const std::string& prefix) {
    auto content = std::make_shared<Content>();
    auto tree = Tree();
    tree.m_bounds = Rect(0, 0, 0, 0);
    if(auto shape = std::get_if<Box<Shape>>(&payload)) {
      if(auto rectangle =
          std::get_if<RectangleShape>(&(*shape)->m_geometry)) {
        tree.m_bounds = rectangle->m_bounds;
      }
    } else if(auto text = std::get_if<Box<Text>>(&payload)) {
      if(auto area = std::get_if<AreaFrame>(&(*text)->m_frame)) {
        tree.m_bounds = Rect(0, 0, area->m_size.m_width, area->m_size.m_height);
      }
    }
    header.m_id = generate_node_id();
    tree.m_nodes.push_back(
      Entry(std::move(header), std::move(payload), NodeId()));
    content->m_trees.push_back(std::move(tree));
    content->m_is_sequenced = true;
    content->m_prefix = prefix;
    content->m_mark = QUuid::createUuid().toByteArray();
    return content;
  }

  std::optional<Rect> get_bounds() const {
    auto bounds = std::optional<Rect>();
    for(auto& tree : m_trees) {
      bounds = unite_bounds(bounds, get_bounds(tree));
    }
    return bounds;
  }

  static Rect get_bounds(const Tree& tree) {
    if(tree.m_bounds) {
      return *tree.m_bounds;
    }
    return Rect(tree.m_transform.m_tx, tree.m_transform.m_ty, 0, 0);
  }

  static bool is_artboard(const Tree& tree) {
    return std::holds_alternative<Box<Artboard>>(
      tree.m_nodes.front().m_payload);
  }
};

class Clipboard::PasteCommand : public Command {
  public:
    PasteCommand(std::shared_ptr<const Content> content,
        PasteTarget target) noexcept
      : m_content(std::move(content)),
        m_target(std::move(target)) {}

    std::string get_label() const override {
      return "Paste";
    }

    void apply(const Document& document, Transaction& transaction) override {
      m_copies.clear();
      add_resources(document, transaction);
      auto& trees = m_content->m_trees;
      if(std::any_of(trees.begin(), trees.end(), &Content::is_artboard)) {
        paste_artboards(document, transaction);
        return;
      }
      for(auto& artboard : find_targets(document)) {
        paste_artwork(document, transaction, artboard);
      }
    }

    std::optional<std::vector<NodeId>> get_selection() const override {
      return m_copies;
    }

  private:
    struct Placement {
      NodeId m_artboard;
      NodeId m_target;
      std::vector<Point> m_deltas;
      bool m_is_overflowing = false;
    };

    std::shared_ptr<const Content> m_content;
    PasteTarget m_target;
    std::vector<NodeId> m_copies;

    static bool is_mapped(const Mapping& mapping, const NodeId& id) {
      return std::any_of(mapping.begin(), mapping.end(),
        [&] (const auto& entry) { return entry.second == id; });
    }

    static const Rect* get_frame(const Document& document, const NodeId& id) {
      if(auto node = document.find(id)) {
        if(auto artboard = node->get_artboard()) {
          return &artboard->m_frame;
        }
      }
      return nullptr;
    }

    std::vector<NodeId> find_targets(const Document& document) const {
      auto targets = std::vector<NodeId>();
      for(auto& id : m_target.m_selection) {
        if(auto frame = get_frame(document, id);
            frame && overlaps_view(m_target.m_view, *frame) &&
            std::find(targets.begin(), targets.end(), id) == targets.end()) {
          targets.push_back(id);
        }
      }
      if(targets.empty()) {
        if(get_frame(document, m_target.m_focus)) {
          targets.push_back(m_target.m_focus);
        } else {
          targets.push_back(NodeId());
        }
      }
      return targets;
    }

    Placement place(const Document& document, const NodeId& target) const {
      auto& trees = m_content->m_trees;
      auto placement = Placement();
      placement.m_artboard = target;
      placement.m_target = target;
      placement.m_deltas.assign(trees.size(), Point(0, 0));
      auto container = target;
      auto region = std::optional<Rect>();
      if(auto frame = get_frame(document, container)) {
        region = *frame;
      }
      auto is_offset = std::all_of(trees.begin(), trees.end(),
        [] (const auto& tree) { return static_cast<bool>(tree.m_offset); });
      if(!region || !overlaps_view(m_target.m_view, *region)) {
        container = NodeId();
        placement.m_artboard = NodeId();
        auto bounds = m_content->get_bounds();
        if(!m_content->m_is_foreign && !m_content->m_is_sequenced && bounds &&
            overlaps_view(m_target.m_view, *bounds)) {
          placement.m_target = NodeId();
          return placement;
        }
        is_offset = false;
        region = m_target.m_view;
      }
      if(!region) {
        return placement;
      }
      if(m_content->m_is_sequenced) {
        place_in_sequence(document, container, *region, placement);
      } else {
        place_together(document, container, *region, is_offset, placement);
      }
      return placement;
    }

    void place_together(const Document& document, const NodeId& container,
        const Rect& region, bool is_offset, Placement& placement) const {
      auto& trees = m_content->m_trees;
      if(is_offset && !container.is_nil()) {
        auto visible = std::optional<Rect>();
        auto is_overlapping = false;
        auto is_contained = true;
        for(auto& tree : trees) {
          auto bounds = Content::get_bounds(tree);
          auto placed = Rect(region.m_x + tree.m_offset->m_x,
            region.m_y + tree.m_offset->m_y, bounds.m_width, bounds.m_height);
          visible = unite_bounds(visible, placed);
          if(placed.intersects(region)) {
            is_overlapping = true;
          } else {
            is_contained = false;
          }
        }
        if(is_overlapping && is_visible(m_target.m_view, *visible)) {
          placement.m_is_overflowing = !is_contained;
          for(auto i = std::size_t(0); i != trees.size(); ++i) {
            auto bounds = Content::get_bounds(trees[i]);
            placement.m_deltas[i] =
              Point(region.m_x + trees[i].m_offset->m_x - bounds.m_x,
                region.m_y + trees[i].m_offset->m_y - bounds.m_y);
          }
          return;
        }
      }
      auto all = *m_content->get_bounds();
      auto area = region;
      if(!container.is_nil()) {
        area = clip_to_view(m_target.m_view, region);
        placement.m_is_overflowing =
          all.m_width > area.m_width || all.m_height > area.m_height;
      }
      for(auto i = std::size_t(0); i != trees.size(); ++i) {
        auto bounds = Content::get_bounds(trees[i]);
        auto x = area.m_x + area.m_width / 2 +
          (bounds.m_x - all.m_x - all.m_width / 2);
        auto y = area.m_y + area.m_height / 2 +
          (bounds.m_y - all.m_y - all.m_height / 2);
        placement.m_deltas[i] = Point(x - bounds.m_x, y - bounds.m_y);
      }
    }

    void place_in_sequence(const Document& document, const NodeId& container,
        const Rect& region, Placement& placement) const {
      auto& trees = m_content->m_trees;
      auto area = region;
      if(!container.is_nil()) {
        area = clip_to_view(m_target.m_view, region);
      }
      auto advance = 0.0;
      auto top = 0.0;
      for(auto i = std::size_t(0); i != trees.size(); ++i) {
        auto bounds = Content::get_bounds(trees[i]);
        auto shift = advance != 0 ? advance : -bounds.m_width / 2;
        auto x = area.m_x + area.m_width / 2 + shift;
        auto y = top != 0 ? top :
          area.m_y + area.m_height / 2 - bounds.m_height / 2;
        placement.m_deltas[i] = Point(x - bounds.m_x, y - bounds.m_y);
        if(i == 0) {
          advance -= bounds.m_width / 2;
          top = y;
        }
        advance += bounds.m_width + SEQUENCE_PADDING;
      }
    }

    void paste_artwork(const Document& document, Transaction& transaction,
        const NodeId& target) {
      auto placement = place(document, target);
      auto mapping = make_mapping();
      auto& trees = m_content->m_trees;
      auto namer = std::optional<std::string>();
      auto copies = std::vector<NodeId>();
      for(auto i = std::size_t(0); i != trees.size(); ++i) {
        auto& tree = trees[i];
        auto delta = Point(round_delta(placement.m_deltas[i].m_x),
          round_delta(placement.m_deltas[i].m_y));
        auto transform =
          make_translation(delta.m_x, delta.m_y) * tree.m_transform;
        auto container = find_container(document, placement.m_artboard,
          Content::get_bounds(tree), delta);
        auto copy = make_copy(tree.m_nodes.front(), mapping);
        if(auto inverse = invert(get_world_transform(document, container))) {
          copy.first.m_transform = *inverse * transform;
        }
        if(!m_content->m_prefix.empty()) {
          copy.first.m_name = make_node_name(document, m_content->m_prefix);
        }
        auto is_kept = (!tree.m_artboard.is_nil() &&
          !placement.m_target.is_nil() &&
          tree.m_artboard != placement.m_target) ||
          has_image_fill(copy.second);
        auto id = transaction.append(
          container, std::move(copy.first), std::move(copy.second));
        insert_descendants(transaction, tree.m_nodes, mapping);
        name_copy(document, transaction, id, is_kept);
        copies.push_back(id);
      }
      insert_states(transaction, mapping);
      add_interactions(document, transaction, mapping);
      if(placement.m_is_overflowing && copies.size() > 1) {
        copies = {group(document, transaction, copies)};
      }
      m_copies.insert(m_copies.end(), copies.begin(), copies.end());
    }

    NodeId find_container(const Document& document, const NodeId& artboard,
        const Rect& bounds, Point delta) const {
      if(!m_target.m_container.is_nil() &&
          document.find(m_target.m_container)) {
        return m_target.m_container;
      } else if(!artboard.is_nil()) {
        return artboard;
      }
      return find_artboard_at(document, Rect(bounds.m_x + delta.m_x,
        bounds.m_y + delta.m_y, bounds.m_width, bounds.m_height));
    }

    static NodeId group(const Document& document, Transaction& transaction,
        const std::vector<NodeId>& nodes) {
      auto parent = document.get(nodes.front()).m_parent;
      auto header = NodeHeader();
      header.m_name = make_node_name(
        document, std::string(localize("name", "Group")) + ' ');
      auto id = transaction.append(parent, std::move(header), Group());
      for(auto i = std::size_t(0); i != nodes.size(); ++i) {
        transaction.move(nodes[i], id, i);
      }
      return id;
    }

    void paste_artboards(const Document& document, Transaction& transaction) {
      auto& trees = m_content->m_trees;
      auto bounds = std::optional<Rect>();
      for(auto& tree : trees) {
        if(Content::is_artboard(tree)) {
          auto& artboard = std::get<Box<Artboard>>(
            tree.m_nodes.front().m_payload);
          bounds = unite_bounds(bounds, artboard->m_frame);
        }
      }
      auto offset = Point(0, 0);
      if(m_target.m_view && bounds) {
        auto placed = place_artboards(
          document, m_target.m_selection, *m_target.m_view, *bounds);
        offset = Point(placed.m_x - bounds->m_x, placed.m_y - bounds->m_y);
      }
      auto mapping = make_mapping();
      for(auto& tree : trees) {
        if(!Content::is_artboard(tree)) {
          continue;
        }
        auto copy = make_copy(tree.m_nodes.front(), mapping);
        auto artboard = *std::get<Box<Artboard>>(copy.second);
        artboard.m_frame.m_x += offset.m_x;
        artboard.m_frame.m_y += offset.m_y;
        if(!artboard.m_layout_grid && !m_target.m_is_prototyping) {
          artboard.m_layout_grid = make_initial_grid(
            artboard, document.get_resources().m_grid_defaults);
        }
        copy.second = std::move(artboard);
        copy.first.m_name =
          make_artboard_name(document, copy.first.m_name, true);
        auto id = transaction.insert(NodeId(), get_artboard_index(document),
          std::move(copy.first), std::move(copy.second));
        insert_descendants(transaction, tree.m_nodes, mapping);
        name_copy(document, transaction, id, false);
        m_copies.push_back(id);
      }
      insert_states(transaction, mapping);
      add_interactions(document, transaction, mapping);
    }

    Mapping make_mapping() const {
      auto mapping = Mapping();
      for(auto& tree : m_content->m_trees) {
        for(auto& entry : tree.m_nodes) {
          mapping.emplace(entry.m_header.m_id, generate_node_id());
        }
      }
      for(auto& tree : m_content->m_states) {
        for(auto& entry : tree) {
          mapping.emplace(entry.m_header.m_id, generate_node_id());
        }
      }
      return mapping;
    }

    void insert_states(Transaction& transaction, const Mapping& mapping) {
      for(auto& tree : m_content->m_states) {
        auto copy = make_copy(tree.front(), mapping);
        transaction.insert_root(std::move(copy.first), std::move(copy.second));
        insert_descendants(transaction, tree, mapping);
      }
    }

    void add_interactions(const Document& document, Transaction& transaction,
        const Mapping& mapping) {
      for(auto interaction : m_content->m_interactions) {
        if(!m_target.m_is_prototyping &&
            !m_content->m_instanced.contains(interaction.m_owner)) {
          continue;
        }
        interaction.m_id = NodeId();
        remap(interaction.m_owner, mapping);
        if(auto destination = get_destination(interaction.m_action)) {
          remap(*destination, mapping);
          if(!destination->is_nil() && !document.find(*destination) &&
              !is_mapped(mapping, *destination)) {
            continue;
          }
        }
        transaction.add_interaction(std::move(interaction));
      }
    }

    void add_resources(const Document& document, Transaction& transaction) {
      auto& resources = document.get_resources();
      for(auto& image : m_content->m_images) {
        if(!resources.m_images.contains(image.m_id)) {
          transaction.add_image(image);
        }
      }
      auto missing = std::unordered_set<ComponentId>();
      for(auto& component : m_content->m_components) {
        if(!resources.m_components.contains(component.m_id)) {
          missing.insert(component.m_id);
        }
      }
      if(missing.empty()) {
        return;
      }
      for(auto& tree : m_content->m_masters) {
        if(document.find(tree.front().m_header.m_id)) {
          continue;
        }
        transaction.insert_root(tree.front().m_header, tree.front().m_payload);
        for(auto i = std::size_t(1); i < tree.size(); ++i) {
          transaction.append(
            tree[i].m_parent, tree[i].m_header, tree[i].m_payload);
        }
      }
      for(auto& component : m_content->m_components) {
        if(missing.contains(component.m_id)) {
          transaction.add_component(component);
        }
      }
    }

    static std::pair<NodeHeader, Payload> make_copy(
        const Content::Entry& entry, const Mapping& mapping) {
      auto header = entry.m_header;
      header.m_id = mapping.at(entry.m_header.m_id);
      auto payload = entry.m_payload;
      remap(payload, mapping);
      if(auto group = std::get_if<Group>(&payload)) {
        if(auto& instance = group->m_component_instance) {
          instance->m_is_main = false;
        }
      }
      return std::pair(std::move(header), std::move(payload));
    }

    static void insert_descendants(Transaction& transaction,
        const std::vector<Content::Entry>& entries, const Mapping& mapping) {
      for(auto i = std::size_t(1); i < entries.size(); ++i) {
        auto copy = make_copy(entries[i], mapping);
        transaction.append(mapping.at(entries[i].m_parent),
          std::move(copy.first), std::move(copy.second));
      }
    }
};

class Clipboard::AppearanceCommand : public Command {
  public:
    AppearanceCommand(Appearance appearance, std::vector<NodeId> nodes)
      : m_appearance(std::move(appearance)),
        m_nodes(std::move(nodes)) {}

    static std::optional<Appearance> make_appearance(const Content& content) {
      if(content.m_is_text || content.m_trees.size() != 1) {
        return std::nullopt;
      }
      auto& entries = content.m_trees.front().m_nodes;
      auto& root = entries.front();
      auto appearance = Appearance();
      appearance.m_kind = get_appearance_kind(root.m_payload);
      appearance.m_images = content.m_images;
      if(content.m_is_image) {
        appearance.m_is_image = true;
        auto fill = *find_fill(root.m_payload);
        if(auto image = std::get_if<ImageFill>(&fill)) {
          image->m_scale_behavior = ScaleBehavior::COVER;
        }
        appearance.m_fill = std::move(fill);
        return appearance;
      }
      appearance.m_opacity = root.m_header.m_opacity;
      appearance.m_blend_mode = root.m_header.m_blend_mode;
      if(appearance.m_kind == AppearanceKind::GROUP) {
        merge_leaves(entries, appearance);
        return appearance;
      }
      auto payload = root.m_payload;
      if(auto text = std::get_if<Box<Text>>(&payload)) {
        appearance.m_fill = get_text_fill(**text);
        appearance.m_text = **text;
      } else if(auto fill = find_fill(payload)) {
        appearance.m_fill = *fill;
      }
      if(auto style = find_style(payload)) {
        if(style->m_stroke) {
          appearance.m_stroke = make_stroke_edit(*style->m_stroke);
        } else {
          appearance.m_is_strokeless = true;
        }
        appearance.m_effects = style->m_effects;
      }
      if(auto shape = std::get_if<Box<Shape>>(&payload)) {
        if(auto rectangle =
            std::get_if<RectangleShape>(&(*shape)->m_geometry)) {
          appearance.m_radii = rectangle->m_radii;
        }
      }
      return appearance;
    }

    std::string get_label() const override {
      return "Paste Appearance";
    }

    void apply(const Document& document, Transaction& transaction) override {
      auto& fill = m_appearance.m_fill;
      if(auto image = fill ? std::get_if<ImageFill>(&*fill) : nullptr) {
        auto& images = m_appearance.m_images;
        auto resource = std::find_if(images.begin(), images.end(),
          [&] (const auto& resource) {
            return resource.m_id == image->m_image;
          });
        if(resource != images.end() &&
            !document.get_resources().m_images.contains(image->m_image)) {
          transaction.add_image(*resource);
        }
      }
      for(auto& id : get_top_level(document, m_nodes)) {
        apply(document, transaction, id, true);
        add_children(document, transaction, id);
      }
    }

  private:
    Appearance m_appearance;
    std::vector<NodeId> m_nodes;

    static bool visit(const std::vector<Content::Entry>& entries,
        const NodeId& parent, LeafStyles& styles) {
      for(auto& entry : entries) {
        if(entry.m_parent != parent) {
          continue;
        }
        if(std::holds_alternative<Group>(entry.m_payload)) {
          if(visit(entries, entry.m_header.m_id, styles)) {
            return true;
          }
          continue;
        }
        auto kind = get_appearance_kind(entry.m_payload);
        auto payload = entry.m_payload;
        if(has_fill(kind)) {
          auto& fill = *find_fill(payload);
          if(is_enabled(fill) && !std::holds_alternative<ImageFill>(fill)) {
            styles.m_fill.add(fill);
          }
        }
        if(auto style = find_style(payload);
            style && style->m_stroke && style->m_stroke->m_is_enabled) {
          auto& stroke = *style->m_stroke;
          styles.m_color.add(stroke.m_color);
          styles.m_width.add(stroke.m_width);
          styles.m_alignment.add(stroke.m_alignment);
          styles.m_dashes.add(stroke.m_dashes);
          styles.m_join.add(stroke.m_join);
          styles.m_cap.add(stroke.m_cap);
        }
        if(!styles.is_any_set()) {
          return true;
        }
      }
      return false;
    }

    static void merge_leaves(
        const std::vector<Content::Entry>& entries, Appearance& appearance) {
      auto styles = LeafStyles();
      if(visit(entries, entries.front().m_header.m_id, styles)) {
        return;
      }
      appearance.m_kind = AppearanceKind::RECTANGLE;
      appearance.m_effects = std::vector<Effect>();
      appearance.m_radii = CornerRadii(0, 0, 0, 0);
      if(styles.m_fill.is_set()) {
        appearance.m_fill = *styles.m_fill.m_value;
      }
      auto& stroke = appearance.m_stroke;
      stroke.m_is_enabled = true;
      stroke.m_miter_limit = Stroke().m_miter_limit;
      if(styles.m_color.is_set()) {
        stroke.m_color = *styles.m_color.m_value;
      }
      if(styles.m_width.is_set()) {
        stroke.m_width = *styles.m_width.m_value;
      }
      if(styles.m_alignment.is_set()) {
        stroke.m_alignment = *styles.m_alignment.m_value;
      }
      if(styles.m_dashes.is_set()) {
        stroke.m_dashes = *styles.m_dashes.m_value;
      }
      if(styles.m_join.is_set()) {
        stroke.m_join = *styles.m_join.m_value;
      }
      if(styles.m_cap.is_set()) {
        stroke.m_cap = *styles.m_cap.m_value;
      }
    }

    void add_children(const Document& document, Transaction& transaction,
        const NodeId& id) {
      auto group = document.get(id).get_group();
      if(!group || group->m_repeat_grid || group->m_scroll_group ||
          group->m_mask || group->m_component_instance) {
        return;
      }
      auto children = document.get(id).m_children;
      for(auto& child : children) {
        apply(document, transaction, child, false);
        add_children(document, transaction, child);
      }
    }

    void apply(const Document& document, Transaction& transaction,
        const NodeId& id, bool is_selected) {
      auto& appearance = m_appearance;
      auto kind = get_appearance_kind(document.get(id).m_payload);
      if(kind == AppearanceKind::NONE) {
        return;
      }
      if(is_selected && !appearance.m_is_image &&
          has_opacity(kind) && has_opacity(appearance.m_kind)) {
        auto header = document.get(id).m_header;
        header.m_opacity = appearance.m_opacity;
        header.m_blend_mode = appearance.m_blend_mode;
        if(header != document.get(id).m_header) {
          transaction.replace_header(std::move(header));
        }
      }
      apply_fill(document, transaction, id, kind, is_selected);
      if(appearance.m_is_image) {
        return;
      }
      auto payload = document.get(id).m_payload;
      auto is_path_open = is_open(payload);
      if(auto style = find_editable_style(payload)) {
        apply_style(*style, kind, is_path_open, is_selected);
      }
      if(auto shape = std::get_if<Box<Shape>>(&payload);
          shape && appearance.m_radii &&
          appearance.m_kind == AppearanceKind::RECTANGLE) {
        if(auto rectangle =
            std::get_if<RectangleShape>(&shape->edit().m_geometry)) {
          rectangle->m_radii = *appearance.m_radii;
        }
      }
      if(auto text = std::get_if<Box<Text>>(&payload);
          text && appearance.m_text) {
        auto edited = edit_text_style(**text,
          TextRange(0, (*text)->m_content->size()),
          make_text_edit(*appearance.m_text));
        auto& paragraph = appearance.m_text->m_paragraph_style;
        auto paragraph_edit = ParagraphStyleEdit();
        paragraph_edit.m_alignment = paragraph.m_alignment;
        paragraph_edit.m_line_height = paragraph.m_line_height;
        paragraph_edit.m_paragraph_spacing = paragraph.m_paragraph_spacing;
        *text = edit_paragraph_style(edited, paragraph_edit);
      }
      if(payload != document.get(id).m_payload) {
        transaction.replace_payload(id, std::move(payload));
      }
    }

    void apply_fill(const Document& document, Transaction& transaction,
        const NodeId& id, AppearanceKind kind, bool is_selected) {
      auto& source = m_appearance.m_fill;
      auto target = find_fill(document.get(id).m_payload);
      if(!source || !target || !has_fill(kind) ||
          !has_fill(m_appearance.m_kind)) {
        return;
      }
      if(!is_selected && !(is_enabled(*target) && is_enabled(*source))) {
        return;
      } else if(kind == AppearanceKind::TEXT &&
          !std::holds_alternative<SolidFill>(*source)) {
        return;
      } else if(kind == AppearanceKind::ARTBOARD &&
          std::holds_alternative<ImageFill>(*source)) {
        return;
      }
      auto command = SetFillCommand(std::vector{id}, *source);
      command.apply(document, transaction);
    }

    void apply_style(Style& style, AppearanceKind kind, bool is_open,
        bool is_selected) const {
      auto& appearance = m_appearance;
      if(!has_stroke(kind) || !has_stroke(appearance.m_kind)) {
        return;
      }
      auto& source = appearance.m_stroke;
      auto is_source_enabled =
        !appearance.m_is_strokeless && source.m_is_enabled.value_or(true);
      auto is_target_enabled = style.m_stroke && style.m_stroke->m_is_enabled;
      if(is_selected || (is_target_enabled && is_source_enabled)) {
        apply_stroke(style.m_stroke, kind, is_open);
      } else if(style.m_stroke && !appearance.m_is_strokeless) {
        apply_stroke_shape(*style.m_stroke);
      }
      if(appearance.m_effects) {
        replace_effects(
          style.m_effects, *appearance.m_effects, EffectKind::DROP_SHADOW);
        replace_effects(
          style.m_effects, *appearance.m_effects, EffectKind::BLUR);
        if(has_inner_shadow(kind) && has_inner_shadow(appearance.m_kind)) {
          replace_effects(
            style.m_effects, *appearance.m_effects, EffectKind::INNER_SHADOW);
        }
      }
    }

    void apply_stroke(std::optional<Stroke>& target, AppearanceKind kind,
        bool is_open) const {
      auto& appearance = m_appearance;
      auto& source = appearance.m_stroke;
      if(appearance.m_is_strokeless) {
        if(kind == AppearanceKind::LINE) {
          auto stroke = target.value_or(Stroke());
          stroke.m_color = make_color(112, 112, 112);
          target = stroke;
        } else if(kind != AppearanceKind::TEXT || target) {
          target = std::nullopt;
        }
        return;
      }
      auto is_new = !target;
      auto stroke = target.value_or(Stroke());
      if(is_new) {
        stroke.m_is_enabled = false;
      }
      if(source.m_color) {
        stroke.m_color = *source.m_color;
      }
      if(source.m_width) {
        stroke.m_width = *source.m_width;
      }
      apply_stroke_shape(stroke);
      if(source.m_alignment && kind != AppearanceKind::TEXT &&
          kind != AppearanceKind::LINE && !is_open) {
        stroke.m_alignment = *source.m_alignment;
      }
      auto is_enabling = source.m_is_enabled &&
        !(kind == AppearanceKind::TEXT &&
          appearance.m_kind != AppearanceKind::TEXT) &&
        !(kind == AppearanceKind::LINE && !*source.m_is_enabled);
      if(is_enabling) {
        stroke.m_is_enabled = *source.m_is_enabled;
      }
      target = stroke;
    }

    void apply_stroke_shape(Stroke& stroke) const {
      auto& source = m_appearance.m_stroke;
      if(source.m_cap) {
        stroke.m_cap = *source.m_cap;
      }
      if(source.m_join) {
        stroke.m_join = *source.m_join;
      }
      if(source.m_miter_limit) {
        stroke.m_miter_limit = *source.m_miter_limit;
      }
      if(source.m_dashes) {
        stroke.m_dashes = *source.m_dashes;
      }
    }
};

std::shared_ptr<const Clipboard::Content>& Clipboard::get_published() {
  static auto published = std::shared_ptr<const Content>();
  return published;
}

Clipboard::Clipboard()
  : Clipboard(get_default_image_directory()) {}

Clipboard::Clipboard(std::filesystem::path directory)
  : m_directory(std::move(directory)) {}

Clipboard::~Clipboard() = default;

bool Clipboard::is_empty() const {
  return !m_content;
}

bool Clipboard::is_copyable(
    const Document& document, const std::vector<NodeId>& nodes) {
  return !get_copyable(document, nodes).empty();
}

bool Clipboard::copy(const Document& document,
    const std::vector<NodeId>& nodes, const TextMeasurer& measurer,
    const XdTextLayout& layout) {
  auto roots = get_copyable(document, nodes);
  if(roots.empty()) {
    return false;
  }
  if(document.get(roots.front()).get_artboard()) {
    auto get_position = [&] (const NodeId& id) {
      return get_world_bounds(document, id, BoundsType::GEOMETRY,
        measurer).value_or(Rect(0, 0, 0, 0)).get_top_left();
    };
    std::stable_sort(roots.begin(), roots.end(),
      [&] (const auto& left, const auto& right) {
        auto first = get_position(left);
        auto second = get_position(right);
        return first.m_y < second.m_y ||
          (first.m_y == second.m_y && first.m_x < second.m_x);
      });
  } else {
    sort_by_z_order(document, roots);
  }
  auto content = Content::make(document, roots, measurer);
  auto items = std::vector<XdClipboardItem>();
  for(auto i = std::size_t(0); i != roots.size(); ++i) {
    items.push_back(XdClipboardItem(roots[i],
      content->m_trees[i].m_transform, content->m_trees[i].m_offset));
  }
  auto folder = std::filesystem::path();
  auto error = std::error_code();
  if(!m_directory.empty()) {
    folder = m_directory / to_folder_name(content->m_mark);
    if(!std::filesystem::create_directories(folder, error) || error) {
      folder.clear();
    }
  }
  content->m_xd = write_xd_clipboard(document, items,
    content->get_bounds().value_or(Rect(0, 0, 0, 0)), layout, folder);
  auto& folders = get_image_folders();
  for(auto& earlier : folders) {
    std::filesystem::remove_all(earlier, error);
  }
  folders.clear();
  if(!folder.empty()) {
    if(std::filesystem::is_empty(folder, error)) {
      std::filesystem::remove(folder, error);
    } else {
      folders.push_back(folder);
    }
  }
  m_content = std::move(content);
  get_published() = m_content;
  return true;
}

void Clipboard::clear() {
  m_content.reset();
}

std::filesystem::path Clipboard::get_default_image_directory() {
  return std::filesystem::path(
    QDir::temp().filePath(IMAGE_DIRECTORY).toStdU16String());
}

void Clipboard::remove_stale_images(
    const std::filesystem::path& directory, const QMimeData* data) {
  auto error = std::error_code();
  auto current = std::filesystem::path();
  auto time = std::optional<std::filesystem::file_time_type>();
  if(data && data->hasFormat(MARK_TYPE)) {
    current = directory / to_folder_name(data->data(MARK_TYPE));
    if(auto modified = std::filesystem::last_write_time(current, error);
        !error) {
      time = modified;
    }
  }
  auto stale = std::vector<std::filesystem::path>();
  for(auto& entry :
      std::filesystem::directory_iterator(directory, error)) {
    if(entry.path() == current) {
      continue;
    }
    auto modified = entry.last_write_time(error);
    if(error || !time || modified < *time) {
      stale.push_back(entry.path());
    }
  }
  for(auto& path : stale) {
    std::filesystem::remove_all(path, error);
  }
}

std::unique_ptr<QMimeData> Clipboard::make_mime_data() const {
  if(!m_content) {
    return nullptr;
  }
  auto data = std::make_unique<QMimeData>();
  data->setData(MARK_TYPE, m_content->m_mark);
  if(!m_content->m_xd.isEmpty()) {
    data->setData(XD_TYPE, m_content->m_xd);
  }
  return data;
}

bool Clipboard::is_loadable(const QMimeData& data) {
  return data.hasFormat(MARK_TYPE) || data.hasFormat(XD_TYPE) ||
    data.hasImage() || data.hasText() || !find_svg(data).isEmpty();
}

bool Clipboard::load(const QMimeData& data, const TextMeasurer& measurer) {
  if(data.hasFormat(MARK_TYPE)) {
    auto mark = data.data(MARK_TYPE);
    if(m_content && m_content->m_mark == mark) {
      return true;
    } else if(auto& published = get_published();
        published && published->m_mark == mark) {
      m_content = published;
      return true;
    }
  }
  if(data.hasFormat(XD_TYPE)) {
    auto scratch = Document();
    try {
      auto read = read_xd_clipboard(data.data(XD_TYPE), scratch);
      auto& canvas = scratch.get_canvas();
      if(!canvas.empty()) {
        auto content = Content::make(scratch, canvas, measurer);
        auto index = std::size_t(0);
        for(auto& tree : content->m_trees) {
          tree.m_artboard = NodeId();
          if(!Content::is_artboard(tree)) {
            if(index < read.m_offsets.size()) {
              tree.m_offset = read.m_offsets[index];
            }
            ++index;
          }
        }
        content->m_is_foreign = true;
        m_content = std::move(content);
        return true;
      }
    } catch(const std::exception&) {}
  }
  if(auto svg = find_svg(data); !svg.isEmpty()) {
    auto scratch = Document();
    try {
      read_svg(svg, scratch);
      auto& canvas = scratch.get_canvas();
      if(!canvas.empty()) {
        auto content = Content::make(scratch, canvas, measurer);
        content->m_is_sequenced = true;
        content->m_is_foreign = true;
        m_content = std::move(content);
        return true;
      }
    } catch(const std::exception&) {}
  }
  if(data.hasImage()) {
    auto image = qvariant_cast<QImage>(data.imageData());
    if(!image.isNull()) {
      auto bytes = QByteArray();
      auto buffer = QBuffer(&bytes);
      buffer.open(QIODevice::WriteOnly);
      image.save(&buffer, "PNG");
      auto resource = ImageResource();
      resource.m_id = generate_node_id();
      resource.m_media_type = "image/png";
      auto raw = std::vector<std::byte>(static_cast<std::size_t>(bytes.size()));
      std::memcpy(raw.data(), bytes.constData(), raw.size());
      resource.m_data = Shared(std::move(raw));
      auto size = Size(image.width(), image.height());
      auto fill = ImageFill();
      fill.m_image = resource.m_id;
      fill.m_size = size;
      auto shape = Shape();
      shape.m_geometry =
        RectangleShape(Rect(0, 0, size.m_width, size.m_height));
      shape.m_style.m_fill = fill;
      auto content = Content::make_node(NodeHeader(), shape,
        std::string(localize("name", "Image")) + ' ');
      content->m_images.push_back(std::move(resource));
      content->m_is_image = true;
      m_content = std::move(content);
      return true;
    }
  }
  if(data.hasText()) {
    auto content = data.text().toStdU16String();
    if(!content.empty()) {
      auto style = get_default_text_style();
      auto text = Text();
      text.m_content = Shared(content);
      text.m_base_style = style;
      text.m_style.m_fill = SolidFill(style.m_color);
      text.m_frame = AreaFrame(Size(TEXT_FRAME_SIZE, TEXT_FRAME_SIZE));
      auto header = NodeHeader();
      header.m_name = make_text_name(content);
      auto pasted =
        Content::make_node(std::move(header), text, std::string());
      pasted->m_is_text = true;
      m_content = std::move(pasted);
      return true;
    }
  }
  return static_cast<bool>(m_content);
}

std::unique_ptr<Command> Clipboard::make_paste_command(
    const PasteTarget& target) const {
  if(!m_content) {
    return nullptr;
  }
  return std::make_unique<PasteCommand>(m_content, target);
}

std::unique_ptr<Command> Clipboard::make_paste_appearance_command(
    std::vector<NodeId> nodes) const {
  if(!m_content || nodes.empty()) {
    return nullptr;
  }
  auto appearance = AppearanceCommand::make_appearance(*m_content);
  if(!appearance) {
    return nullptr;
  }
  return std::make_unique<AppearanceCommand>(
    std::move(*appearance), std::move(nodes));
}
