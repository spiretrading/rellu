#include <algorithm>
#include <exception>
#include <memory>
#include <utility>
#include <vector>
#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMimeData>
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Prototype/InteractionCommands.hpp"
#include "Rellu/Ui/EditActions.hpp"
#include "Rellu/Ui/Translation.hpp"

using namespace Rellu;

namespace {
  QString escape(const QString& label) {
    return QString(label).replace("&", "&&");
  }

  const QMimeData* get_system_data() {
    if(auto clipboard = QGuiApplication::clipboard()) {
      return clipboard->mimeData();
    }
    return nullptr;
  }

  void remember_text(Editor& editor) {
    for(auto& id : editor.get_selection().get_nodes()) {
      if(auto node = editor.get_document().find(id)) {
        if(auto text = node->get_text()) {
          editor.get_text_selection().remember(*text);
          return;
        }
      }
    }
  }

  QString truncate_middle(const QString& text, qsizetype length) {
    constexpr auto ELLIPSIS = qsizetype(3);
    length = std::max(length, qsizetype(5));
    auto points = text.toUcs4();
    if(points.size() <= length) {
      return text;
    }
    auto head = (length - ELLIPSIS + 1) / 2;
    auto tail = (length - ELLIPSIS) / 2;
    return QString::fromUcs4(points.constData(), head) + "..." +
      QString::fromUcs4(points.constData() + points.size() - tail, tail);
  }

  QString make_history_text(const QString& text, const std::string& label) {
    if(label.empty()) {
      return translate("menu", text);
    }
    return translate("menu", text + " %1").arg(
      escape(translate("history", QString::fromStdString(label))));
  }
}

QString Rellu::make_linked_paste_message(
    const std::vector<std::string>& documents) {
  constexpr auto MAXIMUM_NAME_LENGTH = qsizetype(20);
  if(documents.empty()) {
    return QString();
  }
  auto name = QString();
  if(documents.size() == 1) {
    name = QFileInfo(QString::fromStdString(documents.front())).fileName();
  }
  if(name.isEmpty()) {
    return translate("To paste linked assets across documents, publish a "
      "library from source documents. [Learn more|https://www.adobe.com/go/"
      "xd_designsys_migration_links_not_migrated_learnmore] ");
  }
  return translate("To paste linked assets across documents, publish a "
    "library from '%1'. [Learn more|https://www.adobe.com/go/"
    "xd_designsys_migration_links_not_migrated_learnmore] ").arg(
      truncate_middle(name, MAXIMUM_NAME_LENGTH));
}

