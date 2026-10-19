#include <exception>
#include <memory>
#include <utility>
#include <QClipboard>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMimeData>
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Ui/EditActions.hpp"

using namespace Rellu;

namespace {
  QString escape(const std::string& label) {
    return QString::fromStdString(label).replace("&", "&&");
  }

  const QMimeData* get_system_data() {
    if(auto clipboard = QGuiApplication::clipboard()) {
      return clipboard->mimeData();
    }
    return nullptr;
  }

  QString make_history_text(const QString& text, const std::string& label) {
    if(label.empty()) {
      return text;
    }
    return QString("%1 %2").arg(text, escape(label));
  }
}

EditActions::EditActions(
    ActionRegistry& actions, const CommandRegistry& commands)
    : m_actions(&actions),
      m_commands(&commands),
      m_editor(nullptr),
      m_workspace(Workspace::DESIGN),
      m_measurer(estimate_text_bounds) {
  set_action("edit.undo", [this] {
    run([&] { m_editor->undo(); });
  });
  set_action("edit.redo", [this] {
    run([&] { m_editor->redo(); });
  });
  set_action("edit.cut", [this] { cut(); });
  set_action("edit.copy", [this] { copy(); });
  set_action("edit.paste", [this] { paste(); });
  set_action("edit.paste_appearance", [this] { paste_appearance(); });
  set_action("edit.select_all", [this] {
    run([&] { m_editor->select_all(); });
  });
  set_action("edit.deselect_all", [this] {
    run([&] { m_editor->get_selection().clear(); });
  });
  for(auto& name : m_actions->get_names()) {
    if(!m_commands->contains(name)) {
      continue;
    }
    if(name == "edit.duplicate") {
      set_action(name, [this] { duplicate(); });
    } else {
      set_action(name, [this, name] {
        run([&] { m_editor->execute(*m_commands, name); });
      });
    }
    m_command_actions.push_back(name);
  }
  auto& remove = m_actions->get("edit.delete");
  auto shortcuts = remove.shortcuts();
  if(!shortcuts.contains(QKeySequence(Qt::Key_Backspace))) {
    shortcuts.append(QKeySequence(Qt::Key_Backspace));
    remove.setShortcuts(shortcuts);
  }
  if(auto clipboard = QGuiApplication::clipboard()) {
    m_clipboard_connection = QObject::connect(clipboard,
      &QClipboard::dataChanged, [this] { update(); });
  }
  update();
}

EditActions::~EditActions() {
  QObject::disconnect(m_clipboard_connection);
}

Editor* EditActions::get_editor() const {
  return m_editor;
}

