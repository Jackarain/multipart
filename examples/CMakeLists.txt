add_executable(multipart_example_basic_decode basic_decode.cpp)
add_executable(multipart_example_basic_encode basic_encode.cpp)
add_executable(multipart_example_file_upload file_upload.cpp)
add_executable(multipart_example_lazy_zero_copy lazy_zero_copy.cpp)
add_executable(multipart_example_events events.cpp)

set(MULTIPART_EXAMPLES
    multipart_example_basic_decode
    multipart_example_basic_encode
    multipart_example_file_upload
    multipart_example_lazy_zero_copy
    multipart_example_events)

foreach(ex IN LISTS MULTIPART_EXAMPLES)
    target_link_libraries(${ex} PRIVATE multipart::multipart)
endforeach()