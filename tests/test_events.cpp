// Event callbacks: streaming notifications during decode.

#include <gtest/gtest.h>

#include <multipart/multipart.hpp>

using namespace multipart;

namespace {

struct EventRecorder
{
    std::vector<std::string> events;

    event_cb callback()
    {
        event_cb ecb;
        ecb.boundary_ = [this](std::string_view b) {
            events.push_back("boundary:" + std::string(b));
            return 0;
        };
        ecb.header_field_ = [this](std::string_view h) {
            events.push_back("field:" + std::string(h));
            return 0;
        };
        ecb.header_value_ = [this](std::string_view v) {
            events.push_back("value:" + std::string(v));
            return 0;
        };
        ecb.part_data_ = [this](std::string_view d) {
            events.push_back("data:" + std::string(d));
            return 0;
        };
        ecb.error_ = [this](std::string_view m) {
            events.push_back("error:" + std::string(m));
            return 0;
        };
        return ecb;
    }
};

} // namespace

TEST(events, ordered_notifications)
{
    EventRecorder rec;
    decode(
        "--b\r\n"
        "A: 1\r\n"
        "B: 2\r\n"
        "\r\n"
        "payload\r\n"
        "--b\r\n"
        "\r\n"
        "second\r\n"
        "--b--\r\n",
        rec.callback());

    const std::vector<std::string> expected = {
        // boundary fires once per document (not per part)
        "boundary:--b",
        "field:A", "value:1", "field:B", "value:2",
        "data:payload",
        "data:second",
    };
    EXPECT_EQ(expected, rec.events);
}

TEST(events, single_part_document)
{
    EventRecorder rec;
    decode("--b\r\n\r\nx\r\n--b--\r\n", rec.callback());

    const std::vector<std::string> expected = {
        "boundary:--b", "data:x",
    };
    EXPECT_EQ(expected, rec.events);
}

TEST(events, empty_part_still_emits_data_event)
{
    EventRecorder rec;
    decode("--b\r\n\r\n\r\n--b--\r\n", rec.callback());

    const std::vector<std::string> expected = {
        "boundary:--b", "data:",
    };
    EXPECT_EQ(expected, rec.events);
}

TEST(events, error_callback_on_parse_failure)
{
    EventRecorder rec;
    decode("--b\r\n\r\nunterminated", rec.callback());
    ASSERT_FALSE(rec.events.empty());
    EXPECT_EQ("error:multipart: parse error", rec.events.back());
}

TEST(events, error_callback_fires_once)
{
    EventRecorder rec;
    decode("", rec.callback());
    ASSERT_EQ(1u, rec.events.size());
    EXPECT_EQ("error:multipart: parse error", rec.events[0]);
}

TEST(events, nested_document_emits_nested_events)
{
    EventRecorder rec;
    decode(
        "--outer\r\n"
        "Content-Type: multipart/mixed; boundary=inner\r\n"
        "\r\n"
        "--inner\r\n"
        "\r\n"
        "leaf\r\n"
        "--inner--\r\n"
        "--outer--\r\n",
        rec.callback());

    const std::vector<std::string> expected = {
        "boundary:--outer",
        "field:Content-Type",
        "value:multipart/mixed; boundary=inner",
        "boundary:--inner",
        "data:leaf",
    };
    EXPECT_EQ(expected, rec.events);
}

TEST(events, callbacks_receive_non_owning_views)
{
    // With decode_lazy the part_data_ view points directly into the source
    // buffer. (With the owning decode() it points into a temporary copy that
    // is only valid for the duration of the callback.)
    std::string src = "--b\r\n\r\nabc\r\n--b--\r\n";
    std::string_view captured;

    event_cb ecb;
    ecb.part_data_ = [&](std::string_view d) {
        captured = d; // view, not copy
        return 0;
    };
    decode_lazy(src, ecb);

    EXPECT_EQ("abc", captured);
    EXPECT_GE(captured.data(), src.data());
    EXPECT_LT(captured.data(), src.data() + src.size());
}