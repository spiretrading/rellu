# Rellu document model

This is the reference design for `RelluModel`, the in-memory representation of
a design document. Every other module reads it: the XD importer fills it, the
renderer draws it, tools and panels edit it through transactions, and the
XD writer saves it.

## Goals and constraints

- **Faithful to Adobe XD.** Every construct found in the XD corpus survey must
  have a place in the model, so that loading an `.xd` file loses nothing that
  affects how it looks or behaves. `.xd` is Rellu's only document format, and
  what the model does not represent is kept as opaque data (see "Opaque
  data") so that saving loses nothing either.
- **Fast at XD scale.** A single document can hold hundreds of thousands of
  nodes. Reading must be cheap, edits must not copy the document, and the
  renderer must be able to find what changed without rescanning.
- **Editable.** All changes go through transactions that produce change
  notifications and support undo and redo.
- **Portable and testable.** The model depends only on the standard library
  and `RelluGeometry`. It does not use Qt or Beam.
- **Self-describing values.** Node data are plain value types, which keeps
  writing them straightforward.

## Coordinates and units

- Lengths are `double`, in document pixels. The y axis points down.
- Every node has a `Matrix` transform (XD's `a b c d tx ty`) relative to its
  parent. A node's geometry is expressed in its own local space.
- Artboards have a frame (`x`, `y`, `width`, `height`) in document space, and
  their children are positioned relative to the artboard's origin, as in XD.
- Colors are `Color` (straight sRGB with alpha). Blending happens in sRGB, as
  in XD.
- Text indices are UTF-16 code units (see "Text").

## Identity

- `NodeId` is a 128-bit UUID, stored as 16 bytes with value semantics,
  hashing, ordering and formatting to and from the canonical text form. XD's
  `id` and `guid` strings map directly onto it.
- Ids are unique within a document and stable across edits, undo, save and
  load. New nodes get freshly generated ids.
- Other identities reuse the same type: `ComponentId` (XD `symbolId`),
  `StateId`, `InteractionId`, `ImageId` (XD resource `uid`), `SwatchId`.

## Structure

```
Document
  Canvas: the infinite pasteboard
    top-level nodes: artboards and loose items, in z order (first = bottom)
  Resources
    components (masters with states)
    images (encoded bytes by ImageId)
    color swatches, character styles
  Prototype
    interactions, home artboard
```

### Node kinds

Every node shares a common header and carries one kind-specific payload:

| Kind | XD source | Payload |
|---|---|---|
| `Artboard` | `type: artboard` plus the resource `artboards` table | frame, background fill, viewport height, layout grid, children |
| `Group` | `type: group` | children, plus optional roles (below) |
| `Shape` | `type: shape` | geometry, style |
| `Text` | `type: text` | text content, styles, frame, stored layout |
| `Reference` | `type: syncRef` | source node, optional transform, optional children |

The common header holds: `id`, `name`, `has_custom_name`, `is_visible`,
`is_locked`, `transform`, `opacity`, `blend_mode`, `is_isolated` (XD
`isolation: isolate`), `constraints` (responsive resize, below),
`is_marked_for_export`, `is_fixed_position` (XD `meta.ux.fixed`, Fix Position
When Scrolling) and `aspect_lock` (XD `meta.ux.aspectLock`, the size whose
proportions a resize keeps while Lock Aspect is on).

The transform of an artboard's child is relative to the artboard. XD stores
it in document coordinates as `transform` and, on save, relative to the
artboard as `meta.ux.localTransform`, the last `ux` member of every child
of an artboard and of no other node (ArtboardExtension.js
`serializeChildrenAGC`); on load `localTransform` wins over `transform`
(`deserializeAGC`). XD also writes `meta.ux.rotation`, the node's rotation
in degrees, for every rotated node other than a `syncRef`, right after its
name key: a flipped or rotated text carries it like any shape or group.
For a 3D node `localTransform` takes the 3D matrix form and `rotation` is
the turn about the z axis (verified in XD 61).

The header's `rotation` is that stored rotation, the value the inspector
shows, which XD keeps rather than derives (verified in XD 61 by saving the
results of each operation):
- Setting a transform keeps the rotation while it still turns as the
  transform does and otherwise derives it in [0, 360) (a node at -90 or 360
  that is moved keeps it). Transaction applies this to every header.
- A resize or flip finalizes it (DefaultNodeResizer.js `finalizeRotation`):
  the fitted rotation, less 360 when it was negative, modulo 360, so 360
  becomes 0 and -90 stays -90.
- Flipping a rotated shape or text turns its transform to 180 minus the
  rotation (horizontally) or 360 minus it (vertically) and mirrors it within
  its own bounds top to bottom (SimplifiedFlipScale.js Y_REFLECTION): 30
  flips to 150 or 330, -90 vertically to -270, text toggling `flipY`.
- The inspector's field normalizes the value typed within a turn, keeping
  its sign and 360 (PropertyUtils.js `normalizeRotation`), so 360500 sets
  140.
- Ungrouping, or moving a node out of a container, adds the container's
  rotation, modulo 360 unless the sum is a whole turn (GroupExtension.js
  `reparent`): -90 in a group at 30 becomes -60.
- Dragging a rotate handle turns by whole degrees, first turning each node
  back by the fraction of a degree its rotation has (180.0000153 turns as
  180), and sets the rotation RotationDrag computes (RotateDragMode.js).

`has_custom_name` is XD's `hasCustomName`, which every node carries: a name
given explicitly (renaming, Outline Stroke, a new component state) rather
than generated for the kind of node (Rectangle 3, Union 1, a text's content).
XD writes `meta.ux.hasCustomName: true` for a custom name and otherwise the
kind's `meta.ux.nameL10N` key (SHAPE_RECTANGLE, SHAPE_IMAGE, SHAPE_GROUP for
text, ...), and reads a named node without `nameL10N` as custom
(GroupExtensionSerialization.js `deserializeAGC`). Reading a node with
`nameL10N` (other than text) replaces its stored name's kind with the kind's
default name in the user interface language, keeping the number it ends with
(NamingRegistry.js `localizeName`), so a document saved in English shows and
saves Rechteck 3 in German and a Box named by default reads as Rectangle; a
text named by default takes its content, cut to 256 code units
(TextExtension.js `assignName`). Copies keep custom names
and renumber generated ones (SceneNodeUtil.js `applyNamingRules`), a text's
generated name follows its content, and a boolean group's generated name
follows its operation. `ComponentInstance::m_is_name_inherited` is XD's
`isCustomNameInherited`: the instance carries the main component's custom
name, so Reset to Main State and Swap treat it as not renamed, and renaming
the component renames it along with the instances without a custom name
(SymbolGroupExtension.js `setInheritedName`).

### Groups and their roles

A group is a container. XD encodes several behaviours as group metadata; each
becomes an optional role on the group payload. A group may have more than one
role (for example a component instance that is also a stack).

| Role | XD source | Data |
|---|---|---|
| Mask | `meta.ux.clipPathResources` | the mask shapes (their union clips the children) |
| Repeat grid | `meta.ux.repeatGrid` | cell size, padding, rows, columns, overall size; children are the cells |
| Scroll group | `meta.ux.scrollingType`, `viewportWidth/Height`, `offsetX/Y` | direction (horizontal, vertical, panning), viewport, offset |
| Stack | `contentStackType`, `contentStackMarginAfter` (per child), `contentPadding`, `hasContentBackground` | direction, per-child spacing, padding, whether the first child is the background |
| Padding | `contentPadding`, `isContentAware`, `hasContentBackground` | padding around content with a background child |
| Component instance | `meta.ux.symbolId`, `stateId`, `syncMap`, `symbolOverrides` | component, active state, override bookkeeping (including the group within the instance that records each added, removed or reordered node) |

XD stores the laid-out result of stacks, padding and repeat grids as ordinary
child transforms and sizes. The model keeps those results as authoritative
when loading; the layout roles describe how to recompute them after an edit.

A scroll group draws its children where they are, clipped to its viewport: the
rectangle at the offset with the viewport's size, in the group's coordinates,
bounded only along the directions it scrolls in. A repeat grid clips its cells
to its overall size, from the group's origin.

### References

XD stores parts of component instances that are unchanged from the master as
`syncRef` nodes pointing into the master's tree (`syncSourceGuid`), optionally
overriding the transform and listing children that are themselves references.
The model keeps these as `Reference` nodes rather than copying the master:

- `source`: the `NodeId` of the referenced node inside a component's master or
  state tree.
- `transform`: present only when overridden.
- `children`: present only when the reference lists its own children.

A reference renders as its source subtree with the overrides applied. The
renderer resolves references when it builds its render scene; editing a
master therefore updates every reference to it. Like XD, which writes a
`syncRef` only for a node whose own properties other than its transform and
interactions equal its source's, a reference has no header of its own: it
shows its source's name, visibility, lock, opacity and blend mode, and an
edit of any of them first replaces it with a full copy whose children stay
references.

### Shapes

A shape's geometry is one of:

| Geometry | XD `shape.type` | Data |
|---|---|---|
| `RectangleShape` | `rect` | rect, four corner radii (XD order: top-left, top-right, bottom-right, bottom-left) |
| `EllipseShape` | `ellipse`, `circle` | center, x and y radii |
| `LineShape` | `line` | two points |
| `PolygonShape` | `polygon` | corner count, size, corner radius, star ratio (`uxdesign#starRatio`, 100 when absent), vertices |
| `PathShape` | `path` | `Path` (parsed SVG data) and fill rule |
| `CompoundShape` | `compound` | `CompoundOperation`, operands (child shape, group and reference nodes), cached result `Path` |

Geometry converts to a `Path` with the `RelluGeometry` builders
(`make_rectangle`, `make_ellipse`, `make_polygon`, `parse_svg_path`,
`combine`). A compound shape keeps its operands so it stays editable; the
cached result is XD's stored `path`, recomputed when an operand changes. A
group operand stands for the union of its children's outlines and a reference
operand for the outline of what it renders (CompoundResolution.hpp); a nested
compound shape contributes its result (verified in XD with a group in an
exclusion). XD never lets a node inside a compound shape, or an artboard, be
hidden.

A star is a polygon whose star ratio is below 100: after each corner it has
an inner vertex on the line from the center to the middle of the edge the
polygon would have there, at that percentage of the distance. A video
(`shape.meta.ux.subtype: video`) is a rectangle shape with a `Video`
payload: its poster frame (`mediaFill`), the media resource
(`mediaFileGuid`) and whether it loops; it has no fill or stroke and is drawn
as its poster.

### Style

`Style` belongs to shapes, text and artboards (artboards use only the fill):

- `fill`: one of `NoFill{paint}`, `SolidFill{color, swatch}`,
  `GradientFill{kind (linear, radial), stops (offset, color), geometry
  (x1 y1 x2 y2 for linear; cx cy r fx fy for radial), units
  (object bounding box or user space), transform}`,
  `ImageFill{image, scale behaviour (fill, cover), offset, scale, flips}`.
  A fill turned off keeps its paint, as XD does (`type: none` with the
  paint's members and `uxdesign#type`), in `NoFill::m_paint`; turning it
  back on restores it.
- `stroke`: `Stroke{paint (solid color for now), width, alignment (inside,
  outside, center), cap (butt, round, square), join (miter, round, bevel),
  miter limit, dash array, is_enabled}`. XD omits the alignment for center.
- `effects`: an ordered list of `DropShadow{offset, blur, color,
  is_visible}`, `InnerShadow{...}` and `Blur{amount, brightness,
  fill_opacity, is_background}`. One XD `dropShadow` filter can hold several
  shadows; each becomes its own effect.

Opacity and blend mode live in the node header because groups have them too.

### Text

- `content`: `std::u16string`. XD is a JavaScript application, so its
  character offsets (`from`, `to`, ranged style lengths) are UTF-16 code
  units; storing UTF-16 keeps them exact and matches Qt's text layout.
- `frame`: `Positioned` (a point; text does not wrap), `Area{size}` (wraps
  and clips) or `AutoHeight{width}` (wraps; height follows content).
- `base_style` plus `runs`: a sequence of `TextRun{length, font (family,
  style, PostScript name, size), letter_spacing (XD charSpacing, thousandths
  of an em), fill color, underline, strikethrough, text_transform (none,
  uppercase, lowercase, title case), script (normal, superscript,
  subscript)}`. Runs cover the whole content in order.
- `paragraph_style`: alignment (left, center, right), line height (absent
  means automatic), paragraph spacing after.
- `layout`: optional stored layout from XD: per paragraph, per line, the runs
  `{from, to, x, y}`. When present the renderer keeps these line breaks and
  runs instead of breaking lines itself, which reproduces XD's line breaks
  exactly. The stored positions are not used as given: XD recomputes them
  when it loads a document (documents saved with older font versions store
  stale baselines), so Rellu recomputes x (line start, from alignment) and
  the baselines from font metrics the same way XD does. Any edit to content,
  frame, paragraph style or the parts of a character style that affect
  layout (font, letter spacing, case, script, run lengths) discards the
  stored layout, and Rellu's own layout takes over. Recoloring, underlining
  or striking through a run keeps it, dropping the color the layout stored
  for spans whose run color changed. Stored runs are not validated against
  the content: consumers clamp `from` and `to` to the content length.
- `is_flipped_vertically`: XD `meta.ux.flipY`. The text is drawn mirrored
  top to bottom about the middle of its bounds; the node's transform, as XD
  stores it, does not include the mirror. Flip Horizontal turns the
  transform by 180 degrees about the text's center and toggles the flag
  (ResizeUtil.js `flipText`), so the text shows and saves rotation 180.
  While the text is edited it is drawn without the mirror, as XD's
  `editInProgress` draws it, and the flip commands are disabled
  (FlipCommands.js `canFlip`).

### Artboards

`Artboard{frame, name, background (fill), viewport_height (scrollable
artboards), layout_grid (columns, gutter, margins, column/row strokes,
visibility), guides, children}`. The frame comes from the resource
`artboards` table, keyed by artboard id.

### Responsive resize constraints

`Constraints{is_enabled, left, right, top, bottom, width, height}` as XD's
`constraint*` booleans, with `is_custom` for `customConstraints`. They only
affect how a node responds when its parent is resized; they never move
anything at load time. `is_enabled` defaults to on, as XD's
`constraintsDisabled` does; new artboards are created with it off.

Resizing (Commands/ResponsiveResize) follows XD 61's resizers: Auto
constraints are inferred from position and recorded on the nodes, then
children are laid out by them; with Responsive Resize off on an artboard, or
Shift held, the artboard's children keep their artboard-relative positions
(verified in XD: dragging the left or top handle moves them with the edge).

The operands of a boolean group, like the children of repeat grids, masks
and scroll groups, never have constraints inferred: before a resize their
Auto pins are cleared, so they scale with the group, while Manual pins are
kept and laid out against the group's bounds as in a plain group
(ConstraintsUtil.js `_isUnconstrainedContainer`/`_resetConstraints`,
BooleanGroupRotationNodeResizer.js `rotateScale`). Making a boolean group
clears the pins of everything it combines, Manual ones included, keeping
the Manual mode (BooleanCommands.js `createBooleanGroup`).

Verified in XD 61:

- Resizing an instance keeps its children as `syncRef` references: only a
  moved child gets a transform, and nothing is marked Manual.
- A Manual child pinned right with a fixed width ends at the group's new
  right edge.
- A group's frame is the bounds of its visible children; hidden children
  are laid out against it like the others.
- A scroll group's handles change only its viewport, and its bounds are the
  viewport.
- A rotated plain group whose parent stretches it unevenly keeps its
  rotation: the stretch, expressed in the group's own axes, is a skew that
  each child takes with its own constraints, which XD measures against the
  bounding boxes of the group before and after the skew in its parent's
  space (GroupRotationNodeResizer.js `rotateScale`). Rectangles, ellipses
  and polygons without Lock Aspect that end up skewed become paths, named
  Path N unless their names are custom, paths and lines take the skew in
  their points, and the other children are transformed as before
  (NodeResizers.js, SkewRotationNodeResizer.js, PathRotationNodeResizer.js,
  LineRotationNodeResizer.js).
- Every artboard XD creates has a layout grid, hidden and saved as
  `gridStyle`: the default grid for its width. Resizing an artboard adapts
  its grid to the new width (ArtboardUtil.js `updateArtboardAfterResize`,
  GridStyle.js `updateArtboardSize`), and gives an artboard without one the
  default grid for its new width, as XD's `_createGridStyle` does on first
  use.

### Components

`Component{id, name, master (a group subtree), states, default state,
link, placements}`:

- `states`: `ComponentState{id, name, tree (a group subtree),
  added (node ids), removed (node ids), overridden property names,
  interactions, size (XD `width` and `height`)}`. XD stores each state as a
  full group plus bookkeeping of how it differs from the master; the model
  keeps both.
- `link`: for components linked from another document or library
  (`linkedElementRef`, `resolvedRefName`, `linkedElementLastModified`), the
  source description. XD embeds a local copy, which the model uses; the link
  is informational until linked updates are supported.
- `placements`: the component's places among the groups of the document's
  assets, XD's `assetGroupData` on the master's `meta.ux` (the canonical
  instance's, SymbolGroupExtension.js `serializeAGC`). The component's
  library element is identified by its master's id (LibraryFolder.js
  `getElementIdForSymbol`), which the groups of the `groupComponents`
  section list. A name with slashes, given to a single node made a component
  or to a main component, files the component into the groups its parts
  name (SymbolCommands.js `createSymbolHelper`, RenameCommands.js
  `renameSymbolNode`), and the Layers panel shows a component instance
  under the names of those groups followed by its own (LayersPane.js
  `_getDisplayName`). The copies of `assetGroupData` XD leaves on instances
  are kept as opaque data.
- Deleting a component from the Document Assets panel (SymbolCommands.js
  `deleteSymbolById`, `_convertSymbolToGroup`) replaces each instance, the
  main component among them, with a new ordinary group (a new id), or with
  its only child when that child is a group or boolean group; references,
  sync maps, overrides, interactions and the selection then refer to the
  replacement. An instance mirroring another instance of the component
  within a component instance or state becomes a copy of what that instance
  became, which the model makes a reference. Deleting more than 2000 assets
  at once first asks Permanently delete (main.js `deleteElements`,
  `deleteGroups`, `MAX_REMOVED_ELEMENTS`).
- Masters and states live in `Resources`, not on the canvas. Their nodes are
  ordinary nodes in the same node store, so references can point at them.

### Resources

- `images`: `ImageId` to encoded bytes plus media type, taken from the
  package's `resources/` entries.
- `color_swatches`: named colors (`colorSwatches`, document library colors).
- `character_styles`: named text styles from the document library.

### Prototype

- `Interaction{id, owner (node id), trigger, action, is_enabled}`.
- Triggers: `Tap`, `Hover`, `Drag`, `Time{delay}`, `KeyOrGamepad{key}`.
- Actions: `ArtboardTransition{destination, transition (none, dissolve, slide
  variants, push, auto-animate), easing, duration, preserve_scroll}`,
  `Overlay{destination, position, ...}`, `StateTransition{target state}`,
  `PreviousArtboard`, `VideoPlayback{...}`.
- `home_artboard`: the flow's starting point.

Interactions from both the node metadata and `interactions.json` merge into
this table.

## Storage

- Nodes live in a `NodeStore` owned by the document: a slot array of node
  records with a free list, plus an open addressing index from `NodeId` to
  slot (linear probing over 8-byte buckets holding a slot and a hash tag).
  Parent and child links are stored as `NodeId`s.
- Node records are immutable values. Changing a node replaces its record.
  Large payloads (paths, text content, images) are held by
  `std::shared_ptr<const T>`, so replacing a record never deep-copies them and
  undo keeps the old value cheaply.
- Records stay small whatever their kind: the artboard, shape and text
  payloads are each held in a `Box` and a group's roles in an `OptionalBox`
  (Box.hpp), single pointers to a value shared by copies and copied only when
  modified while shared. A plain group or a reference is held inline.
- Reads go through a `const` API: lookup by id, parent, children, and typed
  accessors for each payload. Readers never see a partially applied
  transaction.

## Changes

- All mutation happens inside a `Transaction`: insert, remove, move
  (reparent or reorder), and replace a node's header or payload, plus
  resource and prototype edits.
- Committing a transaction publishes one `ChangeSet`: inserted, removed and
  moved node ids, and for modified nodes a mask of what changed (transform,
  geometry, style, text, visibility, children, role data, and so on).
  Observers subscribe with a callback and receive change sets in commit
  order; a subscription is released through the handle it returns.
- Each committed transaction records the previous records it replaced, as one
  revision. Undo and redo swap them back as a transaction of their own, so
  observers see ordinary change sets. `commit` reports whether it recorded a
  revision, and the document exposes its undo and redo counts.
- A transaction may instead `amend` the most recent revision: its changes are
  published as usual, but they join that revision, so a single undo reverts
  both; a revision left with no net change is discarded. The commands
  layer's history merges consecutive steps this way.
- The document exposes the records the most recent revision replaced
  (`get_original_records`), so that the commands layer can compare a step's
  result with the state before it.
- The commands layer (separate module) builds named user actions on top of
  transactions; the model itself knows nothing about tools or UI.

## Opaque data

`OpaqueData` is an immutable, shared string of bytes that a document format
keeps without the model interpreting it: a single pointer, null when empty.
Every `NodeHeader` has one (`m_opaque`), and so does the document as a whole
(`Document::get_opaque`/`set_opaque`, set outside transactions because it is
never edited). Opaque data travels with its node through every edit, copy and
undo and disappears only with the node. The model compares it (it is part of
structural equality) but never reads it.

The XD importer and writer (in `Io`) define its content. For a node it is the
patch turning the JSON the writer generates from the model into the JSON
read from the file: members the model does not represent, plus encoding
details of modelled members guarded by a hash of the generated value so that
they apply only while the modelled value is unchanged (an edit always wins).
For the document it is the manifest's JSON text as read, the entries the
model does not use (previews, renditions, metadata, sharing, unknown
entries), the archive order and the patches of the entries' roots. The
writer edits the manifest in place, keeping its member order and XD's
compact formatting, so that a document saved without changes keeps the
manifest XD wrote, apart from what XD itself changes on save: the `name`
(the file's name followed by U+0001), a new `uxdesign#serializationId`,
`uxdesign#id`, `uxdesign#parentDocId` and `uxdesign#compositeID`
(DocumentCommands.js `saveToComposite`, serialization.js `serializeToDCX`,
SerializationUtil.js `serializeDocAndParentId`), and `uxdesign#version`
"2.33" and `uxdesign#applicationVersion` "61.0.12.1". Save As to another
file makes a new DCX composite, as `saveLocalDocument` does with
`dcx.createEmptyComposite`: a new manifest `id` and `uxdesign#compositeID`,
new ids for every folder and component of the manifest (derived from the old
ones and the composite, so later saves keep them); the copy also gets a new
`uxdesign#id` and keeps `uxdesign#parentDocId`, or takes its new id as its
parent when there is none.

Every save also rewrites the XMP in META-INF/metadata.xml as XD 61's XMP
toolkit serializes it (`x:xmptk` "Adobe XMP Core 9.0-c001 ...", attribute
form, LF line endings, 2048 bytes of padding). Dates are the save time in
the local time zone, with milliseconds stripped of trailing zeros
(`2026-10-19T23:45:45.2-04:00`, `Z` for UTC). A save over the file sets
`ModifyDate` and `MetadataDate` to the time, `InstanceID` to a new id and the
`dc:title` to the name (U+0001 becomes a space), and adds a `saved` event by
`XD` to the history, or updates the instance and time of the last event when
it already is a `saved` event by `XD` (no corpus file has two consecutive
`saved` events by the same agent); everything else is kept. A save to a new
file writes a fresh XMP: `CreatorTool` XD, the three dates equal, the new
composite id as `DocumentID` and `OriginalDocumentID`, and a `created` event
(the composite id) followed by a `saved` event (the new `InstanceID`). Writes
that are not saves, such as the corpus round trip, record no save in the XMP
and keep the versions as read.

## What the importer does

The XD importer (in `Io`) builds a document from an `.xd` package:

1. Read `manifest`, `resources/graphics/graphicContent.agc` (components,
   swatches, artboard frames, gradients), every
   `artwork/*/graphics/graphicContent.agc`, `interactions/interactions.json`
   and the image resources.
2. Create nodes depth-first, preserving z order and ids, and translate each
   XD field into the model as described above. `syncRef` becomes
   `Reference`. The component masters, then the top-level canvas nodes, are
   translated in parallel and inserted in order, with the same result as
   importing them one after another.
3. Report every JSON key it does not recognise, with counts. That report is
   the import coverage metric and must reach zero for the golden documents.
4. Keep everything else as opaque data, so that writing an unchanged
   document reproduces the package's JSON.

## Implementation notes

The implementation (Include/Rellu/Model) follows this design with these
refinements:

- Geometry, fill and stroke type names carry suffixes (`RectangleShape`,
  `NoFill`, `StrokeCap`, ...) to avoid clashes with Windows and renderer names.
- `BlendMode` and the gradient geometry (`GradientStop`, `LinearGradient`,
  `RadialGradient`) live in `RelluGeometry`, like the stroke enums, because
  the renderer's draw lists use them too.
- Parent and children belong to the node record managed by the `NodeStore`,
  not to payloads.
- A transaction applies its edits immediately, so its owner reads them back
  through the normal API; observers and undo see only the committed result.
  A transaction destroyed without committing reverts.
- Bulk construction is linear: a transaction marks the store slots it has
  recorded instead of keeping a set of touched ids, and its change set only
  sorts inserted nodes by depth when nodes were moved, since inserts
  otherwise arrive parents first. A change set is copied for publication
  only when there are observers.
- Equality of node data treats NaNs as equal (`equals` in
  Geometry/Equality.hpp), so a document compares equal to a copy of itself
  even when it holds NaNs; every other value compares as with `==`.
- Interactions of component states live in the prototype table, owned by the
  state's root node.
- `ComponentInstance` also records the instance size, sync map and its own
  state trees, as XD stores them. The state trees are not validated on
  insert or replace: consumers must tolerate a state whose tree is missing
  or is not a group root.
- `ComponentInstance::m_is_main` marks the main component on the canvas
  (XD `isMaster`). The master in `Resources` stays the component's
  definition; the commands layer keeps the two in step
  (`refresh_components`, Commands/Components.hpp): an edit of the main
  component changes the master, or the tree of the state the main
  component shows, and then every instance except the properties an
  instance overrides; an edit of any other instance adds the XD names of
  the properties it changed to the instance's overrides. The header of a
  tree's root (the root of an instance, of the main component or of a
  master) is its own, as in XD: it is neither synced nor recorded as an
  override, except that a nested instance's root records its overrides
  against the node it mirrors in the outer tree. An instance mirrors
  the master or, in a state other than the default, that state's tree: its
  sync map maps its nodes to the nodes of that tree.
- `resolve_reference` (ReferenceResolution.hpp) is the single rule for what a
  reference renders: the final non-reference source, with the nearest
  transform and children overrides.
- The model stores compound results as given. The commands layer recomputes
  them (`refresh_compounds`, Commands/Compounds.hpp): each command's step
  also recomputes the compound shapes whose operands it moved, reshaped,
  added or removed, nested ones first, so that one undo reverts both. The XD
  session skips compound shapes with children the importer could not
  represent, as the writer does.
- Layouts are recomputed the same way (`refresh_layouts`,
  Commands/Layouts.hpp), innermost first, only for the stacks, padding
  groups and repeat grids a step changed. A stack's order is the reverse of
  its children's z order (the topmost child comes first), its background is
  the bottom-most child, hidden children take no space, and the last item
  has no spacing entry. Measuring is by geometric bounds. Edits keep the
  stored gaps and the stack's start: items after a resized item shift by
  the change, removed items close up with the spacing of the item before
  them, inserted items take the most common spacing, and an item that was
  only moved takes the place in the order its center calls for. A padding
  background is kept around the content (edits of the background itself
  are undone, as in XD). A repeat grid's cells are complete copies in row
  major order, cell `i` translated by its column and row pitch from the
  first; an edit in one cell is repeated in every other cell except text
  content and image fills, the cell size follows the first cell's content,
  and new cells copy the nearest existing cell. XD keeps text data series
  as `repeatOverrides` on each text node, which the model leaves opaque.

## Open questions

- How XD clamps corner radii larger than half a side (geometry currently
  clamps each radius to half the shorter side).
- How XD rounds polygon corners.
- Whether any XD text uses characters outside the Basic Multilingual Plane
  (none in the corpus), which would confirm the UTF-16 offset assumption.
- How XD orders a stack turned on from the inspector (the plugin API keeps
  the z order; Rellu orders by position) and what new cells copy when
  columns are added (rows copy the last cell).
- Where XD places the inner vertices of a star stretched to a box that is
  not equilateral, and whether the corner radius rounds them (no star in
  the corpus).
- Whether XD draws vertically flipped text mirrored (the After Effects
  export mirrors it about the middle of its bounds; the only flipped text
  in the corpus is hidden).
