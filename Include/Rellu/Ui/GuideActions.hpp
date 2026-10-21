#ifndef RELLU_UI_GUIDE_ACTIONS_HPP
#define RELLU_UI_GUIDE_ACTIONS_HPP
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <QByteArray>
#include <QMimeData>
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Model/Artboard.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Tools/GuideSettings.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/AppBar.hpp"

namespace Rellu {

  /**
   * Handles the actions of Adobe XD's View > Guides menu, enabled in the
   * design workspace: view.guides.copy copies the guides of the single
   * artboard selected or holding the selection, view.guides.paste merges
   * copied guides into the artboards selected or holding the selection,
   * showing guides if hidden as part of the same step, view.guides.clear
   * removes the guides of those artboards while guides are shown,
   * view.guides.hide toggles between Hide All Guides and Show All Guides
   * and view.guides.lock between Lock All Guides and Unlock All Guides, each
   * toggle being an undoable step of the editor's history labelled as its
   * menu item was.
   */
  class GuideActions {
    public:

      /**
       * The type of function told how guides are to be shown on the canvas.
       * @param settings The settings, guides being hidden outside of the
       *        design workspace.
       */
      using SettingsHandler = std::function<void (const GuideSettings&)>;

      /**
       * Sets the handlers of the actions, which are disabled until an
       * editor is set. The registry must already have Adobe XD's menus.
       * @param actions The registry holding the actions.
       */
      explicit GuideActions(ActionRegistry& actions);

      ~GuideActions();

      /**
       * Sets the editor acted on.
       * @param editor The editor, or <code>nullptr</code> for none. It must
       *        outlive this object or be replaced first.
       */
      void set_editor(Editor* editor);

      /** Returns the settings chosen with the menu. */
      const GuideSettings& get_settings() const;

      /**
       * Sets the settings chosen with the menu.
       * @param settings The settings.
       */
      void set_settings(GuideSettings settings);

      /**
       * Sets the workspace shown.
       * @param workspace The workspace.
       */
      void set_workspace(Workspace workspace);

      /**
       * Sets the function told how guides are to be shown, calling it at
       * once.
       * @param handler The function.
       */
      void set_settings_handler(SettingsHandler handler);

      /**
       * Pastes the guides copied to system clipboard data.
       * @param data The clipboard data.
       * @param is_applied Whether to paste rather than only test.
       * @return <code>true</code> iff the data holds copied guides.
       */
      bool paste(const QMimeData& data, bool is_applied);

      /** Updates the text and enabled state of every action. */
      void update();

    private:
      ActionRegistry* m_actions;
      Editor* m_editor;
      GuideSettings m_settings;
      Workspace m_workspace;
      SettingsHandler m_settings_handler;
      std::shared_ptr<GuideActions*> m_self;
      Subscription m_selection_subscription;
      Subscription m_history_subscription;

      GuideActions(const GuideActions&) = delete;
      GuideActions& operator =(const GuideActions&) = delete;
      std::vector<NodeId> get_artboards() const;
      void copy();
      void change(GuideSettings settings, std::string label);
      std::pair<std::function<void ()>, std::function<void ()>> make_change(
        GuideSettings settings);
      void publish();
  };

  /**
   * Makes the system clipboard data holding copied guides.
   * @param guides The guides.
   */
  std::unique_ptr<QMimeData> make_guides_data(const std::vector<Guide>& guides);

  /**
   * Reads the guides copied to system clipboard data.
   * @param data The clipboard data.
   * @return The guides, or none if the data holds no copied guides.
   */
  std::optional<std::vector<Guide>> read_guides_data(const QMimeData& data);

  /**
   * Finds the guide settings recorded for a document in a store of them, as
   * Adobe XD keeps them under guidesGlobalSettings in its preferences.
   * @param store The store, as find_document_settings reads it.
   * @param document The uxdesign#id of the document.
   * @return The settings recorded, or none if the store has none for the
   *         document.
   */
  std::optional<GuideSettings> find_guide_settings(
    const QByteArray& store, std::string_view document);

  /**
   * Records the guide settings of a document in a store of them as Adobe XD
   * does, the default settings removing the document's record.
   * @param store The store, as record_document_settings writes it.
   * @param document The uxdesign#id of the document.
   * @param settings The settings.
   * @param time The time of the recording, in milliseconds since the epoch.
   * @return The store with the settings recorded.
   */
  QByteArray record_guide_settings(const QByteArray& store,
    std::string_view document, const GuideSettings& settings,
    std::int64_t time);
}

#endif
