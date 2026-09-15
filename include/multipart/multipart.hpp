//
// multipart.hpp - a small, self-contained C++17 multipart parser/serializer.
//
// Copyright (C) 2021 Jack.
// Author: jack
// Email:  jack.wgm at gmail dot com
//
// Maintained and extended as a complete header-only library.
// See README.md and docs/ for usage, design notes and examples.
//

#pragma once

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <iterator>
#include <list>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// 异常支持检测
// ---------------------------------------------------------------------------
#if !defined(_NO_EXCEPTIONS)
#  if defined(__clang__)
#    if !__has_feature(cxx_exceptions)
#      define _NO_EXCEPTIONS
#    endif
#  elif defined(__GNUC__)
#    if !defined(__EXCEPTIONS)
#      define _NO_EXCEPTIONS
#    endif
#  elif defined(_MSC_VER)
#    if !defined(_CPPUNWIND)
#      define _NO_EXCEPTIONS
#    endif
#  endif
#endif

namespace multipart {

namespace detail {

template <int V1, int V2>
struct max { enum { value = V1 > V2 ? V1 : V2 }; };

inline bool is_print(char c)
{
    unsigned char u = static_cast<unsigned char>(c);
    return (u >= 32 && u < 127) || u == '\r' || u == '\n';
}

inline std::string to_hex(std::string_view s)
{
    static const char hex_chars[] = "0123456789abcdef";
    std::string ret;
    ret.reserve(s.size() * 2);
    for (unsigned char c : s) {
        ret += hex_chars[c >> 4];
        ret += hex_chars[c & 0x0f];
    }
    return ret;
}

template <class T>
inline void call_destructor(T* o)
{
    assert(o && "o is nullptr");
    (void)o;
    o->~T();
}

// 安全地把 [b, e) 构造成 String；空 range 时避免 &*end 的 UB
template <class String, class InIt>
inline String make_string(InIt b, InIt e)
{
    auto n = std::distance(b, e);
    if (n <= 0) return String{};
    return String(std::addressof(*b), static_cast<std::size_t>(n));
}

// 比较 [p, p+len) 是否与 sv 相等（不做边界检查，调用方保证）
inline bool region_equal(const char* p, std::size_t len, std::string_view sv)
{
    return sv.size() == len && std::memcmp(p, sv.data(), len) == 0;
}

// 大小写不敏感地比较两个字符串
inline bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// 大小写不敏感地查找子串（简单实现）
inline std::size_t ifind(std::string_view haystack, std::string_view needle,
                         std::size_t pos = 0)
{
    static const std::size_t npos = std::string_view::npos;
    if (needle.empty()) return pos <= haystack.size() ? pos : npos;
    if (needle.size() > haystack.size()) return npos;
    if (pos > haystack.size()) return npos;   // 防御：避免 substr 越界
    const std::size_t limit = haystack.size() - needle.size();
    for (; pos <= limit; ++pos) {
        if (iequals(haystack.substr(pos, needle.size()), needle))
            return pos;
    }
    return npos;
}

// 前向声明：把用户传入的期望 boundary 归一化为候选分隔符
inline void init_expected(std::string_view in, std::string& out,
                          std::string& out_alt);

} // namespace detail

[[noreturn]] inline void throw_type_error(std::string msg = "")
{
#ifndef _NO_EXCEPTIONS
    throw std::runtime_error(msg.empty() ? "multipart: type error" : std::move(msg));
#else
    (void)msg;
    std::abort();
#endif
}

// ---------------------------------------------------------------------------
// basic_part<StringType>
//   std::string      -> part      (owning)
//   std::string_view -> lazy_part (zero-copy)
// ---------------------------------------------------------------------------
template <typename StringType>
struct basic_part
{
public:
    enum class data_type { content_t, list_t, undefined_t };

    using list_type      = std::list<basic_part>;
    using content_type   = StringType;
    using string_type    = StringType;
    using keyvalue_type  = std::pair<string_type, string_type>;
    using prototype_type = std::vector<keyvalue_type>;

    basic_part() = default;

    basic_part(content_type v, prototype_type p = {})
    {
        new (data_) content_type(std::move(v));
        prototype_ = std::move(p);
        type_ = data_type::content_t;
    }

    basic_part(list_type v, prototype_type p = {})
    {
        new (data_) list_type(std::move(v));
        prototype_ = std::move(p);
        type_ = data_type::list_t;
    }

    explicit basic_part(data_type t) { construct(t); }

    basic_part(const basic_part& e) { copy(e); }
    basic_part(basic_part&& e) noexcept { move_from(std::move(e)); }

    ~basic_part() { destruct(); }

