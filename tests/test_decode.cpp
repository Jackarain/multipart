// Decoding tests: standard forms, headers, boundaries, content, preamble, etc.

#include <gtest/gtest.h>

#include <multipart/multipart.hpp>

using namespace multipart;

namespace {

// A typical multipart/form-data body with two parts.
const char* kTwoPartForm =
    "--boundary\r\n"
    "Content-Disposition: form-data; name=\"field1\"\r\n"
    "\r\n"
    "value1\r\n"
    "--boundary\r\n"
    "Content-Disposition: form-data; name=\"file\"; filename=\"a.txt\"\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
    "hello world\r\n"
    "--boundary--\r\n";

} // namespace

TEST(decode, two_part_form)
{
    bool ok = false;
    part p = decode(kTwoPartForm, ok);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(p.is_list());
    ASSERT_EQ(2u, p.list().size());

    auto it = p.list().begin();
    EXPECT_EQ("value1", it->content());
    EXPECT_EQ("form-data; name=\"field1\"",
              it->header("Content-Disposition"));
    EXPECT_EQ("--boundary", it->boundary());

    ++it;
    EXPECT_EQ("hello world", it->content());
    EXPECT_EQ("form-data; name=\"file\"; filename=\"a.txt\"",
              it->header("content-disposition"));
    EXPECT_EQ("text/plain", it->header("Content-Type"));
    EXPECT_EQ("--boundary", it->boundary());
}

TEST(decode, single_part_is_still_a_list)
{
    bool ok = false;
    part p = decode("--b\r\n\r\nx\r\n--b--\r\n", ok);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(p.is_list());
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("x", p.list().front().content());
    EXPECT_EQ("--b", p.boundary());
}

TEST(decode, empty_body_is_a_parse_error)
{
    // RFC 2046 requires 1*encapsulation: a multipart body must contain at
    // least one part, so a bare closing delimiter is rejected.
    bool ok = true;
    part p = decode("--b--\r\n", ok);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(p.is_list());
}

TEST(decode, part_with_no_headers)
{
    bool ok = false;
    part p = decode("--b\r\n\r\nbare\r\n--b--\r\n", ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    EXPECT_TRUE(p.list().front().prototype().empty());
    EXPECT_EQ("bare", p.list().front().content());
}

TEST(decode, part_with_empty_content)
{
    bool ok = false;
    part p = decode("--b\r\n\r\n\r\n--b--\r\n", ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("", p.list().front().content());
}

TEST(decode, header_without_value)
{
    bool ok = false;
    part p = decode("--b\r\nX-Flag:\r\n\r\nz\r\n--b--\r\n", ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("", p.list().front().header("X-Flag"));
}

TEST(decode, header_value_leading_space_is_trimmed)
{
    bool ok = false;
    part p = decode("--b\r\nX:  value-with-one-space\r\n\r\nz\r\n--b--\r\n", ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("value-with-one-space", p.list().front().header("X"));
}

TEST(decode, multiple_headers_preserve_order)
{
    bool ok = false;
    part p = decode(
        "--b\r\n"
        "A: 1\r\n"
        "B: 2\r\n"
        "C: 3\r\n"
        "\r\n"
        "z\r\n"
        "--b--\r\n",
        ok);
    ASSERT_TRUE(ok);
    auto& proto = p.list().front().prototype();
    ASSERT_EQ(3u, proto.size());
    EXPECT_EQ("A", proto[0].first);
    EXPECT_EQ("B", proto[1].first);
    EXPECT_EQ("C", proto[2].first);
}

TEST(decode, preamble_is_skipped)
{
    bool ok = false;
    part p = decode(
        "This is the preamble.\r\n"
        "--b\r\n"
        "\r\n"
        "data\r\n"
        "--b--\r\n"
        "this is the epilogue",
        ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("data", p.list().front().content());
}

TEST(decode, boundary_string_matching_content_is_not_a_delimiter)
{
    bool ok = false;
    part p = decode(
        "--b\r\n"
        "\r\n"
        "a\r\n"
        "--b\r\n"
        "\r\n"
        "--b\r\n"
        "--b--\r\n",
        ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(2u, p.list().size());
    auto it = p.list().begin();
    EXPECT_EQ("a", it->content());
    ++it;
    EXPECT_EQ("--b", it->content()); // boundary-looking text stays as content
}

TEST(decode, boundary_with_special_characters)
{
    bool ok = false;
    part p = decode(
        "--my.bound_ary+!~*'\r\n"
        "\r\n"
        "special\r\n"
        "--my.bound_ary+!~*'--\r\n",
        ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("special", p.list().front().content());
    EXPECT_EQ("--my.bound_ary+!~*'", p.list().front().boundary());
}

TEST(decode, binary_content_with_nul_bytes)
{
    std::string body = "--b\r\n\r\n";
    body += std::string("a\0b\x01\xff" "c", 6);
    body += "\r\n--b--\r\n";

    bool ok = false;
    part p = decode(body, ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    std::string expected("a\0b\x01\xff" "c", 6);
    EXPECT_EQ(expected, p.list().front().content());
}

TEST(decode, content_containing_crlf_pairs)
{
    bool ok = false;
    part p = decode(
        "--b\r\n"
        "\r\n"
        "line1\r\n"
        "line2\r\n"
        "\r\n"
        "--b--\r\n",
        ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("line1\r\nline2\r\n", p.list().front().content());
}

TEST(decode, quoted_boundary_in_content_type_not_required)
{
    // The decoder does not need the Content-Type header at all: it discovers
    // the boundary from the body itself.
    bool ok = false;
    part p = decode("--b\r\n\r\nx\r\n--b--\r\n", ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("x", p.list().front().content());
}

TEST(decode, header_names_case_preserved_content_lookup_insensitive)
{
    bool ok = false;
    part p = decode(
        "--b\r\n"
        "content-type: text/plain\r\n"
        "\r\n"
        "x\r\n"
        "--b--\r\n",
        ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ("text/plain", p.list().front().header("CONTENT-TYPE"));
    EXPECT_EQ("content-type", p.list().front().prototype()[0].first);
}

TEST(decode, many_parts)
{
    std::string body;
    for (int i = 0; i < 100; ++i) {
        body += "--b\r\n\r\npart-" + std::to_string(i) + "\r\n";
    }
    body += "--b--\r\n";

    bool ok = false;
    part p = decode(body, ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(100u, p.list().size());

    int i = 0;
    for (auto& c : p.list()) {
        EXPECT_EQ("part-" + std::to_string(i++), c.content());
    }
}

TEST(decode, from_string_view_via_ok_overload)
{
    std::string data = "--b\r\n\r\nx\r\n--b--\r\n";
    bool ok = false;
    auto p = decode(std::string_view(data), ok);
    EXPECT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
}

TEST(decode, content_type_case_insensitive_for_nesting_detection)
{
    // Lower-cased Content-Type must still trigger nested parsing.
    bool ok = false;
    part p = decode(
        "--outer\r\n"
        "content-type: multipart/mixed; boundary=inner\r\n"
        "\r\n"
        "--inner\r\n"
        "\r\n"
        "x\r\n"
        "--inner--\r\n"
        "--outer--\r\n",
        ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    EXPECT_TRUE(p.list().front().is_list());
    EXPECT_EQ("--inner", p.list().front().boundary());
}