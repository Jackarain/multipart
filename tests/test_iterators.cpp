// decode/encode against various container & iterator types.

#include <gtest/gtest.h>

#include <multipart/multipart.hpp>

#include <deque>
#include <list>
#include <string>
#include <vector>

using namespace multipart;

namespace {

const char* kBody =
    "--b\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
    "from-iterators\r\n"
    "--b--\r\n";

} // namespace

TEST(iterators, decode_from_string)
{
    part p = decode<part>(std::string(kBody));
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("from-iterators", p.list().front().content());
}

TEST(iterators, decode_from_string_view)
{
    part p = decode<part>(std::string_view(kBody));
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("from-iterators", p.list().front().content());
}

TEST(iterators, decode_from_vector_char)
{
    std::vector<char> v(kBody, kBody + std::strlen(kBody));
    part p = decode<part>(v);
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("from-iterators", p.list().front().content());
}

TEST(iterators, decode_from_deque_char)
{
    std::deque<char> d(kBody, kBody + std::strlen(kBody));
    part p = decode<part>(d.begin(), d.end());
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("from-iterators", p.list().front().content());
}

TEST(iterators, decode_from_raw_pointers)
{
    part p = decode<part>(kBody, kBody + std::strlen(kBody));
    ASSERT_EQ(1u, p.list().size());
    EXPECT_EQ("from-iterators", p.list().front().content());
}

// NOTE: decode() requires random-access iterators (it does random lookahead
// on the input range), so input streams must be buffered first. The container
// and raw-pointer tests above cover the supported input types.

TEST(iterators, decode_lazy_from_vector)
{
    std::vector<char> v(kBody, kBody + std::strlen(kBody));
    lazy_part lp = decode<lazy_part>(v);
    ASSERT_EQ(1u, lp.list().size());
    EXPECT_EQ("from-iterators", lp.list().front().content());
    EXPECT_GE(lp.list().front().content().data(), v.data());
    EXPECT_LT(lp.list().front().content().data(), v.data() + v.size());
}

TEST(iterators, encode_to_back_inserters)
{
    part p = decode(kBody);

    std::string out_str;
    encode(std::back_inserter(out_str), p);
    EXPECT_EQ(kBody, out_str);

    std::vector<char> out_vec;
    encode(std::back_inserter(out_vec), p);
    EXPECT_EQ(kBody, std::string(out_vec.begin(), out_vec.end()));

    std::list<char> out_list;
    encode(std::back_inserter(out_list), p);
    EXPECT_EQ(kBody, std::string(out_list.begin(), out_list.end()));
}

TEST(iterators, encode_to_raw_iterator)
{
    part p = decode(kBody);
    std::vector<char> buf(std::strlen(kBody));
    char* it = buf.data();
    // encode() takes the iterator by value and returns the number of bytes
    // written; the caller advances the iterator itself.
    int n = encode(it, p);
    it += n; // caller advances the iterator using the returned byte count
    EXPECT_EQ(static_cast<int>(std::strlen(kBody)), n);
    EXPECT_EQ(buf.data() + n, it);
    EXPECT_EQ(kBody, std::string(buf.data(), static_cast<std::size_t>(n)));
}

TEST(iterators, encode_generic_entry)
{
    // encode() is templated on the entry type; lazy parts work too.
    std::string src = "--b\r\n\r\nx\r\n--b--\r\n";
    lazy_part lp = decode_lazy(src);
    EXPECT_EQ(src, encode(lp));
}