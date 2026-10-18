#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>
#include "Rellu/Format/DocumentFile.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "ByteReader.hpp"
#include "ByteWriter.hpp"
#include "Codec.hpp"
#include "Container.hpp"
#include "Parallel.hpp"

using namespace Rellu;
using namespace Rellu::Details;

namespace {
  constexpr auto BLOCK_SIZE = std::size_t(8192);
  constexpr auto PARALLEL_SIZE = std::uint64_t(1) << 18;

  struct Info {
    std::size_t m_node_count = 0;
    std::size_t m_canvas_count = 0;
    std::size_t m_root_count = 0;
    std::size_t m_id_count = 0;
    std::size_t m_block_count = 0;
    std::size_t m_image_count = 0;
  };

  struct Layout {
    std::vector<const Node*> m_nodes;
    std::size_t m_canvas_count = 0;
    std::size_t m_root_count = 0;
    std::vector<NodeId> m_ids;
    IdIndex m_index;
  };

  struct Contents {
    Info m_info;
    std::vector<NodeId> m_ids;
    std::vector<NodeBlock> m_blocks;
    ResourceRecord m_resources;
    Prototype m_prototype;
    std::vector<std::vector<std::byte>> m_images;
  };

  std::string to_text(const std::filesystem::path& path) {
    auto text = path.u8string();
    return std::string(text.begin(), text.end());
  }

  void add_id(Layout& layout, const NodeId& id) {
    if(id.is_nil()) {
      return;
    }
    if(layout.m_ids.size() > IdIndex::MAXIMUM_VALUE) {
      throw FormatError("Document too large.");
    }
    if(layout.m_index.insert(
        id, static_cast<std::uint32_t>(layout.m_ids.size()))) {
      layout.m_ids.push_back(id);
    }
  }

  Layout make_layout(const Document& document) {
    constexpr auto UNVISITED = std::numeric_limits<std::uint32_t>::max();
    constexpr auto PENDING = UNVISITED - 1;
    auto layout = Layout();
    auto& nodes = document.get_nodes();
    auto& canvas = document.get_canvas();
    if(nodes.get_size() > IdIndex::MAXIMUM_VALUE) {
      throw FormatError("Document too large.");
    }
    auto canvas_ids = IdIndex();
    canvas_ids.reserve(canvas.size());
    for(auto& id : canvas) {
      canvas_ids.insert(id, 0);
    }
    auto records = std::vector<const Node*>();
    records.reserve(nodes.get_size());
    auto& positions = layout.m_index;
    positions.reserve(nodes.get_size());
    auto roots = std::vector<NodeId>();
    nodes.for_each([&] (const auto& node) {
      positions.insert(
        node.get_id(), static_cast<std::uint32_t>(records.size()));
      records.push_back(&node);
      if(node.m_parent.is_nil() && !canvas_ids.find(node.get_id())) {
        roots.push_back(node.get_id());
      }
    });
    std::sort(roots.begin(), roots.end());
    auto order = std::vector<std::uint32_t>(records.size(), UNVISITED);
    layout.m_nodes.reserve(records.size());
    layout.m_ids.reserve(records.size());
    auto pending = std::vector<std::uint32_t>();
    auto push = [&] (const NodeId& id) {
      auto position = positions.find(id);
      if(!position || order[*position] != UNVISITED) {
        throw FormatError("Inconsistent document: node tree mismatch.");
      }
      order[*position] = PENDING;
      pending.push_back(*position);
    };
    auto add_subtree = [&] (const NodeId& root) {
      push(root);
      while(!pending.empty()) {
        auto position = pending.back();
        pending.pop_back();
        auto node = records[position];
        order[position] = static_cast<std::uint32_t>(layout.m_nodes.size());
        layout.m_nodes.push_back(node);
        layout.m_ids.push_back(node->get_id());
        for(auto i = node->m_children.rbegin();
            i != node->m_children.rend(); ++i) {
          push(*i);
        }
      }
    };
    for(auto& root : canvas) {
      add_subtree(root);
    }
    for(auto& root : roots) {
      add_subtree(root);
    }
    if(layout.m_nodes.size() != records.size() ||
        canvas_ids.get_size() != canvas.size()) {
      throw FormatError("Inconsistent document: node tree mismatch.");
    }
    layout.m_canvas_count = canvas.size();
    layout.m_root_count = canvas.size() + roots.size();
    positions.transform([&] (const auto& position) {
      return order[position];
    });
    auto& resources = document.get_resources();
    for(auto& entry : resources.m_components) {
      add_id(layout, entry.first);
      for(auto& state : entry.second.m_states) {
        add_id(layout, state.m_id);
      }
    }
    for(auto& swatch : resources.m_swatches) {
      add_id(layout, swatch.m_id);
    }
    for(auto& style : resources.m_character_styles) {
      add_id(layout, style.m_id);
    }
    for(auto& entry : resources.m_images) {
      add_id(layout, entry.first);
    }
    return layout;
  }