    basic_part& operator=(const basic_part& e)
    {
        if (this != &e) { destruct(); copy(e); }
        return *this;
    }

    basic_part& operator=(basic_part&& e) noexcept
    {
        if (this != &e) { destruct(); move_from(std::move(e)); }
        return *this;
    }

    basic_part& operator=(content_type v)
    {
        destruct();
        new (data_) content_type(std::move(v));
        type_ = data_type::content_t;
        return *this;
    }

    basic_part& operator=(list_type v)
    {
        destruct();
        new (data_) list_type(std::move(v));
        type_ = data_type::list_t;
        return *this;
    }

    data_type type() const noexcept { return type_; }
    bool is_content() const noexcept { return type_ == data_type::content_t; }
    bool is_list()    const noexcept { return type_ == data_type::list_t; }

    content_type& content()
    {
        if (type_ == data_type::undefined_t) construct(data_type::content_t);
#ifndef _NO_EXCEPTIONS
        if (type_ != data_type::content_t) throw_type_error("not content");
#else
        assert(type_ == data_type::content_t);
#endif
        return *reinterpret_cast<content_type*>(data_);
    }

    list_type& list()
    {
        if (type_ == data_type::undefined_t) construct(data_type::list_t);
#ifndef _NO_EXCEPTIONS
        if (type_ != data_type::list_t) throw_type_error("not list");
#else
        assert(type_ == data_type::list_t);
#endif
        return *reinterpret_cast<list_type*>(data_);
    }

    const content_type& content() const
    {
#ifndef _NO_EXCEPTIONS
        if (type_ != data_type::content_t) throw_type_error("not content");
#else
        assert(type_ == data_type::content_t);
#endif
        return *reinterpret_cast<const content_type*>(data_);
    }

    const list_type& list() const
    {
#ifndef _NO_EXCEPTIONS
        if (type_ != data_type::list_t) throw_type_error("not list");
#else
        assert(type_ == data_type::list_t);
#endif
        return *reinterpret_cast<const list_type*>(data_);
    }

    prototype_type&       prototype()       { return prototype_; }
    const prototype_type& prototype() const { return prototype_; }

    std::string&       boundary()       { return boundary_; }
    const std::string& boundary() const { return boundary_; }

    // 设置边界字符串。若传入的标准形式（不带前导 "--"），自动补全为库内部
    // 表示（带前导 "--"）。
    void set_boundary(std::string b)
    {
        if (b.empty()) { boundary_.clear(); return; }
        if (b.size() >= 2 && b[0] == '-' && b[1] == '-')
            boundary_ = std::move(b);
        else
            boundary_ = "--" + std::move(b);
    }

    // 大小写不敏感地查找 header，返回第一个匹配值
    string_type header(std::string_view name) const
    {
        for (const auto& kv : prototype_) {
            if (detail::iequals(kv.first, name)) return kv.second;
        }
        return string_type{};
    }

    bool has_header(std::string_view name) const
    {
        for (const auto& kv : prototype_) {
            if (detail::iequals(kv.first, name)) return true;
        }
        return false;
    }

    void add_header(string_type name, string_type value)
    {
        prototype_.emplace_back(std::move(name), std::move(value));
    }

    // 大小写不敏感地替换同名 header；不存在则追加
    void set_header(string_type name, string_type value)
    {
        for (auto& kv : prototype_) {
            if (detail::iequals(kv.first, name)) {
                kv.second = std::move(value);
                return;
            }
        }
        add_header(std::move(name), std::move(value));
    }

    void remove_header(std::string_view name)
    {
        prototype_.erase(
            std::remove_if(prototype_.begin(), prototype_.end(),
                [&name](const keyvalue_type& kv) {
                    return detail::iequals(kv.first, name);
                }),
            prototype_.end());
    }

    friend bool operator==(const basic_part& a, const basic_part& b)
    {
        if (a.type_ != b.type_) return false;
        if (a.boundary_ != b.boundary_) return false;
        if (a.prototype_ != b.prototype_) return false;
        switch (a.type_) {
        case data_type::content_t: return a.content() == b.content();
        case data_type::list_t:    return a.list() == b.list();
        default:                   return true;
        }
    }

