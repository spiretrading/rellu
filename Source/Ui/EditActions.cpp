#include <exception>
#include <memory>
#include <utility>
#include <QKeySequence>
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
  add_action("edit.undo", "&Undo", QKeySequence("Ctrl+Z"), "edit", "history",
    [this] {
      run([&] { m_editor->undo(); });
    });
  add_action("edit.redo", "&Redo", QKeySequence("Shift+Ctrl+Z"), "edit",
    "history", [this] {
      run([&] { m_editor->redo(); });
    });
  add_action("edit.cut", "Cu&t", QKeySequence("Ctrl+X"), "edit", "clipboard",
    [this] { cut(); });
  add_action("edit.copy", "&Copy", QKeySequence("Ctrl+C"), "edit",
    "clipboard", [this] { copy(); });
  add_action("edit.paste", "&Paste", QKeySequence("Ctrl+V"), "edit",
    "clipboard", [this] { paste(); });
  add_command("edit.delete", "&Delete", QKeySequence(Qt::Key_Delete), "edit",
    "clipboard");
  add_command("edit.duplicate", "D&uplicate", QKeySequence("Ctrl+D"), "edit",
    "duplicate");
  add_action("edit.select_all", "Select &All", QKeySequence("Ctrl+A"), "edit",
    "selection", [this] {
      run([&] { m_editor->select_all(); });
    });
  m_actions->add(MenuDefinition("object", "&Object", "", "object"));
  add_command("object.group", "&Group", QKeySequence("Ctrl+G"), "object",
    "group");
  add_command("object.ungroup", "&Ungroup", QKeySequence("Shift+Ctrl+G"),
    "object", "group");
  add_command("object.toggle_lock", "&Lock", QKeySequence("Ctrl+L"),
    "object", "state");
  add_command("object.toggle_visibility", "&Hide",
    QKeySequence("Shift+Ctrl+H"), "object", "state");
  m_actions->add(
    MenuDefinition("object.arrange", "&Arrange", "object", "arrange"));
  add_command("arrange.bring_to_front", "Bring to &Front",
    QKeySequence("Shift+Ctrl+]"), "object.arrange", "arrange");
  add_command("arrange.bring_forward", "Bring &Forward",
    QKeySequence("Ctrl+]"), "object.arrange", "arrange");
  add_command("arrange.send_backward", "Send &Backward",
    QKeySequence("Ctrl+["), "object.arrange", "arrange");
  add_command("arrange.send_to_back", "Send to &Back",
    QKeySequence("Shift+Ctrl+["), "object.arrange", "arrange");
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
    undo.setText("&Undo");
    redo.setText("&Redo");
    for(auto& name : m_actions->get_names()) {
      if(name.starts_with("edit.") || name.starts_with("object.") ||
          name.starts_with("arrange.")) {
        m_actions->set_enabled(name, false);
      }
    }
    return;
  }
  auto& history = m_editor->get_history();
  auto& nodes = m_editor->get_selection().get_nodes();
  undo.setText(make_history_text("&Undo", history.get_undo_label()));
  redo.setText(make_history_text("&Redo", history.get_redo_label()));
  m_actions->set_enabled("edit.undo", history.can_undo());
  m_actions->set_enabled("edit.redo", history.can_redo());
  auto is_copyable = Clipboard::is_copyable(*document, nodes);
  m_actions->set_enabled("edit.copy", is_copyable);
  m_actions->set_enabled("edit.cut", is_copyable &&
    m_commands->make("edit.delete", *document, nodes));
  m_actions->set_enabled("edit.paste", !m_clipboard.is_empty());
  m_actions->set_enabled("edit.select_all", true);
  for(auto& name : m_command_actions) {
    auto command = m_commands->make(name, *document, nodes);
    m_actions->set_enabled(name, static_cast<bool>(command));
    if(command && (name == "object.toggle_lock" ||
        name == "object.toggle_visibility")) {
      m_actions->get(name).setText("&" + escape(command->get_label()));
    }
  }
}

void EditActions::add_command(const std::string& name, const QString& text,
    const QKeySequence& shortcut, const std::string& menu,
    const std::string& section) {
  add_action(name, text, shortcut, menu, section, [this, name] {
    run([&] { m_editor->execute(*m_commands, name); });
  });
  m_command_actions.push_back(name);
}

void EditActions::add_action(const std::string& name, const QString& text,
    const QKeySequence& shortcut, const std::string& menu,
    const std::string& section, std::function<void ()> handler) {
  m_actions->add(ActionDefinition(name, text, shortcut, menu, section));
  m_actions->set_handler(name, std::move(handler));
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
