# Adobe XD editor UI

This is the specification of Adobe XD's editor user interface that Rellu's
editor follows. It describes Adobe XD 61.0.12 (Store package
`Adobe.XD_pc75e8sa7ep4e`) on Windows 11, observed on 2026-10-18 through UI
Automation tree dumps and screenshots taken in an interactive session. The
display ran at 3840x2160 and 150% scaling; sizes below are given in device
independent pixels (dip, physical pixels / 1.5) unless stated otherwise.

Everything stated here was observed unless it is marked *(inferred)*.
Screenshots and tree dumps live outside the repository in
`Nextcloud\rellu_references\ui` and are cited by path relative to that
folder, for example `run4/sel-rectangle.png`. A `*-tree.txt` file is a UI
Automation dump (control type, name, automation id, class, accelerator,
value, toggle state and screen rectangle in physical pixels); a
`*-inspector.txt` file is the property inspector part of a dump.

## Window layout

The editor is a single document window (`document.png`,
`run2/select-all.png`). From top to bottom and left to right:

| Region | Size (dip) | Contents |
|---|---|---|
| Menu row | 32 high | XD app icon (magenta "Xd" square), menu bar: File, Edit, Object, Plugins, View, Window, Help; window caption buttons at the right |
| App bar | 42 high | Home button; mode switcher Design / Prototype / Share; document title with a version-history caret (centered); Invite to Document, Device Preview, Desktop Preview (play), zoom field with dropdown |
| Toolbar | 50 wide, left edge | tool buttons at the top, panel buttons at the bottom |
| Left panel | 407 wide | Document Assets, Layers or Plugins (one at a time) |
| Canvas | remaining width | pasteboard with artboards |
| Property inspector | 290 wide, right edge | context-sensitive sections, scrolls vertically |

The window title is `<document name> - Adobe XD`. A modified document shows
`*` after its name in the app bar title (`run6/grid-on.png`). Colours sampled
from the screenshots: pasteboard `#E4E4E4`, menu row `#E4E4E4`, panels,
toolbar and app bar `#F5F5F5`, artboard background as authored (white by
default), selection outline and handles `#2680EB`, selected layer row
`#E0F0FA`.

The app bar mode switcher is three toggle buttons, `Design Workspace`,
`Prototype Workspace` and `Share Workspace`; the active one is drawn in black
with an underline, the others in grey.

## Home screen

Opening XD without a document shows the home screen (`home.png`,
`home-tree.txt`): XD logo top left, cloud status and account avatar top
right; a left column with **New file** (blue pill button), **Open**, then
**Home**, a FILES heading, **Your files**, **Shared with you**,
**Manage links...**, **Deleted**. The main area has the heading
"Design. Prototype. Share." with artboard presets, each a dropdown: iPhone 14
Pro Max (430 x 932), Web 1920 (1920 x 1080), Instagram Story (1080 x 1920),
and **Custom Size** with W and H fields. Below is **Recent**, a grid of
document thumbnails with name and folder.

## Modes

- **Design** (`run2/select-all.png`): full toolbar, left panel, property
  inspector.
- **Prototype** (`run2/prototype-mode.png`, `run6/proto-rect.png`): the
  toolbar holds only Select and Zoom (plus the panel buttons); the left panel
  lists artboards; the inspector shows INTERACTION (Trigger) and ACTION (Type,
  Preserve Scroll Position, Destination, Animation, Easing, Duration) and, for
  a node, SCROLLING (Fix Position When Scrolling). A selected node shows a
  blue circular wire handle with a chevron on its right edge; the home
  artboard shows a small home tab at its top left (`run6/proto-rect.png`).
  Defaults shown before an interaction exists: Trigger Tap, Type Transition,
  Destination empty, Animation None, Easing Ease In, Duration 0.2 s; the
  dropdowns are disabled until an interaction is added with the `+` of the
  INTERACTION header.
- **Share** (`run2/share-mode.png`): toolbar holds Select and Zoom; no left
  panel; the canvas spans to the inspector, which shows LINK SETTINGS: Link
  (document name dropdown), View Setting (Design Review), Link Access (Anyone
  with the link), a colour management notice and a **Create Link** button.
  The artboard is outlined in blue with a blue name label.

