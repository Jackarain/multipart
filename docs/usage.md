# Usage & API reference

This document describes the public API of `multipart.hpp`.

All symbols live in the `multipart` namespace. The header is header-only and
self-contained: `#include <multipart/multipart.hpp>`.

---

## 1. Types

### `template <typename StringType> struct basic_part`

The polymorphic node of a multipart tree. It holds **either**:

- `content_type` — the part body (`StringType`), or
- `list_type` — a `std::list<basic_part>` of child parts (a nested
  `multipart/*` document),

plus a list of MIME headers (`prototype()`) and a boundary string.

```cpp
using part      = basic_part<std::string>;        // owning
using lazy_part = basic_part<std::string_view>;   // zero-copy views
```

Aliases inside `basic_part`:

| Name            | Type                                        |
|-----------------|---------------------------------------------|
| `data_type`     | `enum class { content_t, list_t, undefined_t }` |
| `content_type`  | `StringType`                                |
| `string_type`   | `StringType`                                |
| `list_type`     | `std::list<basic_part>`                     |
| `keyvalue_type` | `std::pair<string_type, string_type>`       |
| `prototype_type`| `std::vector<keyvalue_type>`                |

#### Type inspection

```cpp
part::data_type type() const;  // content_t | list_t | undefined_t
bool is_content() const;
bool is_list() const;
```

#### Accessors

```cpp
content_type&  content();                 // auto-constructs a content part if undefined;
                                          // throws/asserts if it is a list
content_type   content() const;           // same, but const (throws std::runtime_error)
list_type&     list();                    // auto-constructs a list part if undefined
list_type      list() const;
```