void EditActions::set_editor(Editor* editor) {
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

Clipboard& EditActions::get_clipboard() {
  return m_clipboard;
}

void EditActions::set_view_provider(ViewProvider provider) {
  m_view_provider = std::move(provider);
}

void EditActions::set_reveal_handler(RevealHandler handler) {
  m_reveal_handler = std::move(handler);
}

void EditActions::set_workspace(Workspace workspace) {
  m_workspace = workspace;
}

void EditActions::set_text_measurer(TextMeasurer measurer) {
  m_measurer = std::move(measurer);
}

void EditActions::set_error_handler(ErrorHandler handler) {
  m_error_handler = std::move(handler);
}

void EditActions::update() {
  auto document = m_editor ? &m_editor->get_document() : nullptr;
  auto& undo = m_actions->get("edit.undo");
  auto& redo = m_actions->get("edit.redo");
  if(!m_editor) {
    undo.setText("Undo");
    redo.setText("Redo");
    for(auto& name : m_names) {
      m_actions->set_enabled(name, false);
    }
    return;
  }
  auto& history = m_editor->get_history();
  auto& nodes = m_editor->get_selection().get_nodes();
  undo.setText(make_history_text("Undo", history.get_undo_label()));
  redo.setText(make_history_text("Redo", history.get_redo_label()));
  m_actions->set_enabled("edit.undo", history.can_undo());
  m_actions->set_enabled("edit.redo", history.can_redo());
  auto is_copyable = Clipboard::is_copyable(*document, nodes);
  m_actions->set_enabled("edit.copy", is_copyable);
  m_actions->set_enabled("edit.cut", is_copyable &&
    m_commands->make("edit.delete", *document, nodes));
  m_actions->set_enabled("edit.paste",
    !m_clipboard.is_empty() || (get_system_data() &&
      Clipboard::is_loadable(*get_system_data())));
  m_actions->set_enabled("edit.paste_appearance", !nodes.empty());
  m_actions->set_enabled("edit.select_all", true);
  m_actions->set_enabled(
    "edit.deselect_all", !m_editor->get_selection().is_empty());
  for(auto& name : m_command_actions) {
    auto command = m_commands->make(name, *document, nodes);
    m_actions->set_enabled(name, static_cast<bool>(command));
    if(command && (name == "object.toggle_lock" ||
        name == "object.toggle_visibility" || name == "object.ungroup")) {
      m_actions->get(name).setText(escape(command->get_label()));
    } else if(!command && name == "object.ungroup") {
      m_actions->get(name).setText("Ungroup");
    }
  }
}

void EditActions::set_action(
    const std::string& name, std::function<void ()> handler) {
  m_actions->set_handler(name, std::move(handler));
  m_names.push_back(name);
}

void EditActions::run(auto operation) {
  if(!m_editor) {
    return;
  }
  try {
    operation();
  } catch(const std::exception& error) {
    if(m_error_handler) {
      m_error_handler(QString::fromUtf8(error.what()));
    }
  }
  update();
}

bool EditActions::copy_selection() {
  if(!m_clipboard.copy(m_editor->get_document(),
      m_editor->get_selection().get_nodes(), m_measurer)) {
    return false;
  }
  if(auto clipboard = QGuiApplication::clipboard()) {
    if(auto data = m_clipboard.make_mime_data()) {
      clipboard->setMimeData(data.release());
    }
  }
  return true;
}

void EditActions::cut() {
  run([&] {
    auto& document = m_editor->get_document();
    auto& nodes = m_editor->get_selection().get_nodes();
    auto command = m_commands->make("edit.delete", document, nodes);
    if(!command || !copy_selection()) {
      return;
    }
    auto commands = std::vector<std::unique_ptr<Command>>();
    commands.push_back(std::move(command));
    m_editor->execute(
      std::make_unique<CompositeCommand>("Cut", std::move(commands)));
  });
}

void EditActions::copy() {
  run([&] {
    copy_selection();
  });
}

void EditActions::paste() {
  run([&] {
    if(auto data = get_system_data()) {
      m_clipboard.load(*data, m_measurer);
    }
    auto target = PasteTarget();
    target.m_selection = m_editor->get_selection().get_nodes();
    target.m_focus = m_editor->get_context().get_artboard();
    target.m_container = m_editor->get_context().get_container();
    target.m_view = get_view();
    target.m_is_prototyping = m_workspace == Workspace::PROTOTYPE;
    if(auto command = m_clipboard.make_paste_command(target);
        command && m_editor->execute(std::move(command))) {
      reveal_artboards();
    }
  });
}

void EditActions::paste_appearance() {
  run([&] {
    if(auto data = get_system_data()) {
      m_clipboard.load(*data, m_measurer);
    }
    if(auto command = m_clipboard.make_paste_appearance_command(
        m_editor->get_selection().get_nodes())) {
      m_editor->execute(std::move(command));
    }
  });
}

void EditActions::duplicate() {
  run([&] {
    auto& document = m_editor->get_document();
    auto& nodes = m_editor->get_selection().get_nodes();
    if(!m_commands->make("edit.duplicate", document, nodes)) {
      return;
    }
    auto mode = DuplicateMode::DESIGN;
    if(m_workspace == Workspace::PROTOTYPE) {
      mode = DuplicateMode::PROTOTYPE;
    }
    auto roots = get_top_level(document, nodes);
    auto command = std::unique_ptr<Command>();
    if(auto view = get_view()) {
      command = std::make_unique<DuplicateCommand>(roots, *view, mode);
    } else {
      command = std::make_unique<DuplicateCommand>(roots, Point(0, 0), mode);
    }
    if(m_editor->execute(std::move(command))) {
      reveal_artboards();
    }
  });
}

std::optional<Rect> EditActions::get_view() const {
  if(!m_view_provider) {
    return std::nullopt;
  }
  return m_view_provider();
}

void EditActions::reveal_artboards() {
  auto& document = m_editor->get_document();
  auto first = std::optional<Rect>();
  for(auto& id : m_editor->get_selection().get_nodes()) {
    auto node = document.find(id);
    auto artboard = node ? node->get_artboard() : nullptr;
    if(!artboard) {
      return;
    }
    auto& frame = artboard->m_frame;
    if(!first || frame.m_y < first->m_y ||
        (frame.m_y == first->m_y && frame.m_x < first->m_x)) {
      first = frame;
    }
  }
  if(!first) {
    return;
  }
  auto& context = m_editor->get_context();
  while(!context.get_container().is_nil()) {
    context.exit();
  }
  if(m_reveal_handler) {
    m_reveal_handler(*first);
  }
}
