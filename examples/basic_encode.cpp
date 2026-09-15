// basic_encode.cpp
//
// Build a multipart/form-data document from scratch and serialize it.

#include <multipart/multipart.hpp>

#include <iostream>

int main()
{
    using namespace multipart;

    // 1. Generate a fresh boundary (already in the library's internal form,
    //    i.e. with a leading "--").
    std::string boundary = make_boundary();

    // 2. Build the document root and give it a boundary.
    part form;
    form.set_boundary(boundary);

    // 3. Build parts with the convenience helpers, and remember to give every
    //    child the same boundary.
    part field = make_field("username", "alice");
    field.set_boundary(boundary);

    part avatar = make_file("avatar", "alice.png", "image/png",
                            std::string("fake-png-bytes"));
    avatar.set_boundary(boundary);

    part notes;
    notes.content() = "line1\nline2";               // free-form part
    notes.add_header("Content-Disposition",
                     "form-data; name=\"notes\"");
    notes.set_boundary(boundary);

    form.list().push_back(std::move(field));
    form.list().push_back(std::move(avatar));
    form.list().push_back(std::move(notes));

    // 4. Serialize.
    std::string body = encode(form);

    // 5. The matching Content-Type header for this body:
    std::cout << "Content-Type: " << make_content_type(boundary) << "\n\n";
    std::cout << body;

    return 0;
}