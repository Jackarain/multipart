// Encoding tests: flat documents, manual construction, header handling.

#include <gtest/gtest.h>

#include <multipart/multipart.hpp>

using namespace multipart;

TEST(encode, two_part_flat_form)
{
    part top;
    top.set_boundary("b");

    part f1 = make_field("k", "v");
    f1.set_boundary("b");
    part f2 = make_field("k2", "v2");
    f2.set_boundary("b");

    top.list().push_back(f1);
    top.list().push_back(f2);

    std::string expected =
        "--b\r\n"
        "Content-Disposition: form-data; name=\"k\"\r\n"
        "\r\n"
        "v\r\n"
        "--b\r\n"
        "Content-Disposition: form-data; name=\"k2\"\r\n"
        "\r\n"
        "v2\r\n"
        "--b--\r\n";

    EXPECT_EQ(expected, encode(top));
}

TEST(encode, root_boundary_falls_back_to_children)
{
    // When the root list has no boundary of its own, the encoder falls back to
    // the first child's boundary (typical for a decoded document).
    part top;
    part f1 = make_field("k", "v");
    f1.boundary() = "--fallback";
    part f2 = make_field("k2", "v2");
    f2.boundary() = "--fallback";
    top.list().push_back(f1);
    top.list().push_back(f2);

    std::string expected =
        "--fallback\r\n"
        "Content-Disposition: form-data; name=\"k\"\r\n"
        "\r\n"
        "v\r\n"
        "--fallback\r\n"
        "Content-Disposition: form-data; name=\"k2\"\r\n"
        "\r\n"
        "v2\r\n"
        "--fallback--\r\n";

    EXPECT_EQ(expected, encode(top));
}

TEST(encode, single_content_root_with_boundary)
{
    // A lone content part with a boundary encodes as a single-part multipart.
    part p;
    p.boundary() = "--b";
    p.content() = "data";
    p.add_header("Content-Type", "text/plain");

    EXPECT_EQ("--b\r\nContent-Type: text/plain\r\n\r\ndata\r\n--b--\r\n",
              encode(p));
}

TEST(encode, bare_content_root_without_boundary)
{
    part p;
    p.content() = "data";
    p.add_header("X-A", "1");
    // No boundary -> bare part, headers then content, and no trailing CRLF
    // (the CRLF belongs to a boundary delimiter, which does not exist here).
    EXPECT_EQ("X-A: 1\r\n\r\ndata", encode(p));
}

TEST(encode, empty_list_without_boundary_produces_nothing)
{
    // A list with no boundary and no children must not emit "--\r\n".
    part p;
    p.list();
    EXPECT_EQ("", encode(p));
}

TEST(encode, to_string_and_back_inserter_agree)
{
    part p = decode("--b\r\n\r\nx\r\n--b--\r\n");
    std::string from_str = encode(p);

    std::string from_iter;
    encode(std::back_inserter(from_iter), p);
    EXPECT_EQ(from_str, from_iter);
}

TEST(encode, to_raw_pointer_buffer)
{
    part p = decode("--b\r\n\r\nx\r\n--b--\r\n");
    std::string expected = encode(p);

    std::vector<char> buf(expected.size());
    char* it = buf.data();
    encode(it, p);

    EXPECT_EQ(expected, std::string(buf.data(), buf.size()));
}

TEST(encode, empty_list_encodes_as_empty_document)
{
    part p;
    p.list(); // make it a list (set_boundary alone does not change the type)
    p.set_boundary("b");
    EXPECT_EQ("--b--\r\n", encode(p));
}

TEST(encode, headers_round_trip_through_encode)
{
    std::string body =
        "--b\r\n"
        "Content-Disposition: form-data; name=\"x\"\r\n"
        "Content-Type: text/plain\r\n"
        "X-Custom: yes\r\n"
        "\r\n"
        "content\r\n"
        "--b--\r\n";

    part p = decode(body);
    EXPECT_EQ(body, encode(p));
}

TEST(encode, encode_is_byte_exact_for_form_data)
{
    // A realistic multipart/form-data body must round-trip byte-for-byte.
    std::string body =
        "--formdata123\r\n"
        "Content-Disposition: form-data; name=\"title\"\r\n"
        "\r\n"
        "My Title\r\n"
        "--formdata123\r\n"
        "Content-Disposition: form-data; name=\"upload\"; filename=\"photo.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n"
        "\r\n"
        "file-content-with-\r\n"
        "multiple-lines\r\n"
        "--formdata123--\r\n";

    part p = decode(body);
    EXPECT_EQ(body, encode(p));
}

TEST(encode, binary_content_preserved)
{
    std::string raw("a\0\x01\xff" "b", 6);
    std::string body = "--b\r\n\r\n" + raw + "\r\n--b--\r\n";

    part p = decode(body);
    EXPECT_EQ(body, encode(p));
}