    friend bool operator!=(const basic_part& a, const basic_part& b)
    {
        return !(a == b);
    }

#if defined(_DEBUG) || defined(DEBUG)
    void print(std::ostream& os, int indent = 0) const
    {
        assert(indent >= 0);
        for (int i = 0; i < indent; ++i) os << ' ';
        os << "boundary=" << boundary() << '\n';

        for (const auto& r : prototype_) {
            for (int i = 0; i < indent; ++i) os << ' ';
            os << std::string(r.first) << ": " << std::string(r.second) << '\n';
        }

        switch (type_) {
        case data_type::content_t: {
            bool binary_string = false;
            for (auto i = content().begin(); i != content().end(); ++i) {
                if (!detail::is_print(static_cast<char>(*i))) {
                    binary_string = true;
                    break;
                }
            }
            for (int i = 0; i < indent; ++i) os << ' ';
            if (binary_string)
                os << detail::to_hex(std::string(content())) << "\n\n";
            else
                os << std::string(content()) << "\n\n";
        } break;

        case data_type::list_t:
            os << "list\n";
            for (const auto& child : list())
                child.print(os, indent + 2);
            break;

        default:
            os << "<uninitialized>\n\n";
        }
    }
#endif

protected:
    void construct(data_type t)
    {
        switch (t) {
        case data_type::content_t: new (data_) content_type; break;
        case data_type::list_t:    new (data_) list_type;    break;
        default:                   assert(t == data_type::undefined_t);
        }
        type_ = t;
    }

    void copy(const basic_part& e)
    {
        switch (e.type_) {
        case data_type::content_t: new (data_) content_type(e.content()); break;
        case data_type::list_t:    new (data_) list_type(e.list());       break;
        default: break;
        }
        type_      = e.type_;
        prototype_ = e.prototype_;
        boundary_  = e.boundary_;
    }

    void move_from(basic_part&& e) noexcept
    {
        switch (e.type_) {
        case data_type::content_t:
            new (data_) content_type(
                std::move(*reinterpret_cast<content_type*>(e.data_)));
            break;
        case data_type::list_t:
            new (data_) list_type(
                std::move(*reinterpret_cast<list_type*>(e.data_)));
            break;
        default: break;
        }
        type_      = e.type_;
        prototype_ = std::move(e.prototype_);
        boundary_  = std::move(e.boundary_);
        e.type_    = data_type::undefined_t;
    }

    void destruct()
    {
        switch (type_) {
        case data_type::content_t:
            detail::call_destructor(reinterpret_cast<content_type*>(data_));
            break;
        case data_type::list_t:
            detail::call_destructor(reinterpret_cast<list_type*>(data_));
            break;
        default: break;
        }
        type_ = data_type::undefined_t;
        prototype_.clear();
    }

    static constexpr std::size_t union_size =
        detail::max<sizeof(list_type), sizeof(content_type)>::value;