> In builds **with** exceptions, calling `content()` / `list()` on the wrong
> type throws `std::runtime_error`. In `-fno-exceptions` builds it becomes an
> assertion (`NDEBUG` builds assert nothing — don't misuse the accessors).

```cpp
std::string&       boundary();
const std::string& boundary() const;
void set_boundary(std::string b);   // adds "--" prefix if missing; "" clears it
```

The boundary is stored **in the library's internal form, with a leading `--`**
(e.g. `"--myboundary"`). See [design.md](design.md) for details.

#### Header helpers

Headers are stored as an ordered list of `(name, value)` pairs, preserving
the order they appeared in. All lookups are case-insensitive.

```cpp
string_type header(std::string_view name) const; // first match or ""
bool        has_header(std::string_view name) const;
void        add_header(string_type name, string_type value);
void        set_header(string_type name, string_type value); // replace first match, else append
void        remove_header(std::string_view name);            // removes all matches
prototype_type&       prototype();        // direct access to the raw list
const prototype_type& prototype() const;
```

#### Construction & assignment

```cpp
basic_part();                              // undefined_t
basic_part(content_type v, prototype_type p = {});
basic_part(list_type v, prototype_type p = {});
explicit basic_part(data_type t);

basic_part(const basic_part&);             // deep copy
basic_part(basic_part&&) noexcept;
basic_part& operator=(const basic_part&);  // deep copy
basic_part& operator=(basic_part&&) noexcept;
basic_part& operator=(content_type v);
basic_part& operator=(list_type v);

bool operator==(const basic_part&, const basic_part&) const; // deep, structural
bool operator!=(const basic_part&, const basic_part&) const;
```

`operator==` compares type, boundary, headers **and** content recursively, so
it is convenient for tests and assertions.

---

## 2. Decoding

```cpp
// Owning decode (copies all content into std::string).
template <typename Entry, typename InIt>
Entry decode(InIt start, InIt end, bool& ok, event_cb ecb = {});
template <typename Entry, typename InIt>
Entry decode(InIt start, InIt end, event_cb ecb = {});

// Same, but with an explicit expected boundary (recommended when the
// Content-Type header is available).
template <typename Entry, typename InIt>
Entry decode(InIt start, InIt end, std::string_view expected_boundary,
             bool& ok, event_cb ecb = {});
template <typename Entry, typename InIt>
Entry decode(InIt start, InIt end, std::string_view expected_boundary,
             event_cb ecb = {});

// Container overloads (anything with begin()/end()).
template <typename Entry, typename Container>
Entry decode(const Container& c, bool& ok, event_cb ecb = {});
template <typename Entry, typename Container>
Entry decode(const Container& c, std::string_view expected_boundary,
             bool& ok, event_cb ecb = {});
template <typename Entry, typename Container>
Entry decode(const Container& c, event_cb ecb = {});
template <typename Entry, typename Container>
Entry decode(const Container& c, std::string_view expected_boundary,
             event_cb ecb = {});

// Convenience overloads for std::string_view.
part decode(std::string_view data, bool& ok, event_cb ecb = {});
part decode(std::string_view data, std::string_view expected_boundary,
            bool& ok, event_cb ecb = {});
part decode(std::string_view data, event_cb ecb = {});
part decode(std::string_view data, std::string_view expected_boundary,
            event_cb ecb = {});

// Zero-copy variant: Entry == lazy_part.
lazy_part decode_lazy(std::string_view data, bool& ok, event_cb ecb = {});
lazy_part decode_lazy(std::string_view data, std::string_view expected_boundary,
                      bool& ok, event_cb ecb = {});
lazy_part decode_lazy(std::string_view data, event_cb ecb = {});
lazy_part decode_lazy(std::string_view data, std::string_view expected_boundary,
                      event_cb ecb = {});
```

Notes:

- **Iterator requirements**: the input iterators must be *random access*
  (the parser performs random lookahead). `std::string`, `std::string_view`,
  `std::vector<char>`, `std::deque<char>`, and raw pointers all qualify.
  Buffered input from a stream first if you use `istreambuf_iterator`.
- **`ok`**: reports whether the body parsed successfully. On failure it
  returns the **partially parsed** tree (parts completed before the error)
  and `ok == false` — it never throws. Use the `bool&` overload (or
  `event_cb::error_`) to distinguish success from failure.
- **Expected boundary**: pass the value from `extract_boundary()` (or
  `make_content_type()`) to decode *exactly* and skip the body-discovery
  heuristic. This correctly handles documents whose preamble contains `--`
  sequences and **empty documents** (`--b--\r\n` with zero parts). Both the
  token form (`"b"`) and the library-internal form (`"--b"`) are accepted.
- **Result shape**: a successfully parsed document is always a `list_t`
  (even a single-part body).
- **Nesting**: a part whose `Content-Type` is `multipart/*` (case-insensitive
  match, boundary extracted from the header value) becomes a `list_t` child;
  the declared inner boundary is used to parse it.

---

## 3. Encoding

```cpp
template <class OutIt, class Entry>
int encode(OutIt out, const Entry& e);   // writes bytes, returns byte count

template <class Entry>
std::string encode(const Entry& e);      // convenience: returns a std::string
```

`encode` accepts both `part` and `lazy_part`. The iterator is taken by value;
use the **returned byte count** to advance a raw iterator:

```cpp
std::vector<char> buf(1024);
char* it = buf.data();
int n = encode(it, doc);
it += n;                        // encode() did NOT advance the caller's it
```

The encoder is structure-aware:

- A `list_t` node is a multipart document.
- A `content_t` child inside a document is one body-part.
- A `list_t` child inside a document is a *nested multipart document*: it is
  wrapped in the parent's boundary, its own headers are written as that part's
  headers, and its body is serialized as an inner `--inner...--inner--`
  document.

Byte-exact round-trips are guaranteed for anything produced by `decode()`
(this is covered by the test suite).

---

## 4. Streaming events

```cpp
struct event_cb {
    callback_func boundary_;      // called with "--boundary" when a boundary line is seen
    callback_func header_field_;  // header name (before ':')
    callback_func header_value_;  // header value
    callback_func part_data_;     // a part's body bytes
    callback_func error_;         // called once if parsing fails
};
// callback_func = std::function<int(const std::string_view&)>
```

Callbacks receive **non-owning views**:

- With `decode_lazy`, the views point into the original input buffer and stay
  valid as long as that buffer lives.
- With the owning `decode`, `part_data_`/`header_*` views point into a
  temporary that is only valid **during the callback** — do not store them.

The `boundary_` callback fires once per (nested) document, not per part.

---

## 5. Helpers

```cpp
// Random boundary in the library's internal form (leading "--"):
std::string make_boundary();

// "multipart/form-data; boundary=<b>" (strips the leading "--" if present).
// Boundaries containing characters outside RFC 2046 bcharsnospace are quoted
// (and " / \ are escaped) per RFC 2046 quoted-string:
std::string make_content_type(std::string_view boundary,
                              std::string_view subtype = "form-data");

// Does the Content-Type describe a multipart/* document?
bool is_multipart(std::string_view content_type);

// Extract the "boundary=" parameter (RFC form, no leading "--"; "" if none).
// Note: a quoted boundary is returned with the quotes removed but without
// unescaping embedded quotes/backslashes:
std::string extract_boundary(std::string_view content_type);

// One-call form builders:
part make_field(std::string name, std::string value);
part make_file(std::string name, std::string filename,
               std::string content_type, std::string data);
```

### Putting it together

```cpp
using namespace multipart;

// Decode side: get the boundary from the HTTP Content-Type header.
std::string boundary = extract_boundary(http_content_type); // e.g. "abc"
part form = decode(body, boundary, ok); // expected-boundary decode
form.set_boundary(boundary);            // for re-encoding with the same boundary

// Encode side: fresh random boundary.
const std::string b = make_boundary();
part request;
request.set_boundary(b);
request.list().push_back(make_field("user", "alice"));
request.list().push_back(make_file("f", "a.txt", "text/plain", data));
std::string body = encode(request);
std::string ct   = make_content_type(b);
```

---

## 6. Error handling & exceptions

- Malformed input **never throws**: pass a `bool&` (or `event_cb::error_`) to
  learn whether parsing failed.
- Accessing `content()` / `list()` on the wrong type throws
  `std::runtime_error` (when exceptions are enabled).
- `multipart::throw_type_error()` is the single throw point; it aborts in
  `-fno-exceptions` builds.
- Nesting depth is limited to 64 levels (`detail::max_recursive_depth`); deeper
  documents are rejected as parse errors.

## 7. Debugging

When compiled with `_DEBUG` or `DEBUG` defined, every part gains a debug
method:

```cpp
#if defined(_DEBUG) || defined(DEBUG)
void print(std::ostream& os, int indent = 0) const;  // dumps the tree
#endif
```