  std::vector<std::byte> encode_info(const Info& info) {
    auto writer = ByteWriter();
    writer.write_varint(info.m_node_count);
    writer.write_varint(info.m_canvas_count);
    writer.write_varint(info.m_root_count);
    writer.write_varint(info.m_id_count);
    writer.write_varint(info.m_block_count);
    writer.write_varint(info.m_image_count);
    return writer.release();
  }

  Info decode_info(std::span<const std::byte> data) {
    auto reader = ByteReader(data, "INFO chunk");
    auto read = [&] {
      auto value = reader.read_varint();
      if(value > std::numeric_limits<std::uint32_t>::max()) {
        reader.fail("count out of range");
      }
      return static_cast<std::size_t>(value);
    };
    auto info = Info();
    info.m_node_count = read();
    info.m_canvas_count = read();
    info.m_root_count = read();
    info.m_id_count = read();
    info.m_block_count = read();
    info.m_image_count = read();
    reader.expect_end();
    if(info.m_canvas_count > info.m_root_count ||
        info.m_root_count > info.m_node_count ||
        info.m_node_count > info.m_id_count ||
        info.m_block_count > info.m_node_count) {
      reader.fail("inconsistent counts");
    }
    return info;
  }

  std::vector<std::byte> encode_ids(const std::vector<NodeId>& ids) {
    auto data = std::vector<std::byte>(ids.size() * NodeId::SIZE);
    for(auto i = std::size_t(0); i != ids.size(); ++i) {
      std::memcpy(data.data() + i * NodeId::SIZE, ids[i].get_bytes().data(),
        NodeId::SIZE);
    }
    return data;
  }

  std::vector<NodeId> decode_ids(
      std::span<const std::byte> data, std::size_t count) {
    if(data.size() != count * NodeId::SIZE) {
      throw FormatError("Corrupt IDS chunk: size mismatch.");
    }
    auto ids = std::vector<NodeId>(count);
    for(auto i = std::size_t(0); i != count; ++i) {
      auto bytes = std::array<std::uint8_t, NodeId::SIZE>();
      std::memcpy(bytes.data(), data.data() + i * NodeId::SIZE, NodeId::SIZE);
      ids[i] = NodeId(bytes);
    }
    return ids;
  }

  void require_empty(const Document& document) {
    if(document.get_nodes().get_size() != 0 ||
        !document.get_canvas().empty() ||
        !(document.get_resources() == Resources()) ||
        !(document.get_prototype() == Prototype())) {
      throw std::invalid_argument("The document to load into is not empty.");
    }
  }