    std::string    boundary_;
    data_type      type_ = data_type::undefined_t;
    prototype_type prototype_;
    alignas(std::max_align_t) unsigned char data_[union_size] = {};
};

using part      = basic_part<std::string>;
using lazy_part = basic_part<std::string_view>;

// ---------------------------------------------------------------------------
// event callbacks
// ---------------------------------------------------------------------------
using callback_func = std::function<int(const std::string_view&)>;

struct event_cb
{
    callback_func boundary_;
    callback_func header_field_;
    callback_func header_value_;
    callback_func part_data_;
    callback_func error_;
};

namespace detail {

enum {
    s_start,
    s_start_boundary,
    s_header_field,
    s_header_value,
    s_part_data,
};

enum {
    max_recursive_depth = 64,
    max_boundary_length = 200,
};

// 检测 Content-Type 是否为 multipart/*，并提取 boundary 参数
inline bool extract_multipart_boundary(std::string_view value,
                                       std::string& out_boundary)
{
    // 跳过前导空白
    std::size_t i = 0;
    while (i < value.size() && (value[i] == ' ' || value[i] == '\t')) ++i;

    // 前缀比较大小写不敏感（MIME 类型大小写不敏感）
    static const char prefix[] = "multipart/";
    constexpr std::size_t plen = sizeof(prefix) - 1;
    if (value.size() - i < plen) return false;
    if (!iequals(value.substr(i, plen), prefix)) return false;

    // 查找 "boundary="（参数名大小写不敏感）
    std::string_view rest = value.substr(i + plen);
    static const char key[] = "boundary=";
    constexpr std::size_t klen = sizeof(key) - 1;

    std::size_t p = 0;
    while (true) {
        p = ifind(rest, key, p);
        if (p == std::string_view::npos) return false;
        // 边界必须紧跟在 ';' 或空白之后（避免匹配 "Xboundary="）
        if (p == 0 || rest[p - 1] == ';' || rest[p - 1] == ' ' ||
            rest[p - 1] == '\t') {
            break;
        }
        p += klen;
    }

    p += klen;
    while (p < rest.size() && (rest[p] == ' ' || rest[p] == '\t')) ++p;
    if (p >= rest.size()) return false;

    if (rest[p] == '"') {
        ++p;
        std::size_t e = rest.find('"', p);
        if (e == std::string_view::npos) return false;
        out_boundary.assign(rest.data() + p, e - p);
        return !out_boundary.empty();
    }

    std::size_t e = p;
    while (e < rest.size() && rest[e] != ';' &&
           rest[e] != ' ' && rest[e] != '\t' && rest[e] != '\r') {
        ++e;
    }
    out_boundary.assign(rest.data() + p, e - p);
    return !out_boundary.empty();
}

template <typename InIt, typename Entry>
std::ptrdiff_t decode_recursive(InIt in, InIt end, Entry& ret,
                                bool& err, int depth, const event_cb& ecb,
                                const std::string* expected = nullptr,
                                const std::string* expected_alt = nullptr)
{
    if (depth >= max_recursive_depth || in == end) {
        err = true;
        return 0;
    }

    InIt start = in;
    int  state = s_start;
    std::string boundary;

    using data_type      = typename Entry::data_type;
    using string_type    = typename Entry::string_type;
    using prototype_type = typename Entry::prototype_type;

    string_type    key, value;
    InIt           cbegin = in, cend = in;
    prototype_type prototype;
    Entry          tmp;

    while (in != end)
    {
        char c = *in++;

        switch (state)
        {
        case s_start:
            if (expected) {
                // 已知期望 boundary：直接在输入中定位它（跳过 preamble），
                // 避免把 preamble 中的 "--" 误当成边界，也能识别紧跟其后的
                // 关闭分隔符（空文档）。
                // expected 为 token 形式（"--boundary"），expected_alt 为用户
                // 直接传入 delimiter 形式时兜底。
                if (c == '-') {
                    InIt here = in - 1;
                    const std::string* cands[2] = { expected, expected_alt };
                    for (int ci = 0; ci < 2 && cands[ci]; ++ci) {
                        const std::string& bd = *cands[ci];
                        auto remaining = std::distance(here, end);
                        if (remaining < static_cast<std::ptrdiff_t>(
                                             bd.size() + 2))
                            continue; // 剩余空间不足，换下一个候选
                        const char* hp = std::addressof(*here);
                        if (!region_equal(hp, bd.size(), bd))
                            continue; // 不是这个候选，继续
                        boundary = bd;
                        if (hp[bd.size()] == '-' && hp[bd.size() + 1] == '-') {
                            // 关闭分隔符紧跟其后：空 multipart 文档
                            if (ret.type() != data_type::list_t)
                                ret = Entry(data_type::list_t);
                            if (ret.boundary().empty())
                                ret.boundary() = boundary;
                            if (ecb.boundary_)
                                ecb.boundary_(boundary);
                            in = here + bd.size() + 2;
                            return std::distance(start, in);
                        }
                        if (hp[bd.size()] == '\r' &&
                            hp[bd.size() + 1] == '\n') {
                            // 正常开头
                            in = here + bd.size() + 2;
                            cbegin = cend = in;
                            state = s_header_field;
                            if (ecb.boundary_)
                                ecb.boundary_(boundary);
                            continue;
                        }
                        err = true;
                        return in - start;
                    }
                    continue; // 未命中任何候选，跳过该字符继续扫描
                }
                continue;
            }

            // 无期望边界：跳过 preamble（第一个 boundary 之前的任意文本，
            // RFC 2046），从 "--" 开始自行发现边界。
            if (c != '-') {
                cbegin = cend = in;
                continue;
            }

            if (in == end) { err = true; return in - start; }
            c = *in++;
            if (c != '-') { cbegin = cend = in; continue; }

            boundary.clear();
            boundary.append("--");
            state = s_start_boundary;
            continue;

        case s_start_boundary:
            if (c == '\r') {
                if (in == end) { err = true; return in - start; }
                if (*in == '\n') {
                    ++in;
                    cbegin = cend = in;
                    state = s_header_field;
                    if (ecb.boundary_)
                        ecb.boundary_(boundary);
                    continue;
                }
            }
            // 防止恶意输入用超长 boundary 造成无界内存分配
            if (boundary.size() >= max_boundary_length) {
                err = true;
                return in - start;
            }
            boundary.push_back(c);
            continue;

        case s_header_field:
            if (c == '\r') {
                if (in == end) { err = true; return in - start; }
                if (*in == '\n') {
                    ++in;
                    cbegin = cend = in;
                    state = s_part_data;
                    continue;
                }
                err = true;
                return in - start;
            }
            if (c == '\n') {
                err = true;
                return in - start;
            }
            if (c == ':') {
                key = make_string<string_type>(cbegin, cend);
                if (ecb.header_field_)
                    ecb.header_field_(key);
                cbegin = cend = in;
                state = s_header_value;
                continue;
            }
            cend = in;
            continue;

        case s_header_value:
            if (cbegin + 1 == in && c == ' ') {
                cbegin = cend = in;
                continue;
            }
            if (c != '\r') {
                cend = in;
                continue;
            }

            value = make_string<string_type>(cbegin, cend);
            prototype.emplace_back(key, value);

            if (in == end) { err = true; return in - start; }
            c = *in++;
            if (c != '\n') { err = true; return in - start; }

            if (ecb.header_value_)
                ecb.header_value_(value);

            if (in == end) { err = true; return in - start; }
            c = *in;

            if (c != '\r') {
                cbegin = cend = in;
                state = s_header_field;
                continue;
            }

            ++in;
            if (in == end || *in != '\n') { err = true; return in - start; }
            ++in;

            cbegin = cend = in;

            // 检测嵌套 multipart，并提取内层 boundary
            if (iequals(key, "Content-Type")) {
                std::string nested_boundary;
                if (extract_multipart_boundary(std::string_view(value),
                                               nested_boundary)) {
                    // 用 Content-Type 声明的边界作为内层期望边界，
                    // 内层文档即可正确处理空文档 / preamble。
                    std::string nested_lib, nested_alt;
                    init_expected(nested_boundary, nested_lib, nested_alt);

                    bool nested_err = false;
                    auto consumed = decode_recursive(
                        in, end, tmp, nested_err, depth + 1, ecb,
                        &nested_lib,
                        nested_alt.empty() ? nullptr : &nested_alt);
                    if (nested_err) { err = true; return in - start; }

                    // 嵌套 list 的 boundary 应为内层 boundary
                    if (tmp.type() == data_type::list_t &&
                        tmp.boundary().empty())
                        tmp.boundary() = nested_lib;

                    in += consumed;
                    cbegin = cend = in;
                    state = s_part_data;
                    continue;
                }
            }

            state = s_part_data;
            continue;

        case s_part_data:
            if (c == '\r') {
                if (in == end) { err = true; return in - start; }
                if (*in != '\n') { cend = in; continue; }

                auto p = in;
                ++p;  // 跳过 '\n'

                auto remaining = std::distance(p, end);
                if (remaining < static_cast<decltype(remaining)>(
                                    boundary.size() + 2)) {
                    err = true;
                    return in - start;
                }

                // 只比较 boundary 本身，避免临时 string
                const char* pdata = std::addressof(*p);

                if (region_equal(pdata, boundary.size(), boundary) &&
                    pdata[boundary.size()] == '-' &&
                    pdata[boundary.size() + 1] == '-') {
                    // 最终分隔符
                    if (tmp.type() == data_type::undefined_t) {
                        auto sv = make_string<string_type>(cbegin, cend);
                        if (ecb.part_data_)
                            ecb.part_data_(sv);
                        tmp.content() = sv;
                    }
                    if (tmp.boundary().empty())
                        tmp.boundary() = boundary;
                    if (!prototype.empty())
                        tmp.prototype() = std::move(prototype);

                    // multipart 文档始终是 part 序列（即使只有一个 part），
                    // 因此统一产出 list_t，避免嵌套时结构坍缩丢失外层信息。
                    if (ret.type() != data_type::list_t)
                        ret = Entry(data_type::list_t);
                    if (ret.boundary().empty())
                        ret.boundary() = boundary;
                    ret.list().push_back(std::move(tmp));

                    p += boundary.size() + 2;
                    return std::distance(start, p);
                }

                if (region_equal(pdata, boundary.size(), boundary) &&
                    pdata[boundary.size()] == '\r' &&
                    pdata[boundary.size() + 1] == '\n') {
                    // 下一个 part 的起始边界
                    if (tmp.type() == data_type::undefined_t) {
                        auto sv = make_string<string_type>(cbegin, cend);
                        if (ecb.part_data_)
                            ecb.part_data_(sv);
                        tmp.content() = sv;
                    }
                    if (tmp.boundary().empty())
                        tmp.boundary() = boundary;
                    if (!prototype.empty())
                        tmp.prototype() = std::move(prototype);

                    prototype.clear();

                    if (ret.type() != data_type::list_t)
                        ret = Entry(data_type::list_t);
                    if (ret.boundary().empty())
                        ret.boundary() = boundary;
                    ret.list().push_back(std::move(tmp));
                    tmp = Entry();

                    p += boundary.size() + 2;
                    in = p;
                    cbegin = cend = in;
                    state = s_header_field;
                    continue;
                }

                // 不是真正的边界（如 "\r\n--something"），当作数据
            }
            cend = in;
            continue;
        }
    }

    err = true;
    return in - start;
}

// ---------------------------------------------------------------------------
// encode
//
// 编码模型：
//   - encode_document() 序列化一棵完整的文档树（根节点）。
//   - encode_part()     把某个 part 作为父文档中的一个分块输出。
// 一个 list part 代表一份 multipart 文档；当它作为父文档的子节点时，
// 会被包在父文档的 boundary 之内，其正文则是嵌套的 multipart 文档。
// ---------------------------------------------------------------------------

template <class OutIt>
inline int write_string(OutIt& out, std::string_view val)
{
    for (char c : val) *out++ = c;
    return static_cast<int>(val.size());
}

// 输出一段 header 列表：每行 "Name: value\r\n"（value 为空时输出 "Name:\r\n"，
// 保证与解码结果字节一致）
template <class OutIt, class Prototype>
inline int write_headers(OutIt& out, const Prototype& prototype)
{
    int ret = 0;
    for (const auto& p : prototype) {
        ret += write_string(out, p.first);
        ret += write_string(out, ":");
        if (!p.second.empty()) {
            ret += write_string(out, " ");
            ret += write_string(out, p.second);
        }
        ret += write_string(out, "\r\n");
    }
    return ret;
}

template <class OutIt, class Entry>
int encode_part(OutIt& out, const Entry& e, const std::string& parent_boundary,
                int depth);

// 取文档使用的 boundary：自身已设置则用之，否则回退到第一个子节点的 boundary
template <class Entry>
const std::string& resolve_boundary(const Entry& e)
{
    if (!e.boundary().empty()) return e.boundary();
    static const std::string empty;
    if (e.is_list() && !e.list().empty())
        return e.list().front().boundary();
    return empty;
}

// 序列化一个完整文档（根节点）。支持：
//   - list_t:    multipart 文档
//   - content_t: 单个 part（boundary 为空时输出裸 part；否则单 part 的 multipart）
template <class OutIt, class Entry>
int encode_document(OutIt& out, const Entry& e, int depth)
{
    using data_type = typename Entry::data_type;
    int ret = 0;
    const std::string& b = resolve_boundary(e);

    switch (e.type())
    {
    case data_type::content_t:
        if (!b.empty()) {
            ret += write_string(out, b);
            ret += write_string(out, "\r\n");
        }
        ret += write_headers(out, e.prototype());
        ret += write_string(out, "\r\n");
        ret += write_string(out, e.content());
        // CRLF 属于分隔符前缀：仅在存在 boundary 时追加
        if (!b.empty()) {
            ret += write_string(out, "\r\n");
            ret += write_string(out, b);
            ret += write_string(out, "--\r\n");
        }
        break;

    case data_type::list_t:
        if (depth >= max_recursive_depth) return 0;
        // 每个 part 以 "boundary\r\n" 作为分隔符开头（也是文档的首个边界），
        // 因此这里不再额外输出一次开头的 boundary。
        for (const auto& child : e.list())
            ret += encode_part(out, child, b, depth + 1);
        // 无 boundary 时（如空 list 且子节点也无边界）不输出 "--\r\n"
        if (!b.empty()) {
            ret += write_string(out, b);
            ret += write_string(out, "--\r\n");
        }
        break;

    default:
        break;
    }
    return ret;
}

// 把 part 作为父文档（boundary 为 parent_boundary）中的一个分块输出。
//  - content part: 输出 boundary + headers + content。
//  - list part:    输出 boundary + 自身 headers，正文为嵌套的 multipart 文档。
template <class OutIt, class Entry>
int encode_part(OutIt& out, const Entry& e, const std::string& parent_boundary,
                int depth)
{
    using data_type = typename Entry::data_type;
    int ret = 0;

    if (!parent_boundary.empty()) {
        ret += write_string(out, parent_boundary);
        ret += write_string(out, "\r\n");
    }

    switch (e.type())
    {
    case data_type::content_t:
        ret += write_headers(out, e.prototype());
        ret += write_string(out, "\r\n");
        ret += write_string(out, e.content());
        ret += write_string(out, "\r\n");
        break;

    case data_type::list_t:
        if (depth >= max_recursive_depth) return 0;
        ret += write_headers(out, e.prototype());
        ret += write_string(out, "\r\n");
        // 嵌套文档：以自身 boundary 开启一份新的 multipart 文档
        ret += encode_document(out, e, depth);
        break;

    default:
        break;
    }
    return ret;
}

} // namespace detail

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------

namespace detail {

// 共享的解码实现：expected 非空时按期望 boundary 解析。
// 出错时 ok 置为 false，并返回已解析的部分结果（不含错误发生后的数据）。
template <typename Entry, typename InIt>
Entry decode_impl(InIt start, InIt end, const std::string* expected,
                  const std::string* expected_alt, bool& ok, event_cb ecb)
{
    Entry e;
    bool err = false;
    detail::decode_recursive(start, end, e, err, 0, ecb, expected,
                             expected_alt);
    ok = !err;
    if (err) {
        if (ecb.error_)
            ecb.error_("multipart: parse error");
        return e; // 保留已成功解析的部分
    }
    return e;
}

// 把用户传入的期望 boundary 归一化为候选分隔符。
//  - out        ：token 形式（"--" + in），始终有效；
//  - out_alt    ：当 in 以 "--" 开头时，原样作为 delimiter 形式兜底；
//    （in 为空串时两者都为空，表示不提供期望边界。）
inline void init_expected(std::string_view in, std::string& out,
                          std::string& out_alt)
{
    out.clear();
    out_alt.clear();
    if (in.empty()) return;
    out.reserve(in.size() + 2);
    out.append("--");
    out.append(in.data(), in.size());
    if (in.size() >= 2 && in[0] == '-' && in[1] == '-')
        out_alt.assign(in.data(), in.size());
}

} // namespace detail

template <typename Entry, typename InIt>
Entry decode(InIt start, InIt end, bool& ok, event_cb ecb = {})
{
    return detail::decode_impl<Entry>(start, end, nullptr, nullptr, ok,
                                      std::move(ecb));
}

template <typename Entry, typename InIt>
Entry decode(InIt start, InIt end, std::string_view expected_boundary,
             bool& ok, event_cb ecb = {})
{
    std::string expected, expected_alt;
    detail::init_expected(expected_boundary, expected, expected_alt);
    return detail::decode_impl<Entry>(
        start, end, expected.empty() ? nullptr : &expected,
        expected_alt.empty() ? nullptr : &expected_alt, ok, std::move(ecb));
}

template <typename Entry, typename InIt>
Entry decode(InIt start, InIt end, event_cb ecb = {})
{
    bool ok;
    return decode<Entry>(start, end, ok, std::move(ecb));
}

template <typename Entry, typename InIt>
Entry decode(InIt start, InIt end, std::string_view expected_boundary,
             event_cb ecb = {})
{
    bool ok;
    return decode<Entry>(start, end, expected_boundary, ok, std::move(ecb));
}

template <typename Entry, typename Container>
Entry decode(const Container& c, bool& ok, event_cb ecb = {})
{
    using std::begin;
    using std::end;
    return decode<Entry>(begin(c), end(c), ok, std::move(ecb));
}

template <typename Entry, typename Container>
Entry decode(const Container& c, std::string_view expected_boundary,
             bool& ok, event_cb ecb = {})
{
    using std::begin;
    using std::end;
    return decode<Entry>(begin(c), end(c), expected_boundary, ok,
                         std::move(ecb));
}

template <typename Entry, typename Container>
Entry decode(const Container& c, event_cb ecb = {})
{
    using std::begin;
    using std::end;
    return decode<Entry>(begin(c), end(c), std::move(ecb));
}

template <typename Entry, typename Container>
Entry decode(const Container& c, std::string_view expected_boundary,
             event_cb ecb = {})
{
    using std::begin;
    using std::end;
    return decode<Entry>(begin(c), end(c), expected_boundary,
                         std::move(ecb));
}

template <class OutIt, class Entry>
int encode(OutIt out, const Entry& e)
{
    return detail::encode_document(out, e, 0);
}

template <class Entry>
std::string encode(const Entry& e)
{
    std::string result;
    result.reserve(4096);
    auto it = std::back_inserter(result);
    encode(it, e);
    return result;
}

// ---- 便利重载 -------------------------------------------------------------

inline part decode(std::string_view data, bool& ok, event_cb ecb = {})
{
    return decode<part>(data.begin(), data.end(), ok, std::move(ecb));
}

inline part decode(std::string_view data, std::string_view expected_boundary,
                   bool& ok, event_cb ecb = {})
{
    std::string expected, expected_alt;
    detail::init_expected(expected_boundary, expected, expected_alt);
    return detail::decode_impl<part>(
        data.begin(), data.end(),
        expected.empty() ? nullptr : &expected,
        expected_alt.empty() ? nullptr : &expected_alt, ok, std::move(ecb));
}

inline part decode(std::string_view data, event_cb ecb = {})
{
    bool ok;
    return decode<part>(data, ok, std::move(ecb));
}

inline part decode(std::string_view data, std::string_view expected_boundary,
                   event_cb ecb = {})
{
    bool ok;
    return decode(data, expected_boundary, ok, std::move(ecb));
}

inline lazy_part decode_lazy(std::string_view data, bool& ok, event_cb ecb = {})
{
    return decode<lazy_part>(data.begin(), data.end(), ok, std::move(ecb));
}

inline lazy_part decode_lazy(std::string_view data,
                             std::string_view expected_boundary,
                             bool& ok, event_cb ecb = {})
{
    std::string expected, expected_alt;
    detail::init_expected(expected_boundary, expected, expected_alt);
    return detail::decode_impl<lazy_part>(
        data.begin(), data.end(),
        expected.empty() ? nullptr : &expected,
        expected_alt.empty() ? nullptr : &expected_alt, ok, std::move(ecb));
}

inline lazy_part decode_lazy(std::string_view data, event_cb ecb = {})
{
    bool ok;
    return decode<lazy_part>(data, ok, std::move(ecb));
}

inline lazy_part decode_lazy(std::string_view data,
                             std::string_view expected_boundary,
                             event_cb ecb = {})
{
    bool ok;
    return decode_lazy(data, expected_boundary, ok, std::move(ecb));
}

// ---- 辅助工具 -------------------------------------------------------------

// 生成一个随机的 boundary 字符串（库内部表示，带前导 "--"，可直接赋给
// part::boundary()）。
inline std::string make_boundary()
{
    static std::mt19937_64 rng = [] {
        std::random_device rd;
        std::vector<std::uint32_t> seeds(8);
        for (auto& s : seeds) s = rd();
        seeds.push_back(static_cast<std::uint32_t>(
            std::chrono::steady_clock::now()
                .time_since_epoch()
                .count()));
        std::seed_seq seq(seeds.begin(), seeds.end());
        return std::mt19937_64(seq);
    }();
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);

