#include "control/osc.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using namespace avgen;
using namespace avgen::control;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes bytes(std::initializer_list<int> values) {
    Bytes out;
    out.reserve(values.size());
    for (int v : values) {
        out.push_back(static_cast<std::uint8_t>(v));
    }
    return out;
}

Bytes stringBytes(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

void append(Bytes& out, const Bytes& more) {
    out.insert(out.end(), more.begin(), more.end());
}

void appendU32(Bytes& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

OscMessage roundTrip(const OscMessage& message) {
    const Bytes packet = encodeMessage(message);
    REQUIRE(packet.size() % 4 == 0);
    auto decoded = decodePacket(packet);
    REQUIRE(decoded);
    REQUIRE(decoded->size() == 1);
    return decoded->front();
}

OscMessage message(std::string address, std::vector<OscArg> args = {}) {
    OscMessage m;
    m.address = std::move(address);
    m.args = std::move(args);
    return m;
}

template <typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

} // namespace

// ---- encoding ----

TEST_CASE("OSC: encodes the specification examples byte for byte", "[control][osc]") {
    SECTION("/oscillator/4/frequency ,f 440.0") {
        const Bytes packet = encodeMessage(message("/oscillator/4/frequency", {440.0f}));
        const Bytes expected = bytes({0x2f, 0x6f, 0x73, 0x63, 0x69, 0x6c, 0x6c, 0x61, 0x74, 0x6f, 0x72,
                                      0x2f, 0x34, 0x2f, 0x66, 0x72, 0x65, 0x71, 0x75, 0x65, 0x6e, 0x63,
                                      0x79, 0x00, 0x2c, 0x66, 0x00, 0x00, 0x43, 0xdc, 0x00, 0x00});
        CHECK(packet == expected);
    }
    SECTION("/foo ,iisff 1000 -1 hello 1.234 5.678") {
        const Bytes packet = encodeMessage(
            message("/foo", {std::int32_t{1000}, std::int32_t{-1}, std::string("hello"), 1.234f, 5.678f}));
        const Bytes expected =
            bytes({0x2f, 0x66, 0x6f, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x69, 0x69, 0x73, 0x66, 0x66,
                   0x00, 0x00, 0x00, 0x00, 0x03, 0xe8, 0xff, 0xff, 0xff, 0xff, 0x68, 0x65, 0x6c, 0x6c,
                   0x6f, 0x00, 0x00, 0x00, 0x3f, 0x9d, 0xf3, 0xb6, 0x40, 0xb5, 0xb2, 0x2d});
        CHECK(packet == expected);
    }
    SECTION("a message without arguments still carries the ',' type tag string") {
        CHECK(encodeMessage(message("/a")) == bytes({'/', 'a', 0, 0, ',', 0, 0, 0}));
    }
}

TEST_CASE("OSC: round trips every argument type", "[control][osc]") {
    OscMessage original =
        message("/all/types", {std::int32_t{-123456}, 3.5f, std::string("text"),
                               OscBlob{bytes({1, 2, 3, 4, 5})}, std::int64_t{-1234567890123LL},
                               2.718281828459045, true, false, std::monostate{}, std::monostate{}});
    original.typeTags = "ifsbhdTFNI"; // keeps 'I' (impulse) distinct from 'N' when re-encoded

    const OscMessage decoded = roundTrip(original);
    CHECK(decoded.address == "/all/types");
    CHECK(decoded.typeTags == "ifsbhdTFNI");
    CHECK(decoded.timeTag == 1);
    REQUIRE(decoded.args.size() == 10);
    CHECK(std::get<std::int32_t>(decoded.args[0]) == -123456);
    CHECK(std::get<float>(decoded.args[1]) == 3.5f);
    CHECK(std::get<std::string>(decoded.args[2]) == "text");
    CHECK(std::get<OscBlob>(decoded.args[3]).bytes == bytes({1, 2, 3, 4, 5}));
    CHECK(std::get<std::int64_t>(decoded.args[4]) == -1234567890123LL);
    CHECK(std::get<double>(decoded.args[5]) == 2.718281828459045);
    CHECK(std::get<bool>(decoded.args[6]) == true);
    CHECK(std::get<bool>(decoded.args[7]) == false);
    CHECK(std::holds_alternative<std::monostate>(decoded.args[8]));
    CHECK(std::holds_alternative<std::monostate>(decoded.args[9]));

    SECTION("type tags are derived from the values when none are given") {
        original.typeTags.clear();
        CHECK(roundTrip(original).typeTags == "ifsbhdTFNN");
    }
    SECTION("type tags that do not describe the arguments are ignored") {
        original.typeTags = "ssssssssss";
        CHECK(roundTrip(original).typeTags == "ifsbhdTFNN");
    }
    SECTION("numeric conveniences") {
        CHECK(decoded.number(0) == -123456.0f);
        CHECK(decoded.number(1) == 3.5f);
        CHECK(decoded.number(2, 7.0f) == 7.0f); // string
        CHECK(decoded.number(3, 7.0f) == 7.0f); // blob
        CHECK(decoded.number(4) == static_cast<float>(-1234567890123LL));
        CHECK(decoded.number(5) == static_cast<float>(2.718281828459045));
        CHECK(decoded.number(6) == 1.0f);
        CHECK(decoded.number(7) == 0.0f);
        CHECK(decoded.number(8, 7.0f) == 7.0f); // nil
        CHECK(decoded.number(99, 7.0f) == 7.0f);
        CHECK(decoded.hasNumber(0));
        CHECK(decoded.hasNumber(1));
        CHECK_FALSE(decoded.hasNumber(2));
        CHECK(decoded.hasNumber(6));
        CHECK_FALSE(decoded.hasNumber(8));
        CHECK_FALSE(decoded.hasNumber(10));
        CHECK(decoded.numberCount() == 2);
        CHECK(decoded.text(2) == "text");
        CHECK(decoded.text(0, "none") == "none");
        CHECK(decoded.text(42, "none") == "none");
        const OscMessage numbers =
            message("/n", {true, std::int32_t{2}, 3.0f, 4.0, std::int64_t{5}, std::string("x")});
        CHECK(numbers.numberCount() == 5);
        CHECK(message("/n").numberCount() == 0);
    }
    SECTION("extension tags decode to the nearest type and are kept on re-encode") {
        Bytes packet = stringBytes("/ext");
        packet.resize(8, 0);
        append(packet, bytes({',', 'c', 'r', 'm', 't', 'S', 0, 0}));
        appendU32(packet, 'A');
        appendU32(packet, 0x11223344u);
        appendU32(packet, 0x00903C7Fu);
        appendU32(packet, 1);
        appendU32(packet, 2);
        append(packet, bytes({'s', 'y', 'm', 0}));
        auto decoded2 = decodePacket(packet);
        REQUIRE(decoded2);
        REQUIRE(decoded2->size() == 1);
        const OscMessage& ext = decoded2->front();
        CHECK(ext.typeTags == "crmtS");
        REQUIRE(ext.args.size() == 5);
        CHECK(std::get<std::int32_t>(ext.args[0]) == 'A');
        CHECK(std::get<std::int32_t>(ext.args[1]) == 0x11223344);
        CHECK(std::get<std::int32_t>(ext.args[2]) == 0x00903C7F);
        CHECK(std::get<std::int64_t>(ext.args[3]) == 0x100000002LL);
        CHECK(std::get<std::string>(ext.args[4]) == "sym");
        CHECK(encodeMessage(ext) == packet);
    }
    SECTION("array brackets are tolerated and flattened") {
        Bytes packet = stringBytes("/arr");
        packet.resize(8, 0);
        append(packet, bytes({',', '[', 'i', 'i', ']', 0, 0, 0}));
        appendU32(packet, 1);
        appendU32(packet, 2);
        auto decoded2 = decodePacket(packet);
        REQUIRE(decoded2);
        REQUIRE(decoded2->front().args.size() == 2);
        CHECK(decoded2->front().typeTags == "[ii]");
        CHECK(decoded2->front().number(1) == 2.0f);
    }
}

TEST_CASE("OSC: string and blob padding boundaries", "[control][osc]") {
    SECTION("strings of every length modulo 4") {
        const std::vector<std::pair<std::string, std::size_t>> cases = {
            {"", 4}, {"a", 4}, {"abc", 4}, {"abcd", 8}, {"abcde", 8}, {"abcdefg", 8}, {"abcdefgh", 12}};
        for (const auto& [text, paddedSize] : cases) {
            INFO("string '" << text << "'");
            const Bytes packet = encodeMessage(message("/s", {text}));
            CHECK(packet.size() == 4 + 4 + paddedSize);
            const OscMessage decoded = roundTrip(message("/s", {text}));
            CHECK(decoded.text(0) == text);
        }
    }
    SECTION("blobs of every length modulo 4") {
        for (std::size_t length : {0, 1, 2, 3, 4, 5, 7, 8, 9}) {
            INFO("blob of " << length << " bytes");
            Bytes payload;
            for (std::size_t i = 0; i < length; ++i) {
                payload.push_back(static_cast<std::uint8_t>(0xA0 + i));
            }
            const Bytes packet = encodeMessage(message("/b", {OscBlob{payload}}));
            CHECK(packet.size() == 4 + 4 + 4 + ((length + 3) / 4) * 4);
            const OscMessage decoded = roundTrip(message("/b", {OscBlob{payload}}));
            REQUIRE(decoded.args.size() == 1);
            CHECK(std::get<OscBlob>(decoded.args[0]).bytes == payload);
        }
    }
    SECTION("addresses of every length modulo 4") {
        for (const std::string address : {"/", "/ab", "/abc", "/abcd", "/abcde/fghij"}) {
            const Bytes packet = encodeMessage(message(address));
            CHECK(packet.size() == ((address.size() + 1 + 3) / 4) * 4 + 4);
            CHECK(roundTrip(message(address)).address == address);
        }
    }
}

// ---- bundles ----

TEST_CASE("OSC: bundles are flattened with their time tags", "[control][osc]") {
    const std::uint64_t outerTag = 0x1234567890ABCDEFULL;
    const std::uint64_t innerTag = 99;
    const std::vector<OscMessage> outerMessages = {message("/one", {std::int32_t{1}}),
                                                   message("/two", {std::string("2")})};
    const std::vector<OscMessage> innerMessages = {message("/three", {3.0f})};

    Bytes packet = encodeBundle(outerMessages, outerTag);
    CHECK(packet.size() >= 16);
    CHECK(std::string(packet.begin(), packet.begin() + 8) == std::string("#bundle\0", 8));
    const Bytes nested = encodeBundle(innerMessages, innerTag);
    appendU32(packet, static_cast<std::uint32_t>(nested.size()));
    append(packet, nested);

    auto decoded = decodePacket(packet);
    REQUIRE(decoded);
    REQUIRE(decoded->size() == 3);
    CHECK((*decoded)[0].address == "/one");
    CHECK((*decoded)[0].timeTag == outerTag);
    CHECK((*decoded)[0].number(0) == 1.0f);
    CHECK((*decoded)[1].address == "/two");
    CHECK((*decoded)[1].timeTag == outerTag);
    CHECK((*decoded)[1].text(0) == "2");
    CHECK((*decoded)[2].address == "/three");
    CHECK((*decoded)[2].timeTag == innerTag);
    CHECK((*decoded)[2].number(0) == 3.0f);

    SECTION("an empty bundle decodes to no messages") {
        auto empty = decodePacket(encodeBundle({}, 1));
        REQUIRE(empty);
        CHECK(empty->empty());
    }
    SECTION("a bare message has the immediate time tag") {
        CHECK(roundTrip(message("/bare")).timeTag == 1);
    }
    SECTION("truncating the bundle never crashes and never yields all three messages") {
        for (std::size_t length = 0; length < packet.size(); ++length) {
            INFO("prefix of " << length << " bytes");
            auto prefix = decodePacket(std::span<const std::uint8_t>(packet.data(), length));
            CHECK((!prefix || prefix->size() < 3));
        }
    }
    SECTION("a bundle with one malformed element fails as a whole") {
        Bytes broken = encodeBundle(outerMessages, outerTag);
        appendU32(broken, 8);
        append(broken, bytes({'n', 'o', 's', 'l', 'a', 's', 'h', 0}));
        auto result = decodePacket(broken);
        REQUIRE_FALSE(result);
        CHECK_THAT(result.error().message, ContainsSubstring("does not start with '/'"));
    }
    SECTION("an element size beyond the packet is an error") {
        Bytes broken = encodeBundle({}, 1);
        appendU32(broken, 1000);
        append(broken, bytes({'/', 'a', 0, 0, ',', 0, 0, 0}));
        auto result = decodePacket(broken);
        REQUIRE_FALSE(result);
        CHECK_THAT(result.error().message, ContainsSubstring("exceeds"));
    }
    SECTION("absurd nesting is rejected instead of overflowing the stack") {
        Bytes deep = encodeBundle(innerMessages, innerTag);
        for (int level = 0; level < 64; ++level) {
            Bytes wrapper = encodeBundle({}, 1);
            appendU32(wrapper, static_cast<std::uint32_t>(deep.size()));
            append(wrapper, deep);
            deep = std::move(wrapper);
        }
        auto result = decodePacket(deep);
        REQUIRE_FALSE(result);
        CHECK_THAT(result.error().message, ContainsSubstring("nesting"));
    }
}

// ---- malformed input ----

TEST_CASE("OSC: malformed packets are errors, never crashes", "[control][osc]") {
    SECTION("every proper prefix of a valid message is an error") {
        OscMessage full = message("/prefix/test",
                                  {std::int32_t{7}, 1.5f, std::string("abc"), OscBlob{bytes({9, 8, 7, 6, 5})},
                                   std::int64_t{1}, 2.0, true, false, std::monostate{}});
        const Bytes packet = encodeMessage(full);
        REQUIRE(decodePacket(packet));
        for (std::size_t length = 0; length < packet.size(); ++length) {
            INFO("prefix of " << length << " of " << packet.size() << " bytes");
            auto result = decodePacket(std::span<const std::uint8_t>(packet.data(), length));
            CHECK_FALSE(result);
        }
    }
    SECTION("empty packet") {
        auto result = decodePacket({});
        REQUIRE_FALSE(result);
        CHECK_THAT(result.error().message, ContainsSubstring("empty"));
    }
    SECTION("address without leading '/'") {
        auto result = decodePacket(bytes({'a', 'b', 'c', 0, ',', 0, 0, 0}));
        REQUIRE_FALSE(result);
        CHECK_THAT(result.error().message, ContainsSubstring("does not start with '/'"));
    }
    SECTION("unterminated address") {
        auto result = decodePacket(bytes({'/', 'a', 'b', 'c', 'd', 'e', 'f', 'g'}));
        REQUIRE_FALSE(result);
        CHECK_THAT(result.error().message, ContainsSubstring("unterminated address"));
    }
    SECTION("missing type tag string") {
        auto result = decodePacket(bytes({'/', 'a', 0, 0}));
        REQUIRE_FALSE(result);
        CHECK_THAT(result.error().message, ContainsSubstring("type tag"));
    }
    SECTION("type tag string without ','") {
        auto result = decodePacket(bytes({'/', 'a', 0, 0, 'i', 0, 0, 0, 0, 0, 0, 1}));
        REQUIRE_FALSE(result);
        CHECK_THAT(result.error().message, ContainsSubstring("must start with ','"));
    }
    SECTION("unknown type tag fails the message") {
        auto result = decodePacket(bytes({'/', 'a', 0, 0, ',', 'i', 'z', 0, 0, 0, 0, 1, 0, 0, 0, 2}));
        REQUIRE_FALSE(result);
        CHECK_THAT(result.error().message, ContainsSubstring("unknown type tag 'z'"));
    }
    SECTION("blob size beyond the packet") {
        auto result =
            decodePacket(bytes({'/', 'a', 0, 0, ',', 'b', 0, 0, 0x7f, 0xff, 0xff, 0xff, 1, 2, 3, 4}));
        REQUIRE_FALSE(result);
        CHECK_THAT(result.error().message, ContainsSubstring("truncated argument for type tag 'b'"));
    }
    SECTION("negative blob size is treated as too large") {
        auto result = decodePacket(bytes({'/', 'a', 0, 0, ',', 'b', 0, 0, 0xff, 0xff, 0xff, 0xff}));
        CHECK_FALSE(result);
    }
    SECTION("bundle header only") {
        CHECK_FALSE(decodePacket(stringBytes(std::string("#bundle\0", 8))));
        CHECK_FALSE(decodePacket(stringBytes("#bundl")));
    }
    SECTION("trailing bytes after the arguments are tolerated") {
        auto result =
            decodePacket(bytes({'/', 'a', 0, 0, ',', 'i', 0, 0, 0, 0, 0, 5, 0xde, 0xad, 0xbe, 0xef}));
        REQUIRE(result);
        CHECK(result->front().number(0) == 5.0f);
    }
    SECTION("pseudo-random garbage behind valid-looking headers never crashes") {
        std::uint32_t state = 0x9E3779B9u;
        const auto next = [&state] {
            state = state * 1664525u + 1013904223u;
            return static_cast<std::uint8_t>(state >> 24);
        };
        for (int iteration = 0; iteration < 500; ++iteration) {
            Bytes packet =
                (iteration % 2 == 0) ? bytes({'/', 'a', 0, 0}) : stringBytes(std::string("#bundle\0", 8));
            const std::size_t length = next() % 96;
            for (std::size_t i = 0; i < length; ++i) {
                packet.push_back(next());
            }
            (void)decodePacket(packet);
        }
        // Bit flips in a valid bundle.
        const std::vector<OscMessage> one = {message("/x", {std::int32_t{1}, std::string("yz")})};
        const Bytes valid = encodeBundle(one, 5);
        for (std::size_t i = 0; i < valid.size(); ++i) {
            for (int bit = 0; bit < 8; ++bit) {
                Bytes flipped = valid;
                flipped[i] = static_cast<std::uint8_t>(flipped[i] ^ (1u << bit));
                (void)decodePacket(flipped);
            }
        }
        SUCCEED("no crash");
    }
}

// ---- address patterns ----

TEST_CASE("OSC: address pattern matching", "[control][osc]") {
    struct Case {
        const char* pattern;
        const char* address;
        bool expected;
    };
    const Case table[] = {
        // literal
        {"/a/b", "/a/b", true},
        {"/a/b", "/a/bc", false},
        {"/a/b", "/a", false},
        {"/a", "/a/b", false},
        {"/", "/", true},
        {"", "", true},
        {"/a/b", "/A/b", false},
        // '*' within a segment
        {"/a/*", "/a/b", true},
        {"/a/*", "/a/", true},
        {"/a/*", "/a/bcd", true},
        {"/a/*", "/a/b/c", false},
        {"/*/b", "/a/b", true},
        {"/*/b", "/a/c/b", false},
        {"/a/b*", "/a/b", true},
        {"/a/b*", "/a/bxyz", true},
        {"/a/b*", "/a/cb", false},
        {"/a/*b", "/a/xxb", true},
        {"/a/*b*", "/a/xbx", true},
        {"/a/*b*", "/a/xcx", false},
        {"/a/**", "/a/bc", true},
        {"*", "abc", true},
        {"*", "a/b", false},
        {"/a/*/c", "/a/b/c", true},
        {"/a/*/c", "/a/b/d/c", false},
        // '?'
        {"/a/?", "/a/b", true},
        {"/a/?", "/a/bc", false},
        {"/a/?", "/a/", false},
        {"/a?b", "/a/b", false},
        {"/a/b?", "/a/bc", true},
        // character classes
        {"/a/[bc]", "/a/b", true},
        {"/a/[bc]", "/a/c", true},
        {"/a/[bc]", "/a/d", false},
        {"/a/[bc]", "/a/bc", false},
        {"/a/[!b]", "/a/c", true},
        {"/a/[!b]", "/a/b", false},
        {"/a/[!b]", "/a//", false},
        {"/a/[a-c]x", "/a/bx", true},
        {"/a/[a-c]x", "/a/dx", false},
        {"/a/[c-a]", "/a/b", true},
        {"/a/[0-9][0-9]", "/a/42", true},
        {"/a/[0-9][0-9]", "/a/4", false},
        {"/a/[a-]", "/a/-", true},
        {"/a/[a-]", "/a/a", true},
        {"/a/[a-]", "/a/b", false},
        {"/a/[!a-z]", "/a/5", true},
        {"/a/[]", "/a/b", false},
        {"/a/[]", "/a/", false},
        {"/a/[!]", "/a/z", true},
        {"/a/[b", "/a/[b", true}, // unterminated class is literal
        {"/a/[b", "/a/b", false},
        {"/a/[/]", "/a//", false},
        // alternatives
        {"/a/{x,y}/z", "/a/x/z", true},
        {"/a/{x,y}/z", "/a/y/z", true},
        {"/a/{x,y}/z", "/a/w/z", false},
        {"/a/{x,y}/z", "/a/xy/z", false},
        {"/a/{foo,bar}", "/a/foo", true},
        {"/a/{foo,bar}", "/a/bar", true},
        {"/a/{foo,bar}", "/a/ba", false},
        {"/a/{foo,bar}", "/a/barr", false},
        {"/a/{foo,}", "/a/", true},
        {"/a/{f,fo,foo}x", "/a/foox", true},
        {"/a/{x", "/a/{x", true}, // unterminated brace is literal
        {"/a/{x", "/a/x", false},
        // combinations
        {"/scene/*/[xyz]", "/scene/orb/x", true},
        {"/scene/*/[xyz]", "/scene/orb/w", false},
        {"/scene/{orb,cube}/scale?", "/scene/cube/scale2", true},
        {"/scene/{orb,cube}/*", "/scene/cube/scale/x", false},
        {"/*/*/*", "/a/b/c", true},
        {"/*/*/*", "/a/b", false},
        {"/a*a*a*a*b", "/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac", false},
    };
    for (const Case& c : table) {
        INFO("pattern '" << c.pattern << "' vs '" << c.address << "'");
        CHECK(matchAddress(c.pattern, c.address) == c.expected);
    }
    SECTION("pathological patterns terminate") {
        const std::string pattern = "/" + std::string(2000, '*') + "x";
        const std::string address = "/" + std::string(2000, 'y');
        CHECK_FALSE(matchAddress(pattern, address));
        CHECK(matchAddress(pattern, address + "x"));
        std::string alternating;
        for (int i = 0; i < 400; ++i) {
            alternating += "*a";
        }
        CHECK_FALSE(matchAddress("/" + alternating + "b", "/" + std::string(400, 'a')));
        CHECK(matchAddress("/" + alternating + "b", "/" + std::string(400, 'a') + "b"));
        std::string braces;
        for (int i = 0; i < 300; ++i) {
            braces += "{a,a}";
        }
        (void)matchAddress("/" + braces + "b", "/" + std::string(300, 'a')); // budget-limited
        CHECK(matchAddress("/{a,b}{a,b}{a,b}{a,b}{a,b}{a,b}{a,b}{a,b}", "/abababab"));
        SUCCEED("terminated");
    }
}

TEST_CASE("OSC: isValidAddress", "[control][osc]") {
    CHECK(isValidAddress("/"));
    CHECK(isValidAddress("/a"));
    CHECK(isValidAddress("/scene/orb/scale"));
    CHECK(isValidAddress("/scene/orb-1/scale_x.y"));
    CHECK_FALSE(isValidAddress(""));
    CHECK_FALSE(isValidAddress("scene/orb"));
    CHECK_FALSE(isValidAddress("/scene/"));
    CHECK_FALSE(isValidAddress("//"));
    CHECK_FALSE(isValidAddress("/scene//orb"));
    CHECK_FALSE(isValidAddress("/scene orb"));
    CHECK_FALSE(isValidAddress("/scene\torb"));
    CHECK_FALSE(isValidAddress("/scene\n"));
    CHECK_FALSE(isValidAddress(std::string_view("/a\0b", 4)));
    CHECK_FALSE(isValidAddress("/a\x7f"));
}

// ---- receiver + sender ----

TEST_CASE("OSC: loopback receiver and sender", "[control][osc]") {
    OscReceiver receiver;
    CHECK_FALSE(receiver.isOpen());
    CHECK(receiver.port() == 0);
    auto opened = receiver.open(0, "127.0.0.1");
    REQUIRE(opened);
    REQUIRE(receiver.isOpen());
    REQUIRE(receiver.port() != 0);

    OscSender sender;
    CHECK_FALSE(sender.isOpen());
    REQUIRE(sender.open("127.0.0.1", receiver.port()));
    REQUIRE(sender.isOpen());

    REQUIRE(sender.send(message("/first", {std::int32_t{1}})));
    REQUIRE(sender.send(message("/second", {2.5f, std::string("two")})));
    REQUIRE(sender.send(message("/third", {OscBlob{bytes({3, 3, 3})}, true})));
    const std::uint64_t tag = ntpNow();
    const std::vector<OscMessage> bundle = {message("/fourth", {std::int64_t{4}}), message("/fifth", {5.0})};
    REQUIRE(sender.sendBundle(bundle, tag));

    std::vector<OscMessage> received;
    REQUIRE(waitFor([&] {
        receiver.drain(received);
        return received.size() >= 5;
    }));
    REQUIRE(received.size() == 5);
    CHECK(received[0].address == "/first");
    CHECK(received[0].number(0) == 1.0f);
    CHECK(received[0].timeTag == 1);
    CHECK(received[1].address == "/second");
    CHECK(received[1].number(0) == 2.5f);
    CHECK(received[1].text(1) == "two");
    CHECK(received[1].typeTags == "fs");
    CHECK(received[2].address == "/third");
    CHECK(std::get<OscBlob>(received[2].args.at(0)).bytes == bytes({3, 3, 3}));
    CHECK(received[2].number(1) == 1.0f);
    CHECK(received[3].address == "/fourth");
    CHECK(received[3].number(0) == 4.0f);
    CHECK(received[3].timeTag == tag);
    CHECK(received[4].address == "/fifth");
    CHECK(received[4].number(0) == 5.0f);
    CHECK(received[4].timeTag == tag);

    OscReceiver::Stats stats = receiver.stats();
    CHECK(stats.packets == 4);
    CHECK(stats.messages == 5);
    CHECK(stats.errors == 0);
    CHECK(stats.lastError.empty());
    CHECK_THAT(stats.lastSender, StartsWith("127.0.0.1:"));

    SECTION("a malformed packet counts as an error and is reported") {
        REQUIRE(sender.sendRaw(bytes({'n', 'o', 0, 0, ',', 0, 0, 0})));
        REQUIRE(waitFor([&] { return receiver.stats().errors >= 1; }));
        stats = receiver.stats();
        CHECK(stats.packets == 5);
        CHECK(stats.messages == 5);
        CHECK(stats.errors == 1);
        CHECK_THAT(stats.lastError, ContainsSubstring("does not start with '/'"));
        std::vector<OscMessage> none;
        CHECK(receiver.drain(none) == 0);
    }
    SECTION("drain appends and returns the count") {
        REQUIRE(sender.send(message("/again")));
        std::vector<OscMessage> out(1);
        REQUIRE(waitFor([&] { return receiver.drain(out) > 0; }));
        REQUIRE(out.size() == 2);
        CHECK(out[1].address == "/again");
    }
}

TEST_CASE("OSC: queue limit drops the oldest messages", "[control][osc]") {
    OscReceiver receiver;
    receiver.setQueueLimit(2);
    REQUIRE(receiver.open(0, "127.0.0.1"));
    OscSender sender;
    REQUIRE(sender.open("127.0.0.1", receiver.port()));
    for (std::int32_t i = 1; i <= 5; ++i) {
        REQUIRE(sender.send(message("/n", {i})));
    }
    REQUIRE(waitFor([&] { return receiver.stats().messages >= 5; }));
    std::vector<OscMessage> received;
    CHECK(receiver.drain(received) == 2);
    REQUIRE(received.size() == 2);
    CHECK(received[0].number(0) == 4.0f);
    CHECK(received[1].number(0) == 5.0f);
    const OscReceiver::Stats stats = receiver.stats();
    CHECK(stats.packets == 5);
    CHECK(stats.messages == 5);
    CHECK(stats.errors == 3);
    CHECK_THAT(stats.lastError, ContainsSubstring("dropped"));

    SECTION("lowering the limit trims the inbox immediately") {
        for (std::int32_t i = 6; i <= 7; ++i) {
            REQUIRE(sender.send(message("/n", {i})));
        }
        REQUIRE(waitFor([&] { return receiver.stats().messages >= 7; }));
        receiver.setQueueLimit(1);
        received.clear();
        CHECK(receiver.drain(received) == 1);
        REQUIRE(received.size() == 1);
        CHECK(received[0].number(0) == 7.0f);
    }
}

TEST_CASE("OSC: close, reopen and error paths", "[control][osc]") {
    OscReceiver receiver;
    REQUIRE(receiver.open(0, "127.0.0.1"));
    const std::uint16_t firstPort = receiver.port();
    REQUIRE(firstPort != 0);
    receiver.close();
    CHECK_FALSE(receiver.isOpen());
    CHECK(receiver.port() == 0);
    receiver.close(); // idempotent

    REQUIRE(receiver.open(0, "127.0.0.1"));
    CHECK(receiver.isOpen());
    CHECK(receiver.port() != 0);
    OscSender sender;
    REQUIRE(sender.open("localhost", receiver.port())); // name resolution
    REQUIRE(sender.send(message("/after/reopen")));
    std::vector<OscMessage> received;
    REQUIRE(waitFor([&] {
        receiver.drain(received);
        return !received.empty();
    }));
    CHECK(received.front().address == "/after/reopen");
    CHECK(receiver.stats().packets == 1); // stats reset on open

    // Re-opening while open closes the previous socket first.
    REQUIRE(receiver.open(0, "127.0.0.1"));
    CHECK(receiver.isOpen());
    receiver.close();

    SECTION("bad bind address") {
        auto result = receiver.open(0, "not-an-ip");
        REQUIRE_FALSE(result);
        CHECK_THAT(result.error().message, ContainsSubstring("not an IPv4 address"));
        CHECK_FALSE(receiver.isOpen());
    }
    SECTION("sending through a closed sender fails") {
        sender.close();
        CHECK_FALSE(sender.isOpen());
        auto result = sender.send(message("/x"));
        REQUIRE_FALSE(result);
        CHECK_THAT(result.error().message, ContainsSubstring("not open"));
        CHECK_FALSE(sender.sendBundle({}, 1));
        CHECK_FALSE(sender.sendRaw(bytes({'/', 'x', 0, 0, ',', 0, 0, 0})));
        OscSender fresh;
        CHECK_FALSE(fresh.send(message("/x")));
    }
    SECTION("an oversized datagram is reported, not silently truncated") {
        OscReceiver sink;
        REQUIRE(sink.open(0, "127.0.0.1"));
        OscSender big;
        REQUIRE(big.open("127.0.0.1", sink.port()));
        auto result = big.send(message("/huge", {OscBlob{Bytes(200000, 0x55)}}));
        CHECK_FALSE(result);
    }
}

// ---- NTP ----

TEST_CASE("OSC: NTP time tag helpers", "[control][osc]") {
    const std::uint64_t now = ntpNow();
    const std::uint64_t seconds = now >> 32;
    // 2026-01-01 in NTP seconds is 2208988800 + 1767225600; be generous with the upper bound.
    CHECK(seconds > 2208988800ULL + 1767225600ULL);
    CHECK(seconds < 2208988800ULL + 4102444800ULL); // before 2100
    const std::uint64_t later = ntpNow();
    CHECK(later >= now);

    CHECK(ntpFromSeconds(1.5) == ((std::uint64_t{1} << 32) | 0x80000000ULL));
    CHECK(ntpFromSeconds(0.0) == 0);
    CHECK(ntpFromSeconds(-3.0) == 0);
    CHECK(ntpToSeconds(std::uint64_t{1} << 32) == 1.0);
    CHECK(ntpToSeconds((std::uint64_t{2} << 32) | 0x40000000ULL) == 2.25);
    CHECK(ntpToSeconds(1) > 0.0);
    CHECK(ntpToSeconds(1) < 1e-9);

    const double nowSeconds = ntpToSeconds(now);
    const std::uint64_t back = ntpFromSeconds(nowSeconds);
    CHECK((back >> 32) == seconds);
    // A double keeps ~53 bits: the fraction survives to well under a microsecond.
    const auto fractionDelta =
        static_cast<std::int64_t>(back & 0xFFFFFFFFULL) - static_cast<std::int64_t>(now & 0xFFFFFFFFULL);
    CHECK(fractionDelta < 4096);
    CHECK(fractionDelta > -4096);
    const double halfSecond = 12345.5;
    CHECK(ntpToSeconds(ntpFromSeconds(halfSecond)) == halfSecond);
}
