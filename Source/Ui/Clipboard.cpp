#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <QBuffer>
#include <QImage>
#include <QUuid>
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Io/XdClipboard.hpp"
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
  constexpr auto TEXT_FRAME_SIZE = 300.0;
  constexpr auto ARTBOARD_GAP = 70.0;
  constexpr auto GROUP_PREFIX = std::string_view("Group ");
  constexpr auto COPY_SEPARATOR = std::string_view(" \xE2\x80\x93 ");

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
    }
    return nullptr;
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

  std::optional<Rect> unite_bounds(
      const std::optional<Rect>& left, const std::optional<Rect>& right) {
    if(!left) {
      return right;
    } else if(!right) {
      return left;
    }
    return Rellu::unite(*left, *right);
  }

  std::optional<Rect> offset_bounds(
      const std::optional<Rect>& rect, Point offset) {
    if(!rect) {
      return std::nullopt;
    }
    return Rect(rect->m_x + offset.m_x, rect->m_y + offset.m_y,
      rect->m_width, rect->m_height);
  }

  bool is_numbered(std::string_view name, std::string_view prefix) {
    if(!name.starts_with(prefix) || name.size() == prefix.size()) {
      return false;
    }
    return std::all_of(name.begin() + prefix.size(), name.end(),
      [] (auto c) { return c >= '0' && c <= '9'; });
  }

  std::string get_copy_base(const std::string& name) {
    auto position = name.rfind(COPY_SEPARATOR);
    if(position != std::string::npos && is_numbered(
        std::string_view(name).substr(position), COPY_SEPARATOR)) {
      return name.substr(0, position);
    }
    return name;
  }

  class Namer {
    public:
      explicit Namer(const Document& document)
        : m_document(&document) {}

      std::string make(const std::string& prefix) {
        auto [entry, is_new] = m_next.try_emplace(prefix, 0);
        if(is_new) {
          auto name = make_node_name(*m_document, prefix);
          entry->second = std::stoull(name.substr(prefix.size()));
        }
        return prefix + std::to_string(entry->second++);
      }

    private:
      const Document* m_document;
      std::map<std::string, unsigned long long> m_next;
  };
}

struct Clipboard::Content {
  struct Entry {
    NodeHeader m_header;
    Payload m_payload;
    NodeId m_parent;
  };

