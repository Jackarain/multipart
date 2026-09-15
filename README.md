# multipart

A small, self-contained, header-only **C++17** library for parsing and
serializing MIME `multipart/*` documents (most notably
`multipart/form-data`, used for HTML form submissions and file uploads).

It has **zero dependencies**, works with or without exceptions, and supports
zero-copy (`std::string_view`-based) decoding.

```cpp
#include <multipart/multipart.hpp>
```

## Features

- **Parse** `multipart/form-data`, `multipart/mixed`, `multipart/related`, …
  documents without needing the `Content-Type` header — the boundary is
  discovered directly from the body.
- **Serialize** documents back to bytes (byte-exact round-trips).
- **Zero-copy** decoding via `lazy_part` (`std::string_view` slices that point
  into the original buffer).
- **Streaming events** (`event_cb`) for memory-friendly processing of large
  payloads.
- **Nested** multipart documents (a part whose body is itself `multipart/*`),
  including multi-level nesting.
- **Preamble / epilogue** tolerated per RFC 2046.
- **Helpers**: random boundary generation, `Content-Type` construction,
  boundary extraction, and one-call form-field/file part builders.
- **No exceptions required**: the whole library compiles and works with
  `-fno-exceptions` (see `tests/test_no_exceptions.cpp`).
- C++17+, single header, no dependencies, no macros required.

## Requirements

- A C++17 (or newer) compiler. Tested with GCC, Clang (also under
  ASan/UBSan). MSVC is expected to work (C++17 mode).
- CMake ≥ 3.16 for the build system (the header itself has no build step).

## Integration

### CMake (FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(multipart
    GIT_REPOSITORY https://github.com/wanghanqing/multipart
    GIT_TAG        v1.0.0)
FetchContent_MakeAvailable(multipart)

target_link_libraries(my_app PRIVATE multipart::multipart)
```

### CMake (find_package after install)

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
cmake --install build
```

```cmake
find_package(multipart REQUIRED)
target_link_libraries(my_app PRIVATE multipart::multipart)
```

### Plain copy

Drop `include/multipart/multipart.hpp` into your project and add the include
path:

```bash
c++ -std=c++17 -I/path/to/multipart/include my_app.cpp
```

## Quick start

### Decode a `multipart/form-data` body

```cpp
#include <multipart/multipart.hpp>
#include <iostream>

int main() {
    std::string body = "request body received over the network...";

    bool ok = false;
    multipart::part form = multipart::decode(body, ok);
    if (!ok) return 1;                        // malformed body

    std::cout << "boundary: " << form.boundary() << "\n";
    for (const auto& part : form.list()) {
        std::cout << part.header("Content-Disposition") << "\n";
        std::cout << part.content() << "\n";
    }
}
```

### Build a file-upload request

```cpp
using namespace multipart;

const std::string boundary = make_boundary();

part form;
form.set_boundary(boundary);

part field = make_field("comment", "please review");
field.set_boundary(boundary);

part file = make_file("file", "report.pdf", "application/pdf", pdf_bytes);
file.set_boundary(boundary);

form.list().push_back(std::move(field));
form.list().push_back(std::move(file));

std::string request_body = encode(form);
std::string content_type = make_content_type(boundary); // for the HTTP header
```

### Zero-copy decode

```cpp
std::string body = /* ... */;
multipart::lazy_part doc = multipart::decode_lazy(body);

std::string_view payload = doc.list().front().content();
// payload points *into* body — no copy was made.
// NOTE: body must outlive doc.
```

## Documentation

- [README](README.md) — this page
- [docs/usage.md](docs/usage.md) — API reference and examples
- [docs/design.md](docs/design.md) — data model, memory semantics, nesting,
  RFC notes and known limitations

## Building the tests & examples

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Options:

| Option                    | Default | Description                        |
|---------------------------|---------|------------------------------------|
| `MULTIPART_BUILD_TESTS`   | `ON`    | Build the GoogleTest test suite    |
| `MULTIPART_BUILD_EXAMPLES`| `ON`    | Build the examples                 |
| `MULTIPART_INSTALL`       | `ON`    | Install rules + CMake package      |
| `MULTIPART_PEDANTIC`      | `ON`    | `-Wall -Wextra -Wpedantic` / `/W4` |

The test suite is also verified under `-fno-exceptions -fno-rtti` and with
AddressSanitizer + UndefinedBehaviorSanitizer.

## License

Distributed under the MIT License. The original parser (c) 2021 Jack
(<jack.wgm@gmail.com>); see [LICENSE](LICENSE).