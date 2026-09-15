// Error handling: malformed input must fail cleanly (ok == false), never crash.

#include <gtest/gtest.h>

#include <multipart/multipart.hpp>

using namespace multipart;

namespace {

// Helper: returns true when decoding fails.
bool decode_fails(const std::string& data)
{
    bool ok = true;
    decode(data, ok);
    return !ok;
}

} // namespace

TEST(decode_errors, empty_input)
{
    EXPECT_TRUE(decode_fails(""));
}

TEST(decode_errors, missing_final_boundary)
{
    EXPECT_TRUE(decode_fails("--b\r\n\r\nx"));
    EXPECT_TRUE(decode_fails("--b\r\n\r\nx\r\n"));
    EXPECT_TRUE(decode_fails("--b\r\n\r\nx\r\n--b"));
}

TEST(decode_errors, truncated_final_delimiter)
{
    // Final delimiter must be "--b--"; anything shorter is an error.
    EXPECT_TRUE(decode_fails("--b\r\n\r\nx\r\n--b-"));
}

TEST(decode_errors, only_dashes)
{
    EXPECT_TRUE(decode_fails("-"));
    EXPECT_TRUE(decode_fails("--"));
    EXPECT_TRUE(decode_fails("--\r\n"));
}

TEST(decode_errors, content_never_terminated)
{
    // Boundary found at start, then data, but no closing boundary ever.
    EXPECT_TRUE(decode_fails("--b\r\n\r\nsome data without closing"));
}

TEST(decode_errors, boundary_cut_off_at_end)
{
    // The parser requires the boundary delimiter to fit in the remaining input.
    EXPECT_TRUE(decode_fails("--b\r\n\r\nx\r\n--b"));
    EXPECT_TRUE(decode_fails("--b\r\n\r\nx\r\n--bb"));
}

TEST(decode_errors, headers_terminated_by_bare_lf_is_error)
{
    // Header block must be CRLF CRLF; a bare LF inside the header block is
    // not valid.
    EXPECT_TRUE(decode_fails(std::string("--b\r\nA: 1\n\r\nx\r\n--b--\r\n")));
}

TEST(decode_errors, cr_without_lf_inside_header)
{
    EXPECT_TRUE(decode_fails(std::string("--b\r\nA: 1\rX\r\n\r\nx\r\n--b--\r\n")));
}

TEST(decode_errors, error_callback_invoked)
{
    int errors = 0;
    event_cb ecb;
    ecb.error_ = [&errors](std::string_view msg) {
        EXPECT_FALSE(msg.empty());
        ++errors;
        return 0;
    };
    decode("--b\r\n\r\nx", ecb);
    EXPECT_EQ(1, errors);
}

TEST(decode_errors, ok_flag_not_set_on_error)
{
    bool ok = true;
    auto p = decode("--b\r\n\r\nx", ok);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(p.is_list());
    EXPECT_FALSE(p.is_content());
}

TEST(decode_errors, valid_input_sets_ok)
{
    bool ok = false;
    decode("--b\r\n\r\nx\r\n--b--\r\n", ok);
    EXPECT_TRUE(ok);
}

TEST(decode_errors, recursion_depth_limited)
{
    // Build a deeply nested multipart document (70 levels > max 64).
    std::string deep;
    for (int i = 0; i < 70; ++i) {
        deep += "--b" + std::to_string(i) +
                "\r\nContent-Type: multipart/mixed; boundary=b" +
                std::to_string(i + 1) + "\r\n\r\n";
    }
    deep += "--b70\r\n\r\nend\r\n";
    for (int i = 70; i >= 0; --i)
        deep += "--b" + std::to_string(i) + "--\r\n";

    EXPECT_TRUE(decode_fails(deep));
}

TEST(decode_errors, deeply_nested_well_within_limit)
{
    // 10 levels of nesting must succeed.
    std::string doc;
    for (int i = 0; i < 10; ++i) {
        doc += "--b" + std::to_string(i) +
               "\r\nContent-Type: multipart/mixed; boundary=b" +
               std::to_string(i + 1) + "\r\n\r\n";
    }
    doc += "--b10\r\n\r\nend\r\n";
    for (int i = 10; i >= 0; --i)
        doc += "--b" + std::to_string(i) + "--\r\n";

    bool ok = false;
    auto p = decode(doc, ok);
    EXPECT_TRUE(ok);
}

TEST(decode_errors, garbage_input_does_not_crash)
{
    const std::string garbage[] = {
        "\r\n", "\n", "----", "abc", "\xff\xfe\x00", "-------------",
        "--\r\n\r\n", "\r\n--\r\n", "--b\r\n\r\n--b--", "--b--\r\n--b--\r\n",
    };
    for (const auto& g : garbage) {
        bool ok = true;
        decode(g, ok); // must not throw or crash
        (void)ok;
    }
}