  struct Tree {
    NodeId m_container;
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
  std::optional<Rect> m_bounds;
  bool m_is_centered = false;
  bool m_is_foreign = false;
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
      tree.m_container = node.m_parent;
      tree.m_transform = get_world_transform(document, root);
      tree.m_bounds =
        get_world_bounds(document, root, BoundsType::GEOMETRY, measurer);
      if(!node.get_artboard()) {
        tree.m_artboard = find_artboard(document, root);
        if(!tree.m_artboard.is_nil()) {
          auto& frame = document.get(tree.m_artboard).get_artboard()->m_frame;
          tree.m_offset = Point(tree.m_transform.m_tx - frame.m_x,
            tree.m_transform.m_ty - frame.m_y);
        }
      }
      tree.m_nodes = collect(document, root, visited, states, components);
      content->m_bounds = unite_bounds(content->m_bounds, tree.m_bounds);
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
    content->m_bounds = tree.m_bounds;
    content->m_trees.push_back(std::move(tree));
    content->m_is_centered = true;
    content->m_prefix = prefix;
    content->m_mark = QUuid::createUuid().toByteArray();
    return content;
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
      auto namer = Namer(document);
      auto& bounds = m_content->m_bounds;
      auto& view = m_target.m_view;
      auto target = find_target(document, m_target.m_artboard);
      if(target.is_nil() && m_content->m_is_foreign) {
        target = find_target(document, m_target.m_focus);
      }
      auto is_centered = view && bounds &&
        (m_content->m_is_centered || !view->intersects(*bounds));
      auto offset = Point(0, 0);
      if(is_centered) {
        auto center = view->get_center();
        offset = Point(std::round(center.m_x - bounds->m_width / 2) -
          bounds->m_x, std::round(center.m_y - bounds->m_height / 2) -
          bounds->m_y);
      }
      for(auto& tree : m_content->m_trees) {
        auto& root = tree.m_nodes.front();
        auto copy = make_copy(root, mapping);
        auto container = NodeId();
        auto transform = tree.m_transform;
        if(auto box = std::get_if<Box<Artboard>>(&copy.second)) {
          auto artboard = **box;
          artboard.m_frame.m_x += offset.m_x;
          artboard.m_frame.m_y += offset.m_y;
          if(!is_centered) {
            artboard.m_frame.m_x = find_free_column(document, artboard.m_frame);
          }
          copy.second = std::move(artboard);
          if(is_named(document, copy.first.m_name)) {
            copy.first.m_name = namer.make(get_copy_base(copy.first.m_name) +
              std::string(COPY_SEPARATOR));
          }
        } else {
          if(!target.is_nil() && tree.m_offset) {
            auto& frame = document.get(target).get_artboard()->m_frame;
            transform.m_tx = frame.m_x + tree.m_offset->m_x;
            transform.m_ty = frame.m_y + tree.m_offset->m_y;
            container = target;
          } else if(is_centered) {
            transform = make_translation(offset.m_x, offset.m_y) * transform;
            container =
              find_artboard_at(document, offset_bounds(tree.m_bounds, offset));
          } else if(!tree.m_container.is_nil() &&
              document.find(tree.m_container)) {
            container = tree.m_container;
          } else if(!tree.m_artboard.is_nil() &&
              document.find(tree.m_artboard)) {
            container = tree.m_artboard;
          } else if(m_content->m_is_foreign) {
            container = find_artboard_at(document, tree.m_bounds);
          }
          if(auto inverse =
              invert(get_world_transform(document, container))) {
            copy.first.m_transform = *inverse * transform;
          }
          if(!m_content->m_prefix.empty()) {
            copy.first.m_name = namer.make(m_content->m_prefix);
          } else if(std::holds_alternative<Group>(copy.second) &&
              is_numbered(copy.first.m_name, GROUP_PREFIX)) {
            copy.first.m_name = namer.make(std::string(GROUP_PREFIX));
          }
        }
        m_copies.push_back(transaction.append(
          container, std::move(copy.first), std::move(copy.second)));
        insert_descendants(transaction, tree.m_nodes, mapping);
      }
      for(auto& tree : m_content->m_states) {
        auto copy = make_copy(tree.front(), mapping);
        transaction.insert_root(std::move(copy.first), std::move(copy.second));
        insert_descendants(transaction, tree, mapping);
      }
      for(auto interaction : m_content->m_interactions) {
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

    std::optional<std::vector<NodeId>> get_selection() const override {
      return m_copies;
    }

  private:
    std::shared_ptr<const Content> m_content;
    PasteTarget m_target;
    std::vector<NodeId> m_copies;

    static bool is_mapped(const Mapping& mapping, const NodeId& id) {
      return std::any_of(mapping.begin(), mapping.end(),
        [&] (const auto& entry) { return entry.second == id; });
    }

    NodeId find_target(const Document& document, const NodeId& id) const {
      auto node = document.find(id);
      if(!node || !node->get_artboard() ||
          (m_target.m_view &&
            !m_target.m_view->intersects(node->get_artboard()->m_frame))) {
        return NodeId();
      }
      return id;
    }

    static bool is_named(const Document& document, const std::string& name) {
      auto& canvas = document.get_canvas();
      return std::any_of(canvas.begin(), canvas.end(), [&] (const auto& id) {
        auto& node = document.get(id);
        return node.get_artboard() && node.m_header.m_name == name;
      });
    }

    static double find_free_column(const Document& document, Rect frame) {
      auto is_overlapping = true;
      while(is_overlapping) {
        is_overlapping = false;
        for(auto& id : document.get_canvas()) {
          if(auto artboard = document.get(id).get_artboard();
              artboard && artboard->m_frame.intersects(frame)) {
            frame.m_x = artboard->m_frame.get_right() + ARTBOARD_GAP;
            is_overlapping = true;
          }
        }
      }
      return frame.m_x;
    }

    static NodeId find_artboard_at(
        const Document& document, const std::optional<Rect>& bounds) {
      if(!bounds) {
        return NodeId();
      }
      auto& canvas = document.get_canvas();
      for(auto i = canvas.rbegin(); i != canvas.rend(); ++i) {
        if(auto artboard = document.get(*i).get_artboard()) {
          if(artboard->m_frame.intersects(*bounds) ||
              artboard->m_frame.contains(bounds->get_top_left())) {
            return *i;
          }
        }
      }
      return NodeId();
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

std::shared_ptr<const Clipboard::Content>& Clipboard::get_published() {
  static auto published = std::shared_ptr<const Content>();
  return published;
}

Clipboard::Clipboard() = default;

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
  sort_by_z_order(document, roots);
  auto content = Content::make(document, roots, measurer);
  auto items = std::vector<XdClipboardItem>();
  for(auto i = std::size_t(0); i != roots.size(); ++i) {
    items.push_back(XdClipboardItem(roots[i],
      content->m_trees[i].m_transform, content->m_trees[i].m_offset));
  }
  content->m_xd = write_xd_clipboard(document, items,
    content->m_bounds.value_or(Rect(0, 0, 0, 0)), layout);
  m_content = std::move(content);
  get_published() = m_content;
  return true;
}

void Clipboard::clear() {
  m_content.reset();
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
    data.hasImage() || data.hasText();
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
          tree.m_container = NodeId();
          if(!std::holds_alternative<Box<Artboard>>(
              tree.m_nodes.front().m_payload)) {
            if(index < read.m_offsets.size()) {
              tree.m_offset = read.m_offsets[index];
            }
            ++index;
          }
        }
        if(read.m_bounds) {
          content->m_bounds = read.m_bounds;
        }
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
      auto content = Content::make_node(NodeHeader(), shape, "Image ");
      content->m_images.push_back(std::move(resource));
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
      m_content = Content::make_node(std::move(header), text, std::string());
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
