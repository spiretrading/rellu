# Rellu document format

This is the specification of Rellu's native file format (`.rellu`),
implemented by `RelluFormat` (Include/Rellu/Format, Source/Format). It stores
a `Document` (see `model.md`) losslessly: loading a saved file reproduces
every node, header, payload, role, stored text layout, component, resource
(including the original image bytes) and prototype construct, with every id
preserved, so the result compares structurally equal to what was saved.

## API

```cpp
#include "Rellu/Format/DocumentFile.hpp"

save(document, path);              // atomic: writes path.saving, then renames
save(document, stream, options);   // options: compression level, threads
load(path, document);              // document must be empty
load(stream, document);

#include "Rellu/Format/DocumentComparison.hpp"

find_differences(left, right, limit);  // descriptions, empty iff equal
is_equal(left, right);
```

- Every failure to read or write throws `FormatError`
  (Rellu/Format/FormatError.hpp). Loading into a document that is not empty
  throws `std::invalid_argument`.
- `load` decodes and validates the whole file before it touches the
  document, then inserts everything through one `Transaction`, commits it and
  clears the undo history. If anything fails the transaction is reverted, so
  a failed load never leaves a half-loaded document.
- `save` to a path writes `<path>.saving` and renames it over the target
  only once it is complete, so a failed save never destroys the previous
  file.
- `FORMAT_VERSION` is the version written; `FORMAT_EXTENSION` is `.rellu`.

## Design choices

**A chunked binary container, not a ZIP of JSON.** XD itself is a ZIP of JSON;
importing the largest corpus document (1.1M nodes, 98 MB) takes about 7 s,
most of it parsing JSON text. A binary encoding of the model's plain value
types needs no parsing or number formatting, and a container written by us
gives full control over parallelism, validation and compatibility without
depending on Qt's private ZIP classes (`QZipReader` reads whole entries, has
no ZIP64 and no per-entry integrity guarantees beyond its own CRC handling).

**Chunks, like PNG.** The file is a short header followed by a sequence of
self-delimiting chunks, each with a type, flags, sizes and a CRC-32. This
makes the format:

- *streamable*: it is written and read strictly front to back, so it works
  on any `std::ostream`/`std::istream`, no seeking needed;
- *parallel*: the node tree is split into independently compressed blocks
  that are encoded and decoded on all processors;
- *extensible*: a reader skips ancillary chunks it does not know (see
  "Versioning");
- *robust*: every byte, including each chunk header, is covered by a
  checksum, sizes are bounded before anything is allocated, and the end of
  the file is marked explicitly, so truncation and corruption are detected.

**Deflate, per chunk, only where it pays.** Node blocks, resources and the
prototype are deflate compressed (level 6 by default, through the zlib Qt
already links as `Qt6BundledZLIB`, header `<QtZlib/zlib.h>`). Measured on the
corpus, level 6 is 5 to 8% smaller than level 1 and still costs only a few
milliseconds per block; level 9 is about 3 times slower for under 1% more.
A chunk is stored uncompressed if compression does not make it smaller.
Image bytes are always stored exactly as encoded (PNG, JPEG, ...), never
re-encoded or recompressed. The identifier table is stored uncompressed
because ids are random 128-bit values that deflate cannot shrink.

**A compact value encoding.** Integers are LEB128 varints; doubles use a
lossless "scaled integer" form that stores most coordinates in 1 to 3 bytes
(below); strings repeated within a chunk are written once and then referred
to by index; every reference to a node, component, state, swatch, character
style or image is an index into one identifier table instead of 16 bytes;
fields at their default value (identity transforms, opacity 1, normal blend,
no constraints, empty names) are omitted behind flag bits. The node tree is
stored in pre-order with a child count per node, so parent and children
links cost one varint per node.

## File layout

All fixed-size integers are little-endian.

