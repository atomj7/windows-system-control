#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Ordered JSON value. Objects keep insertion order so API responses stay stable.
class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Json() = default;

  static Json nul();
  static Json boolean(bool value);
  static Json number(double value);
  static Json str(std::string value);
  static Json array(std::vector<Json> values = {});
  static Json object();

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_bool() const { return type_ == Type::Bool; }
  bool is_number() const { return type_ == Type::Number; }
  bool is_string() const { return type_ == Type::String; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_object() const { return type_ == Type::Object; }

  bool as_bool(bool fallback = false) const;
  double as_number(double fallback = 0) const;
  const std::string& as_string() const;

  Json& push(Json value);
  Json& set(std::string key, Json value);
  const Json* find(const std::string& key) const;
  const std::vector<Json>& items() const { return array_; }

  std::string dump() const;

 private:
  Type type_ = Type::Null;
  bool bool_ = false;
  double number_ = 0;
  std::string string_;
  std::vector<Json> array_;
  std::vector<std::pair<std::string, Json>> object_;

  void dump_into(std::string& out) const;
};

std::optional<Json> parse_json(std::string_view text);

inline Json jbool(bool value) { return Json::boolean(value); }
inline Json jnum(double value) { return Json::number(value); }
inline Json jstr(std::string value) { return Json::str(std::move(value)); }
