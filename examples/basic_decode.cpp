// basic_decode.cpp
//
// Parse a raw multipart body and walk the resulting tree.

#include <multipart/multipart.hpp>

#include <iostream>
#include <string>

int main()
{
    // A typical HTTP `multipart/form-data` request body (what a browser
    // sends when you submit a form containing a file input).
    std::string body =
        "--myboundary\r\n"
        "Content-Disposition: form-data; name=\"title\"\r\n"
        "\r\n"
        "Hello Multipart!\r\n"
        "--myboundary\r\n"
        "Content-Disposition: form-data; name=\"photo\"; filename=\"cat.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n"
        "\r\n"
        "\xff\xd8\xff\xe0" "fake-jpeg-bytes\r\n"
        "--myboundary--\r\n";

    // decode() returns an empty part when parsing fails; pass a `bool&` to
    // check success explicitly.
    bool ok = false;
    multipart::part form = multipart::decode(body, ok);
    if (!ok) {
        std::cerr << "failed to parse multipart body\n";
        return 1;
    }

    std::cout << "document boundary : " << form.boundary() << "\n";
    std::cout << "number of parts   : " << form.list().size() << "\n\n";

    for (const auto& part : form.list()) {
        std::cout << "--- part ---\n";
        for (const auto& [name, value] : part.prototype())
            std::cout << "  " << name << ": " << value << "\n";
        std::cout << "  [content] " << part.content() << "\n\n";
    }

    // Convenience lookup is case-insensitive.
    const auto& first = form.list().front();
    std::cout << "first part's Content-Disposition: "
              << first.header("content-disposition") << "\n";
    return 0;
}