```
file      = header chunk*            ; ends with an END chunk
header    = signature version flags  ; 16 bytes
signature = 89 52 45 4C 4C 55 0D 0A  ; "\x89RELLU\r\n"
version   = u32                      ; the format version, currently 1
flags     = u32                      ; reserved, written as 0, ignored
```

The signature's first byte is non-ASCII and it ends with CR LF, so files
mangled by 7-bit or text-mode transfers are rejected immediately.

```
chunk        = chunk-header data
chunk-header = type flags stored-size size checksum   ; 28 bytes
type         = 4 ASCII characters
flags        = u32           ; bit 0: data is raw deflate; other bits 0
stored-size  = u64           ; bytes of data that follow
size         = u64           ; bytes once decompressed (= stored-size if
                             ; not compressed)
checksum     = u32           ; CRC-32 of the first 24 header bytes followed
                             ; by the stored data
```

Readers reject sizes above 2^40, a compressed size claiming more than 1032
times its stored size (deflate's maximum ratio), and compressed chunks over
2^31 bytes. Stored data is read in pieces of at most 64 MB, so a corrupt size
can never trigger a huge allocation before the file runs out.

### Chunks

A type whose first character is an upper case letter is *critical*: a reader
that does not know it must reject the file. Any other type is *ancillary*
and is skipped by readers that do not know it.

| Type   | Count            | Compressed | Contents |
|--------|------------------|------------|----------|
| `INFO` | 1, first         | no         | counts (below) |
| `IDS ` | 1                | no         | the identifier table |
| `NODE` | `block-count`    | yes        | a run of nodes in pre-order |
| `RSRC` | 1                | yes        | components, swatches, character styles, image entries |
| `PROT` | 1                | yes        | the prototype |
| `IMAG` | `image-count`    | never      | one image's encoded bytes, in `RSRC` image order |
| `END ` | 1, last          | no         | empty; marks the end of the document |

Version 1 writers emit them in the order of the table. Readers require
`INFO` first and stop at `END `; between them the order of `NODE` chunks
matters (they must cover the pre-order sequence contiguously, in order) and
the order of `IMAG` chunks matters (they pair with the `RSRC` image entries);
the rest may appear in any order. A missing, repeated or surplus chunk is an
error. Bytes after `END ` are not read, so a document can be embedded in a
larger stream.

`INFO` holds six varints: `node-count`, `canvas-count` (the leading roots
that are top-level canvas nodes, in z order), `root-count` (all roots: the
canvas roots followed by every other parentless node, such as component
master and state trees, sorted by id), `id-count`, `block-count` and
`image-count`. Readers check `canvas-count <= root-count <= node-count <=
id-count`.

`IDS ` holds `id-count` identifiers of 16 bytes each (canonical byte
order). The first `node-count` are the node ids in pre-order; then come the
component ids, component state ids, swatch ids, character style ids and
image ids not already present. Node ids must not be nil.

## Value encoding

| Notation    | Encoding |
|-------------|----------|
| `byte`      | one byte |
| `bool`      | one byte, 0 or 1; anything else is corrupt |
| `u16`       | 2 bytes little-endian |
| `varint`    | unsigned LEB128, at most 64 bits |
| `signed`    | zigzag (`(n << 1) ^ (n >> 63)`) then varint |
| `int`       | `signed`, must fit in 32 bits |
| `count`     | `varint`; must not exceed the bytes left in the chunk |
| `enum`      | `byte`, the enumerator's value; values past the last are corrupt |
| `number`    | a double, see below |
| `string`    | `count` then that many UTF-8 bytes |
| `text`      | an interned string, see below |
| `ref`       | an identifier, see below |
| `T?`        | `bool` present, then `T` if present (unless noted as a flag bit) |
| `T*`        | `count` then that many `T` |
| `map<K, V>` | `count` then `K V` pairs in increasing key order |
| `variant`   | `byte` alternative index, then the alternative's fields |

**number.** A `varint` `v` whose low 3 bits are a tag. Tags 0 to 5 mean the
value is `n / scale` with `n` the zigzag-decoded `v >> 3` (|n| <= 2^53) and
`scale` = 1, 10, 100, 1000, 10000 or 255 respectively, computed in IEEE 754
double arithmetic. Tag 7 with `v = 7` means the 8 raw little-endian bytes of
the double follow. Tag 6 is invalid. Writers use a scaled form only when it
reproduces the exact bits of the value (so -0.0, NaN payloads, infinities
and subnormals always round-trip, falling back to raw bits); decimal
coordinates such as 12.5 or 123.45 take 1 to 3 bytes and 8-bit color
channels (`k / 255`) take 1 or 2.

**text.** Each chunk has its own string table, initially empty. `varint 0`
followed by a `string` defines the next entry; `varint k` (k >= 1) repeats
entry `k - 1`.

**ref.** `varint 0` is the nil id; `varint 1` is followed by 16 raw bytes
(an id not in the table); `varint k` (k >= 2) is entry `k - 2` of the
identifier table.

**Matrix.** A form: 0 identity (no values), 1 translation (`tx ty`),
2 general (`a b c d tx ty`), each value a `number`. The form is chosen by
comparing bits exactly, so a matrix with a -0.0 entry is stored as general.
A standalone `matrix` is the form as a `byte` followed by its values.

**Path.** `enum fill-rule`, `count` segments, each `enum type` followed by
its points as `number x, number y`: MOVE and LINE 1 point, QUADRATIC 2,
CUBIC 3, CLOSE none. Readers rebuild the path by replaying `move_to`,
`line_to`, `quadratic_to`, `cubic_to` and `close`, which reproduces the
path's whole state.

Small structs are their fields in declaration order: `Point` (x y),
`Size` (width height), `Rect` (x y width height), `Color` (red green blue
alpha), `CornerRadii` (top-left top-right bottom-right bottom-left),
`Margins` (top right bottom left), `GradientStop` (number offset, color).

### Node blocks

```
node-block = varint first   ; pre-order position of the first node
             count nodes    ; the number of node records
             node*
node       = u16 flags, varint child-count, header fields, payload
```

A node's id is entry `first + i` of the identifier table. Its children are
the next `child-count` subtrees in pre-order, which may continue into later
blocks.

| Flag bits | Meaning |
|-----------|---------|
| 0         | visible |
| 1         | locked |
| 2         | isolated |
| 3         | marked for export |
| 4         | a `text` name follows (absent means empty) |
| 5         | a `number` opacity follows (absent means 1) |
| 6         | an `enum` blend mode follows (absent means normal) |
| 7         | a `byte` of constraints follows (absent means all false) |
| 8 to 9    | the transform's matrix form |
| 10 to 12  | the kind: 0 artboard, 1 group, 2 shape, 3 text, 4 reference |
| 13 to 15  | reserved, must be 0 |

Header fields follow in the order name, transform values, opacity, blend
mode, constraints. The constraints byte holds, from bit 0: enabled, custom,
left, right, top, bottom, width, height.

Payloads:

- **Artboard**: `rect frame`, `fill background`, `byte` flags (bit 0
  viewport height, bit 1 layout grid), `number viewport-height` if flagged,
  `layout-grid` if flagged, `guide*` (`enum orientation`, `number
  position`). `layout-grid` = `enum type`, `bool visible`, `int columns`,
  `number gutter`, `number default-width`, `margins`, `color column`,
  `color row`, `size square`, `color square-column`, `color square-row`.
- **Group**: `byte` roles (bit 0 mask, 1 repeat grid, 2 scroll group,
  3 stack, 4 padding, 5 component instance; bits 6 and 7 must be 0), then
  each present role in that order:
  - mask: `(matrix, shape-geometry)*`;
  - repeat grid: `size cell`, `number column-spacing`, `number
    row-spacing`, `int columns`, `int rows`, `size`;
  - scroll group: `enum direction`, `size viewport`, `point offset`;
  - stack: `enum orientation`, `map<ref, number>` spacing, `margins
    padding`, `bool has-background`;
  - padding: `margins`, `byte` (bit 0 content aware, bit 1 has background);
  - component instance: `ref component`, `ref state`, `size`, `map<ref,
    ref>` sync map, `overrides`, `component-state*`.
- **Shape**: `variant shape-geometry`, `style`.
- **Text**: `count` UTF-16 code units followed by 2 bytes each
  (little-endian; unpaired surrogates are kept), `variant frame`
  (positioned: nothing; area: `size`; auto height: `number width`),
  `style`, `text-style base`, `run*` (`varint length`, `text-style`, `ref
  character-style`), paragraph style (`enum alignment`, `number?
  line-height`, `number spacing`), `bool` stored layout present, then the
  layout: `count` paragraphs, each `count` lines, each `count` runs of
  `varint from`, `varint to`, `point position`, `byte` (bit 0 font, bit 1
  color), `font` and `color` if flagged. Readers check that run lengths do
  not exceed the content.
- **Reference**: `ref source`, `byte` (bit 0 transform override, bit 1 has
  children), `matrix` if overridden.

Shared encodings:

- `shape-geometry` variant: 0 rectangle (`rect`, `corner-radii`),
  1 ellipse (`point center`, `number x-radius`, `number y-radius`), 2 line
  (`point start`, `point end`), 3 polygon (`int corners`, `size`, `number
  corner-radius`, `point*` vertices), 4 path (`path`, `enum fill-rule`),
  5 compound (`enum operation`, `path` result; the operands are the node's
  children).
- `style`: `variant fill`, `bool` stroke present, `stroke`, `effect*`.
- `fill` variant: 0 none; 1 solid (`color`, `ref swatch`); 2 gradient
  (`variant geometry`: 0 linear `point start, point end`, 1 radial `point
  center, number radius, point focus`; `gradient-stop*`; `enum units`;
  `matrix`); 3 image (`ref image`, `size`, `enum scale-behavior`, `point
  offset`, `number scale`, `number horizontal-scale`, `number
  vertical-scale`, `byte` (bit 0 flipped horizontally, bit 1 vertically)).
- `stroke`: `color`, `number width`, `enum alignment`, `enum cap`, `enum
  join`, `number miter-limit`, `number*` dashes, `bool enabled`.
- `effect` variant: 0 drop shadow and 1 inner shadow (`point offset`,
  `number blur`, `color`, `bool visible`); 2 blur (`number amount`, `number
  brightness`, `number fill-opacity`, `byte` (bit 0 background, bit 1
  visible)).
- `font`: `text family`, `text style`, `text postscript-name`, `number
  size`.
- `text-style`: `font`, `number letter-spacing`, `color`, `byte` (bit 0
  underlined, bit 1 struck through, bits 2 to 3 text transform, bits 4 to 5
  script; bits 6 and 7 must be 0).
- `overrides`: `ref*` added, `ref*` removed, `map<ref, ref>` reordered,
  `map<ref, text*>` properties.
- `component-state`: `ref id`, `text name`, `ref tree`, `map<ref, ref>`
  sync map, `overrides`.

### Resources (`RSRC`)

```
resources = component* swatch* character-style* image-entry*
component = ref id, text name, ref master, component-state*,
            ref default-state, bool link, [text reference, text source-name,
            number last-modified]
swatch    = ref id, text name, color, gradient-stop*
character-style = ref id, text name, text-style, number? line-height,
                  number paragraph-spacing
image-entry     = ref id, text media-type, varint size
```

Components are in id order (the model's map order), swatches and character
styles in library order, images in id order. Image entry `i` describes the
`i`-th `IMAG` chunk, whose size must equal the entry's.

### Prototype (`PROT`)

```
prototype   = interaction* ref home-artboard flow*
interaction = ref id, ref owner, variant trigger, variant action,
              bool enabled
flow        = ref artboard, text name
```

Triggers: 0 tap, 1 hover, 2 drag (no fields), 3 time (`number delay`),
4 key (`text keys`, `enum device`). Actions: 0 artboard transition (`ref
destination`, animation, `bool preserve-scroll`), 1 overlay (`ref
destination`, `point position`, animation), 2 state transition (`ref
state`, animation), 3 previous artboard (no fields), 4 video playback (`ref
target`, `enum command`). An animation is `enum transition`, `enum easing`,
`number duration`.

## Loading

1. Read the header, reject an unknown signature or a version outside
   1..`FORMAT_VERSION`.
2. Read chunks up to `END `, checking types, flags, counts and sizes.
3. Decode in parallel: each `NODE` block (checksum, inflate, decode), the
   resources, the prototype, and each image (checksum). Small documents are
   decoded on the calling thread.
4. Check that the blocks cover the pre-order sequence and that the image
   entries match the image chunks.
5. In one transaction: add the images, swatches and character styles,
   insert the nodes in pre-order (canvas roots onto the canvas, the other
   roots with `insert_root`), add the components, interactions, home
   artboard and flows; commit; clear the history. The transaction validates
   the structure (unique ids, parents that accept their children, text runs
   covering their content, component trees that are group roots,
   interaction owners and flow artboards that exist); any violation is
   reported as a `FormatError` and reverts everything.

Every decoder bounds-checks each read, validates every enumerator, variant
index, flag byte and count, and requires each chunk to be consumed exactly,
so a corrupt file fails with a message naming the chunk and the problem
instead of crashing. The test suite truncates a sample file at every byte,
flips every byte, and rewrites every byte of each uncompressed chunk with
the checksum fixed up, requiring each load to either fail with
`FormatError` (leaving the document empty) or succeed.

## Versioning

- The header's version is the format version. A reader accepts every
  version from 1 to its own `FORMAT_VERSION` and rejects any other with a
  `FormatError` naming the version, so a file from a newer Rellu fails
  cleanly instead of being misread.
- An incompatible change (a new or reordered field, a new chunk older
  readers must understand) bumps `FORMAT_VERSION`. The decoders receive the
  file's version, so the reader for version N keeps decoding versions 1 to
  N - 1 by branching on it; `save` always writes the current version.
  Upgrading an old file is therefore just loading it and saving it again.
- Additive data that older readers may safely ignore goes into new
  ancillary chunks (types starting with a lower case letter), keyed by
  pre-order node index or identifier-table index where it refers to nodes
  or resources. Such files keep the same version and remain readable by
  older builds, which skip those chunks. The header's flags word is
  likewise reserved for hints that older readers may ignore.
- New critical chunk types require a version bump; readers reject unknown
  critical chunks.

## Determinism

Saving the same document always produces the same bytes, whatever the
thread count: blocks hold a fixed number of nodes (8192), roots outside the
canvas are ordered by id, maps are written in key order, and string tables
and the identifier table are built in a fixed order.

## Measurements

Release build, Windows, the 1031-document corpus (see the FormatReport tool,
`FormatReport <corpus directory> [list file]`):

- All 1031 documents (5,664,702 nodes) round-trip with no structural
  difference. Totals: XD 1123.7 MB, Rellu 538.9 MB (ratio 0.48; per document
  median 0.11, range 0.001 to 0.99); import 65.1 s, save 5.8 s, load 17.8 s.
- `final/visual_scripting_ui_project/demo.xd` (1,105,031 nodes, 98 MB as
  XD, no images): 6.5 s to import, 0.34 s to save, 2.96 s to load, 24.2 MB
  on disk (0.25 of the XD file). Of the load, only
  about 0.15 s is reading and decoding; the rest is
  `Transaction::insert` (about 1.3 s) and `Transaction::commit` (about
  1.5 s) in the model, which the import pays too. Of the 24.2 MB, 17.7 MB
  is the identifier table: XD ids are random 128-bit UUIDs, which no
  encoding can shrink.
- Documents dominated by images are about 0.96 to 0.99 of the XD size,
  since images are stored exactly as XD stores them.
