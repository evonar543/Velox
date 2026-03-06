#include "settings/settings_loader.h"

#include <cwchar>
#include <cwctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include "platform/win/file_utils.h"

namespace velox::settings {

namespace {

struct JsonValue {
  enum class Type {
    kNull,
    kBool,
    kNumber,
    kString,
    kObject,
  };

  Type type = Type::kNull;
  bool bool_value = false;
  double number_value = 0.0;
  std::wstring string_value;
  std::unordered_map<std::wstring, JsonValue> object_value;
};

class JsonParser {
 public:
  explicit JsonParser(std::wstring input) : input_(std::move(input)) {}

  std::optional<JsonValue> Parse() {
    SkipWhitespace();
    auto value = ParseValue();
    SkipWhitespace();
    if (!value.has_value() || position_ != input_.size()) {
      return std::nullopt;
    }
    return value;
  }

 private:
  std::optional<JsonValue> ParseValue() {
    SkipWhitespace();
    if (position_ >= input_.size()) {
      return std::nullopt;
    }

    const wchar_t current = input_[position_];
    if (current == L'{') {
      return ParseObject();
    }
    if (current == L'"') {
      JsonValue value;
      value.type = JsonValue::Type::kString;
      const auto string_value = ParseString();
      if (!string_value.has_value()) {
        return std::nullopt;
      }
      value.string_value = *string_value;
      return value;
    }
    if (current == L't' || current == L'f') {
      return ParseBool();
    }
    if ((current >= L'0' && current <= L'9') || current == L'-') {
      return ParseNumber();
    }
    return std::nullopt;
  }

  std::optional<JsonValue> ParseObject() {
    if (!Consume(L'{')) {
      return std::nullopt;
    }

    JsonValue object;
    object.type = JsonValue::Type::kObject;

    SkipWhitespace();
    if (Consume(L'}')) {
      return object;
    }

    while (position_ < input_.size()) {
      const auto key = ParseString();
      if (!key.has_value()) {
        return std::nullopt;
      }

      if (!Consume(L':')) {
        return std::nullopt;
      }

      const auto value = ParseValue();
      if (!value.has_value()) {
        return std::nullopt;
      }

      object.object_value.emplace(*key, *value);
      SkipWhitespace();
      if (Consume(L'}')) {
        break;
      }
      if (!Consume(L',')) {
        return std::nullopt;
      }
    }

    return object;
  }

  std::optional<std::wstring> ParseString() {
    if (!Consume(L'"')) {
      return std::nullopt;
    }

    std::wstring value;
    while (position_ < input_.size()) {
      const wchar_t ch = input_[position_++];
      if (ch == L'"') {
        return value;
      }
      if (ch == L'\\') {
        if (position_ >= input_.size()) {
          return std::nullopt;
        }
        const wchar_t escaped = input_[position_++];
        switch (escaped) {
          case L'"':
          case L'\\':
          case L'/':
            value.push_back(escaped);
            break;
          case L'b':
            value.push_back(L'\b');
            break;
          case L'f':
            value.push_back(L'\f');
            break;
          case L'n':
            value.push_back(L'\n');
            break;
          case L'r':
            value.push_back(L'\r');
            break;
          case L't':
            value.push_back(L'\t');
            break;
          default:
            return std::nullopt;
        }
      } else {
        value.push_back(ch);
      }
    }
    return std::nullopt;
  }

  std::optional<JsonValue> ParseBool() {
    JsonValue value;
    value.type = JsonValue::Type::kBool;
    if (MatchLiteral(L"true")) {
      value.bool_value = true;
      return value;
    }
    if (MatchLiteral(L"false")) {
      value.bool_value = false;
      return value;
    }
    return std::nullopt;
  }

  std::optional<JsonValue> ParseNumber() {
    const size_t start = position_;
    if (input_[position_] == L'-') {
      ++position_;
    }
    while (position_ < input_.size() && iswdigit(input_[position_])) {
      ++position_;
    }
    if (position_ < input_.size() && input_[position_] == L'.') {
      ++position_;
      while (position_ < input_.size() && iswdigit(input_[position_])) {
        ++position_;
      }
    }

    JsonValue value;
    value.type = JsonValue::Type::kNumber;
    try {
      value.number_value = std::stod(input_.substr(start, position_ - start));
    } catch (...) {
      return std::nullopt;
    }
    return value;
  }

