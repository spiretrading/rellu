#include <algorithm>
#include <utility>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include "Rellu/Commands/GuideCommands.hpp"
#include "Rellu/Ui/GuideActions.hpp"

using namespace Rellu;

namespace {
  const auto GUIDES_TYPE =
    QStringLiteral("application/x-rellu-artboard-guides");
  const auto COPY = "view.guides.copy";
  const auto PASTE = "view.guides.paste";
  const auto CLEAR = "view.guides.clear";
  const auto HIDE = "view.guides.hide";
  const auto LOCK = "view.guides.lock";

  QJsonObject make_list(const std::vector<Guide>& guides,
      Orientation orientation) {
    auto list = QJsonArray();
    for(auto& guide : guides) {
      if(guide.m_orientation == orientation) {
        auto entry = QJsonObject();
        entry.insert("position", guide.m_position);
        entry.insert("guid", QString::fromStdString(guide.m_id));
        list.append(entry);
      }
    }
    auto object = QJsonObject();
    object.insert("guides", list);
    return object;
  }

  void read_list(const QJsonObject& model, const QString& key,
      Orientation orientation, std::vector<Guide>& guides) {
    for(auto entry : model.value(key).toObject().value("guides").toArray()) {
      auto object = entry.toObject();
      if(auto position = object.value("position"); position.isDouble()) {
        guides.push_back(Guide(orientation, position.toDouble(),
          object.value("guid").toString().toStdString()));
      }
    }
  }

  bool has_guides(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    return node && node->get_artboard() &&
      !node->get_artboard()->m_guides.empty();
  }

  bool has_artboards(const Document& document,
      const std::vector<NodeId>& selection, bool is_multiple,
      bool is_guided) {
    if(!is_multiple && selection.size() != 1) {
      return false;
    }
    return std::any_of(selection.begin(), selection.end(),
      [&] (const auto& id) {
        auto node = document.find(id);
        return node && node->get_artboard() &&
          (!is_guided || has_guides(document, id));
      });
  }
}

GuideActions::GuideActions(ActionRegistry& actions)
    : m_actions(&actions),
      m_editor(nullptr),
      m_workspace(Workspace::DESIGN) {
  m_actions->set_handler(COPY, [this] { copy(); });
  m_actions->set_handler(PASTE, [this] {
    if(auto clipboard = QGuiApplication::clipboard()) {
      if(auto data = clipboard->mimeData()) {
        paste(*data, true);
      }
    }
  });
  m_actions->set_handler(CLEAR, [this] {
    if(m_editor) {
      m_editor->execute(std::make_unique<ClearGuidesCommand>(get_artboards()));
    }
  });
  m_actions->set_handler(HIDE, [this] {
    m_settings.m_is_visible = !m_settings.m_is_visible;
    publish();
  });
  m_actions->set_handler(LOCK, [this] {
    m_settings.m_is_locked = !m_settings.m_is_locked;
    publish();
  });
  m_actions->get(PASTE).setShortcutContext(Qt::WidgetShortcut);
  update();
}

GuideActions::~GuideActions() = default;

void GuideActions::set_editor(Editor* editor) {
  m_selection_subscription.release();
  m_history_subscription.release();
  m_editor = editor;
  if(m_editor) {
    m_selection_subscription = m_editor->get_selection().subscribe([this] {
      update();
    });
    m_history_subscription = m_editor->get_history().subscribe([this] {
      update();
    });
  }
  update();
}

const GuideSettings& GuideActions::get_settings() const {
  return m_settings;
}

void GuideActions::set_settings(GuideSettings settings) {
  m_settings = settings;
  publish();
}

void GuideActions::set_workspace(Workspace workspace) {
  m_workspace = workspace;
  publish();
}

void GuideActions::set_settings_handler(SettingsHandler handler) {
  m_settings_handler = std::move(handler);
  publish();
}