  Contents read_contents(std::istream& in) {
    auto version = read_file_header(in);
    if(version == 0 || version > FORMAT_VERSION) {
      throw FormatError("Unsupported format version " +
        std::to_string(version) + "; this build reads versions 1 to " +
        std::to_string(FORMAT_VERSION) + ".");
    }
    auto info_chunk = read_chunk(in);
    if(info_chunk.m_header.m_type != INFO_CHUNK) {
      throw FormatError("Corrupt file: missing INFO chunk.");
    }
    auto contents = Contents();
    contents.m_info = decode_info(unpack(info_chunk));
    auto& info = contents.m_info;
    auto ids_chunk = std::optional<InputChunk>();
    auto node_chunks = std::vector<InputChunk>();
    auto resources_chunk = std::optional<InputChunk>();
    auto prototype_chunk = std::optional<InputChunk>();
    auto image_chunks = std::vector<InputChunk>();
    auto store_once = [] (std::optional<InputChunk>& slot, InputChunk chunk) {
      if(slot) {
        throw FormatError(
          "Corrupt file: repeated " + to_string(chunk.m_header.m_type) +
          " chunk.");
      }
      slot = std::move(chunk);
    };
    while(true) {
      auto chunk = read_chunk(in);
      auto& type = chunk.m_header.m_type;
      if(is_critical(type) && (chunk.m_header.m_flags & ~COMPRESSED_FLAG)) {
        throw FormatError("Corrupt " + to_string(type) + " chunk: flags.");
      }
      if(type == END_CHUNK) {
        if(!unpack(chunk).empty()) {
          throw FormatError("Corrupt END chunk: unexpected data.");
        }
        break;
      } else if(type == IDS_CHUNK) {
        store_once(ids_chunk, std::move(chunk));
      } else if(type == NODE_CHUNK) {
        if(node_chunks.size() == info.m_block_count) {
          throw FormatError("Corrupt file: too many NODE chunks.");
        }
        node_chunks.push_back(std::move(chunk));
      } else if(type == RESOURCES_CHUNK) {
        store_once(resources_chunk, std::move(chunk));
      } else if(type == PROTOTYPE_CHUNK) {
        store_once(prototype_chunk, std::move(chunk));
      } else if(type == IMAGE_CHUNK) {
        if(image_chunks.size() == info.m_image_count) {
          throw FormatError("Corrupt file: too many IMAG chunks.");
        }
        image_chunks.push_back(std::move(chunk));
      } else if(is_critical(type)) {
        throw FormatError("Unsupported chunk " + to_string(type) + ".");
      }
    }
    if(!ids_chunk || !resources_chunk || !prototype_chunk ||
        node_chunks.size() != info.m_block_count ||
        image_chunks.size() != info.m_image_count) {
      throw FormatError("Corrupt file: missing chunks.");
    }
    contents.m_ids = decode_ids(unpack(*ids_chunk), info.m_id_count);
    for(auto i = std::size_t(0); i != info.m_node_count; ++i) {
      if(contents.m_ids[i].is_nil()) {
        throw FormatError("Corrupt IDS chunk: nil node identifier.");
      }
    }
    contents.m_blocks.resize(node_chunks.size());
    contents.m_images.resize(image_chunks.size());
    auto stored_size = std::uint64_t(0);
    for(auto& chunk : node_chunks) {
      stored_size += chunk.m_header.m_stored_size;
    }
    auto thread_count = stored_size < PARALLEL_SIZE ? 1 : 0;
    auto task_count = node_chunks.size() + image_chunks.size() + 2;
    parallel_for(task_count, thread_count, [&] (auto index) {
      if(index < node_chunks.size()) {
        contents.m_blocks[index] = decode_nodes(unpack(node_chunks[index]),
          contents.m_ids, info.m_node_count, version);
        return;
      }
      index -= node_chunks.size();
      if(index < image_chunks.size()) {
        contents.m_images[index] = unpack(image_chunks[index]);
      } else if(index == image_chunks.size()) {
        contents.m_resources = decode_resources(
          unpack(*resources_chunk), contents.m_ids, version);
      } else {
        contents.m_prototype = decode_prototype(
          unpack(*prototype_chunk), contents.m_ids, version);
      }
    });
    auto position = std::size_t(0);
    for(auto& block : contents.m_blocks) {
      if(block.m_first != position) {
        throw FormatError("Corrupt file: NODE chunks out of order.");
      }
      position += block.m_records.size();
    }
    if(position != info.m_node_count) {
      throw FormatError("Corrupt file: missing nodes.");
    }
    if(contents.m_resources.m_images.size() != contents.m_images.size()) {
      throw FormatError("Corrupt file: image count mismatch.");
    }
    for(auto i = std::size_t(0); i != contents.m_images.size(); ++i) {
      if(contents.m_resources.m_images[i].m_size !=
          contents.m_images[i].size()) {
        throw FormatError("Corrupt file: image size mismatch.");
      }
    }
    return contents;
  }

