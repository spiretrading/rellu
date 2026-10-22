#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <unordered_set>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include "Rellu/Io/BitmapExport.hpp"
#include "Rellu/Io/PdfExport.hpp"
#include "Rellu/Io/SvgExport.hpp"
#include "Rellu/Rendering/OffscreenRenderer.hpp"
#include "Rellu/Scene/AssetRenderer.hpp"
#include "Rellu/Ui/AssetExport.hpp"
#include "Rellu/Ui/NodeDescription.hpp"

using namespace Rellu;

namespace {
  const auto DEFAULT_NAME = QString("asset");
  const auto UNTITLED_NAME = QString("Untitled");
  constexpr auto NAME_ADDITION_LENGTH = 7;
  constexpr auto PATH_ADDITION_LENGTH = 21;
  constexpr auto MAXIMUM_DEPTH = 256;

  QString get_base_name(const QString& name) {
    auto digit = name.indexOf(QRegularExpression("[0-9]"));
    if(digit < 0) {
      return name;
    }
    auto base = name.left(digit);
    if(base.endsWith('-')) {
      base.chop(1);
    }
    if(base.isEmpty()) {
      return DEFAULT_NAME;
    }
    return base;
  }

  int get_maximum_name_length(const QString& folder) {
    return std::min(std::max(0,
      MAXIMUM_EXPORT_NAME_LENGTH - NAME_ADDITION_LENGTH), std::max(0,
        MAXIMUM_EXPORT_PATH_LENGTH - static_cast<int>(
          QDir::toNativeSeparators(folder).size()) - PATH_ADDITION_LENGTH));
  }

  ExportScale make_export_scale(double factor, const QString& name,
      const QString& suffix = QString(),
      const QString& folder = QString()) {
    return ExportScale(factor, name, suffix, folder);
  }

  bool is_container(const Node& node) {
    return node.get_kind() == NodeKind::ARTBOARD ||
      node.get_kind() == NodeKind::GROUP;
  }

  void add_inactive_states(const Document& document, const Node& node,
      const ComponentInstance& instance, std::vector<ExportAsset>& assets) {
    auto& components = document.get_resources().m_components;
    auto component = components.find(instance.m_component);
    if(component == components.end()) {
      return;
    }
    auto name = get_node_name(document, node);
    for(auto& state : component->second.m_states) {
      if(state.m_id == instance.m_state) {
        continue;
      }
      auto tree = state.m_tree;
      for(auto& copy : instance.m_states) {
        if(copy.m_id == state.m_id) {
          tree = copy.m_tree;
        }
      }
      if(document.find(tree)) {
        assets.push_back(ExportAsset(tree,
          name + " (" + QString::fromStdString(state.m_name) + ')'));
      }
    }
  }

  void visit_batch(const Document& document, const NodeId& id, int depth,
      std::unordered_set<ComponentId>& components,
      std::vector<ExportAsset>& assets) {
    auto node = document.find(id);
    if(!node || depth > MAXIMUM_DEPTH) {
      return;
    }
    auto group = node->get_group();
    auto instance = group && group->m_component_instance ?
      &*group->m_component_instance : nullptr;
    if(instance && node->m_header.m_is_marked_for_export &&
        !components.insert(instance->m_component).second) {
      return;
    }
    if(node->m_header.m_is_marked_for_export) {
      assets.push_back(ExportAsset(id, QString()));
    }
    for(auto& child : node->m_children) {
      visit_batch(document, child, depth + 1, components, assets);
    }
    if(instance && node->m_header.m_is_marked_for_export) {
      add_inactive_states(document, *node, *instance, assets);
    }
  }

  std::filesystem::path to_filesystem(const QString& path) {
    return std::filesystem::path(path.toStdU16String());
  }

  bool is_permission_failure(ExportFailure failure) {
    return failure == ExportFailure::READ_ONLY ||
      failure == ExportFailure::ACCESS_DENIED;
  }

  ExportFailure classify_failure(const QString& path) {
    auto file = QFileInfo(path);
    auto directory = QFileInfo(file.absolutePath());
    if((file.exists() && !file.isWritable()) ||
        (directory.exists() && !directory.isWritable())) {
      return ExportFailure::ACCESS_DENIED;
    }
    return ExportFailure::GENERIC;
  }

  void make_directory(const QString& path, std::vector<QString>& created) {
    auto directory = QFileInfo(path).absolutePath();
    if(QDir(directory).exists()) {
      return;
    }
    if(QDir().mkpath(directory)) {
      created.push_back(directory);
    }
  }

