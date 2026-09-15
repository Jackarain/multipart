// Compile- and run-check that the header works without exceptions or RTTI.
// This TU intentionally avoids GoogleTest and STL types that require RTTI.

#include <multipart/multipart.hpp>

#include <cassert>
#include <string>

int main()
{
    using namespace multipart;

    std::string data =
        "--b\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "no-exceptions\r\n"
        "--b--\r\n";

    part p = decode(data);
    assert(p.is_list());
    assert(p.list().size() == 1);
    assert(p.list().front().content() == "no-exceptions");

    std::string encoded = encode(p);
    assert(encoded == data);

    part f = make_field("k", "v");
    f.set_boundary(make_boundary());
    part top;
    top.set_boundary(make_boundary());
    top.list().push_back(f);
    assert(!encode(top).empty());

    assert(is_multipart(make_content_type(make_boundary())));
    assert(!extract_boundary("text/plain").empty() == false);

    return 0;
}