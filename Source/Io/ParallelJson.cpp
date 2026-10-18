#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include "Parallel.hpp"
#include "ParallelJson.hpp"

using namespace Rellu;

namespace {
  constexpr auto MINIMUM_SIZE = std::size_t(256) << 10;
  constexpr auto MINIMUM_PART = std::size_t(32) << 10;
  constexpr auto MAXIMUM_DEPTH = 512;
  constexpr auto NONE = std::numeric_limits<std::size_t>::max();

  struct Span {
    std::size_t m_begin;
    std::size_t m_end;
  };

  struct Step {
    std::string_view m_key;
    qsizetype m_index;
  };

  struct Hole {
    std::vector<Step> m_path;
    Span m_span;
    std::vector<Span> m_elements;
  };

  struct Part {
    Span m_span;
    std::size_t m_depth;
    std::vector<std::size_t> m_holes;
    std::size_t m_leaf;
  };

  struct Group {
    bool m_is_part;
    std::size_t m_index;
  };

  struct Leaf {
    std::string m_text;
    Span m_view;
    bool m_is_owned;
    bool m_is_wrapped;
    bool m_is_valid;
    QJsonValue m_value;
  };

  bool is_whitespace(char character) {
    return character == ' ' || character == '\n' || character == '\r' ||
      character == '\t';
  }

  class Scanner {
    public:
      Scanner(std::string_view text, std::size_t part_size);

      bool scan();
      std::vector<Hole>& get_holes();

    private:
      const char* m_begin;
      const char* m_position;
      const char* m_end;
      std::size_t m_part_size;
      int m_depth;
      std::vector<Step> m_path;
      std::vector<Span> m_elements;
      std::vector<std::string_view> m_keys;
      std::vector<Hole> m_holes;

      std::size_t get_offset() const;
      void skip_whitespace();
      bool scan_value();
      bool scan_string(std::string_view& content);
      bool scan_array();
      bool scan_object();
  };

  Scanner::Scanner(std::string_view text, std::size_t part_size)
    : m_begin(text.data()),
      m_position(text.data()),
      m_end(text.data() + text.size()),
      m_part_size(part_size),
      m_depth(0) {}

  bool Scanner::scan() {
    skip_whitespace();
    if(m_position == m_end || (*m_position != '{' && *m_position != '[')) {
      return false;
    }
    if(!scan_value()) {
      return false;
    }
    skip_whitespace();
    return m_position == m_end;
  }

  std::vector<Hole>& Scanner::get_holes() {
    return m_holes;
  }

  std::size_t Scanner::get_offset() const {
    return static_cast<std::size_t>(m_position - m_begin);
  }

  void Scanner::skip_whitespace() {
    while(m_position != m_end && is_whitespace(*m_position)) {
      ++m_position;
    }
  }

  bool Scanner::scan_value() {
    skip_whitespace();
    if(m_position == m_end) {
      return false;
    }
    if(*m_position == '{') {
      return scan_object();
    } else if(*m_position == '[') {
      return scan_array();
    } else if(*m_position == '"') {
      auto content = std::string_view();
      return scan_string(content);
    }
    auto start = m_position;
    while(m_position != m_end && *m_position != ',' && *m_position != ']' &&
        *m_position != '}' && !is_whitespace(*m_position)) {
      ++m_position;
    }
    return m_position != start;
  }

  bool Scanner::scan_string(std::string_view& content) {
    auto start = ++m_position;
    while(true) {
      auto quote = static_cast<const char*>(std::memchr(
        m_position, '"', static_cast<std::size_t>(m_end - m_position)));
      if(!quote) {
        return false;
      }
      auto escape = quote;
      while(escape != start && escape[-1] == '\\') {
        --escape;
      }
      m_position = quote + 1;
      if((quote - escape) % 2 == 0) {
        content =
          std::string_view(start, static_cast<std::size_t>(quote - start));
        return true;
      }
    }
  }

