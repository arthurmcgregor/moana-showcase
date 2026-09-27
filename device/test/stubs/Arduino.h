#pragma once

#include <cstring>
#include <string>

class String {
 public:
  String() = default;
  String(const char* s) : str_(s ? s : "") {}
  String(const std::string& s) : str_(s) {}

  const char* c_str() const { return str_.c_str(); }
  size_t length() const { return str_.size(); }

  bool operator==(const String& o) const { return str_ == o.str_; }
  bool operator!=(const String& o) const { return str_ != o.str_; }

  String operator+(const String& o) const { return String(str_ + o.str_); }

  friend String operator+(const char* lhs, const String& rhs) {
    return String(std::string(lhs) + rhs.str_);
  }

  bool startsWith(const char* prefix) const {
    return str_.compare(0, strlen(prefix), prefix) == 0;
  }

  bool endsWith(const char* suffix) const {
    size_t len = strlen(suffix);
    if (len > str_.size()) return false;
    return str_.compare(str_.size() - len, len, suffix) == 0;
  }

  int indexOf(const char* s) const {
    auto pos = str_.find(s);
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }

  // Arduino semantics: `to` is exclusive; out-of-range values are clamped.
  String substring(unsigned int from, unsigned int to) const {
    if (from > str_.size()) from = str_.size();
    if (to > str_.size()) to = str_.size();
    if (from >= to) return String();
    return String(str_.substr(from, to - from));
  }
  String substring(unsigned int from) const {
    return substring(from, str_.size());
  }

 private:
  std::string str_;
};
