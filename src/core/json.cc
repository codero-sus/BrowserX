#include "core/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace browserx {
namespace core {
namespace json {
namespace {

constexpr int kMaxDepth = 64;

class Parser {
 public:
  Parser(std::string_view text, std::string* error) : text_(text), error_(error) {}

  bool Run(Value* out) {
    SkipWs();
    if (!ParseValue(out, 0)) return false;
    SkipWs();
    if (pos_ != text_.size()) return Fail("trailing characters after top-level value");
    return true;
  }

 private:
  bool Fail(const std::string& message) {
    if (error_ != nullptr && error_->empty()) {
      *error_ = "offset " + std::to_string(pos_) + ": " + message;
    }
    return false;
  }

  void SkipWs() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  char Peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

  bool ParseValue(Value* out, int depth) {
    if (depth > kMaxDepth) return Fail("maximum nesting depth exceeded");
    if (pos_ >= text_.size()) return Fail("unexpected end of input");
    switch (Peek()) {
      case '{': return ParseObject(out, depth);
      case '[': return ParseArray(out, depth);
      case '"': {
        std::string s;
        if (!ParseString(&s)) return false;
        *out = Value(std::move(s));
        return true;
      }
      case 't':
        if (text_.compare(pos_, 4, "true") == 0) {
          pos_ += 4;
          *out = Value(true);
          return true;
        }
        return Fail("invalid literal");
      case 'f':
        if (text_.compare(pos_, 5, "false") == 0) {
          pos_ += 5;
          *out = Value(false);
          return true;
        }
        return Fail("invalid literal");
      case 'n':
        if (text_.compare(pos_, 4, "null") == 0) {
          pos_ += 4;
          *out = Value(nullptr);
          return true;
        }
        return Fail("invalid literal");
      default: return ParseNumber(out);
    }
  }

  bool ParseObject(Value* out, int depth) {
    ++pos_;  // '{'
    Value::Object object;
    SkipWs();
    if (Peek() == '}') {
      ++pos_;
      *out = Value(std::move(object));
      return true;
    }
    while (true) {
      SkipWs();
      if (Peek() != '"') return Fail("expected object key string");
      std::string key;
      if (!ParseString(&key)) return false;
      SkipWs();
      if (Peek() != ':') return Fail("expected ':' after object key");
      ++pos_;
      SkipWs();
      Value member;
      if (!ParseValue(&member, depth + 1)) return false;
      object[std::move(key)] = std::move(member);
      SkipWs();
      const char c = Peek();
      if (c == ',') {
        ++pos_;
        continue;
      }
      if (c == '}') {
        ++pos_;
        break;
      }
      return Fail("expected ',' or '}' in object");
    }
    *out = Value(std::move(object));
    return true;
  }

  bool ParseArray(Value* out, int depth) {
    ++pos_;  // '['
    Value::Array array;
    SkipWs();
    if (Peek() == ']') {
      ++pos_;
      *out = Value(std::move(array));
      return true;
    }
    while (true) {
      SkipWs();
      Value element;
      if (!ParseValue(&element, depth + 1)) return false;
      array.push_back(std::move(element));
      SkipWs();
      const char c = Peek();
      if (c == ',') {
        ++pos_;
        continue;
      }
      if (c == ']') {
        ++pos_;
        break;
      }
      return Fail("expected ',' or ']' in array");
    }
    *out = Value(std::move(array));
    return true;
  }

  static void AppendUtf8(std::string* out, uint32_t cp) {
    if (cp <= 0x7F) {
      out->push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
      out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  bool ParseHex4(uint32_t* out) {
    if (pos_ + 4 > text_.size()) return Fail("truncated \\u escape");
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_ + static_cast<size_t>(i)];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<uint32_t>(c - 'A' + 10);
      } else {
        return Fail("invalid hex digit in \\u escape");
      }
    }
    pos_ += 4;
    *out = value;
    return true;
  }

  bool ParseString(std::string* out) {
    if (Peek() != '"') return Fail("expected '\"'");
    ++pos_;
    out->clear();
    while (true) {
      if (pos_ >= text_.size()) return Fail("unterminated string");
      const unsigned char c = static_cast<unsigned char>(text_[pos_]);
      if (c == '"') {
        ++pos_;
        return true;
      }
      if (c == '\\') {
        ++pos_;
        if (pos_ >= text_.size()) return Fail("unterminated escape");
        const char e = text_[pos_++];
        switch (e) {
          case '"': out->push_back('"'); break;
          case '\\': out->push_back('\\'); break;
          case '/': out->push_back('/'); break;
          case 'b': out->push_back('\b'); break;
          case 'f': out->push_back('\f'); break;
          case 'n': out->push_back('\n'); break;
          case 'r': out->push_back('\r'); break;
          case 't': out->push_back('\t'); break;
          case 'u': {
            uint32_t cp = 0;
            if (!ParseHex4(&cp)) return false;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
              if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                pos_ += 2;
                uint32_t low = 0;
                if (!ParseHex4(&low)) return false;
                if (low >= 0xDC00 && low <= 0xDFFF) {
                  cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else {
                  AppendUtf8(out, 0xFFFD);
                  cp = low;
                }
              } else {
                cp = 0xFFFD;
              }
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
              cp = 0xFFFD;
            }
            AppendUtf8(out, cp);
            break;
          }
          default: return Fail("invalid escape sequence");
        }
        continue;
      }
      if (c < 0x20) return Fail("unescaped control character in string");
      out->push_back(static_cast<char>(c));
      ++pos_;
    }
  }