  bool Scanner::scan_array() {
    auto begin = get_offset();
    ++m_position;
    if(++m_depth > MAXIMUM_DEPTH) {
      return false;
    }
    auto first = m_elements.size();
    skip_whitespace();
    if(m_position != m_end && *m_position == ']') {
      ++m_position;
      --m_depth;
      return true;
    }
    for(auto index = qsizetype(0);; ++index) {
      skip_whitespace();
      auto element = get_offset();
      m_path.push_back(Step(std::string_view(), index));
      auto is_scanned = scan_value();
      m_path.pop_back();
      if(!is_scanned) {
        return false;
      }
      m_elements.push_back(Span(element, get_offset()));
      skip_whitespace();
      if(m_position == m_end) {
        return false;
      } else if(*m_position == ']') {
        ++m_position;
        break;
      } else if(*m_position != ',') {
        return false;
      }
      ++m_position;
    }
    --m_depth;
    auto end = get_offset();
    if(end - begin >= m_part_size && m_elements.size() - first >= 2) {
      m_holes.push_back(Hole(m_path, Span(begin, end), std::vector<Span>(
        m_elements.begin() + static_cast<std::ptrdiff_t>(first),
        m_elements.end())));
    }
    m_elements.resize(first);
    return true;
  }

  bool Scanner::scan_object() {
    ++m_position;
    if(++m_depth > MAXIMUM_DEPTH) {
      return false;
    }
    auto first_key = m_keys.size();
    auto first_hole = m_holes.size();
    skip_whitespace();
    if(m_position != m_end && *m_position == '}') {
      ++m_position;
      --m_depth;
      return true;
    }
    while(true) {
      skip_whitespace();
      if(m_position == m_end || *m_position != '"') {
        return false;
      }
      auto key = std::string_view();
      if(!scan_string(key)) {
        return false;
      }
      auto is_escaped = std::memchr(key.data(), '\\', key.size()) != nullptr;
      skip_whitespace();
      if(m_position == m_end || *m_position != ':') {
        return false;
      }
      ++m_position;
      m_path.push_back(Step(key, -1));
      auto is_scanned = scan_value();
      m_path.pop_back();
      if(!is_scanned) {
        return false;
      }
      m_keys.push_back(is_escaped ? std::string_view() : key);
      skip_whitespace();
      if(m_position == m_end) {
        return false;
      } else if(*m_position == '}') {
        ++m_position;
        break;
      } else if(*m_position != ',') {
        return false;
      }
      ++m_position;
    }
    --m_depth;
    if(m_holes.size() != first_hole) {
      auto keys = std::vector<std::string_view>(
        m_keys.begin() + static_cast<std::ptrdiff_t>(first_key), m_keys.end());
      for(auto& key : keys) {
        if(!key.data()) {
          return false;
        }
      }
      std::sort(keys.begin(), keys.end());
      if(std::adjacent_find(keys.begin(), keys.end()) != keys.end()) {
        return false;
      }
    }
    m_keys.resize(first_key);
    return true;
  }

  std::optional<QJsonValue> set_path(QJsonValue value,
      const std::vector<Step>& path, std::size_t index, QJsonArray array) {
    if(index == path.size()) {
      return QJsonValue(std::move(array));
    }
    auto& step = path[index];
    if(step.m_index < 0) {
      if(!value.isObject()) {
        return std::nullopt;
      }
      auto object = value.toObject();
      value = QJsonValue();
      auto key = QString::fromUtf8(
        step.m_key.data(), static_cast<qsizetype>(step.m_key.size()));
      if(!object.contains(key)) {
        return std::nullopt;
      }
      auto child =
        set_path(object.take(key), path, index + 1, std::move(array));
      if(!child) {
        return std::nullopt;
      }
      object.insert(key, *child);
      return QJsonValue(std::move(object));
    }
    if(!value.isArray()) {
      return std::nullopt;
    }
    auto elements = value.toArray();
    value = QJsonValue();
    if(step.m_index >= elements.size()) {
      return std::nullopt;
    }
    auto child = set_path(
      elements.takeAt(step.m_index), path, index + 1, std::move(array));
    if(!child) {
      return std::nullopt;
    }
    elements.insert(step.m_index, *child);
    return QJsonValue(std::move(elements));
  }

  class Assembler {
    public:
      Assembler(std::string_view text, std::size_t part_size,
        std::vector<Hole> holes);

      std::optional<QJsonValue> assemble();