  void SkipWhitespace() {
    while (position_ < input_.size() && iswspace(input_[position_])) {
      ++position_;
    }
  }

  bool Consume(wchar_t expected) {
    SkipWhitespace();
    if (position_ >= input_.size() || input_[position_] != expected) {
      return false;
    }
    ++position_;
    return true;
  }

  bool MatchLiteral(const wchar_t* literal) {
    const size_t length = wcslen(literal);
    if (input_.substr(position_, length) != literal) {
      return false;
    }
    position_ += length;
    return true;
  }

  std::wstring input_;
  size_t position_ = 0;
};

const JsonValue* GetObjectMember(const JsonValue& value, const std::wstring& key) {
  if (value.type != JsonValue::Type::kObject) {
    return nullptr;
  }
  const auto it = value.object_value.find(key);
  return it == value.object_value.end() ? nullptr : &it->second;
}

std::filesystem::path ResolveCliPath(const std::filesystem::path& candidate) {
  if (candidate.empty() || candidate.is_absolute()) {
    return candidate;
  }

  std::error_code error_code;
  const auto working_dir = std::filesystem::current_path(error_code);
  if (error_code || working_dir.empty()) {
    return candidate;
  }

  return platform::MakeAbsolute(working_dir, candidate);
}

void ApplyIfString(const JsonValue& root, const std::wstring& key, std::wstring* output) {
  const JsonValue* value = GetObjectMember(root, key);
  if (value != nullptr && value->type == JsonValue::Type::kString) {
    *output = value->string_value;
  }
}

void ApplyIfBool(const JsonValue& root, const std::wstring& key, bool* output) {
  const JsonValue* value = GetObjectMember(root, key);
  if (value != nullptr && value->type == JsonValue::Type::kBool) {
    *output = value->bool_value;
  }
}

void ApplyIfInt(const JsonValue& root, const std::wstring& key, int* output) {
  const JsonValue* value = GetObjectMember(root, key);
  if (value != nullptr && value->type == JsonValue::Type::kNumber) {
    *output = static_cast<int>(value->number_value);
  }
}

std::optional<JsonValue> LoadJson(const std::filesystem::path& config_path) {
  std::ifstream stream(config_path, std::ios::binary);
  if (!stream.is_open()) {
    return std::nullopt;
  }

  std::stringstream buffer;
  buffer << stream.rdbuf();
  JsonParser parser(platform::ToWide(buffer.str()));
  return parser.Parse();
}

}  // namespace

AppSettings LoadSettings(const std::filesystem::path& config_path,
                         const std::filesystem::path& base_dir,
                         const app::CommandLineOptions& command_line) {
  AppSettings settings = DefaultAppSettings(base_dir);

  if (const auto json = LoadJson(config_path); json.has_value()) {
    ApplyIfString(*json, L"startup_url", &settings.startup_url);
    ApplyIfBool(*json, L"incognito_default", &settings.incognito_default);

    if (const JsonValue* window = GetObjectMember(*json, L"window")) {
      ApplyIfInt(*window, L"width", &settings.window.width);
      ApplyIfInt(*window, L"height", &settings.window.height);
    }

    if (const JsonValue* paths = GetObjectMember(*json, L"paths")) {
      std::wstring profile_dir;
      std::wstring cache_dir;
      std::wstring log_dir;
      ApplyIfString(*paths, L"profile_dir", &profile_dir);
      ApplyIfString(*paths, L"cache_dir", &cache_dir);
      ApplyIfString(*paths, L"log_dir", &log_dir);

      if (!profile_dir.empty()) {
        settings.paths.profile_dir = profile_dir;
      }
      if (!cache_dir.empty()) {
        settings.paths.cache_dir = cache_dir;
      }
      if (!log_dir.empty()) {
        settings.paths.log_dir = log_dir;
        settings.paths.log_file = std::filesystem::path(log_dir) / L"velox.log";
      }
    }

    if (const JsonValue* logging = GetObjectMember(*json, L"logging")) {
      std::wstring level;
      ApplyIfString(*logging, L"level", &level);
      if (!level.empty()) {
        settings.log_level = platform::LogLevelFromString(platform::ToUtf8(level));
      }
    }

    if (const JsonValue* benchmarking = GetObjectMember(*json, L"benchmarking")) {
      ApplyIfBool(*benchmarking, L"enabled", &settings.benchmarking.enabled);
      std::wstring output;
      ApplyIfString(*benchmarking, L"output", &output);
      if (!output.empty()) {
        settings.benchmarking.output_file = output;
      }
    }

    if (const JsonValue* privacy = GetObjectMember(*json, L"privacy")) {
      ApplyIfBool(*privacy, L"do_not_track", &settings.privacy.do_not_track);
      ApplyIfBool(*privacy, L"global_privacy_control", &settings.privacy.global_privacy_control);
      ApplyIfBool(*privacy, L"block_third_party_cookies", &settings.privacy.block_third_party_cookies);
      ApplyIfBool(*privacy, L"strip_tracking_query_parameters", &settings.privacy.strip_tracking_query_parameters);
      ApplyIfBool(*privacy, L"strip_cross_site_referrers", &settings.privacy.strip_cross_site_referrers);
      ApplyIfBool(*privacy, L"block_webrtc_non_proxied_udp", &settings.privacy.block_webrtc_non_proxied_udp);
      ApplyIfBool(*privacy, L"disable_password_manager", &settings.privacy.disable_password_manager);
      ApplyIfBool(*privacy, L"block_external_protocols", &settings.privacy.block_external_protocols);
    }

    if (const JsonValue* blocking = GetObjectMember(*json, L"blocking")) {
      ApplyIfBool(*blocking, L"enabled", &settings.blocking.enabled);
      ApplyIfBool(*blocking, L"block_ads", &settings.blocking.block_ads);
      ApplyIfBool(*blocking, L"block_trackers", &settings.blocking.block_trackers);
    }

    if (const JsonValue* search = GetObjectMember(*json, L"search")) {
      ApplyIfString(*search, L"provider_name", &settings.search.provider_name);
      ApplyIfString(*search, L"query_url_template", &settings.search.query_url_template);
    }

    if (const JsonValue* optimization = GetObjectMember(*json, L"optimization")) {
      ApplyIfBool(*optimization, L"auto_tune", &settings.optimization.auto_tune);
      ApplyIfInt(*optimization, L"renderer_process_limit", &settings.optimization.renderer_process_limit);
      ApplyIfBool(*optimization, L"predictive_warmup", &settings.optimization.predictive_warmup);
      ApplyIfInt(*optimization, L"predictor_host_count", &settings.optimization.predictor_host_count);
      ApplyIfInt(*optimization, L"max_cache_size_mb", &settings.optimization.max_cache_size_mb);
      ApplyIfInt(*optimization, L"cache_trim_target_percent", &settings.optimization.cache_trim_target_percent);
    }
  }

  if (command_line.startup_url.has_value()) {
    settings.startup_url = *command_line.startup_url;
  }
  if (command_line.incognito) {
    settings.incognito_default = true;
  }
  if (command_line.profile_dir.has_value()) {
    settings.paths.profile_dir = ResolveCliPath(*command_line.profile_dir);
    settings.paths.cache_dir = settings.paths.profile_dir / L"cache";
  }
  if (command_line.log_file.has_value()) {
    settings.paths.log_file = ResolveCliPath(*command_line.log_file);
    settings.paths.log_dir = settings.paths.log_file.parent_path();
  }
  if (command_line.benchmark_output.has_value()) {
    settings.benchmarking.enabled = true;
    settings.benchmarking.output_file = ResolveCliPath(*command_line.benchmark_output);
  }

  FinalizeSettings(settings, base_dir);
  return settings;
}

}  // namespace velox::settings
