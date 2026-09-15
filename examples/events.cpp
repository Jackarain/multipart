// events.cpp
//
// Streaming decode with callbacks, useful when you do not want to materialize
// the whole document at once (e.g. handling very large uploads).

#include <multipart/multipart.hpp>

#include <iostream>
#include <string>

int main()
{
    std::string body =
        "--b\r\n"
        "Content-Disposition: form-data; name=\"a\"\r\n"
        "\r\n"
        "value-a\r\n"
        "--b\r\n"
        "Content-Disposition: form-data; name=\"b\"; filename=\"f.bin\"\r\n"
        "\r\n";
    // Append binary bytes explicitly: a \x00 in a string literal would
    // truncate the implicit C-string conversion.
    body.append("\x00\x01\x02", 3);
    body += "binary\r\n";
    body += "--b--\r\n";

    multipart::event_cb cb;
    cb.boundary_ = [](std::string_view b) {
        std::cout << "[boundary] " << b << "\n";
        return 0;
    };
    cb.header_field_ = [](std::string_view name) {
        std::cout << "[header]   " << name;
        return 0;
    };
    cb.header_value_ = [](std::string_view value) {
        std::cout << ": " << value << "\n";
        return 0;
    };
    cb.part_data_ = [](std::string_view data) {
        std::cout << "[data]     " << data.size()
                  << " bytes (view into buffer)\n";
        return 0;
    };
    cb.error_ = [](std::string_view msg) {
        std::cout << "[error]    " << msg << "\n";
        return 0;
    };

    multipart::decode(body, cb);
    return 0;
}