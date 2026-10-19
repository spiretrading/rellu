#include <exception>
#include <memory>
#include <utility>
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Ui/EditActions.hpp"

using namespace Rellu;

namespace {
  QString escape(const std::string& label) {
    return QString::fromStdString(label).replace("&", "&&");
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
      m_editor(nullptr) {
  set_action("edit.undo", [this] {
    run([&] { m_editor->undo(); });
  });
  set_action("edit.redo", [this] {
    run([&] { m_editor->redo(); });
  });
  set_action("edit.cut", [this] { cut(); });
  set_action("edit.copy", [this] { copy(); });
  set_action("edit.paste", [this] { paste(); });
  set_action("edit.select_all", [this] {
    run([&] { m_editor->select_all(); });
  });
  set_action("edit.deselect_all", [this] {
    run([&] { m_editor->get_selection().clear(); });
  });
  for(auto& name : m_actions->get_names()) {
    if(m_commands->contains(name)) {
      set_action(name, [this, name] {
        run([&] { m_editor->execute(*m_commands, name); });
      });
      m_command_actions.push_back(name);
    }
  }
  update();
}

EditActions::~EditActions() = default;

Editor* EditActions::get_editor() const {
  return m_editor;
}

void EditActions::set_editor(Editor* editor) {
  m_selection_subscription.release();
  m_history_subscription.release();
  if(editor != m_editor) {
    m_clipboard.clear();
  }
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
  m_actions->set_enabled("edit.paste", !m_clipboard.is_empty());
  m_actions->set_enabled("edit.select_all", true);
  m_actions->set_enabled(
    "edit.deselect_all", !m_editor->get_selection().is_empty());
  for(auto& name : m_command_actions) {
    auto command = m_commands->make(name, *document, nodes);
    m_actions->set_enabled(name, static_cast<bool>(command));
    if(command && (name == "object.toggle_lock" ||
        name == "object.toggle_visibility")) {
      m_actions->get(name).setText(escape(command->get_label()));
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

void EditActions::cut() {
  run([&] {
    auto& document = m_editor->get_document();
    auto& nodes = m_editor->get_selection().get_nodes();
    auto command = m_commands->make("edit.delete", document, nodes);
    if(!command || !m_clipboard.copy(document, nodes)) {
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
    m_clipboard.copy(
      m_editor->get_document(), m_editor->get_selection().get_nodes());
  });
}

void EditActions::paste() {
  run([&] {
    if(auto command = m_clipboard.make_paste_command()) {
      m_editor->execute(std::move(command));
    }
  });
}
