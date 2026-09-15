# Design notes

This document explains the internal data model, memory semantics, parsing
strategy and the known limitations of `multipart.hpp`.

## 1. Data model

A document is a tree of `basic_part` nodes. A node is a small variant:

```
+-----------------------------------------------+
| basic_part                                     |
|   data_type type_            content_t|list_t  |
|   unsigned char data_[union]  the payload     |
|   prototype_                  headers (ordered)|
|   boundary_                   boundary string  |
+-----------------------------------------------+
```

- A **`content_t`** node stores a body (`std::string` for `part`,
  `std::string_view` for `lazy_part`).
- A **`list_t`** node stores a `std::list<basic_part>` of children and
  represents a `multipart/*` document.
- `undefined_t` is the default (moved-from / not yet constructed) state.

The payload lives in a manually managed, aligned byte buffer
(`alignas(std::max_align_t) unsigned char data_[...]`); only one of
`content_type`/`list_type` is constructed at a time. `list_type` is a
`std::list` precisely so that the recursive type is legal and nodes are
stable.

## 2. Boundary representation

The library stores boundaries **with** a leading `--` (the literal text that
appears in the body), e.g. `"--myboundary"`. This differs from the RFC form
found in `Content-Type: ...; boundary=myboundary` (no dashes). The helpers
bridge the two forms:

| Want                                             | Use                            |
|--------------------------------------------------|--------------------------------|
| Body text delimiter (with `--`)                  | `part::boundary()` (as stored) |
| `Content-Type` boundary parameter (no `--`)      | `make_content_type(boundary)`  |
| Pull the RFC-form parameter out of a header      | `extract_boundary(content_type)` |
| Assign either form to a part                     | `part::set_boundary(b)`        |

## 3. Parsing strategy

`decode` is a single-pass, character-oriented state machine
(`detail::decode_recursive`). It does **not** need the `Content-Type` header:
it discovers the boundary from the first `--...\r\n` line of the body.

States:

```
s_start          skip preamble until "--boundary"
s_start_boundary consume boundary until CRLF
s_header_field   read a header name until ':'
s_header_value   read a header value until CRLF
s_part_data      read the part body until "\r\n--boundary" (next part)
                 or "\r\n--boundary--" (final delimiter)
```

Key behaviors:

- **Boundary lookup** compares `\r\n--boundary` / `\r\n--boundary--` against
  the raw bytes without allocating temporaries; the string boundary-looking
  text that is not the real delimiter is treated as content.
- **Header values** have a single leading space trimmed (`X: v` -> `v`).
  Empty values are preserved (`X:` -> `""`) and re-encoded byte-exactly.
- **Nested multipart**: when a part's header is `Content-Type: multipart/*`
  (matched case-insensitively), `decode_recursive` is re-entered on the inner
  document and the part becomes a `list_t`.
- **Result is always a list**: RFC 2046's grammar is
  `1*encapsulation`, so a valid document has at least one part. A single-part
  body therefore decodes to a `list_t` with one element (this also prevents
  nested single-part documents from collapsing and losing the outer
  structure).
- **Depth limit**: recursion is bounded by `max_recursive_depth = 64`.
- **Preamble** before the first boundary is skipped; **epilogue** after the
  closing delimiter is ignored. (Document-level MIME headers that precede the
  first boundary are treated as preamble and are not captured.)

### Memory safety

The parser only dereferences iterators it can see ahead on (the
`remaining >= boundary.size() + 2` guard prevents out-of-bounds reads of the
final-delimiter suffix). Malformed input is reported via the `ok` flag and
`event_cb::error_`, and **never throws**.

## 4. Ownership semantics

`basic_part<StringType>` is parameterized by the string type:

| Alias       | StringType        | Who owns the bytes?                          |
|-------------|-------------------|----------------------------------------------|
| `part`      | `std::string`     | The part owns copies of all data.            |
| `lazy_part` | `std::string_view`| Nobody — views reference the source buffer.  |

With `decode_lazy`, content, header names and header values are `string_view`
slices of the input range, so decoding is **O(1) in allocations**. The caller
must keep the source buffer alive for at least as long as the `lazy_part`
tree (and any views obtained from it).

The same applies to event callbacks: views passed to `event_cb` are
non-owning (see [usage.md](usage.md)).

## 5. Serialization strategy

`encode` walks the tree with two roles:

- `encode_document` — serializes a whole document. For a `list_t` it emits
  each part (each beginning with the boundary delimiter) and finishes with the
  closing `--boundary--`.
- `encode_part` — serializes one part inside a document. A `content_t` child
  emits `--b\r\n<headers>\r\n\r\n<body>\r\n`. A `list_t` child emits
  `--b\r\n<its own headers>\r\n\r\n<inner document>`, where the inner
  document is itself emitted by `encode_document` (with the child's boundary).

This guarantees that `encode(decode(x)) == x` for every document the parser
accepts — verified by the round-trip tests and the property-based random test.

## 6. Known limitations

- Input iterators must be **random access** (the parser does random lookahead).
  Streams must be buffered first.
- The boundary is discovered from the body, not from `Content-Type`. A
  malformed body that contains an accidental leading `--...\r\n` line is
  treated as the boundary and will likely fail to parse (reported as an error).
- Header **folding** (obs-fold continuation lines, RFC 7230) is not supported;
  a continuation line is treated as the start of a new header.
- `multipart` documents with **zero** parts (bare `--b--\r\n`) are rejected.
- Encoded output always uses `\r\n` line endings.
- Nested depth is capped at 64.
- The `part_data_` event callback receives a view into a temporary during the
  owning `decode()`; it must not be retained beyond the callback.