#include "aura/json_reader.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool parses(const std::string& text, aura::JsonValue& value, const aura::JsonLimits& limits = {}) {
    return aura::parse_json(text, value, limits);
}

}  // namespace

int main() {
    using namespace aura;

    {
        JsonValue rows;
        expect(parses(R"([{"a":"x","n":12,"b":true,"z":null}])", rows), "rows parse");
        expect(rows.type() == JsonType::Array && rows.items().size() == 1, "one row");
        const auto& row = rows.items()[0];
        expect(row.string_at("a") == "x", "string member");
        expect(row.string_at("n") == "12" && row.find("n")->as_int() == 12, "number keeps its text");
        expect(row.find("b")->as_bool(), "bool member");
        expect(row.find("z")->type() == JsonType::Null, "null member");
        expect(row.find("missing") == nullptr && row.string_at("b").empty(), "absent and non-string members");
        expect(rows.find("a") == nullptr && row.items().empty(), "type-safe accessors");
    }
    {
        JsonValue value;
        expect(parses(R"(" Α😀\n")", value), "unicode escapes parse");
        expect(value.as_string() == " \xCE\x91\xF0\x9F\x98\x80\n", "escapes decode to UTF-8");
        expect(!parses(R"("\udc00")", value), "lone low surrogate rejected");
        expect(!parses(R"("\ud83d x")", value), "unpaired high surrogate rejected");
        expect(parses("\"\xCE\xB1\xCE\xB2\"", value) && value.as_string() == "\xCE\xB1\xCE\xB2", "raw UTF-8 kept");
    }
    {
        JsonValue value;
        expect(parses("[[[[[[[[1]]]]]]]]", value), "depth 8 accepted");
        expect(!parses("[[[[[[[[[1]]]]]]]]]", value), "depth 9 rejected");
        JsonLimits small;
        small.max_string_bytes = 4;
        expect(parses(R"("abcd")", value, small) && !parses(R"("abcde")", value, small), "string cap");
        small = {};
        small.max_bytes = 4;
        expect(parses("[12]", value, small) && !parses("[123]", value, small), "byte cap");
        small = {};
        small.max_nodes = 3;
        expect(parses("[1,2]", value, small) && !parses("[1,2,3]", value, small), "node cap");
        expect(!parses("{} x", value), "trailing garbage rejected");
    }
    {
        JsonValue value;
        for (const char* bad : {R"({"a":})", "[1,]", R"("open)", "\"tab\there\"", "01", "-", "1.", "{'a':1}",
                                "nul", "[1 2]", ""}) {
            expect(!parses(bad, value), bad);
        }
        expect(parses(" -0.5e+3 ", value) && value.number_text() == "-0.5e+3" && value.as_int(7) == 7,
               "fraction number keeps text, not an int");
        expect(parses(R"({"k":{"inner":[true,false]}})", value) &&
                   value.find("k")->find("inner")->items()[1].as_bool(true) == false,
               "nested object");
    }
    std::cout << "json reader tests passed\n";
    return 0;
}
