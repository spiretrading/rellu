#include <utility>
#include <QJsonObject>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Ui/DocumentSettings.hpp"
#include "Rellu/Ui/GridActions.hpp"
#include "Rellu/Ui/Translation.hpp"

using namespace Rellu;

namespace {
  const auto LAYOUT = "view.layout_grid";
  const auto SQUARE = "view.square_grid";

  std::string get_toggle_label(const GridSettings& settings, GridType type) {
    if(type == GridType::SQUARE) {
      return settings.m_is_square_visible ? "Hide Square Grid" :
        "Show Square Grid";
    }
    return settings.m_is_layout_visible ? "Hide Layout Grid" :
      "Show Layout Grid";
  }
}

GridActions::GridActions(ActionRegistry& actions)
    : m_actions(&actions),
      m_editor(nullptr),
      m_workspace(Workspace::DESIGN),
      m_self(std::make_shared<GridActions*>(this)) {
  m_actions->set_handler(LAYOUT, [this] { toggle(GridType::LAYOUT); });
  m_actions->set_handler(SQUARE, [this] { toggle(GridType::SQUARE); });
  update();
}

GridActions::~GridActions() = default;

void GridActions::set_editor(Editor* editor) {
  m_history_subscription.release();
  m_editor = editor;
  if(m_editor) {
    m_history_subscription = m_editor->get_history().subscribe([this] {
      initialize();
    });
  }
  initialize();
  update();
}

const GridSettings& GridActions::get_settings() const {
  return m_settings;
}

void GridActions::set_settings(GridSettings settings) {
  if(settings == m_settings) {
    return;
  }
  m_settings = settings;
  initialize();
  publish();
}

void GridActions::set_workspace(Workspace workspace) {
  m_workspace = workspace;
  publish();
}

void GridActions::set_settings_handler(SettingsHandler handler) {
  m_settings_handler = std::move(handler);
  publish();
}

void GridActions::toggle(GridType type) {
  if(!m_editor || m_workspace != Workspace::DESIGN) {
    return;
  }
  change(toggle_grid(m_settings, type), get_toggle_label(m_settings, type));
}

void GridActions::choose(GridType type) {
  if(!m_editor || m_workspace != Workspace::DESIGN) {
    return;
  }
  change(choose_grid(m_settings, type), "Grid Type");
}

Subscription GridActions::subscribe(std::function<void ()> observer) {
  return m_signal.subscribe(std::move(observer));
}

void GridActions::update() {
  for(auto [name, type] : {std::pair(LAYOUT, GridType::LAYOUT),
      std::pair(SQUARE, GridType::SQUARE)}) {
    m_actions->get(name).setText(translate("menu",
      QString::fromStdString(get_toggle_label(m_settings, type))));
  }
  auto is_design = m_editor && m_workspace == Workspace::DESIGN;
  m_actions->set_enabled(LAYOUT, is_design);
  m_actions->set_enabled(SQUARE, is_design);
}

void GridActions::change(GridSettings settings, std::string label) {
  if(settings == m_settings) {
    return;
  }
  auto previous = m_settings;
  m_settings = settings;
  initialize();
  publish();
  auto self = std::weak_ptr<GridActions*>(m_self);
  auto apply = [self] (const GridSettings& target) {
    if(auto actions = self.lock()) {
      auto& current = (*actions)->m_settings;
      (*actions)->set_settings(show_grids(current, target.m_is_layout_visible,
        target.m_is_square_visible));
    }
  };
  m_editor->get_history().record(std::move(label), [=] {
    apply(previous);
  }, [=] {
    apply(settings);
  });
}

void GridActions::initialize() {
  if(!m_editor || !is_grid_visible(m_settings) ||
      m_editor->get_document().is_publishing()) {
    return;
  }
  initialize_all_grids(m_editor->get_document());
}

void GridActions::publish() {
  update();
  if(m_settings_handler) {
    auto settings = m_settings;
    if(m_workspace != Workspace::DESIGN) {
      settings.m_is_layout_visible = false;
      settings.m_is_square_visible = false;
    }
    m_settings_handler(settings);
  }
  m_signal();
}

std::optional<GridSettings> Rellu::find_grid_settings(const QByteArray& store,
    std::string_view document, const GridSettings& settings) {
  auto data = find_document_settings(store, document);
  if(!data) {
    return std::nullopt;
  }
  auto result = settings;
  result.m_is_layout_visible = data->value("layoutGridVisible").toBool();
  result.m_is_square_visible = data->value("squareGridVisible").toBool();
  return result;
}

QByteArray Rellu::record_grid_settings(const QByteArray& store,
    std::string_view document, const GridSettings& settings,
    std::int64_t time) {
  auto data = std::optional<QJsonObject>();
  if(is_grid_visible(settings)) {
    data.emplace();
    data->insert("layoutGridVisible", settings.m_is_layout_visible);
    data->insert("squareGridVisible", settings.m_is_square_visible);
  }
  return record_document_settings(store, document, data, time);
}
