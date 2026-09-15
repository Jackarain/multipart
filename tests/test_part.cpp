// Test basic_part semantics: construction, copy/move, header helpers, equality.

#include <gtest/gtest.h>

#include <multipart/multipart.hpp>

using namespace multipart;

namespace {

part make_sample_part()
{
    part p;
    p.content() = "hello";
    p.boundary() = "--b";
    p.add_header("Content-Type", "text/plain");
    p.add_header("X-Test", "1");
    return p;
}

} // namespace

TEST(part, default_construct_is_undefined)
{
    part p;
    EXPECT_FALSE(p.is_content());
    EXPECT_FALSE(p.is_list());
    EXPECT_EQ(part::data_type::undefined_t, p.type());
}

TEST(part, construct_from_content)
{
    part p(std::string("data"));
    EXPECT_TRUE(p.is_content());
    EXPECT_EQ("data", p.content());
}

TEST(part, construct_from_list)
{
    part::list_type children;
    children.emplace_back(std::string("a"));
    part p(std::move(children));
    EXPECT_TRUE(p.is_list());
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("a", p.list().front().content());
}

TEST(part, construct_with_prototype)
{
    part::prototype_type proto{{"A", "1"}, {"B", "2"}};
    part p(std::string("x"), proto);
    EXPECT_EQ(2u, p.prototype().size());
    EXPECT_EQ("1", p.header("A"));
    EXPECT_EQ("2", p.header("B"));
}

TEST(part, implicit_construction_from_content_string)
{
    part p = std::string("value");
    EXPECT_TRUE(p.is_content());
    EXPECT_EQ("value", p.content());
}

TEST(part, copy_constructor_is_deep)
{
    part a = make_sample_part();
    part b(a);
    EXPECT_EQ(a, b);
    b.content() = "changed";
    EXPECT_NE(a, b);
    EXPECT_EQ("hello", a.content());
}

TEST(part, copy_assignment_is_deep)
{
    part a = make_sample_part();
    part b;
    b = a;
    EXPECT_EQ(a, b);
    b.content() = "changed";
    EXPECT_NE(a, b);
    EXPECT_EQ("hello", a.content());
}

TEST(part, move_constructor_transfers)
{
    part a = make_sample_part();
    part b(std::move(a));
    EXPECT_TRUE(b.is_content());
    EXPECT_EQ("hello", b.content());
    EXPECT_FALSE(a.is_content()); // moved-from
}

TEST(part, move_assignment_transfers)
{
    part a = make_sample_part();
    part b;
    b = std::move(a);
    EXPECT_EQ("hello", b.content());
    EXPECT_FALSE(a.is_content());
}

TEST(part, self_assignment_is_safe)
{
    part a = make_sample_part();
    part& r = a;
    a = r;
    EXPECT_TRUE(a.is_content());
    EXPECT_EQ("hello", a.content());
}

TEST(part, assignment_switches_type)
{
    part p;
    p.content() = "str";
    ASSERT_TRUE(p.is_content());

    p = part::list_type{};
    EXPECT_TRUE(p.is_list());

    p = part::content_type("again");
    EXPECT_TRUE(p.is_content());
    EXPECT_EQ("again", p.content());
}

TEST(part, content_initializes_undefined)
{
    part p;
    p.content() = "lazy-init";
    EXPECT_TRUE(p.is_content());
    EXPECT_EQ("lazy-init", p.content());
}

TEST(part, list_initializes_undefined)
{
    part p;
    p.list().emplace_back(std::string("child"));
    EXPECT_TRUE(p.is_list());
    ASSERT_EQ(1u, p.list().size());
}

TEST(part, content_access_throws_on_wrong_type)
{
    part p;
    p.list(); // make it a list
    EXPECT_THROW(p.content(), std::runtime_error);
    EXPECT_THROW(std::as_const(p).content(), std::runtime_error);
}

TEST(part, list_access_throws_on_wrong_type)
{
    part p;
    p.content() = "x";
    EXPECT_THROW(p.list(), std::runtime_error);
    EXPECT_THROW(std::as_const(p).list(), std::runtime_error);
}

TEST(part, header_lookup_is_case_insensitive)
{
    part p = make_sample_part();
    EXPECT_EQ("text/plain", p.header("Content-Type"));
    EXPECT_EQ("text/plain", p.header("content-type"));
    EXPECT_EQ("text/plain", p.header("CONTENT-TYPE"));
    EXPECT_EQ("", p.header("Missing"));
}

TEST(part, add_and_has_header)
{
    part p;
    p.add_header("A", "1");
    p.add_header("B", "2");
    EXPECT_TRUE(p.has_header("a"));
    EXPECT_TRUE(p.has_header("b"));
    EXPECT_FALSE(p.has_header("c"));
    EXPECT_EQ(2u, p.prototype().size());
}

TEST(part, set_header_replaces_in_place)
{
    part p;
    p.add_header("Content-Type", "text/plain");
    p.add_header("X-A", "1");
    p.set_header("content-type", "application/json");

    ASSERT_EQ(2u, p.prototype().size());
    EXPECT_EQ("application/json", p.header("Content-Type"));
    EXPECT_EQ("1", p.header("X-A"));
}

TEST(part, set_header_appends_when_missing)
{
    part p;
    p.set_header("Only", "value");
    ASSERT_EQ(1u, p.prototype().size());
    EXPECT_EQ("value", p.header("only"));
}

TEST(part, remove_header_is_case_insensitive)
{
    part p = make_sample_part();
    p.remove_header("x-test");
    EXPECT_FALSE(p.has_header("X-Test"));
    EXPECT_EQ(1u, p.prototype().size());
    p.remove_header("CONTENT-TYPE");
    EXPECT_TRUE(p.prototype().empty());
}

TEST(part, boundary_access)
{
    part p;
    EXPECT_EQ("", p.boundary());
    p.boundary() = "--abc";
    EXPECT_EQ("--abc", p.boundary());
}

TEST(part, set_boundary_normalizes)
{
    part p;
    p.set_boundary("abc");       // standard form -> library form
    EXPECT_EQ("--abc", p.boundary());
    p.set_boundary("--already"); // already prefixed -> unchanged
    EXPECT_EQ("--already", p.boundary());
    p.set_boundary("");          // empty means "no boundary"
    EXPECT_EQ("", p.boundary());
}

TEST(part, equality)
{
    part a = make_sample_part();
    part b = a;
    EXPECT_EQ(a, b);

    b.boundary() = "--different";
    EXPECT_NE(a, b);

    b = a;
    b.content() = "different";
    EXPECT_NE(a, b);

    b = a;
    b.set_header("X-Test", "9");
    EXPECT_NE(a, b);
}

TEST(part, list_children_equality_recursive)
{
    part::list_type la;
    part c1;
    c1.content() = "x";
    la.push_back(c1);
    part a(std::move(la));

    part::list_type lb;
    part c2;
    c2.content() = "x";
    lb.push_back(c2);
    part b(std::move(lb));

    EXPECT_EQ(a, b);
    a.list().front().content() = "y";
    EXPECT_NE(a, b);
}

TEST(part, nested_list_via_list_access)
{
    part outer;
    part::list_type children;
    part inner;
    inner.list(); // inner is a list
    children.push_back(std::move(inner));
    outer.list() = std::move(children);

    ASSERT_TRUE(outer.is_list());
    ASSERT_EQ(1u, outer.list().size());
    EXPECT_TRUE(outer.list().front().is_list());
}