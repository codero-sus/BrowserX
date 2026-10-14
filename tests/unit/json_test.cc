// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

#include "core/json.h"

#include <string>

#include "test_harness.h"

using browserx::core::json::Parse;
using browserx::core::json::Serialize;
using browserx::core::json::Value;

TEST(Json, ParsesNullBoolNumbers) {
  auto v = Parse("null");
  ASSERT_TRUE_OR_RETURN(v);
  EXPECT_TRUE(v->IsNull());

  auto b = Parse("true");
  ASSERT_TRUE_OR_RETURN(b);
  EXPECT_TRUE(b->IsBool());
  EXPECT_TRUE(b->AsBool());

  auto n = Parse("-12.5e2");
  ASSERT_TRUE_OR_RETURN(n);
  EXPECT_TRUE(n->IsNumber());
  EXPECT_EQ(n->AsDouble(), -1250.0);
}

TEST(Json, ParsesStringsWithEscapes) {
  auto v = Parse("\"line\\nbreak \\u0041 \\u00e9 \\ud83d\\ude00\"");
  ASSERT_TRUE_OR_RETURN(v);
  EXPECT_TRUE(v->IsString());
  EXPECT_EQ(v->AsString(), std::string("line\nbreak A \xC3\xA9 \xF0\x9F\x98\x80"));
}

TEST(Json, RejectsMalformedInput) {
  std::string error;
  EXPECT_FALSE(Parse("{", &error).has_value());
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(Parse("{\"a\":1,}", &error).has_value());
  EXPECT_FALSE(Parse("[1,2", &error).has_value());
  EXPECT_FALSE(Parse("tru", &error).has_value());
  EXPECT_FALSE(Parse("\"unterminated", &error).has_value());
  EXPECT_FALSE(Parse("01", &error).has_value());
  EXPECT_FALSE(Parse("", &error).has_value());
}

TEST(Json, ParsesNestedStructures) {
  const char* text = R"({
    "modes": {"active": "gaming", "list": ["ultra", "gaming"]},
    "enabled": true,
    "pressure": 0.42
  })";
  auto v = Parse(text);
  ASSERT_TRUE_OR_RETURN(v);
  EXPECT_TRUE(v->IsObject());
  EXPECT_EQ(v->Get("modes").Get("active").AsString(), std::string("gaming"));
  EXPECT_EQ(v->Get("modes").Get("list").At(1).AsString(), std::string("gaming"));
  EXPECT_TRUE(v->Get("enabled").AsBool());
  EXPECT_NEAR(v->Get("pressure").AsDouble(), 0.42, 1e-9);
  EXPECT_EQ(v->Get("missing").AsString("fallback"), std::string("fallback"));
  EXPECT_EQ(v->Get("modes").Get("list").size(), 3u - 1u);
}

TEST(Json, DottedPathLookup) {
  auto v = Parse(R"({"a":{"b":{"c":7}}})");
  ASSERT_TRUE_OR_RETURN(v);
  EXPECT_EQ(v->Path("a.b.c").AsInt(), 7);
  EXPECT_TRUE(v->Path("a.b.nope").IsNull());
  EXPECT_TRUE(v->Path("x.y").IsNull());
}

TEST(Json, RoundTripsThroughSerialize) {
  const char* text = R"({"b":[1,2,3],"a":{"nested":"value"},"s":"quo\"te"})";
  auto v = Parse(text);
  ASSERT_TRUE_OR_RETURN(v);
  const std::string compact = Serialize(*v);
  auto again = Parse(compact);
  ASSERT_TRUE_OR_RETURN(again);
  EXPECT_EQ(again->Get("s").AsString(), std::string("quo\"te"));
  EXPECT_EQ(again->Get("b").At(2).AsInt(), 3);
  EXPECT_EQ(again->Get("a").Get("nested").AsString(), std::string("value"));
}

TEST(Json, SerializesIntegersWithoutTrailingZero) {
  Value v(Value::Object{});
  v["count"] = Value(42);
  v["ratio"] = Value(0.5);
  const std::string out = Serialize(v);
  EXPECT_TRUE(out.find("\"count\":42") != std::string::npos);
  EXPECT_TRUE(out.find("\"ratio\":0.5") != std::string::npos);
}

TEST(Json, PrettyPrintIsIndentedAndParsable) {
  auto v = Parse(R"({"a":1,"b":[true,null]})");
  ASSERT_TRUE_OR_RETURN(v);
  const std::string pretty = Serialize(*v, 2);
  EXPECT_TRUE(pretty.find("\n") != std::string::npos);
  auto again = Parse(pretty);
  ASSERT_TRUE_OR_RETURN(again);
  EXPECT_EQ(again->Get("b").At(0).AsBool(), true);
  EXPECT_TRUE(again->Get("b").At(1).IsNull());
}

TEST(Json, MutatesObjectsAndArrays) {
  Value root(Value::Object{});
  root["list"].Push(Value(1));
  root["list"].Push(Value("two"));
  EXPECT_EQ(root.Get("list").size(), 2u);
  EXPECT_EQ(root.Get("list").At(0).AsInt(), 1);
  EXPECT_TRUE(root.Contains("list"));
  EXPECT_TRUE(root.Erase("list"));
  EXPECT_FALSE(root.Contains("list"));
}

TEST(Json, HandlesUnicodeAndLongStrings) {
  std::string long_string(5000, 'x');
  Value v(Value::Object{});
  v["text"] = Value(long_string);
  v["emoji"] = Value("\xF0\x9F\x9A\x80 rocket");
  auto parsed = Parse(Serialize(v));
  ASSERT_TRUE_OR_RETURN(parsed);
  EXPECT_EQ(parsed->Get("text").AsString().size(), 5000u);
  EXPECT_EQ(parsed->Get("emoji").AsString(), std::string("\xF0\x9F\x9A\x80 rocket"));
}

TEST(Json, RejectsDeepNesting) {
  std::string deep;
  for (int i = 0; i < 200; ++i) deep.push_back('[');
  for (int i = 0; i < 200; ++i) deep.push_back(']');
  std::string error;
  EXPECT_FALSE(Parse(deep, &error).has_value());
  EXPECT_FALSE(error.empty());
}