  void insert_nodes(Transaction& transaction, Contents& contents) {
    struct Frame {
      NodeId m_id;
      std::size_t m_remaining;
      std::size_t m_inserted;
    };
    auto& info = contents.m_info;
    auto stack = std::vector<Frame>();
    auto roots = std::size_t(0);
    for(auto& block : contents.m_blocks) {
      for(auto& record : block.m_records) {
        while(!stack.empty() && stack.back().m_remaining == 0) {
          stack.pop_back();
        }
        auto id = record.m_header.m_id;
        if(stack.empty()) {
          if(roots == info.m_root_count) {
            throw FormatError("Corrupt file: too many root nodes.");
          }
          if(roots < info.m_canvas_count) {
            transaction.insert(NodeId(), roots, std::move(record.m_header),
              std::move(record.m_payload));
          } else {
            transaction.insert_root(
              std::move(record.m_header), std::move(record.m_payload));
          }
          ++roots;
        } else {
          auto& parent = stack.back();
          transaction.insert(parent.m_id, parent.m_inserted,
            std::move(record.m_header), std::move(record.m_payload));
          --parent.m_remaining;
          ++parent.m_inserted;
        }
        if(record.m_child_count != 0) {
          stack.push_back(Frame(id, record.m_child_count, 0));
        }
      }
      block.m_records = std::vector<NodeRecord>();
    }
    while(!stack.empty() && stack.back().m_remaining == 0) {
      stack.pop_back();
    }
    if(!stack.empty() || roots != info.m_root_count) {
      throw FormatError("Corrupt file: node tree mismatch.");
    }
  }

  void apply(Document& document, Contents& contents) {
    auto transaction = Transaction(document);
    try {
      auto& resources = contents.m_resources;
      for(auto i = std::size_t(0); i != resources.m_images.size(); ++i) {
        auto& entry = resources.m_images[i];
        transaction.add_image(ImageResource(entry.m_id,
          std::move(entry.m_media_type),
          Shared<std::vector<std::byte>>(std::move(contents.m_images[i]))));
      }
      for(auto& swatch : resources.m_swatches) {
        transaction.add_swatch(std::move(swatch));
      }
      for(auto& style : resources.m_character_styles) {
        transaction.add_character_style(std::move(style));
      }
      insert_nodes(transaction, contents);
      for(auto& component : resources.m_components) {
        transaction.add_component(std::move(component));
      }
      auto& prototype = contents.m_prototype;
      for(auto& interaction : prototype.m_interactions) {
        transaction.add_interaction(std::move(interaction));
      }
      transaction.set_home_artboard(prototype.m_home_artboard);
      transaction.set_flows(std::move(prototype.m_flows));
    } catch(const std::invalid_argument& error) {
      throw FormatError(std::string("Invalid document: ") + error.what());
    } catch(const std::out_of_range& error) {
      throw FormatError(std::string("Invalid document: ") + error.what());
    }
    transaction.commit();
    document.clear_history();
  }
}

