# multipart

一个轻量、自包含、**header-only** 的 **C++17** 库，用于解析和序列化 MIME
`multipart/*` 文档（最典型的是 `multipart/form-data`，用于 HTML 表单提交和文件上传）。

**零依赖**，支持或不支持异常均可使用，并支持零拷贝（基于 `std::string_view`）解码。

```cpp
#include <multipart/multipart.hpp>
```

## 特性

- **解析** `multipart/form-data`、`multipart/mixed`、`multipart/related` 等文档，
  无需 `Content-Type` 头——boundary 直接从请求体中识别。也可传入期望的
  boundary（来自 `Content-Type`）跳过启发式识别，正确处理空文档及含 `--`
  的 preamble。
- **序列化** 文档为字节流（字节级精确的往返）。
- **零拷贝** 解码：通过 `lazy_part`（`std::string_view` 切片直接指向原始缓冲区）。
- **流式事件**（`event_cb`），适合内存敏感地处理超大载荷。
- **嵌套** multipart 文档（part 的正文本身是 `multipart/*`），支持多层嵌套。
- 兼容 RFC 2046 的 **preamble / epilogue**。
- **辅助工具**：随机 boundary 生成、`Content-Type` 构造、boundary 提取，
  以及一行代码构造表单字段/文件 part。
- **无需异常**：整个库在 `-fno-exceptions` 下也能编译运行
  （见 `tests/test_no_exceptions.cpp`）。
- C++17 及以上、单头文件、无依赖、无需定义任何宏。

## 环境要求

- 支持 C++17（或更高）的编译器。已在 GCC、Clang 上测试（含 ASan/UBSan）。
  MSVC 预期可用（C++17 模式）。
- 构建系统需要 CMake ≥ 3.16（头文件本身无需构建步骤）。

## 集成方式

### CMake（FetchContent）

```cmake
include(FetchContent)
FetchContent_Declare(multipart
    GIT_REPOSITORY https://github.com/wanghanqing/multipart
    GIT_TAG        v1.0.0)
FetchContent_MakeAvailable(multipart)

target_link_libraries(my_app PRIVATE multipart::multipart)
```

### CMake（安装后通过 find_package 使用）

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
cmake --install build
```

```cmake
find_package(multipart REQUIRED)
target_link_libraries(my_app PRIVATE multipart::multipart)
```

### 直接拷贝

把 `include/multipart/multipart.hpp` 拷入你的项目，并添加 include 路径：

```bash
c++ -std=c++17 -I/path/to/multipart/include my_app.cpp
```

## 快速上手

### 解析一个 `multipart/form-data` 请求体

```cpp
#include <multipart/multipart.hpp>
#include <iostream>

int main() {
    std::string body = "通过网络收到的请求体...";

    bool ok = false;
    multipart::part form = multipart::decode(body, ok);
    if (!ok) return 1;                        // 请求体格式非法

    std::cout << "boundary: " << form.boundary() << "\n";
    for (const auto& part : form.list()) {
        std::cout << part.header("Content-Disposition") << "\n";
        std::cout << part.content() << "\n";
    }
}
```

### 构造文件上传请求

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
std::string content_type = make_content_type(boundary); // 用于 HTTP 头
```

### 零拷贝解码

```cpp
std::string body = /* ... */;
multipart::lazy_part doc = multipart::decode_lazy(body);

std::string_view payload = doc.list().front().content();
// payload 直接指向 body 内部——没有任何拷贝。
// 注意：body 必须比 doc 存活得更久。
```

## 文档

- [README.md](README.md) —— 本页
- [docs/usage.md](docs/usage.md) —— API 参考与示例
- [docs/design.md](docs/design.md) —— 数据模型、内存语义、嵌套、
  RFC 说明与已知限制

## 构建测试与示例

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

可选选项：

| 选项                       | 默认  | 说明                                    |
|----------------------------|-------|-----------------------------------------|
| `MULTIPART_BUILD_TESTS`    | `ON`  | 构建 GoogleTest 测试套件                |
| `MULTIPART_BUILD_EXAMPLES` | `ON`  | 构建示例                                |
| `MULTIPART_INSTALL`        | `ON`  | 安装规则 + CMake 包配置                 |
| `MULTIPART_PEDANTIC`       | `ON`  | `-Wall -Wextra -Wpedantic` / `/W4`      |

测试套件同时会在 `-fno-exceptions -fno-rtti` 以及
AddressSanitizer + UndefinedBehaviorSanitizer 下验证通过。

## License

以 MIT 协议分发。原始解析器版权 (c) 2021 Jack
(<jack.wgm@gmail.com>)；详见 [LICENSE](LICENSE)。