    private:
      std::string_view m_text;
      std::size_t m_part_size;
      std::vector<Hole> m_holes;
      std::vector<std::vector<std::size_t>> m_element_parts;
      std::vector<std::vector<Group>> m_groups;
      std::vector<Part> m_parts;
      std::vector<Leaf> m_leaves;

      std::size_t add_part(Span span, std::size_t depth);
      void add_chunk(Span span, std::vector<Group>& groups);
      void make_leaf(Part& part);
      std::optional<QJsonValue> assemble_part(std::size_t index);
      std::optional<QJsonArray> assemble_hole(std::size_t index);
  };

  Assembler::Assembler(std::string_view text, std::size_t part_size,
      std::vector<Hole> holes)
      : m_text(text),
        m_part_size(part_size),
        m_holes(std::move(holes)) {
    std::sort(m_holes.begin(), m_holes.end(),
      [] (const auto& left, const auto& right) {
        return left.m_span.m_begin < right.m_span.m_begin;
      });
    m_element_parts.resize(m_holes.size());
    for(auto i = std::size_t(0); i != m_holes.size(); ++i) {
      m_element_parts[i].assign(m_holes[i].m_elements.size(), NONE);
    }
    add_part(Span(0, text.size()), 0);
    auto open = std::vector<std::size_t>();
    for(auto i = std::size_t(0); i != m_holes.size(); ++i) {
      auto& hole = m_holes[i];
      while(!open.empty() &&
          m_holes[open.back()].m_span.m_end <= hole.m_span.m_begin) {
        open.pop_back();
      }
      auto part = std::size_t(0);
      if(!open.empty()) {
        auto parent = open.back();
        auto& elements = m_holes[parent].m_elements;
        auto element = std::upper_bound(elements.begin(), elements.end(),
          hole.m_span.m_begin, [] (auto offset, const auto& span) {
            return offset < span.m_begin;
          }) - elements.begin() - 1;
        auto offset = static_cast<std::size_t>(element);
        auto& entry = m_element_parts[parent][offset];
        if(entry == NONE) {
          entry =
            add_part(elements[offset], m_holes[parent].m_path.size() + 1);
        }
        part = entry;
      }
      m_parts[part].m_holes.push_back(i);
      open.push_back(i);
    }
    m_groups.resize(m_holes.size());
    for(auto i = std::size_t(0); i != m_holes.size(); ++i) {
      auto& elements = m_holes[i].m_elements;
      auto& groups = m_groups[i];
      auto chunk = std::optional<Span>();
      for(auto j = std::size_t(0); j != elements.size(); ++j) {
        auto& element = elements[j];
        if(m_element_parts[i][j] != NONE ||
            element.m_end - element.m_begin >= m_part_size) {
          if(chunk) {
            add_chunk(*chunk, groups);
            chunk = std::nullopt;
          }
          if(m_element_parts[i][j] == NONE) {
            m_element_parts[i][j] =
              add_part(element, m_holes[i].m_path.size() + 1);
          }
          groups.push_back(Group(true, m_element_parts[i][j]));
          continue;
        }
        if(chunk) {
          chunk->m_end = element.m_end;
        } else {
          chunk = element;
        }
        if(chunk->m_end - chunk->m_begin >= m_part_size) {
          add_chunk(*chunk, groups);
          chunk = std::nullopt;
        }
      }
      if(chunk) {
        add_chunk(*chunk, groups);
      }
    }
    for(auto& part : m_parts) {
      make_leaf(part);
    }
  }

  std::optional<QJsonValue> Assembler::assemble() {
    parallel_for(m_leaves.size(), [&] (std::size_t index) {
      auto& leaf = m_leaves[index];
      auto bytes = leaf.m_is_owned ? QByteArray::fromRawData(
        leaf.m_text.data(), static_cast<qsizetype>(leaf.m_text.size())) :
        QByteArray::fromRawData(m_text.data() + leaf.m_view.m_begin,
          static_cast<qsizetype>(leaf.m_view.m_end - leaf.m_view.m_begin));
      auto error = QJsonParseError();
      auto document = QJsonDocument::fromJson(bytes, &error);
      leaf.m_text = std::string();
      if(error.error != QJsonParseError::NoError) {
        return;
      }
      if(document.isObject()) {
        leaf.m_value = document.object();
      } else if(leaf.m_is_wrapped) {
        auto elements = document.array();
        if(elements.size() != 1) {
          return;
        }
        leaf.m_value = elements.first();
      } else {
        leaf.m_value = document.array();
      }
      leaf.m_is_valid = true;
    });
    for(auto& leaf : m_leaves) {
      if(!leaf.m_is_valid) {
        return std::nullopt;
      }
    }
    return assemble_part(0);
  }

