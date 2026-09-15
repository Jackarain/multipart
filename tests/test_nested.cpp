// Nested multipart documents: decode, encode and round-trip.

#include <gtest/gtest.h>

#include <multipart/multipart.hpp>

using namespace multipart;

namespace {

const char* kNestedBody =
    "--outer\r\n"
    "Content-Type: multipart/mixed; boundary=inner\r\n"
    "\r\n"
    "--inner\r\n"
    "X-A: 1\r\n"
    "\r\n"
    "nested-a\r\n"
    "--inner\r\n"
    "\r\n"
    "nested-b\r\n"
    "--inner--\r\n"
    "--outer\r\n"
    "\r\n"
    "after\r\n"
    "--outer--\r\n";

} // namespace

TEST(nested, decode_structure)
{
    bool ok = false;
    part p = decode(kNestedBody, ok);
    ASSERT_TRUE(ok);

    ASSERT_TRUE(p.is_list());
    ASSERT_EQ(2u, p.list().size());
    EXPECT_EQ("--outer", p.boundary());

    // First outer part is itself a multipart/mixed document.
    const part& inner = p.list().front();
    EXPECT_TRUE(inner.is_list());
    EXPECT_EQ("--inner", inner.boundary());
    EXPECT_EQ("multipart/mixed; boundary=inner",
              inner.header("Content-Type"));
    ASSERT_EQ(2u, inner.list().size());
    EXPECT_EQ("nested-a", inner.list().front().content());
    EXPECT_EQ("nested-b", inner.list().back().content());

    // Second outer part is a plain part.
    const part& after = *std::next(p.list().begin());
    EXPECT_TRUE(after.is_content());
    EXPECT_EQ("after", after.content());
}

TEST(nested, encode_round_trip)
{
    part p = decode(kNestedBody);
    EXPECT_EQ(kNestedBody, encode(p));
}

TEST(nested, double_round_trip_is_stable)
{
    part p = decode(kNestedBody);
    std::string once = encode(p);
    std::string twice = encode(decode(once));
    EXPECT_EQ(once, twice);
}

TEST(nested, three_levels)
{
    std::string body =
        "--l1\r\n"
        "Content-Type: multipart/mixed; boundary=l2\r\n"
        "\r\n"
        "--l2\r\n"
        "Content-Type: multipart/mixed; boundary=l3\r\n"
        "\r\n"
        "--l3\r\n"
        "\r\n"
        "leaf\r\n"
        "--l3--\r\n"
        "--l2--\r\n"
        "--l1--\r\n";

    bool ok = false;
    part p = decode(body, ok);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(p.is_list());
    ASSERT_EQ(1u, p.list().size());

    const part& l2 = p.list().front();
    ASSERT_TRUE(l2.is_list());
    ASSERT_EQ(1u, l2.list().size());

    const part& l3 = l2.list().front();
    ASSERT_TRUE(l3.is_list());
    ASSERT_EQ(1u, l3.list().size());

    EXPECT_EQ("leaf", l3.list().front().content());
    EXPECT_EQ("--l3", l3.boundary());

    EXPECT_EQ(body, encode(p));
}

TEST(nested, single_inner_part_does_not_collapse)
{
    // Regression test: a nested multipart with only ONE inner part must not
    // collapse the outer structure into a plain content part.
    std::string body =
        "--outer\r\n"
        "Content-Type: multipart/mixed; boundary=inner\r\n"
        "\r\n"
        "--inner\r\n"
        "\r\n"
        "only-one\r\n"
        "--inner--\r\n"
        "--outer--\r\n";

    bool ok = false;
    part p = decode(body, ok);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(p.is_list());
    ASSERT_EQ(1u, p.list().size());

    const part& inner = p.list().front();
    ASSERT_TRUE(inner.is_list()) << "nested single-part must stay a list";
    ASSERT_EQ(1u, inner.list().size());
    EXPECT_EQ("only-one", inner.list().front().content());

    EXPECT_EQ(body, encode(p));
}

TEST(nested, nested_boundary_uses_library_form)
{
    part p = decode(kNestedBody);
    const part& inner = p.list().front();
    // The stored boundary must include the leading "--".
    EXPECT_EQ("--inner", inner.boundary());
}

TEST(nested, content_type_boundary_with_extra_parameters)
{
    std::string body =
        "--outer\r\n"
        "Content-Type: multipart/mixed; charset=utf-8; boundary=XYZ\r\n"
        "\r\n"
        "--XYZ\r\n"
        "\r\n"
        "data\r\n"
        "--XYZ--\r\n"
        "--outer--\r\n";

    bool ok = false;
    part p = decode(body, ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1u, p.list().size());
    EXPECT_TRUE(p.list().front().is_list());
    EXPECT_EQ("--XYZ", p.list().front().boundary());
}

TEST(nested, manual_construction_with_nested_list_part)
{
    // Build the same shape by hand and check the serialized bytes.
    part top;
    top.set_boundary("outer");

    part inner_doc;
    inner_doc.set_boundary("inner");
    inner_doc.add_header("Content-Type",
                         "multipart/mixed; boundary=inner");

    part leaf;
    leaf.set_boundary("inner");
    leaf.content() = "leaf-data";
    inner_doc.list().push_back(std::move(leaf));

    part tail;
    tail.set_boundary("outer");
    tail.content() = "tail";
    tail.add_header("Content-Type", "text/plain");

    top.list().push_back(std::move(inner_doc));
    top.list().push_back(std::move(tail));

    std::string expected =
        "--outer\r\n"
        "Content-Type: multipart/mixed; boundary=inner\r\n"
        "\r\n"
        "--inner\r\n"
        "\r\n"
        "leaf-data\r\n"
        "--inner--\r\n"
        "--outer\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "tail\r\n"
        "--outer--\r\n";

    EXPECT_EQ(expected, encode(top));

    // And it decodes back to the same shape.
    part back = decode(expected);
    ASSERT_TRUE(back.is_list());
    ASSERT_EQ(2u, back.list().size());
    EXPECT_TRUE(back.list().front().is_list());
    EXPECT_EQ("leaf-data",
              back.list().front().list().front().content());
}