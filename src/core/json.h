// BrowserX — minimal, dependency-free JSON for the core library.
//
// Why not a third-party JSON library? The core must build and unit-test with
// nothing but a C++20 compiler (see `make test`). Chromium/CEF's own Value API
// is only available in the browser process, and the core is deliberately
// CEF-free so it can be tested standalone.
//
// Compile-time constraints inherited from CEF: -fno-exceptions -fno-rtti.
// Therefore: std::get_if everywhere, no throwing accessors, errors returned.

#ifndef BROWSERX_CORE_JSON_H_
#define BROWSERX_CORE_JSON_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace browserx {
namespace core {
namespace json {

class Value {
 public:
  using Array = std::vector<Value>;
  // Transparent comparator so lookups can take a std::string_view directly.
  using Object = std::map<std::string, Value, std::less<>>;

  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };

  Value() = default;
  Value(std::nullptr_t) {}
  Value(bool b) : storage_(b) {}
  Value(double d) : storage_(d) {}
  Value(int i) : storage_(static_cast<double>(i)) {}
  Value(int64_t i) : storage_(static_cast<double>(i)) {}
  Value(uint64_t i) : storage_(static_cast<double>(i)) {}
  Value(const char* s) : storage_(std::string(s ? s : "")) {}
  Value(std::string s) : storage_(std::move(s)) {}
  Value(std::string_view s) : storage_(std::string(s)) {}
  Value(Array a) : storage_(std::move(a)) {}
  Value(Object o) : storage_(std::move(o)) {}

  Type type() const {
    switch (storage_.index()) {
      case 1: return Type::kBool;
      case 2: return Type::kNumber;
      case 3: return Type::kString;
      case 4: return Type::kArray;
      case 5: return Type::kObject;
      default: return Type::kNull;
    }
  }

  const char* type_name() const {
    switch (type()) {
      case Type::kBool: return "bool";
      case Type::kNumber: return "number";
      case Type::kString: return "string";
      case Type::kArray: return "array";
      case Type::kObject: return "object";
      default: return "null";
    }
  }

  bool IsNull() const { return type() == Type::kNull; }
  bool IsBool() const { return type() == Type::kBool; }
  bool IsNumber() const { return type() == Type::kNumber; }
  bool IsString() const { return type() == Type::kString; }
  bool IsArray() const { return type() == Type::kArray; }
  bool IsObject() const { return type() == Type::kObject; }

  // ---- scalar readers (never throw) ---------------------------------------
  bool AsBool(bool fallback = false) const {
    if (auto* v = std::get_if<bool>(&storage_)) return *v;
    if (auto* n = std::get_if<double>(&storage_)) return *n != 0.0;
    if (auto* s = std::get_if<std::string>(&storage_))
      return *s == "true" || *s == "1" || *s == "yes" || *s == "on";
    return fallback;
  }

  double AsDouble(double fallback = 0.0) const {
    if (auto* n = std::get_if<double>(&storage_)) return *n;
    if (auto* v = std::get_if<bool>(&storage_)) return *v ? 1.0 : 0.0;
    return fallback;
  }

  int64_t AsInt64(int64_t fallback = 0) const {
    if (auto* n = std::get_if<double>(&storage_)) {
      if (*n != *n) return fallback;  // NaN
      return static_cast<int64_t>(*n < 0 ? *n - 0.5 : *n + 0.5);
    }
    if (auto* v = std::get_if<bool>(&storage_)) return *v ? 1 : 0;
    return fallback;
  }

  int AsInt(int fallback = 0) const {
    int64_t v = AsInt64(fallback);
    if (v > 2147483647LL) return 2147483647;
    if (v < -2147483648LL) return -2147483648LL;
    return static_cast<int>(v);
  }

  /// Numbers are stringified too (settings are stored as strings internally).
  std::string AsString(std::string_view fallback = "") const {
    if (auto* s = std::get_if<std::string>(&storage_)) return *s;
    if (auto* b = std::get_if<bool>(&storage_)) return *b ? "true" : "false";
    if (auto* n = std::get_if<double>(&storage_)) return NumberToString(*n);
    if (IsNull()) return std::string(fallback);
    return std::string(fallback);
  }

  // ---- containers ---------------------------------------------------------
  const Array& AsArray() const {
    if (auto* a = std::get_if<Array>(&storage_)) return *a;
    static const Array kEmpty;
    return kEmpty;
  }

  const Object& AsObject() const {
    if (auto* o = std::get_if<Object>(&storage_)) return *o;
    static const Object kEmpty;
    return kEmpty;
  }

  Array& AsArrayMut() {
    if (!std::holds_alternative<Array>(storage_)) storage_ = Array{};
    return std::get<Array>(storage_);
  }

  Object& AsObjectMut() {
    if (!std::holds_alternative<Object>(storage_)) storage_ = Object{};
    return std::get<Object>(storage_);
  }

  size_t size() const {
    if (auto* a = std::get_if<Array>(&storage_)) return a->size();
    if (auto* o = std::get_if<Object>(&storage_)) return o->size();
    return 0;
  }

  bool empty() const { return size() == 0; }

  void Push(Value v) { AsArrayMut().push_back(std::move(v)); }

  bool Contains(std::string_view key) const {
    if (auto* o = std::get_if<Object>(&storage_)) return o->find(key) != o->end();
    return false;
  }

  /// Read-only lookup; returns null value when absent (never throws).
  const Value& Get(std::string_view key) const {
    if (auto* o = std::get_if<Object>(&storage_)) {
      auto it = o->find(key);
      if (it != o->end()) return it->second;
    }
    static const Value kNull;
    return kNull;
  }

  const Value& At(size_t index) const {
    if (auto* a = std::get_if<Array>(&storage_)) {
      if (index < a->size()) return (*a)[index];
    }
    static const Value kNull;
    return kNull;
  }

  /// Mutable object member; promotes this value to an object when needed.
  Value& operator[](std::string_view key) {
    Object& o = AsObjectMut();
    return o[std::string(key)];
  }

  bool Erase(std::string_view key) {
    if (auto* o = std::get_if<Object>(&storage_)) return o->erase(std::string(key)) > 0;
    return false;
  }

  void SetNull() { storage_ = std::monostate{}; }

  /// Convenience: "a.b.c" style path lookup.
  const Value& Path(std::string_view dotted) const;

  static std::string NumberToString(double d);

 private:
  std::variant<std::monostate, bool, double, std::string, Array, Object> storage_;
};

/// Parses `text`. On failure returns nullopt and fills `error` (with offset).
std::optional<Value> Parse(std::string_view text, std::string* error = nullptr);

/// Serialises. `indent == 0` produces compact output.
std::string Serialize(const Value& value, int indent = 0);

/// Escapes a string as a JSON string literal (including the quotes).
std::string EscapeString(std::string_view text);

/// Convenience: parse, or return an empty object on failure.
Value ParseOrObject(std::string_view text, std::string* error = nullptr);

}  // namespace json
}  // namespace core
}  // namespace browserx

#endif  // BROWSERX_CORE_JSON_H_
