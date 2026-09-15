// lazy_zero_copy.cpp
//
// Decode without copying: `lazy_part` stores std::string_view slices that
// point directly into the original buffer.

#include <multipart/multipart.hpp>

#include <iostream>
#include <string>

int main()
{
    // Large in-memory body that we do NOT want to duplicate.
    std::string body;
    body = "--b\r\n"
           "Content-Disposition: form-data; name=\"big\"\r\n"
           "\r\n" + std::string(1024, 'X') +
           "\r\n"
           "--b--\r\n";

    multipart::lazy_part doc = multipart::decode_lazy(body);

    const auto& first = doc.list().front();
    const std::string_view payload = first.content();

    std::cout << "payload size    : " << payload.size() << "\n"
              << "payload is a view into body: "
              << (payload.data() >= body.data() &&
                  payload.data() < body.data() + body.size() ? "yes" : "no")
              << "\n"
              << "content-type    : " << first.header("Content-Type") << "\n"
              << "boundary        : " << doc.boundary() << "\n"
              << "first 8 bytes   : "
              << std::string(payload.substr(0, 8)) << "\n";

    // Serializing a lazy_part back out requires no extra copies either.
    std::string reencoded = multipart::encode(doc);
    std::cout << "re-encode matches: " << (reencoded == body ? "yes" : "no")
              << "\n";

    // NOTE: the buffer that the views reference must outlive the lazy_part.
    return 0;
}