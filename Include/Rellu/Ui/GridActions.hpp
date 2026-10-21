#ifndef RELLU_UI_GRID_ACTIONS_HPP
#define RELLU_UI_GRID_ACTIONS_HPP
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <QByteArray>
#include "Rellu/Commands/Signal.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Tools/GridSettings.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/AppBar.hpp"

namespace Rellu {

  /**
   * Keeps how a window shows the design grids of artboards and handles the
   * actions of Adobe XD's View menu toggling them, enabled in the design
   * workspace: view.layout_grid toggles between Show Layout Grid and Hide
   * Layout Grid and view.square_grid between Show Square Grid and Hide
   * Square Grid, each hiding the other kind of grid, and choosing the grid
   * type shows only that kind. Each change is an undoable step of the
   * editor's history, a toggle labelled as its menu item was and a choice
   * labelled Grid Type. While a grid is shown, every artboard lacking a grid
   * is given the grid of make_initial_grid, as initialize_all_grids does.
   */
  class GridActions {
    public:

      /**
       * The type of function told how grids are to be shown on the canvas.
       * @param settings The settings, grids being hidden outside of the
       *        design workspace.
       */
      using SettingsHandler = std::function<void (const GridSettings&)>;

      /**
       * Sets the handlers of the actions, which are disabled until an
       * editor is set. The registry must already have Adobe XD's menus.
       * @param actions The registry holding the actions.
       */
      explicit GridActions(ActionRegistry& actions);

      ~GridActions();

      /**
       * Sets the editor acted on.
       * @param editor The editor, or <code>nullptr</code> for none. It must
       *        outlive this object or be replaced first.
       */
      void set_editor(Editor* editor);

      /** Returns how the window shows grids. */
      const GridSettings& get_settings() const;

      /**
       * Sets how the window shows grids, outside of the editor's history.
       * @param settings The settings.
       */
      void set_settings(GridSettings settings);

      /**
       * Sets the workspace shown.
       * @param workspace The workspace.
       */
      void set_workspace(Workspace workspace);

      /**
       * Sets the function told how grids are to be shown, calling it at
       * once.
       * @param handler The function.
       */
      void set_settings_handler(SettingsHandler handler);

      /**
       * Toggles a kind of grid as its View menu item does.
       * @param type The kind of grid.
       */
      void toggle(GridType type);

      /**
       * Shows only one kind of grid, as choosing the grid type in the
       * property inspector does.
       * @param type The kind of grid.
       */
      void choose(GridType type);

      /**
       * Subscribes an observer to every change of the settings.
       * @param observer The function called after each change.
       * @return The handle ending the subscription when released.
       */
      [[nodiscard]] Subscription subscribe(std::function<void ()> observer);

      /** Updates the text and enabled state of every action. */
      void update();

    private:
      ActionRegistry* m_actions;
      Editor* m_editor;
      GridSettings m_settings;
      Workspace m_workspace;
      SettingsHandler m_settings_handler;
      Signal m_signal;
      std::shared_ptr<GridActions*> m_self;
      Subscription m_history_subscription;

      GridActions(const GridActions&) = delete;
      GridActions& operator =(const GridActions&) = delete;
      void change(GridSettings settings, std::string label);
      void initialize();
      void publish();
  };

  /**
   * Finds the grid settings recorded for a document in a store of them, as
   * Adobe XD keeps them under gridGlobalSettings in its preferences, the
   * kind of grid shown most recently being kept from the settings given.
   * @param store The store, as find_document_settings reads it.
   * @param document The uxdesign#id of the document.
   * @param settings The settings in effect.
   * @return The settings recorded, or none if the store has none for the
   *         document.
   */
  std::optional<GridSettings> find_grid_settings(const QByteArray& store,
    std::string_view document, const GridSettings& settings);

  /**
   * Records the grid settings of a document in a store of them as Adobe XD
   * does, settings showing no grid removing the document's record.
   * @param store The store, as record_document_settings writes it.
   * @param document The uxdesign#id of the document.
   * @param settings The settings.
   * @param time The time of the recording, in milliseconds since the epoch.
   * @return The store with the settings recorded.
   */
  QByteArray record_grid_settings(const QByteArray& store,
    std::string_view document, const GridSettings& settings,
    std::int64_t time);
}

#endif