  std::size_t Assembler::add_part(Span span, std::size_t depth) {
    m_parts.push_back(Part(span, depth, std::vector<std::size_t>(), NONE));
    return m_parts.size() - 1;
  }

  void Assembler::add_chunk(Span span, std::vector<Group>& groups) {
    auto text = std::string();
    text.reserve(span.m_end - span.m_begin + 2);
    text += '[';
    text += m_text.substr(span.m_begin, span.m_end - span.m_begin);
    text += ']';
    m_leaves.push_back(
      Leaf(std::move(text), Span(0, 0), true, false, false, QJsonValue()));
    groups.push_back(Group(false, m_leaves.size() - 1));
  }

  void Assembler::make_leaf(Part& part) {
    auto start = part.m_span.m_begin;
    while(start != part.m_span.m_end && is_whitespace(m_text[start])) {
      ++start;
    }
    auto is_wrapped = start == part.m_span.m_end ||
      (m_text[start] != '{' && m_text[start] != '[');
    if(part.m_holes.empty() && !is_wrapped) {
      m_leaves.push_back(
        Leaf(std::string(), part.m_span, false, false, false, QJsonValue()));
    } else {
      auto text = std::string();
      if(is_wrapped) {
        text += '[';
      }
      auto position = part.m_span.m_begin;
      for(auto hole : part.m_holes) {
        auto& span = m_holes[hole].m_span;
        text += m_text.substr(position, span.m_begin - position);
        text += "[]";
        position = span.m_end;
      }
      text += m_text.substr(position, part.m_span.m_end - position);
      if(is_wrapped) {
        text += ']';
      }
      m_leaves.push_back(Leaf(
        std::move(text), Span(0, 0), true, is_wrapped, false, QJsonValue()));
    }
    part.m_leaf = m_leaves.size() - 1;
  }

  std::optional<QJsonValue> Assembler::assemble_part(std::size_t index) {
    auto& part = m_parts[index];
    auto value = std::move(m_leaves[part.m_leaf].m_value);
    for(auto hole : part.m_holes) {
      auto array = assemble_hole(hole);
      if(!array) {
        return std::nullopt;
      }
      auto result = set_path(std::move(value), m_holes[hole].m_path,
        part.m_depth, std::move(*array));
      if(!result) {
        return std::nullopt;
      }
      value = std::move(*result);
    }
    return value;
  }

  std::optional<QJsonArray> Assembler::assemble_hole(std::size_t index) {
    auto array = QJsonArray();
    for(auto& group : m_groups[index]) {
      if(group.m_is_part) {
        auto value = assemble_part(group.m_index);
        if(!value) {
          return std::nullopt;
        }
        array.append(*value);
      } else {
        auto& leaf = m_leaves[group.m_index];
        auto elements = leaf.m_value.toArray();
        leaf.m_value = QJsonValue();
        for(auto element : elements) {
          array.append(element);
        }
      }
    }
    return array;
  }
}

QJsonDocument Rellu::parse_json(
    const QByteArray& bytes, QJsonParseError& error) {
  auto size = static_cast<std::size_t>(bytes.size());
  if(size >= MINIMUM_SIZE) {
    auto text = std::string_view(bytes.constData(), size);
    auto part_size = std::max(
      MINIMUM_PART, size / (8 * (WorkerPool::get().get_size() + 1)));
    auto scanner = Scanner(text, part_size);
    if(scanner.scan() && !scanner.get_holes().empty()) {
      auto assembler =
        Assembler(text, part_size, std::move(scanner.get_holes()));
      if(auto value = assembler.assemble()) {
        error.offset = 0;
        error.error = QJsonParseError::NoError;
        if(value->isObject()) {
          return QJsonDocument(value->toObject());
        }
        return QJsonDocument(value->toArray());
      }
    }
  }
  return QJsonDocument::fromJson(bytes, &error);
}
