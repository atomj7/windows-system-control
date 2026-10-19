#include "json.hpp"

#include <cmath>
#include <cctype>
#include <cstdio>

namespace {

void append_escaped(std::string& out, const std::string& value) {
  out.push_back('"');
  for (unsigned char c : value) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          out += buffer;
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out.push_back('"');
}

class Parser {
 public:
  explicit Parser(std::string_view text) : cur_(text.data()), end_(text.data() + text.size()) {}

  std::optional<Json> parse() {
    skip();
    auto value = parse_value();
    skip();
    if (!ok_ || cur_ != end_) return std::nullopt;
    return value;
  }

 private:
  const char* cur_;
  const char* end_;
  bool ok_ = true;

  void skip() {
    while (cur_ < end_ && std::isspace(static_cast<unsigned char>(*cur_))) ++cur_;
  }

  bool take(char expected) {
    skip();
    if (cur_ < end_ && *cur_ == expected) {
      ++cur_;
      return true;
    }
    ok_ = false;
    return false;
  }

  Json parse_value() {
    skip();
    if (cur_ >= end_) {
      ok_ = false;
      return Json::nul();
    }
    const char c = *cur_;
    if (c == '{') return parse_object();
    if (c == '[') return parse_array();
    if (c == '"') return Json::str(parse_string());
    if (c == 't') return parse_literal("true", Json::boolean(true));
    if (c == 'f') return parse_literal("false", Json::boolean(false));
    if (c == 'n') return parse_literal("null", Json::nul());
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
    ok_ = false;
    return Json::nul();
  }

  Json parse_literal(const char* literal, Json value) {
    for (const char* p = literal; *p; ++p) {
      if (cur_ >= end_ || *cur_ != *p) {
        ok_ = false;
        return Json::nul();
      }
      ++cur_;
    }
    return value;
  }

  Json parse_number() {
    const char* start = cur_;
    if (*cur_ == '-') ++cur_;
    if (cur_ >= end_ || !std::isdigit(static_cast<unsigned char>(*cur_))) {
      ok_ = false;
      return Json::nul();
    }
    while (cur_ < end_ && std::isdigit(static_cast<unsigned char>(*cur_))) ++cur_;
    if (cur_ < end_ && *cur_ == '.') {
      ++cur_;
      while (cur_ < end_ && std::isdigit(static_cast<unsigned char>(*cur_))) ++cur_;
    }
    if (cur_ < end_ && (*cur_ == 'e' || *cur_ == 'E')) {
      ++cur_;
      if (cur_ < end_ && (*cur_ == '+' || *cur_ == '-')) ++cur_;
      while (cur_ < end_ && std::isdigit(static_cast<unsigned char>(*cur_))) ++cur_;
    }
    try {
      return Json::number(std::stod(std::string(start, cur_)));
    } catch (...) {
      ok_ = false;
      return Json::nul();
    }
  }