    static const char hex_chars[] = "0123456789abcdef";
    std::string boundary = "------------------------------";
    boundary.reserve(boundary.size() + 16);
    // 一次 rng() 产生 64 位，取低 16 个半字节即可（避免 16 次调用）
    const std::uint64_t v = rng();
    for (int i = 0; i < 16; ++i)
        boundary += hex_chars[(v >> (i * 4)) & 0x0f];
    return boundary;
}

// 根据 boundary 生成 "Content-Type: multipart/<subtype>; boundary=..."。
// boundary 可以带前导 "--"（库内部表示），会自动剥离为标准形式。
// 若 boundary 含 token 字符集以外的字符，则按 RFC 2046 用引号包裹。
inline std::string make_content_type(std::string_view boundary,
                                     std::string_view subtype = "form-data")
{
    std::string result = "multipart/";
    result.append(subtype.data(), subtype.size());
    result.append("; boundary=");
    if (boundary.size() >= 2 && boundary[0] == '-' && boundary[1] == '-')
        boundary.remove_prefix(2);

    // boundary 是否需要在 Content-Type 里加引号。
    // 只有 RFC 2046 的 bcharsnospace（可进入 token）才可裸写；
    // 空格、引号、反斜杠及其它特殊字符都需要引号包裹。
    auto needs_quote = [](std::string_view s) {
        for (char c : s) {
            bool bchar =
                std::isalnum(static_cast<unsigned char>(c)) ||
                c == '\'' || c == '(' || c == ')' || c == '+' ||
                c == '_' || c == ',' || c == '-' || c == '.' ||
                c == '/' || c == ':' || c == '=' || c == '?';
            if (!bchar || c == '"' || c == '\\') return true;
        }
        return false;
    };

    if (needs_quote(boundary)) {
        result += '"';
        for (char c : boundary) {
            if (c == '"' || c == '\\') result += '\\';
            result += c;
        }
        result += '"';
    } else {
        result.append(boundary.data(), boundary.size());
    }
    return result;
}

// 判断 Content-Type 是否为 multipart/*
inline bool is_multipart(std::string_view content_type)
{
    std::string boundary;
    return detail::extract_multipart_boundary(content_type, boundary);
}

// 从 Content-Type 中提取 boundary 参数（标准形式，不带前导 "--"）。
// 非 multipart 或未找到时返回空字符串。
inline std::string extract_boundary(std::string_view content_type)
{
    std::string boundary;
    detail::extract_multipart_boundary(content_type, boundary);
    return boundary;
}

// 便捷构造一个表单字段 part
inline part make_field(std::string name, std::string value)
{
    part p;
    p.content() = std::move(value);
    p.add_header("Content-Disposition",
                 "form-data; name=\"" + name + "\"");
    return p;
}

// 便捷构造一个文件上传 part
inline part make_file(std::string name, std::string filename,
                      std::string content_type, std::string data)
{
    part p;
    p.content() = std::move(data);
    p.add_header("Content-Disposition",
                 "form-data; name=\"" + name + "\"; filename=\"" +
                     filename + "\"");
    if (!content_type.empty())
        p.add_header("Content-Type", std::move(content_type));
    return p;
}

} // namespace multipart