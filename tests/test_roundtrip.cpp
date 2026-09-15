// Round-trip tests: decode -> encode -> decode must be stable and faithful.

#include <gtest/gtest.h>

#include <multipart/multipart.hpp>

#include <random>
#include <string>

using namespace multipart;

namespace {

// Asserts decode(encode(decode(x))) == decode(x) is unnecessary; instead we
// check the stronger property encode(decode(x)) == x for a set of documents.
const std::string kDocuments[] = {
    // plain
    "--b\r\n\r\nx\r\n--b--\r\n",
    // two parts
    "--b\r\n\r\n1\r\n--b\r\n\r\n2\r\n--b--\r\n",
    // headers + content
    "--b\r\nContent-Type: text/plain\r\n\r\nhello\r\n--b--\r\n",
    // header without value
    "--b\r\nX-Flag:\r\n\r\nz\r\n--b--\r\n",
    // long values
    std::string("--b\r\n\r\n") + std::string(5000, 'a') + "\r\n--b--\r\n",
    // binary-ish content
    "--b\r\n\r\nline1\r\nline2\r\n--b--\r\n",
    // many parts
    [] {
        std::string d;
        for (int i = 0; i < 50; ++i)
            d += "--b\r\n\r\np" + std::to_string(i) + "\r\n";
        d += "--b--\r\n";
        return d;
    }(),
    // nested multipart
    "--outer\r\nContent-Type: multipart/mixed; boundary=inner\r\n\r\n"
    "--inner\r\n\r\na\r\n--inner\r\n\r\nb\r\n--inner--\r\n"
    "--outer\r\n\r\ntail\r\n--outer--\r\n",
    // nested single-part
    "--outer\r\nContent-Type: multipart/mixed; boundary=inner\r\n\r\n"
    "--inner\r\n\r\nonly\r\n--inner--\r\n"
    "--outer--\r\n",
};

} // namespace

TEST(roundtrip, stable_documents_encode_identically)
{
    for (const auto& doc : kDocuments) {
        part p = decode(doc);
        EXPECT_EQ(doc, encode(p)) << "round-trip mismatch for document:\n"
                                  << doc;
    }
}

TEST(roundtrip, encode_then_decode_is_stable)
{
    for (const auto& doc : kDocuments) {
        part once = decode(encode(decode(doc)));
        part twice = decode(encode(once));
        EXPECT_EQ(once, twice) << "double round-trip unstable for:\n" << doc;
    }
}

TEST(roundtrip, generated_bodies_round_trip)
{
    std::mt19937 rng(12345);

    auto random_string = [&rng](std::size_t n) {
        static const char alphabet[] =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
            "\r\n \t-_=+~";
        std::string s;
        s.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            s += alphabet[rng() % (sizeof(alphabet) - 1)];
        return s;
    };

    for (int iter = 0; iter < 200; ++iter) {
        std::string body;
        const int nparts = 1 + static_cast<int>(rng() % 6);
        for (int i = 0; i < nparts; ++i) {
            body += "--rb\r\n";
            if (rng() % 2)
                body += "Content-Disposition: form-data; name=\"field" +
                        std::to_string(i) + "\"\r\n";
            body += "\r\n";
            body += random_string(rng() % 200);
            body += "\r\n";
        }
        body += "--rb--\r\n";

        bool ok = false;
        part p = decode(body, ok);
        if (!ok)
            continue; // some random bodies legitimately fail to parse
        EXPECT_EQ(body, encode(p)) << "failed on iteration " << iter;
    }
}