void Rellu::save(
    const Document& document, std::ostream& out, const SaveOptions& options) {
  auto layout = make_layout(document);
  auto& resources = document.get_resources();
  auto info = Info();
  info.m_node_count = layout.m_nodes.size();
  info.m_canvas_count = layout.m_canvas_count;
  info.m_root_count = layout.m_root_count;
  info.m_id_count = layout.m_ids.size();
  info.m_block_count = (layout.m_nodes.size() + BLOCK_SIZE - 1) / BLOCK_SIZE;
  info.m_image_count = resources.m_images.size();
  auto images = std::vector<const ImageResource*>();
  images.reserve(resources.m_images.size());
  for(auto& entry : resources.m_images) {
    images.push_back(&entry.second);
  }
  auto level = std::clamp(options.m_compression_level, 0, 9);
  auto blocks = std::vector<OutputChunk>(info.m_block_count);
  auto image_chunks = std::vector<OutputChunk>(images.size());
  auto resources_chunk = OutputChunk();
  auto prototype_chunk = OutputChunk();
  auto ids_chunk = OutputChunk();
  auto thread_count =
    blocks.size() <= 1 ? 1 : std::max(options.m_thread_count, 0);
  auto task_count = blocks.size() + image_chunks.size() + 3;
  parallel_for(task_count, thread_count, [&] (auto index) {
    if(index < blocks.size()) {
      auto first = index * BLOCK_SIZE;
      auto count = std::min(BLOCK_SIZE, layout.m_nodes.size() - first);
      blocks[index] = pack(NODE_CHUNK, encode_nodes(std::span(
        layout.m_nodes).subspan(first, count), first, layout.m_index), level);
      return;
    }
    index -= blocks.size();
    if(index < image_chunks.size()) {
      image_chunks[index] = pack_view(IMAGE_CHUNK, *images[index]->m_data);
    } else if(index == image_chunks.size()) {
      resources_chunk = pack(RESOURCES_CHUNK,
        encode_resources(resources, layout.m_index), level);
    } else if(index == image_chunks.size() + 1) {
      prototype_chunk = pack(PROTOTYPE_CHUNK,
        encode_prototype(document.get_prototype(), layout.m_index), level);
    } else {
      ids_chunk = pack(IDS_CHUNK, encode_ids(layout.m_ids), 0);
    }
  });
  write_file_header(out, FORMAT_VERSION);
  write_chunk(out, pack(INFO_CHUNK, encode_info(info), 0));
  write_chunk(out, ids_chunk);
  for(auto& block : blocks) {
    write_chunk(out, block);
  }
  write_chunk(out, resources_chunk);
  write_chunk(out, prototype_chunk);
  for(auto& chunk : image_chunks) {
    write_chunk(out, chunk);
  }
  write_chunk(out, pack(END_CHUNK, std::vector<std::byte>(), 0));
  out.flush();
  if(!out) {
    throw FormatError("Cannot write the document.");
  }
}

void Rellu::save(const Document& document, const std::filesystem::path& path,
    const SaveOptions& options) {
  auto temporary = path;
  temporary += ".saving";
  try {
    {
      auto file = std::ofstream(
        temporary, std::ios::binary | std::ios::trunc);
      if(!file) {
        throw FormatError("Cannot create " + to_text(temporary) + ".");
      }
      save(document, file, options);
      file.close();
      if(!file) {
        throw FormatError("Cannot write " + to_text(temporary) + ".");
      }
    }
    auto error = std::error_code();
    std::filesystem::rename(temporary, path, error);
    if(error) {
      throw FormatError(
        "Cannot replace " + to_text(path) + ": " + error.message() + ".");
    }
  } catch(...) {
    auto error = std::error_code();
    std::filesystem::remove(temporary, error);
    throw;
  }
}

void Rellu::load(std::istream& in, Document& document) {
  require_empty(document);
  auto contents = read_contents(in);
  apply(document, contents);
}

void Rellu::load(const std::filesystem::path& path, Document& document) {
  require_empty(document);
  auto file = std::ifstream(path, std::ios::binary);
  if(!file) {
    throw FormatError("Cannot open " + to_text(path) + ".");
  }
  auto contents = read_contents(file);
  apply(document, contents);
}
