#ifndef RELLU_UI_ASSET_MOVE_DIALOG_HPP
#define RELLU_UI_ASSET_MOVE_DIALOG_HPP
#include <string>
#include <vector>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /**
   * Adobe XD's Move to dialog of the document's assets, choosing the group
   * that colors, character styles or components move to. It lists the
   * groups within
   * the group shown, starting at the section of the assets moved, a back
   * arrow leading up to the sections of Document Assets, where only that
   * section can be chosen. Choosing a group shows it, Move moves the assets
   * to the group shown, and the add group button creates a group within it
   * from a name entered, at once, then shows it.
   */
  class AssetMoveDialog : public QFrame {
    public:

      /** A group listed. */
      struct Entry {

        /** The group's id, or a section's. */
        std::string m_group;

        /** The name shown. */
        QString m_name;

        /** Whether the assets can move to the group. */
        bool m_is_enabled = true;
      };

      /**
       * Constructs an AssetMoveDialog.
       * @param editor The editor of the document holding the assets, which
       *        must outlive the dialog.
       * @param assets The colors, character styles or components' masters
       *        to move, of one section.
       * @param parent The parent widget.
       */
      AssetMoveDialog(Editor& editor, std::vector<NodeId> assets,
        QWidget* parent = nullptr);

      /** Returns the title shown: Move to, or Create new group. */
      QString get_title() const;

      /**
       * Returns the id of the group shown, or empty at the top of Document
       * Assets.
       */
      const std::string& get_group() const;

      /** Returns the name of the group shown. */
      QString get_location() const;

      /** Returns the groups listed within the group shown. */
      std::vector<Entry> get_entries() const;

      /** Returns whether the back arrow is shown. */
      bool can_go_back() const;

      /** Returns whether a group is being created. */
      bool is_creating() const;

      /** Returns whether the button submitting, Move or Create, is enabled. */
      bool can_submit() const;

      /**
       * Shows a group listed, as clicking it does.
       * @param group The group's id.
       * @return <code>true</code> iff the group is shown.
       */
      bool open(const std::string& group);

      /** Shows the group holding the one shown, as the back arrow does. */
      void go_back();

      /**
       * Starts creating a group within the one shown, as the add group
       * button does, proposing the name New group or New group followed by
       * the lowest number free.
       */
      void start_creating();

      /**
       * Sets the name of the group created.
       * @param name The name.
       */
      void set_name(const QString& name);

      /**
       * Submits, as Enter or the submit button does: creates the group
       * named, or moves the assets and closes.
       * @return <code>true</code> iff something changed.
       */
      bool submit();

      /** Stops creating a group, as Cancel or Escape does. */
      void cancel_creating();

      /**
       * Shows the dialog as a popup at a point of the screen, kept on the
       * screen.
       * @param position The point in global coordinates.
       */
      void popup(QPoint position);

    protected:
      void keyPressEvent(QKeyEvent* event) override;

    private:
      Editor* m_editor;
      std::vector<NodeId> m_assets;
      std::string m_section;
      std::string m_group;
      bool m_is_creating;
      QLabel* m_title;
      QToolButton* m_back;
      QLabel* m_location;
      QWidget* m_rows;
      QVBoxLayout* m_rows_layout;
      QLineEdit* m_name;
      QLabel* m_error;
      QToolButton* m_add;
      QPushButton* m_cancel;
      QPushButton* m_submit;

      std::string get_parent(const std::string& group) const;
      QString get_name_error() const;
      void refresh();
  };
}

#endif