  ExportFailure write_task(const Document& document, const ExportTask& task,
      const std::vector<NodeId>& nodes, const ExportOptions& options,
      const TextMeasurer& measurer, std::unique_ptr<AssetRenderer>& renderer,
      std::vector<QString>& created) {
    make_directory(task.m_path, created);
    auto info = QFileInfo(task.m_path);
    if(info.exists() && !info.isWritable()) {
      return ExportFailure::READ_ONLY;
    }
    try {
      auto path = to_filesystem(task.m_path);
      if(options.m_format == ExportFormat::SVG) {
        write_svg_export(document, task.m_node, path, options);
      } else if(options.m_format == ExportFormat::PDF) {
        write_pdf_export(document, nodes, path, options);
      } else {
        if(task.m_is_too_big) {
          return ExportFailure::GENERIC;
        }
        auto bounds = get_world_bounds(
          document, task.m_node, BoundsType::DRAW, measurer);
        if(!bounds) {
          return ExportFailure::GENERIC;
        }
        if(!renderer) {
          renderer = std::make_unique<AssetRenderer>(document);
        }
        auto image = renderer->render(task.m_node, *bounds, task.m_scale);
        if(options.m_format == ExportFormat::JPG) {
          write_jpg_export(image, path, options.m_jpg_quality);
        } else {
          write_png_export(image, path);
        }
      }
    } catch(const std::exception&) {
      return classify_failure(task.m_path);
    }
    return ExportFailure::NONE;
  }

  struct ScaleParser {
    QString m_text;
    qsizetype m_position = 0;

    void skip() {
      while(m_position < m_text.size() && m_text[m_position].isSpace()) {
        ++m_position;
      }
    }

    bool accept(QChar c) {
      skip();
      if(m_position < m_text.size() && m_text[m_position] == c) {
        ++m_position;
        return true;
      }
      return false;
    }

    std::optional<double> parse_primary() {
      if(accept('(')) {
        auto value = parse_sum();
        if(!value || !accept(')')) {
          return std::nullopt;
        }
        return value;
      }
      if(accept('-')) {
        auto value = parse_primary();
        if(!value) {
          return std::nullopt;
        }
        return -*value;
      }
      skip();
      auto start = m_position;
      while(m_position < m_text.size() &&
          (m_text[m_position].isDigit() || m_text[m_position] == '.')) {
        ++m_position;
      }
      auto is_valid = false;
      auto value = m_text.mid(start, m_position - start).toDouble(&is_valid);
      if(!is_valid) {
        return std::nullopt;
      }
      return value;
    }

    std::optional<double> parse_product() {
      auto value = parse_primary();
      while(value) {
        if(accept('*')) {
          auto right = parse_primary();
          value = right ? std::optional(*value * *right) : std::nullopt;
        } else if(accept('/')) {
          auto right = parse_primary();
          value = right ? std::optional(*value / *right) : std::nullopt;
        } else {
          break;
        }
      }
      return value;
    }

    std::optional<double> parse_sum() {
      auto value = parse_product();
      while(value) {
        if(accept('+')) {
          auto right = parse_product();
          value = right ? std::optional(*value + *right) : std::nullopt;
        } else if(accept('-')) {
          auto right = parse_product();
          value = right ? std::optional(*value - *right) : std::nullopt;
        } else {
          break;
        }
      }
      return value;
    }
  };

  bool has_marked(const Document& document, const NodeId& id, int depth) {
    auto node = document.find(id);
    if(!node || depth > MAXIMUM_DEPTH) {
      return false;
    }
    if(node->m_header.m_is_marked_for_export) {
      return true;
    }
    return std::any_of(node->m_children.begin(), node->m_children.end(),
      [&] (const auto& child) {
        return has_marked(document, child, depth + 1);
      });
  }
}

std::optional<double> Rellu::parse_export_scale(const QString& text) {
  auto source = text.trimmed();
  if(source.endsWith('x', Qt::CaseInsensitive)) {
    source.chop(1);
  }
  auto parser = ScaleParser(source);
  auto value = parser.parse_sum();
  parser.skip();
  if(!value || parser.m_position != parser.m_text.size() ||
      !std::isfinite(*value) || *value <= 0) {
    return std::nullopt;
  }
  return value;
}

QString Rellu::sanitize_file_name(const QString& name) {
  auto result = name;
  result.remove(QRegularExpression("[?<>\\\\:*|\":]"));
  result.remove(QRegularExpression("[\\x{0}-\\x{1f}\\x{80}-\\x{9f}]"));
  if(QRegularExpression("^\\.+$").match(result).hasMatch()) {
    result.clear();
  }
  if(QRegularExpression("^(con|prn|aux|nul|com[0-9]|lpt[0-9])(\\..*)?$",
      QRegularExpression::CaseInsensitiveOption).match(result).hasMatch()) {
    result.clear();
  }
  result.replace(QRegularExpression("/+"), "-");
  return result.trimmed();
}