QString Rellu::make_color_profile_message() {
  return translate("Colors of the pasted object may look slightly different "
    "compared to the original. [Learn more about Color Management.|https://"
    "www.adobe.com/go/xd_color_management]");
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
  set_action("edit.copy_svg", [this] { copy_svg(); });
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
    } else if(name == "edit.delete") {
      set_action(name, [this, name] {
        run([&] {
          if(!remove_interactions() && is_available(name)) {
            m_editor->execute(*m_commands, name);
          }
        });
      });
    } else {
      set_action(name, [this, name] {
        run([&] {
          if(is_available(name)) {
            m_editor->execute(*m_commands, name);
          }
        });
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
  m_text_selection_subscription.release();
  m_editor = editor;
  if(m_editor) {
    m_selection_subscription = m_editor->get_selection().subscribe([this] {
      update();
    });
    m_history_subscription = m_editor->get_history().subscribe([this] {
      update();
    });
    m_text_selection_subscription =
      m_editor->get_text_selection().subscribe(
        [this, is_editing = m_editor->get_text_selection().is_editing()]
            () mutable {
          if(m_editor->get_text_selection().is_editing() != is_editing) {
            is_editing = !is_editing;
            update();
          }
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

void EditActions::set_text_layout(XdTextLayout layout) {
  m_layout = std::move(layout);
}

void EditActions::set_image_renderer(ImageRenderer renderer) {
  m_image_renderer = std::move(renderer);
}

void EditActions::set_source_provider(SourceProvider provider) {
  m_source_provider = std::move(provider);
}

void EditActions::set_error_handler(ErrorHandler handler) {
  m_error_handler = std::move(handler);
}

void EditActions::set_paste_handler(PasteHandler handler) {
  m_paste_handler = std::move(handler);
  update();
}

void EditActions::set_notice_handler(NoticeHandler handler) {
  m_notice_handler = std::move(handler);
}

void EditActions::update() {
  auto document = m_editor ? &m_editor->get_document() : nullptr;
  auto is_srgb = document &&
    document->get_resources().m_color_profile == ColorProfile::SRGB;
  m_actions->get("edit.color_profile.srgb").setChecked(is_srgb);
  m_actions->get("edit.color_profile.unmanaged").setChecked(!is_srgb);
  auto& undo = m_actions->get("edit.undo");
  auto& redo = m_actions->get("edit.redo");
  if(!m_editor) {
    undo.setText(translate("menu", "Undo"));
    redo.setText(translate("menu", "Redo"));
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
  m_actions->set_enabled("edit.copy_svg", is_copyable);
  m_actions->set_enabled("edit.cut", is_copyable &&
    m_commands->make("edit.delete", *document, nodes));
  auto data = get_system_data();
  m_actions->set_enabled("edit.paste", !m_clipboard.is_empty() ||
    (data && (Clipboard::is_loadable(*data) ||
      (m_paste_handler && m_paste_handler(*data, false)))));
  m_actions->set_enabled("edit.paste_appearance", !nodes.empty() &&
    std::none_of(nodes.begin(), nodes.end(), [&] (const auto& id) {
      auto node = document->find(id);
      return node && node->get_shape() && node->get_shape()->m_video;
    }));
  m_actions->set_enabled("edit.select_all", true);
  m_actions->set_enabled(
    "edit.deselect_all", !m_editor->get_selection().is_empty());
  for(auto& name : m_command_actions) {
    auto command = std::unique_ptr<Command>();
    if(is_available(name)) {
      command = m_commands->make(name, *document, nodes);
    }
    m_actions->set_enabled(name, static_cast<bool>(command));
    if(command && (name == "object.toggle_lock" ||
        name == "object.toggle_visibility" || name == "object.ungroup")) {
      m_actions->get(name).setText(escape(
        translate("menu", QString::fromStdString(command->get_label()))));
    } else if(!command && name == "object.ungroup") {
      m_actions->get(name).setText(translate("menu", "Ungroup"));
    }
  }
  if(m_workspace == Workspace::PROTOTYPE &&
      !m_editor->get_context().get_interactions().empty()) {
    m_actions->set_enabled("edit.delete", true);
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

bool EditActions::remove_interactions() {
  if(m_workspace != Workspace::PROTOTYPE) {
    return false;
  }
  auto& document = m_editor->get_document();
  auto& selection = m_editor->get_selection();
  auto& edit = m_editor->get_context();
  if(selection.get_size() != 1 ||
      edit.get_interaction_owner() != selection.get_primary()) {
    return false;
  }
  auto removed = std::vector<InteractionId>();
  for(auto& id : edit.get_interactions()) {
    if(std::ranges::any_of(document.get_prototype().m_interactions,
        [&] (const auto& interaction) {
          return interaction.m_id == id;
        })) {
      removed.push_back(id);
    }
  }
  if(removed.empty()) {
    if(edit.get_pending().empty()) {
      return false;
    }
    edit.set_interactions(selection.get_primary(), {});
    return true;
  }
  auto next = find_next_interaction(document, removed);
  auto commands = std::vector<std::unique_ptr<Command>>();
  for(auto& id : removed) {
    commands.push_back(std::make_unique<RemoveInteractionCommand>(id));
  }
  auto is_recorded = m_editor->execute(
    std::make_unique<CompositeCommand>("Delete", std::move(commands))) !=
      nullptr;
  auto chosen = std::vector<InteractionId>();
  if(!next.is_nil()) {
    chosen.push_back(next);
  }
  edit.set_interactions(selection.get_primary(), std::move(chosen));
  if(is_recorded) {
    m_editor->update_selection();
  }
  return true;
}

bool EditActions::is_available(const std::string& name) const {
  return (name != "object.reset_main_state" &&
    name != "object.transform.flip_horizontally" &&
    name != "object.transform.flip_vertically") ||
    !m_editor->get_text_selection().is_editing();
}

bool EditActions::copy_selection() {
  auto source = XdClipboardSource();
  if(m_source_provider) {
    source = m_source_provider();
  }
  auto& document = m_editor->get_document();
  auto& nodes = m_editor->get_selection().get_nodes();
  if(!m_clipboard.copy(document, nodes, m_measurer, m_layout, source)) {
    return false;
  }
  remember_text(*m_editor);
  if(auto clipboard = QGuiApplication::clipboard()) {
    if(auto data = m_clipboard.make_mime_data(m_image_renderer)) {
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

void EditActions::copy_svg() {
  run([&] {
    auto svg = Clipboard::make_svg_code(m_editor->get_document(),
      m_editor->get_selection().get_nodes(), m_measurer, m_layout);
    if(svg.empty()) {
      return;
    }
    if(auto clipboard = QGuiApplication::clipboard()) {
      clipboard->setMimeData(Clipboard::make_svg_mime_data(svg).release());
    }
  });
}

void EditActions::paste() {
  run([&] {
    if(auto data = get_system_data()) {
      if(m_paste_handler && m_paste_handler(*data, true)) {
        return;
      }
      m_clipboard.load(*data, m_measurer);
    }
    auto target = PasteTarget();
    target.m_selection = m_editor->get_selection().get_nodes();
    target.m_focus = m_editor->get_context().get_artboard();
    target.m_container = m_editor->get_context().get_container();
    target.m_view = get_view();
    target.m_is_prototyping = m_workspace == Workspace::PROTOTYPE;
    if(m_source_provider) {
      target.m_document = m_source_provider().m_id;
    }
    if(auto command = m_clipboard.make_paste_command(target);
        command && m_editor->execute(std::move(command))) {
      reveal_artboards();
      if(m_notice_handler && m_clipboard.is_from_other_document(
          target.m_document)) {
        auto profile =
          m_editor->get_document().get_resources().m_color_profile;
        if(profile != ColorProfile::SRGB) {
          m_notice_handler(make_color_profile_message());
        }
      }
      if(m_notice_handler && m_source_provider) {
        auto message = make_linked_paste_message(
          m_clipboard.get_linked_documents(m_source_provider()));
        if(!message.isEmpty()) {
          m_notice_handler(message);
        }
      }
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
      remember_text(*m_editor);
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
