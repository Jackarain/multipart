// lazy_part: zero-copy decoding using string_view.

#include <gtest/gtest.h>

#include <multipart/multipart.hpp>

using namespace multipart;

TEST(lazy, decodes_without_copying)
{
    std::string src = "--b\r\n\r\nlazy-data\r\n--b--\r\n";

    bool ok = false;
    lazy_part lp = decode_lazy(src, ok);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(lp.is_list());
    ASSERT_EQ(1u, lp.list().size());

    const lazy_part& child = lp.list().front();
    EXPECT_EQ("lazy-data", child.content());

    // The view must point *into* the source buffer, not a copy.
    EXPECT_GE(child.content().data(), src.data());
    EXPECT_LT(child.content().data(), src.data() + src.size());
    EXPECT_EQ(child.content().size(), 9u);
}

TEST(lazy, header_values_are_views)
{
    std::string src =
        "--b\r\n"
        "Content-Disposition: form-data; name=\"f\"\r\n"
        "\r\n"
        "x\r\n"
        "--b--\r\n";

    lazy_part lp = decode_lazy(src);
    const lazy_part& child = lp.list().front();

    std::string_view v = child.header("Content-Disposition");
    EXPECT_EQ("form-data; name=\"f\"", v);
    // points into src
    EXPECT_GE(v.data(), src.data());
    EXPECT_LT(v.data(), src.data() + src.size());
}

TEST(lazy, original_buffer_must_outlive_views)
{
    std::string src = "--b\r\n\r\nx\r\n--b--\r\n";
    lazy_part lp = decode_lazy(src);
    EXPECT_EQ("x", lp.list().front().content());
    // (No access after src goes out of scope: that would be UB by design.)
}

TEST(lazy, header_fields_are_views)
{
    std::string src = "--b\r\nSome-Header: v\r\n\r\nx\r\n--b--\r\n";
    lazy_part lp = decode_lazy(src);
    const auto& proto = lp.list().front().prototype();
    ASSERT_EQ(1u, proto.size());
    EXPECT_EQ("Some-Header", proto[0].first);
    EXPECT_EQ("v", proto[0].second);
}

TEST(lazy, encode_from_lazy_part)
{
    std::string src = "--b\r\n\r\nlazy\r\n--b--\r\n";
    lazy_part lp = decode_lazy(src);
    EXPECT_EQ(src, encode(lp));
}

TEST(lazy, boundary_views)
{
    std::string src = "--b\r\n\r\nx\r\n--b--\r\n";
    lazy_part lp = decode_lazy(src);
    // boundary() is a std::string in every part type (ownership, not a view).
    EXPECT_EQ("--b", lp.boundary());
    EXPECT_EQ("--b", lp.list().front().boundary());
}

TEST(lazy, multiple_parts_are_views)
{
    std::string src =
        "--b\r\n\r\none\r\n"
        "--b\r\n\r\ntwo\r\n"
        "--b\r\n\r\nthree\r\n"
        "--b--\r\n";

    lazy_part lp = decode_lazy(src);
    ASSERT_EQ(3u, lp.list().size());

    // Every content view points into the source buffer.
    for (const auto& c : lp.list()) {
        EXPECT_GE(c.content().data(), src.data());
        EXPECT_LT(c.content().data(), src.data() + src.size());
        EXPECT_LE(c.content().data() + c.content().size(),
                  src.data() + src.size());
    }
    EXPECT_EQ("one", lp.list().front().content());
    EXPECT_EQ("three", lp.list().back().content());
}

TEST(lazy, works_with_generic_decode)
{
    std::string src = "--b\r\n\r\nx\r\n--b--\r\n";
    lazy_part lp = decode<lazy_part>(src.data(), src.data() + src.size());
    ASSERT_TRUE(lp.is_list());
    EXPECT_EQ("x", lp.list().front().content());
}

TEST(lazy, error_returns_empty_lazy_part)
{
    std::string src = "--b\r\n\r\nx"; // truncated
    bool ok = true;
    lazy_part lp = decode_lazy(src, ok);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(lp.is_list());
    EXPECT_FALSE(lp.is_content());
}