QString ExportNamer::make(QString name, int limit) {
  if(name.isEmpty()) {
    name = DEFAULT_NAME;
  }
  if(limit >= 0 && name.size() > limit) {
    name.truncate(limit);
  }
  auto key = name.toLower();
  auto base = get_base_name(name);
  auto base_key = base.toLower();
  auto existing = m_names.find(key);
  if(existing == m_names.end()) {
    m_bases.try_emplace(base_key, 0);
    m_names.emplace(key, base_key);
    return name;
  }
  auto holder = existing->second;
  auto count = m_bases[holder];
  auto candidate = QString();
  do {
    ++count;
    candidate = base_key + '-' + QString::number(count);
  } while(m_names.contains(candidate));
  if(limit >= 0 && candidate.size() > limit) {
    if(name.size() < 2) {
      return name;
    }
    return make(name.left(name.size() - 1), limit);
  }
  m_bases[holder] = count;
  m_names.emplace(candidate, holder);
  return base + '-' + QString::number(count);
}

std::vector<ExportScale> Rellu::get_export_scales(
    const ExportOptions& options) {
  if(is_vector(options.m_format)) {
    return {make_export_scale(1, "")};
  }
  if(options.m_preset == ExportPreset::WEB) {
    return {make_export_scale(1, "1x"), make_export_scale(2, "2x", "@2x")};
  } else if(options.m_preset == ExportPreset::IOS) {
    return {make_export_scale(1, "1x"), make_export_scale(2, "2x", "@2x"),
      make_export_scale(3, "3x", "@3x")};
  } else if(options.m_preset == ExportPreset::ANDROID) {
    return {make_export_scale(0.75, "ldpi", "", "drawable-ldpi"),
      make_export_scale(1, "mdpi", "", "drawable-mdpi"),
      make_export_scale(1.5, "hdpi", "", "drawable-hdpi"),
      make_export_scale(2, "xhdpi", "", "drawable-xhdpi"),
      make_export_scale(3, "xxhdpi", "", "drawable-xxhdpi"),
      make_export_scale(4, "xxxhdpi", "", "drawable-xxxhdpi")};
  }
  return {make_export_scale(
    options.m_scale, QString::number(options.m_scale) + 'x')};
}
std::vector<ExportAsset> Rellu::to_export_assets(
    const std::vector<NodeId>& nodes) {
  auto assets = std::vector<ExportAsset>();
  for(auto& id : nodes) {
    assets.push_back(ExportAsset(id, QString()));
  }
  return assets;
}

std::vector<ExportAsset> Rellu::get_batch_export_assets(
    const Document& document) {
  auto assets = std::vector<ExportAsset>();
  auto components = std::unordered_set<ComponentId>();
  for(auto& id : document.get_canvas()) {
    visit_batch(document, id, 0, components, assets);
  }
  return assets;
}

bool Rellu::has_batch_export_nodes(const Document& document) {
  return std::any_of(document.get_canvas().begin(),
    document.get_canvas().end(), [&] (const auto& id) {
      return has_marked(document, id, 0);
    });
}

std::vector<NodeId> Rellu::get_export_artboards(const Document& document) {
  auto artboards = std::vector<NodeId>();
  for(auto& id : document.get_canvas()) {
    if(auto node = document.find(id); node && node->get_artboard()) {
      artboards.push_back(id);
    }
  }
  return artboards;
}

