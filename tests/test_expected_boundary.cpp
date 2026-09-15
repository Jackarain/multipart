// decode() with an explicit expected boundary.
//
// Passing the boundary (typically obtained from the Content-Type header via
// extract_boundary()) lets the parser skip the heuristic body-discovery,
// which fixes two cases:
//   - empty documents ("--b--\r\n" with no parts), and
//   - preamble text that itself contains "--" sequences.

#include <gtest/gtest.h>

#include <multipart/multipart.hpp>

using namespace multipart;

namespace {

// The library-internal boundary form has a leading "--".
std::string lib_form(std::string_view b)
{
    return "--" + std::string(b);
}

} // namespace

TEST(expected_boundary, accepts_both_forms)
{
    bool ok = false;
    part a = decode("--b\r\n\r\nx\r\n--b--\r\n", "b", ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(1u, a.list().size());

    ok = false;
    part b = decode("--b\r\n\r\nx\r\n--b--\r\n", "--b", ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(1u, b.list().size());
    EXPECT_EQ(a, b);
}

TEST(expected_boundary, empty_document)
{
    // With a known boundary, "--b--\r\n" is a valid zero-part document.
    bool ok = false;
    part p = decode("--b--\r\n", "b", ok);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(p.is_list());
    EXPECT_TRUE(p.list().empty());
    EXPECT_EQ("--b", p.boundary());
}

TEST(expected_boundary, preamble_with_dashes)
{
    // Preamble contains "--fake" but the expected boundary is "b".
    bool ok = false;
    part p = decode("preamble --fake\r\n--b\r\n\r\nx\r\n--b--\r\n", "b", ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("x", p.list().front().content());
}

TEST(expected_boundary, wrong_boundary_fails)
{
    bool ok = true;
    decode("--b\r\n\r\nx\r\n--b--\r\n", "wrong", ok);
    EXPECT_FALSE(ok);
}

TEST(expected_boundary, boundary_not_present_fails)
{
    bool ok = true;
    decode("nothing relevant here", "b", ok);
    EXPECT_FALSE(ok);
}

TEST(expected_boundary, lazy_variant)
{
    std::string src = "preamble --x\r\n--b\r\n\r\ny\r\n--b--\r\n";
    bool ok = false;
    lazy_part lp = decode_lazy(src, "b", ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, lp.list().size());
    EXPECT_EQ("y", lp.list().front().content());
}

TEST(expected_boundary, nested_empty_document)
{
    // The inner multipart document has zero parts.
    std::string body =
        "--outer\r\n"
        "Content-Type: multipart/mixed; boundary=inner\r\n"
        "\r\n"
        "--inner--\r\n"
        "--outer--\r\n";

    bool ok = false;
    part p = decode(body, ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    const part& inner = p.list().front();
    ASSERT_TRUE(inner.is_list());
    EXPECT_TRUE(inner.list().empty());
    EXPECT_EQ("--inner", inner.boundary());

    // Round-trips back to the same bytes.
    EXPECT_EQ(body, encode(p));
}

TEST(expected_boundary, from_content_type_header)
{
    // Typical usage: pull the boundary from Content-Type and pass it in.
    std::string boundary = extract_boundary(make_content_type(make_boundary()));

    std::string body =
        "some preamble with --stray dashes\r\n" +
        lib_form(boundary) + "\r\n\r\nvalue\r\n" +
        lib_form(boundary) + "--\r\n";

    bool ok = false;
    part p = decode(body, boundary, ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("value", p.list().front().content());
}

TEST(expected_boundary, iterators_and_containers)
{
    std::string src = "--b\r\n\r\nx\r\n--b--\r\n";
    std::vector<char> v(src.begin(), src.end());

    bool ok = false;
    part p = decode<part>(v, "b", ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("x", p.list().front().content());

    ok = false;
    part q = decode<part>(src.data(), src.data() + src.size(), "--b", ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(p, q);
}