bool GuideActions::paste(const QMimeData& data, bool is_applied) {
  auto guides = read_guides_data(data);
  if(!guides) {
    return false;
  }
  if(!is_applied || !m_editor || m_workspace != Workspace::DESIGN) {
    return true;
  }
  auto artboards = get_artboards();
  if(artboards.empty()) {
    return true;
  }
  m_editor->execute(std::make_unique<PasteGuidesCommand>(
    std::move(artboards), std::move(*guides), [] {
      return QUuid::createUuid().toString(QUuid::WithoutBraces).
        toStdString();
    }));
  if(!m_settings.m_is_visible) {
    m_settings.m_is_visible = true;
    publish();
  }
  return true;
}

void GuideActions::update() {
  m_actions->get(HIDE).setText(m_settings.m_is_visible ?
    "Hide All Guides" : "Show All Guides");
  m_actions->get(LOCK).setText(m_settings.m_is_locked ?
    "Unlock All Guides" : "Lock All Guides");
  auto is_design = m_editor && m_workspace == Workspace::DESIGN;
  m_actions->set_enabled(HIDE, is_design);
  m_actions->set_enabled(LOCK, is_design);
  if(!is_design) {
    m_actions->set_enabled(COPY, false);
    m_actions->set_enabled(PASTE, false);
    m_actions->set_enabled(CLEAR, false);
    return;
  }
  auto& document = m_editor->get_document();
  auto& selection = m_editor->get_selection().get_nodes();
  m_actions->set_enabled(COPY,
    has_artboards(document, selection, false, true) ||
      is_within_artboards(document, selection, false, true));
  m_actions->set_enabled(PASTE,
    has_artboards(document, selection, true, false) ||
      is_within_artboards(document, selection, true, false));
  m_actions->set_enabled(CLEAR, m_settings.m_is_visible &&
    (has_artboards(document, selection, true, true) ||
      is_within_artboards(document, selection, true, true)));
}

std::vector<NodeId> GuideActions::get_artboards() const {
  if(!m_editor) {
    return {};
  }
  return get_guide_artboards(m_editor->get_document(),
    m_editor->get_selection().get_nodes());
}

void GuideActions::copy() {
  if(!m_editor) {
    return;
  }
  auto artboards = get_artboards();
  if(artboards.size() != 1) {
    return;
  }
  auto& artboard =
    *m_editor->get_document().get(artboards.front()).get_artboard();
  if(artboard.m_guides.empty()) {
    return;
  }
  if(auto clipboard = QGuiApplication::clipboard()) {
    clipboard->setMimeData(make_guides_data(artboard.m_guides).release());
  }
}

void GuideActions::publish() {
  update();
  if(m_settings_handler) {
    auto settings = m_settings;
    settings.m_is_visible =
      settings.m_is_visible && m_workspace == Workspace::DESIGN;
    m_settings_handler(settings);
  }
}

std::unique_ptr<QMimeData> Rellu::make_guides_data(
    const std::vector<Guide>& guides) {
  auto model = QJsonObject();
  model.insert("verticalGuides", make_list(guides, Orientation::VERTICAL));
  model.insert("horizontalGuides", make_list(guides, Orientation::HORIZONTAL));
  auto data = std::make_unique<QMimeData>();
  data->setData(
    GUIDES_TYPE, QJsonDocument(model).toJson(QJsonDocument::Compact));
  return data;
}

std::optional<std::vector<Guide>> Rellu::read_guides_data(
    const QMimeData& data) {
  if(!data.hasFormat(GUIDES_TYPE)) {
    return std::nullopt;
  }
  auto model = QJsonDocument::fromJson(data.data(GUIDES_TYPE)).object();
  auto guides = std::vector<Guide>();
  read_list(model, "horizontalGuides", Orientation::HORIZONTAL, guides);
  read_list(model, "verticalGuides", Orientation::VERTICAL, guides);
  return guides;
}
