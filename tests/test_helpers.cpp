// Public helper utilities: boundaries, content-type strings, form builders.

#include <gtest/gtest.h>

#include <multipart/multipart.hpp>

using namespace multipart;

TEST(helpers, make_boundary_shape)
{
    std::string b = make_boundary();
    // Library-internal form: starts with "--", length >= 30 (24 dashes + hex).
    EXPECT_GE(b.size(), 30u);
    EXPECT_EQ("--", b.substr(0, 2));
    for (char c : b)
        EXPECT_TRUE((c == '-') || (c >= '0' && c <= '9') ||
                    (c >= 'a' && c <= 'f'));
}

TEST(helpers, make_boundary_unique)
{
    std::string a = make_boundary();
    std::string b = make_boundary();
    EXPECT_NE(a, b);
}

TEST(helpers, make_content_type_default_subtype)
{
    std::string ct = make_content_type("--abc123");
    EXPECT_EQ("multipart/form-data; boundary=abc123", ct);
}

TEST(helpers, make_content_type_strips_leading_dashes)
{
    EXPECT_EQ("multipart/form-data; boundary=xyz",
              make_content_type("xyz"));
    EXPECT_EQ("multipart/form-data; boundary=xyz",
              make_content_type("--xyz"));
    // Exactly two leading dashes are removed (the library-internal form);
    // any further dashes belong to the boundary itself.
    EXPECT_EQ("multipart/form-data; boundary=--xyz",
              make_content_type("----xyz"));
}

TEST(helpers, make_content_type_custom_subtype)
{
    EXPECT_EQ("multipart/mixed; boundary=b",
              make_content_type("--b", "mixed"));
}

TEST(helpers, make_content_type_round_trips_with_boundary)
{
    std::string b = make_boundary();
    std::string ct = make_content_type(b);
    EXPECT_EQ(b.substr(2), extract_boundary(ct));
}

TEST(helpers, make_content_type_quotes_special_boundaries)
{
    // Space is a valid bchar but not a token char -> must be quoted.
    EXPECT_EQ("multipart/form-data; boundary=\"a b\"",
              make_content_type("a b"));
    // ';' is not a boundary char at all -> quoted (and parseable again).
    std::string quoted = make_content_type("x;y");
    EXPECT_EQ("multipart/form-data; boundary=\"x;y\"", quoted);
    // Quotes and backslashes are escaped inside the quoted-string.
    EXPECT_EQ("multipart/form-data; boundary=\"q\\\"q\"",
              make_content_type("q\"q"));
    EXPECT_EQ("multipart/form-data; boundary=\"a\\\\b\"",
              make_content_type("a\\b"));
    // Safe token chars stay unquoted.
    EXPECT_EQ("multipart/form-data; boundary=abc123", make_content_type("abc123"));
    EXPECT_EQ("multipart/form-data; boundary=a(b)c", make_content_type("a(b)c"));
}

TEST(helpers, quoted_content_type_still_extracts)
{
    std::string ct = make_content_type("a b");
    EXPECT_EQ("a b", extract_boundary(ct));
    EXPECT_TRUE(is_multipart(ct));
}

TEST(helpers, is_multipart)
{
    EXPECT_TRUE(is_multipart("multipart/form-data; boundary=abc"));
    EXPECT_TRUE(is_multipart("multipart/mixed; boundary=\"quoted\""));
    EXPECT_TRUE(is_multipart("  multipart/related; boundary=x"));
    EXPECT_TRUE(is_multipart("Multipart/Form-Data; boundary=x"));

    EXPECT_FALSE(is_multipart("text/plain"));
    EXPECT_FALSE(is_multipart("multipart/form-data")); // no boundary
    EXPECT_FALSE(is_multipart(""));
    EXPECT_FALSE(is_multipart("application/json"));
}

TEST(helpers, extract_boundary)
{
    EXPECT_EQ("abc", extract_boundary("multipart/form-data; boundary=abc"));
    EXPECT_EQ("quoted", extract_boundary(
                            "multipart/mixed; boundary=\"quoted\""));
    EXPECT_EQ("x", extract_boundary(
                       "multipart/mixed; charset=utf-8; boundary=x; foo=bar"));
    EXPECT_EQ("", extract_boundary("text/plain"));
    EXPECT_EQ("", extract_boundary("multipart/mixed"));
}

TEST(helpers, extract_boundary_does_not_match_prefix)
{
    // "Xboundary=" must not be mistaken for "boundary=".
    EXPECT_EQ("", extract_boundary(
                      "multipart/mixed; xboundary=should-not-match"));
}

TEST(helpers, ifind_guards_against_oversized_pos)
{
    using multipart::detail::ifind;
    constexpr auto npos = std::string_view::npos;

    std::string_view hay("abc");
    EXPECT_EQ(npos, ifind(hay, "c", 5));                      // pos > size
    EXPECT_EQ(npos, ifind(hay, "c", npos));                   // pos == SIZE_MAX
    EXPECT_EQ(0u, ifind(hay, "a", 0));
    EXPECT_EQ(2u, ifind(hay, "c", 2));
    EXPECT_EQ(2u, ifind(hay, "C", 0));                        // case-insensitive
    EXPECT_EQ(npos, ifind(hay, "abcd", 0));                   // needle > hay
    EXPECT_EQ(npos, ifind(hay, "z", 0));
}

TEST(helpers, make_field)
{
    part p = make_field("username", "alice");
    ASSERT_TRUE(p.is_content());
    EXPECT_EQ("alice", p.content());
    EXPECT_EQ("form-data; name=\"username\"",
              p.header("Content-Disposition"));
    EXPECT_EQ(1u, p.prototype().size());
}

TEST(helpers, make_file)
{
    part p = make_file("upload", "report.pdf", "application/pdf",
                       std::string("PDF\x01\x02", 5));
    ASSERT_TRUE(p.is_content());
    EXPECT_EQ(std::string("PDF\x01\x02", 5), p.content());
    EXPECT_EQ("form-data; name=\"upload\"; filename=\"report.pdf\"",
              p.header("Content-Disposition"));
    EXPECT_EQ("application/pdf", p.header("Content-Type"));
}

TEST(helpers, make_file_without_content_type)
{
    part p = make_file("f", "x.bin", "", "data");
    EXPECT_FALSE(p.has_header("Content-Type"));
}

TEST(helpers, end_to_end_form_build)
{
    part form;
    form.set_boundary(make_boundary());
    form.add_header("Content-Type", make_content_type(form.boundary()));

    part user = make_field("user", "bob");
    user.set_boundary(form.boundary());
    part file = make_file("file", "a.txt", "text/plain", "hello");
    file.set_boundary(form.boundary());

    form.list().push_back(std::move(user));
    form.list().push_back(std::move(file));

    std::string body = encode(form);

    // The body itself must decode to the same parts.
    part back = decode(body);
    ASSERT_EQ(2u, back.list().size());
    EXPECT_EQ("bob", back.list().front().content());
    EXPECT_NE(std::string::npos,
              back.list().back().header("Content-Disposition")
                  .find("filename=\"a.txt\""));
    EXPECT_EQ("hello", back.list().back().content());
}