Rellu has no cloud: Share and Invite need no equivalent beyond their
placement *(inferred from Rellu's scope)*.

## Toolbar

Design mode toolbar, top to bottom (automation ids from `document-tree.txt`;
shortcuts verified by pressing the key and reading which button toggled on,
`run4/survey.log`):

| Button | Automation id | Shortcut |
|---|---|---|
| Select | `toolbarSelectButton` | V (Escape also returns to Select) |
| Rectangle | `toolbarRectButton` | R |
| Ellipse | `toolbarEllipseButton` | E |
| Polygon | `toolbarPolygonButton` | Y |
| Line | `toolbarLineButton` | L |
| Pen | `toolbarPenButton` | P |
| Text | `toolbarTextButton` | T |
| Artboard | `toolbarArtboardButton` | A |
| Zoom | `toolbarZoomButton` | Z |
| *(gap)* | | |
| Document Assets panel | `toolbarLibraryPaneButton` | Shift+Ctrl+Y (View menu) |
| Layers panel | `toolbarNavigationPaneButton` | Ctrl+Y (View menu) |
| Plugins panel | `toolbarPluginsPaneButton` | Shift+Ctrl+P (View menu) |

Each button is a 40 dip square toggle with a monochrome icon; the active
tool's icon is blue. The buttons carry no accessible names; their tooltips
could not be captured because injected mouse input does not reach XD (see
Limits).

## Menus

The menu bar items are XAML `MenuBarItem`s (`menu-*-0.txt` and the matching
screenshots). Items listed in order; separators are visible in the
screenshots but not in the dumps. "Toggle" items show a check mark.

**File** (`menu-File.png`): New Ctrl+N; Open... Ctrl+O; Open from Your
Computer... Shift+Ctrl+O; Open Recent > (recent files, Clear Menu); Get UI
Kits...; Manage Libraries...; Close Alt+F4; Rename...; Save Ctrl+S; Save
As... Shift+Ctrl+S; Save As Local Document... Shift+Ctrl+Alt+S; Revert to
Saved; Show Document History; Export > (Batch... Shift+Ctrl+E, Selected...
Ctrl+E, All Artboards..., After Effects... Ctrl+Alt+F); Import...
Shift+Ctrl+I.

**Edit**: Undo Ctrl+Z; Redo Shift+Ctrl+Z; Cut Ctrl+X; Copy Ctrl+C; Copy SVG
Code; Paste Ctrl+V; Paste Appearance Ctrl+Alt+V; Duplicate Ctrl+D; Rename
Ctrl+Alt+R; Delete Del; Select All Ctrl+A; Deselect All Shift+Ctrl+A; Color
Profile > (sRGB, Unmanaged; toggles, Unmanaged on); Turn Spellcheck On;
Preferences > (Language >, Show Productivity Tips toggle, Auto-create Link
when Sharing toggle).

**Object**: Group Ctrl+G; Ungroup Shift+Ctrl+G; Lock Ctrl+L; Hide Ctrl+,;
Add Color to Assets Shift+Ctrl+C; Add Character Style to Assets
Shift+Ctrl+T; Make Component Ctrl+K; Edit Main Component Shift+Ctrl+K; Back
to Previous Instance Shift+Alt+K; Reset to Main State; Mark for Export
Shift+E; Reveal Color in Assets; Reveal Character Style in Assets; Reveal
Component in Assets; Mask with Shape Shift+Ctrl+M; Make Repeat Grid Ctrl+R;
Make Scroll Group > (Horizontal Shift+Alt+H, Vertical Shift+Alt+V,
Horizontal & Vertical Shift+Alt+D); Path > (Add Ctrl+Alt+U, Subtract
Ctrl+Alt+S, Intersect Ctrl+Alt+I, Exclude Overlap Ctrl+Alt+X, Convert to
Path Ctrl+8, Outline Stroke Ctrl+Alt+O); Text > (Bold Ctrl+B, Italic Ctrl+I,
Underline Ctrl+U, Strikethrough, Uppercase, Lowercase, Title Case,
Superscript, Subscript, Increase Font Size Shift+Ctrl+>, Decrease Font Size
Shift+Ctrl+<; Underline through Subscript are toggles); Arrange > (Bring to
Front Shift+Ctrl+], Bring Forward Ctrl+], Send Backward Ctrl+[, Send to Back
Shift+Ctrl+[); Transform > (Flip Horizontally, Flip Vertically, Reset 3D
Transforms Ctrl+Alt+T); Align to Pixel Grid; Align > (Left Shift+Ctrl+Left,
Center (Horizontally) Shift+C, Right Shift+Ctrl+Right, Top Shift+Ctrl+Up,
Middle (Vertically) Shift+M, Bottom Shift+Ctrl+Down); Distribute >
(Horizontally Shift+Ctrl+H, Vertically Shift+Ctrl+V).

**Plugins**: Browse Plugins...; Manage Plugins...; one submenu per installed
plugin; Development > (Create a Plugin..., Get Developer Tools..., Reload
Plugins (Legacy) Shift+Ctrl+R, Developer Console (Legacy), Show Develop
Folder (Legacy)).

**View**: Zoom In Ctrl++; Zoom Out Ctrl+-; 100% Ctrl+1; 200% Ctrl+2; Zoom
to Selection Ctrl+3; Zoom to Fit All Ctrl+0; Libraries Shift+Ctrl+Y; Layers
Ctrl+Y; Plugins Shift+Ctrl+P (the three panels are toggles); Guides > (Copy
Guides, Paste Guides Ctrl+V, Clear Guides, Hide All Guides Ctrl+;, Lock All
Guides Shift+Ctrl+;); Show Layout Grid Shift+Ctrl+'; Show Square Grid
Ctrl+'; Show 3D Transforms Ctrl+T; Hide Live Cursors.

**Window**: Preview Ctrl+Enter (toggle); then one toggle item per open
document window.

**Help**: Learn XD...; Productivity Tips...; User Guide...; What's New...;
Release Notes...; Provide Feedback...; About XD; Manage My Account...; Sign
Out... (account); Updates....

Items are disabled when they do not apply (for example Save on an unchanged
document, everything selection-based with no selection). XD has no in-app
keyboard shortcut reference on Windows; the shortcuts above are the menu
accelerators plus the tool keys, which together are XD's shortcut set as far
as observable *(other unmenued shortcuts, such as arrow-key nudging, were
not observable without mouse or text input)*.

## Context menus

Right-clicking the canvas, an object or an artboard could not be surveyed:
XD ignores injected mouse input, and neither Shift+F10 nor the Applications
key opens a context menu (`run4/context-object.png`,
`run5/context-object-apps.png`). *(Inferred: XD's context menus repeat the
Edit and Object menu commands that apply to the selection.)*

## Canvas

- Pasteboard `#E4E4E4`; artboards are white rectangles without a shadow,
  with their name drawn above the top left corner in small dark grey text.
  A selected artboard's name turns blue and an export icon button appears
  above it (`run4/sel-artboard.png`).
- Zoom presets in the zoom dropdown (`run2/zoom-menu.png`): 25%, 33%, 50%,
  100% (Ctrl+1), 150%, 200% (Ctrl+2), 300%, 400%. The zoom field accepts a
  typed value and shows one decimal when needed (30.6%, 168.9%, 193%).
- Selection: a 1 dip `#2680EB` outline around the object's bounds with eight
  hollow circular handles (corners and edge midpoints, white fill, blue
  stroke). A rectangle also shows four corner radius handles inside its
  corners (small hollow circles, `run4/sel-rectangle.png`). A multiple
  selection draws each object's bounds as a thin blue outline plus one
  outline with handles around the union (`run2/select-all.png`). Text in a
  fixed-size frame shows the frame with a dashed bottom edge
  (`run4/sel-text-area.png`).
- The layout grid is drawn as translucent cyan columns over the artboard
  (`run6/grid-on.png`).

## Layers panel

`run2/layers-tree.txt`, `run4/sel-rectangle.png`. A search box ("All Items"
with a filter dropdown) heads the panel. With no selection it lists
ARTBOARDS (artboard icon and name). With a selection inside an artboard it
shows a back chevron and the artboard name in capitals, then the artboard's
children topmost first, one 32 dip row each with a kind icon (rectangle,
ellipse, polygon, line, path, boolean group, text "T", group folder) and the
layer name. Selected rows are highlighted `#E0F0FA`. Rows of containers have
an expand/collapse chevron (`expandCollapseFolder`).

## Document Assets panel

`document.png`, `run2/zoom-menu.png`. Header "DOCUMENT ASSETS" with a back
chevron and a share/export button, a search field ("Search document
assets") with a dropdown, filter and sort buttons, and list/grid view
toggles. Sections, each collapsible with a `+` button: **Colors** (swatch and
hex or name), **Character Styles** ("Ag" preview with family and size, such
as "Roboto - 12pt", drawn with an em dash),
**Components** (thumbnail and name), **Videos**. An empty document shows an
illustration with "Add your styles and components, or use elements from
existing libraries." and a "View libraries" link.

## Property inspector

The inspector is a vertical stack of sections. Section headers are small
grey capitals with an (i) info icon where shown. Numeric fields are borderless
with an underline; units are implicit pixels except opacity (%) and rotation
(degrees). Mixed values in a multiple selection show an em dash
(`run4/sel-multiple-inspector.txt`). Order for a shape
(`run4/sel-rectangle-inspector.txt`):

1. **Align bar** (always first, disabled without a selection): Align Top,
   Align Middle (Vertically), Align Bottom, Distribute Horizontally | Align
   Left, Align Center (Horizontally), Align Right, Distribute Vertically.
2. **Repeat Grid** button and the boolean buttons Add, Subtract, Intersect,
   Exclude Overlap (enabled for two or more shapes; on a boolean group the
   active operation is toggled on, `run4/sel-compound-inspector.txt`).
3. **COMPONENT**: `+` (Make Component, Ctrl+K). For an instance the header
   reads COMPONENT (INSTANCE) with the component name, a Default State
   dropdown and Edit Main Component (`run4/sel-instance-inspector.txt`).
4. **TRANSFORM**: 3D Transforms button; W, H with a Lock Aspect toggle
   between them; X, Y; Rotation (degrees); Flip Horizontal, Flip Vertical;
   scroll buttons Horizontal Scroll, Vertical Scroll, Horizontal & Vertical
   Scroll; Fix Position When Scrolling checkbox.
5. **LAYOUT**: Responsive Resize switch (constraints).
6. **APPEARANCE**: Opacity slider (0 to 1) with a percent field; Blend Mode
   dropdown; then per kind:
   - rectangle: corner radius mode (same radius / different radius for each
     corner) with one field or four fields (TL, TR, BR, BL);
   - polygon: Corner Count, Corner Radius, Star Ratio (%)
     (`run4/sel-polygon-inspector.txt`);
   - **Fill** row: apply checkbox, colour swatch button, label, eyedropper;
   - **Border** row: apply checkbox, swatch, label, eyedropper; Size, Dash,
     Gap fields; alignment Inner / Outer / Center Stroke; caps Butt / Round /
     Projecting; joins Miter / Round / Bevel. For a line, alignment and joins
     are disabled (`run4/sel-line-inspector.txt`).
7. **EFFECTS**: Inner Shadow and Drop Shadow rows (apply checkbox, swatch,
   label) with X, Y and B fields; a blur row with a checkbox and a type
   dropdown (Background Blur by default).
8. **Mark for Export** checkbox.

Per kind differences:

- **Artboard** (`run4/sel-artboard.png`): no component or align controls;
  TRANSFORM has W, H, X, Y and Portrait / Landscape toggles; LAYOUT has
  Responsive Resize and SCROLLING (None by default); APPEARANCE has only Fill;
  then **GRID**: show checkbox and a type dropdown (Layout, Square). Turning on
  the layout grid shows Columns (with colour swatch) 4, Gutter Width 10, Column
  Width 94, linked or per-side margins (37), and Use Default / Make Default
  (`run6/grid-on.png`, values are XD's defaults for a 480 wide artboard).
- **Text** (`run4/sel-text-area.png`): a **TEXT** section after LAYOUT with
  font family dropdown, font size, font style dropdown, character spacing,
  line spacing and paragraph spacing fields; alignment Left / Center / Right;
  frame mode Auto Width / Auto Height / Fixed Size; Uppercase, Lowercase,
  Title Case, Superscript, Subscript, Underline, Strikethrough toggles; an
  "Insert URL here" hyperlink field. A point text's W and H are disabled
  (`run4/sel-text-point-inspector.txt`). Mixed styles show an em dash in the
  style dropdown.
- **Group, mask group and component instance**: Blend Mode defaults to
  Pass Through; no corner radius; Fill and Border rows apply to contained
  shapes.
- **Image fill** (`run4/sel-image.png`): the shape keeps its own controls;
  the fill swatch shows the image.

Popups: the colour picker (`run5/fill-picker.png`) opens to the left of the
inspector with a fill type dropdown (Solid Color), a saturation/brightness
square, vertical hue and alpha sliders, a Hex dropdown with the value field,
an opacity field, an eyedropper and `+` to add the colour to assets. The
Blend Mode dropdown (`run5/blend-list.png`) lists Normal, then groups
separated by rules: Darken, Multiply, Color Burn | Lighten, Screen, Color
Dodge | Overlay, Soft Light, Hard Light | ... *(the list scrolls; the
remaining groups, Difference, Exclusion and Hue, Saturation, Color,
Luminosity, are inferred from the blend modes XD files use)*.

Display conventions that differ from the stored values:

- The Drop Shadow **B** field shows twice the stored `r`: a shadow written
  with `r: 8` displays B 16 (`run4/sel-rectangle-inspector.txt`).
- X and Y of text are the top left of its bounds, not the stored baseline
  origin: point text written at y 40 shows Y 17.

## Rendering facts relevant to Rellu (from the acceptance tests)

- XD lays out text itself on opening: a text node with no `paragraphs`, an
  empty `paragraphs` array, or one paragraph per line at (0, 0) all render
  identically and correctly wrapped.
- XD renders a boolean group from its stored `path`; it does not recompute
  the result from the operands.
- XD computes a polygon's vertices when `points` is absent; a six-sided
  polygon has flat top and bottom edges, unlike Rellu's current
  construction (XD export of the new `shapes.xd` test document).

## Limits of this survey

- XD ignores injected mouse input (SendInput), so hover tooltips, context
  menus, drag interactions, handle behaviour and direct manipulation were not
  observed. Selection was driven through a development plugin
  (`tools\xd_acceptance\survey_plugin`) and commands through UI Automation.
- Dropdown lists inside the inspector expose no expand pattern; only the
  Blend Mode list was captured, by screenshot.
- The Plugins panel, Edit > Preferences > Language and prototype dropdown
  contents were not captured.