  std::string parse_string() {
    if (!take('"')) return {};
    std::string out;
    while (cur_ < end_) {
      char c = *cur_++;
      if (c == '"') return out;
      if (c == '\\') {
        if (cur_ >= end_) break;
        char e = *cur_++;
        switch (e) {
          case '"':
          case '\\':
          case '/':
            out.push_back(e);
            break;
          case 'b':
            out.push_back('\b');
            break;
          case 'f':
            out.push_back('\f');
            break;
          case 'n':
            out.push_back('\n');
            break;
          case 'r':
            out.push_back('\r');
            break;
          case 't':
            out.push_back('\t');
            break;
          case 'u': {
            if (cur_ + 4 > end_) {
              ok_ = false;
              return {};
            }
            int code = 0;
            for (int i = 0; i < 4; ++i) {
              char h = *cur_++;
              code <<= 4;
              if (h >= '0' && h <= '9') code += h - '0';
              else if (h >= 'a' && h <= 'f') code += h - 'a' + 10;
              else if (h >= 'A' && h <= 'F') code += h - 'A' + 10;
              else {
                ok_ = false;
                return {};
              }
            }
            if (code < 0x80) {
              out.push_back(static_cast<char>(code));
            } else if (code < 0x800) {
              out.push_back(static_cast<char>(0xC0 | (code >> 6)));
              out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
              out.push_back(static_cast<char>(0xE0 | (code >> 12)));
              out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            break;
          }
          default:
            ok_ = false;
            return {};
        }
      } else {
        out.push_back(c);
      }
    }
    ok_ = false;
    return {};
  }

  Json parse_array() {
    Json array = Json::array();
    if (!take('[')) return array;
    skip();
    if (cur_ < end_ && *cur_ == ']') {
      ++cur_;
      return array;
    }
    while (ok_) {
      array.push(parse_value());
      skip();
      if (cur_ < end_ && *cur_ == ',') {
        ++cur_;
        continue;
      }
      if (cur_ < end_ && *cur_ == ']') {
        ++cur_;
        return array;
      }
      break;
    }
    ok_ = false;
    return array;
  }

  Json parse_object() {
    Json object = Json::object();
    if (!take('{')) return object;
    skip();
    if (cur_ < end_ && *cur_ == '}') {
      ++cur_;
      return object;
    }
    while (ok_) {
      skip();
      if (cur_ >= end_ || *cur_ != '"') break;
      std::string key = parse_string();
      if (!take(':')) break;
      object.set(std::move(key), parse_value());
      skip();
      if (cur_ < end_ && *cur_ == ',') {
        ++cur_;
        continue;
      }
      if (cur_ < end_ && *cur_ == '}') {
        ++cur_;
        return object;
      }
      break;
    }
    ok_ = false;
    return object;
  }
};

}  // namespace

Json Json::nul() { return Json(); }

Json Json::boolean(bool value) {
  Json json;
  json.type_ = Type::Bool;
  json.bool_ = value;
  return json;
}

Json Json::number(double value) {
  Json json;
  json.type_ = Type::Number;
  json.number_ = value;
  return json;
}

Json Json::str(std::string value) {
  Json json;
  json.type_ = Type::String;
  json.string_ = std::move(value);
  return json;
}

Json Json::array(std::vector<Json> values) {
  Json json;
  json.type_ = Type::Array;
  json.array_ = std::move(values);
  return json;
}

Json Json::object() {
  Json json;
  json.type_ = Type::Object;
  return json;
}

bool Json::as_bool(bool fallback) const { return type_ == Type::Bool ? bool_ : fallback; }

double Json::as_number(double fallback) const { return type_ == Type::Number ? number_ : fallback; }

const std::string& Json::as_string() const {
  static const std::string empty;
  return type_ == Type::String ? string_ : empty;
}

Json& Json::push(Json value) {
  type_ = Type::Array;
  array_.push_back(std::move(value));
  return *this;
}

Json& Json::set(std::string key, Json value) {
  type_ = Type::Object;
  for (auto& entry : object_) {
    if (entry.first == key) {
      entry.second = std::move(value);
      return *this;
    }
  }
  object_.emplace_back(std::move(key), std::move(value));
  return *this;
}

const Json* Json::find(const std::string& key) const {
  for (const auto& entry : object_) {
    if (entry.first == key) return &entry.second;
  }
  return nullptr;
}

void Json::dump_into(std::string& out) const {
  switch (type_) {
    case Type::Null:
      out += "null";
      break;
    case Type::Bool:
      out += bool_ ? "true" : "false";
      break;
    case Type::Number: {
      char buffer[64];
      // %.6g collapses unix timestamps to the same second-bucket, so charts become a vertical line.
      if (std::isfinite(number_) && std::floor(number_) == number_ && std::abs(number_) < 1e15) {
        std::snprintf(buffer, sizeof(buffer), "%.0f", number_);
      } else {
        std::snprintf(buffer, sizeof(buffer), "%.17g", number_);
      }
      out += buffer;
      break;
    }
    case Type::String:
      append_escaped(out, string_);
      break;
    case Type::Array:
      out.push_back('[');
      for (size_t i = 0; i < array_.size(); ++i) {
        if (i) out.push_back(',');
        array_[i].dump_into(out);
      }
      out.push_back(']');
      break;
    case Type::Object:
      out.push_back('{');
      for (size_t i = 0; i < object_.size(); ++i) {
        if (i) out.push_back(',');
        append_escaped(out, object_[i].first);
        out.push_back(':');
        object_[i].second.dump_into(out);
      }
      out.push_back('}');
      break;
  }
}

std::string Json::dump() const {
  std::string out;
  out.reserve(256);
  dump_into(out);
  return out;
}

std::optional<Json> parse_json(std::string_view text) { return Parser(text).parse(); }
