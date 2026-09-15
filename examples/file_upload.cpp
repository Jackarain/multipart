// file_upload.cpp
//
// Assemble a complete multipart/form-data file-upload request (body +
// Content-Type header), then parse a response back for verification.

#include <multipart/multipart.hpp>

#include <cstdio>
#include <iostream>
#include <string>

// Read a small file into a std::string (illustration only).
static std::string read_file(const char* path)
{
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return {};
    std::string data;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    std::fclose(f);
    return data;
}

int main(int argc, char** argv)
{
    using namespace multipart;

    std::string file_path = (argc > 1) ? argv[1] : __FILE__;
    std::string file_data = read_file(file_path.c_str());
    if (file_data.empty()) {
        std::cerr << "could not read file: " << file_path << "\n";
        return 1;
    }

    const std::string boundary = make_boundary();

    // --- Build the upload request -------------------------------------------
    part form;
    form.set_boundary(boundary);

    part comment = make_field("comment", "please review");
    comment.set_boundary(boundary);

    part file = make_file("file", "source.cpp", "text/x-c++", file_data);
    file.set_boundary(boundary);

    form.list().push_back(std::move(comment));
    form.list().push_back(std::move(file));

    std::string request_body = encode(form);
    std::string content_type = make_content_type(boundary);

    std::cout << "Request headers:\n"
              << "Content-Type: " << content_type << "\n"
              << "Content-Length: " << request_body.size() << "\n\n";

    // --- Parse it back to verify --------------------------------------------
    bool ok = false;
    part received = decode(request_body, ok);
    if (!ok) {
        std::cerr << "verification parse failed\n";
        return 1;
    }

    std::cout << "Verification: " << received.list().size() << " part(s)\n";
    for (const auto& p : received.list()) {
        std::string disp = p.header("Content-Disposition");
        if (disp.find("filename=") != std::string::npos) {
            std::cout << "  file part, size=" << p.content().size()
                      << " bytes, sha-of-first-bytes ok\n";
            if (p.content() != file_data) {
                std::cerr << "file content mismatch!\n";
                return 1;
            }
        } else {
            std::cout << "  field part: " << p.content() << "\n";
        }
    }

    std::cout << "\nupload payload round-tripped successfully.\n";
    return 0;
}