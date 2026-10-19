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

The common header holds: `id`, `name`, `is_visible`, `is_locked`, `transform`,
`opacity`, `blend_mode`, `is_isolated` (XD `isolation: isolate`),
`constraints` (responsive resize, below) and `is_marked_for_export`.

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
| Component instance | `meta.ux.symbolId`, `stateId`, `syncMap`, `symbolOverrides` | component, active state, override bookkeeping |

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
master therefore updates every reference to it.

### Shapes

A shape's geometry is one of:

| Geometry | XD `shape.type` | Data |
|---|---|---|
| `RectangleShape` | `rect` | rect, four corner radii (XD order: top-left, top-right, bottom-right, bottom-left) |
| `EllipseShape` | `ellipse`, `circle` | center, x and y radii |
| `LineShape` | `line` | two points |
| `PolygonShape` | `polygon` | corner count, size, corner radius, vertices |
| `PathShape` | `path` | `Path` (parsed SVG data) and fill rule |
| `CompoundShape` | `compound` | `CompoundOperation`, operand shapes (child shape nodes), cached result `Path` |

Geometry converts to a `Path` with the `RelluGeometry` builders
(`make_rectangle`, `make_ellipse`, `make_polygon`, `parse_svg_path`,
`combine`). A compound shape keeps its operands so it stays editable; the
cached result is XD's stored `path`, recomputed when an operand changes.

### Style

`Style` belongs to shapes, text and artboards (artboards use only the fill):

- `fill`: one of `NoFill`, `SolidFill{color, swatch}`,
  `GradientFill{kind (linear, radial), stops (offset, color), geometry
  (x1 y1 x2 y2 for linear; cx cy r fx fy for radial), units
  (object bounding box or user space), transform}`,
  `ImageFill{image, scale behaviour (fill, cover), offset, scale, flips}`.
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

### Artboards

`Artboard{frame, name, background (fill), viewport_height (scrollable
artboards), layout_grid (columns, gutter, margins, column/row strokes,
visibility), guides, children}`. The frame comes from the resource
`artboards` table, keyed by artboard id.

### Responsive resize constraints

`Constraints{is_enabled, left, right, top, bottom, width, height}` as XD's
`constraint*` booleans, with `is_custom` for `customConstraints`. They only
affect how a node responds when its parent is resized; they never move
anything at load time.

### Components

`Component{id, name, master (a group subtree), states, default state,
link}`:

- `states`: `ComponentState{id, name, tree (a group subtree),
  added (node ids), removed (node ids), overridden property names,
  interactions}`. XD stores each state as a full group plus bookkeeping of how
  it differs from the master; the model keeps both.
- `link`: for components linked from another document or library
  (`linkedElementRef`, `resolvedRefName`, `linkedElementLastModified`), the
  source description. XD embeds a local copy, which the model uses; the link
  is informational until linked updates are supported.
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
For the document it is the manifest, the entries the model does not use
(previews, renditions, metadata, sharing, unknown entries), the archive
order and the patches of the entries' roots.

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
- `resolve_reference` (ReferenceResolution.hpp) is the single rule for what a
  reference renders: the final non-reference source, with the nearest
  transform and children overrides.
- The model stores compound results as given. The commands layer recomputes
  them (`refresh_compounds`, Commands/Compounds.hpp): each command's step
  also recomputes the compound shapes whose operands it moved, reshaped,
  added or removed, nested ones first, so that one undo reverts both. The XD
  session skips compound shapes with children the importer could not
  represent, as the writer does.

## Open questions

- How XD clamps corner radii larger than half a side (geometry currently
  clamps each radius to half the shorter side).
- How XD rounds polygon corners.
- Whether any XD text uses characters outside the Basic Multilingual Plane
  (none in the corpus), which would confirm the UTF-16 offset assumption.
- Exact stack and padding recomputation rules after an edit; until they are
  needed, imported layouts are taken as given.