  bool ParseNumber(Value* out) {
    const size_t start = pos_;
    if (Peek() == '-') ++pos_;
    if (!IsDigit(Peek())) return Fail("invalid number");
    if (Peek() == '0') {
      ++pos_;
    } else {
      while (IsDigit(Peek())) ++pos_;
    }
    if (Peek() == '.') {
      ++pos_;
      if (!IsDigit(Peek())) return Fail("expected digit after '.'");
      while (IsDigit(Peek())) ++pos_;
    }
    if (Peek() == 'e' || Peek() == 'E') {
      ++pos_;
      if (Peek() == '+' || Peek() == '-') ++pos_;
      if (!IsDigit(Peek())) return Fail("expected digit in exponent");
      while (IsDigit(Peek())) ++pos_;
    }
    const std::string token(text_.substr(start, pos_ - start));
    errno = 0;
    const double value = std::strtod(token.c_str(), nullptr);
    if (errno == ERANGE && (value == HUGE_VAL || value == -HUGE_VAL)) {
      return Fail("number out of range");
    }
    *out = Value(value);
    return true;
  }

  static bool IsDigit(char c) { return c >= '0' && c <= '9'; }

  std::string_view text_;
  std::string* error_;
  size_t pos_ = 0;
};

void SerializeValue(const Value& value, int indent, int depth, std::string* out) {
  const bool pretty = indent > 0;
  const std::string pad(pretty ? static_cast<size_t>(indent) * static_cast<size_t>(depth + 1) : 0, ' ');
  const std::string pad_close(pretty ? static_cast<size_t>(indent) * static_cast<size_t>(depth) : 0, ' ');
  switch (value.type()) {
    case Value::Type::kNull: out->append("null"); break;
    case Value::Type::kBool: out->append(value.AsBool() ? "true" : "false"); break;
    case Value::Type::kNumber: out->append(Value::NumberToString(value.AsDouble())); break;
    case Value::Type::kString: out->append(EscapeString(value.AsString())); break;
    case Value::Type::kArray: {
      const Value::Array& array = value.AsArray();
      if (array.empty()) {
        out->append("[]");
        break;
      }
      out->push_back('[');
      bool first = true;
      for (const Value& element : array) {
        if (!first) out->push_back(',');
        first = false;
        if (pretty) {
          out->push_back('\n');
          out->append(pad);
        }
        SerializeValue(element, indent, depth + 1, out);
      }
      if (pretty) {
        out->push_back('\n');
        out->append(pad_close);
      }
      out->push_back(']');
      break;
    }
    case Value::Type::kObject: {
      const Value::Object& object = value.AsObject();
      if (object.empty()) {
        out->append("{}");
        break;
      }
      out->push_back('{');
      bool first = true;
      for (const auto& [key, member] : object) {
        if (!first) out->push_back(',');
        first = false;
        if (pretty) {
          out->push_back('\n');
          out->append(pad);
        }
        out->append(EscapeString(key));
        out->push_back(':');
        if (pretty) out->push_back(' ');
        SerializeValue(member, indent, depth + 1, out);
      }
      if (pretty) {
        out->push_back('\n');
        out->append(pad_close);
      }
      out->push_back('}');
      break;
    }
  }
}

}  // namespace

std::string Value::NumberToString(double d) {
  if (std::isnan(d) || std::isinf(d)) return "0";
  // Integers print without a trailing ".0"; everything else uses %g with
  // enough precision to round-trip a double.
  if (d == static_cast<double>(static_cast<int64_t>(d)) && std::fabs(d) < 9.007199254740992e15) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
    return std::string(buf);
  }
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%.17g", d);
  return std::string(buf);
}

const Value& Value::Path(std::string_view dotted) const {
  const Value* current = this;
  size_t start = 0;
  while (start <= dotted.size()) {
    const size_t dot = dotted.find('.', start);
    const std::string_view segment =
        (dot == std::string_view::npos) ? dotted.substr(start) : dotted.substr(start, dot - start);
    if (!segment.empty()) {
      current = &current->Get(segment);
      if (current->IsNull() && !current->IsObject()) {
        // Keep walking only while there is something to walk.
        if (dot == std::string_view::npos) break;
      }
    }
    if (dot == std::string_view::npos) break;
    start = dot + 1;
  }
  return *current;
}

std::optional<Value> Parse(std::string_view text, std::string* error) {
  if (error != nullptr) error->clear();
  Parser parser(text, error);
  Value value;
  if (!parser.Run(&value)) return std::nullopt;
  return value;
}

Value ParseOrObject(std::string_view text, std::string* error) {
  auto parsed = Parse(text, error);
  if (!parsed.has_value()) return Value(Value::Object{});
  if (!parsed->IsObject() && !parsed->IsArray()) {
    if (error != nullptr && error->empty()) *error = "top-level value is not an object or array";
    return Value(Value::Object{});
  }
  return *parsed;
}

std::string EscapeString(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (const char raw : text) {
    const unsigned char c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"': out.append("\\\""); break;
      case '\\': out.append("\\\\"); break;
      case '\b': out.append("\\b"); break;
      case '\f': out.append("\\f"); break;
      case '\n': out.append("\\n"); break;
      case '\r': out.append("\\r"); break;
      case '\t': out.append("\\t"); break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out.append(buf);
        } else {
          out.push_back(raw);
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

std::string Serialize(const Value& value, int indent) {
  std::string out;
  SerializeValue(value, indent, 0, &out);
  return out;
}

}  // namespace json
}  // namespace core
}  // namespace browserx