std::vector<ExportTask> Rellu::make_export_tasks(const Document& document,
    const std::vector<ExportAsset>& assets, const ExportOptions& options,
    const QString& folder, const QString& document_name,
    const TextMeasurer& measurer) {
  auto tasks = std::vector<ExportTask>();
  auto root = folder;
  if(!root.endsWith('/')) {
    root += '/';
  }
  auto is_bitmap = !is_vector(options.m_format);
  auto scales = get_export_scales(options);

  auto extension = QString::fromUtf8(get_extension(options.m_format));
  auto is_single_pdf = options.m_format == ExportFormat::PDF &&
    options.m_is_pdf_single && assets.size() > 1;
  for(auto& scale : scales) {
    auto namer = ExportNamer();
    auto unnamed = 1;
    for(auto& asset : assets) {
      auto& id = asset.m_node;
      auto node = document.find(id);
      if(!node) {
        continue;
      }
      if(is_bitmap) {
        auto bounds = get_local_bounds(document, id, BoundsType::DRAW,
          measurer);
        if(!bounds || bounds->m_width * bounds->m_height < 1) {
          continue;
        }
      }
      if(is_container(*node) && node->m_children.empty()) {
        continue;
      }
      auto name = sanitize_file_name(asset.m_name.isEmpty() ?
        get_node_name(document, *node) : asset.m_name);
      auto limit = get_maximum_name_length(root);
      if(node->get_artboard() || !name.isEmpty()) {
        name = namer.make(name, limit);
      } else {
        name = namer.make(DEFAULT_NAME + QString::number(unnamed));
        ++unnamed;
      }
      if(is_single_pdf) {
        name = document_name.isEmpty() ? UNTITLED_NAME : document_name;
      }
      auto directory = root;
      if(!scale.m_folder.isEmpty()) {
        directory += scale.m_folder + '/';
      }
      auto factor = scale.m_factor;
      auto is_too_big = false;
      if(is_bitmap) {
        auto bounds = get_local_bounds(document, id, BoundsType::GEOMETRY,
          measurer);
        is_too_big = bounds && factor * factor * bounds->m_width *
          bounds->m_height > static_cast<double>(MAXIMUM_EXPORT_AREA);
      }
      tasks.push_back(ExportTask(id, factor,
        directory + name + scale.m_suffix + '.' + extension, is_too_big));
    }
  }
  return tasks;
}

std::vector<QString> Rellu::find_existing_files(
    const std::vector<ExportTask>& tasks) {
  auto names = std::vector<QString>();
  for(auto& task : tasks) {
    auto info = QFileInfo(task.m_path);
    if(info.exists() && std::find(names.begin(), names.end(),
        info.fileName()) == names.end()) {
      names.push_back(info.fileName());
    }
  }
  return names;
}

ExportResult Rellu::run_export(const Document& document,
    const std::vector<ExportTask>& tasks, const ExportOptions& options,
    const TextMeasurer& measurer, const ExportProgress& progress) {
  auto result = ExportResult();
  auto created = std::vector<QString>();
  auto renderer = std::unique_ptr<AssetRenderer>();
  auto failures = std::vector<std::pair<std::size_t, ExportFailure>>();
  auto is_single_pdf = options.m_format == ExportFormat::PDF &&
    options.m_is_pdf_single && tasks.size() > 1;
  auto count = is_single_pdf ? std::size_t(1) : tasks.size();
  auto total = static_cast<int>(count);
  auto report = [&] (int done) {
    return !progress || progress(done, total);
  };
  if(!report(0)) {
    result.m_is_cancelled = true;
  }
  for(auto i = std::size_t(0); i != count && !result.m_is_cancelled; ++i) {
    auto nodes = std::vector<NodeId>();
    if(is_single_pdf) {
      for(auto& task : tasks) {
        nodes.push_back(task.m_node);
      }
    } else {
      nodes.push_back(tasks[i].m_node);
    }
    auto failure = write_task(
      document, tasks[i], nodes, options, measurer, renderer, created);
    if(failure == ExportFailure::NONE) {
      result.m_written.push_back(tasks[i].m_path);
    } else {
      failures.emplace_back(i, failure);
    }
    if(!report(static_cast<int>(i + 1)) && i + 1 != count) {
      result.m_is_cancelled = true;
    }
  }
  if(result.m_is_cancelled) {
    for(auto& path : result.m_written) {
      QFile::remove(path);
    }
    result.m_written.clear();
    for(auto i = created.rbegin(); i != created.rend(); ++i) {
      QDir().rmdir(*i);
    }
    return result;
  }
  auto has_permission_failure = std::any_of(failures.begin(), failures.end(),
    [] (const auto& failure) {
      return is_permission_failure(failure.second);
    });
  for(auto& failure : failures) {
    if(has_permission_failure && !is_permission_failure(failure.second)) {
      continue;
    }
    if(failure.second == ExportFailure::ACCESS_DENIED) {
      result.m_failure = ExportFailure::ACCESS_DENIED;
    } else if(result.m_failure == ExportFailure::NONE) {
      result.m_failure = failure.second;
    }
    auto& node = tasks[failure.first].m_node;
    if(has_permission_failure || std::find(result.m_failed_nodes.begin(),
        result.m_failed_nodes.end(), node) == result.m_failed_nodes.end()) {
      result.m_failed_nodes.push_back(node);
      result.m_failed_paths.push_back(tasks[failure.first].m_path);
    }
  }
  if(has_permission_failure &&
      result.m_failure != ExportFailure::ACCESS_DENIED) {
    result.m_failure = ExportFailure::READ_ONLY;
  }
  return result;
}
