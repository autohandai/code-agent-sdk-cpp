#include <autohand/sdk.hpp>

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace autohand {
namespace {

std::string now_id() {
  return "run_" + std::to_string(static_cast<long long>(std::time(nullptr))) + "_" +
         std::to_string(::getpid());
}

void append_value(std::vector<std::string>& args, const std::string& flag, const std::optional<std::string>& value) {
  if (value && !value->empty()) {
    args.push_back(flag);
    args.push_back(*value);
  }
}

template <class T>
void append_value(std::vector<std::string>& args, const std::string& flag, const std::optional<T>& value) {
  if (value) {
    args.push_back(flag);
    args.push_back(std::to_string(*value));
  }
}

void append_json_separator(std::ostringstream& out, bool& first) {
  if (!first) out << ',';
  first = false;
}

void append_json_string(std::ostringstream& out, bool& first, const std::string& key, const std::string& value) {
  append_json_separator(out, first);
  out << '"' << key << "\":\"" << json_escape(value) << '"';
}

template <class T>
void append_json_number(std::ostringstream& out, bool& first, const std::string& key, T value) {
  append_json_separator(out, first);
  out << '"' << key << "\":" << value;
}

void append_json_bool(std::ostringstream& out, bool& first, const std::string& key, bool value) {
  append_json_separator(out, first);
  out << '"' << key << "\":" << (value ? "true" : "false");
}

void append_json_raw(std::ostringstream& out, bool& first, const std::string& key, const std::string& value) {
  append_json_separator(out, first);
  out << '"' << key << "\":" << value;
}

void append_json_strings(
    std::ostringstream& out, bool& first, const std::string& key, const std::vector<std::string>& values) {
  if (values.empty()) return;
  append_json_separator(out, first);
  out << '"' << key << "\":[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) out << ',';
    out << '"' << json_escape(values[i]) << '"';
  }
  out << ']';
}

bool is_blank(std::string_view value) {
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return std::isspace(character) != 0;
  });
}

void append_joined(
    std::vector<std::string>& args,
    const std::string& flag,
    const std::vector<std::string>& values) {
  if (values.empty()) return;
  args.push_back(flag);
  std::ostringstream joined;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) joined << ',';
    joined << values[i];
  }
  args.push_back(joined.str());
}

std::string autoresearch_phase_for_method(const std::string& method) {
  if (method == "autohand.autoresearch.start") return "start";
  if (method == "autohand.autoresearch.status") return "status";
  if (method == "autohand.autoresearch.pause") return "pause";
  return {};
}

enum class JsonKind { null_value, boolean, number, string, array, object };

struct JsonValue {
  JsonKind kind = JsonKind::null_value;
  bool boolean = false;
  std::string scalar;
  std::vector<JsonValue> array;
  std::map<std::string, JsonValue> object;

  const JsonValue* member(const std::string& key) const {
    if (kind != JsonKind::object) return nullptr;
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
  }
};

void append_utf8(std::string& output, unsigned int codepoint) {
  if (codepoint <= 0x7f) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ff) {
    output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0xffff) {
    output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else {
    output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  }
}

class JsonParser {
 public:
  explicit JsonParser(std::string_view input) : input_(input) {}

  JsonValue parse_document() {
    auto value = parse_value();
    skip_whitespace();
    if (position_ != input_.size()) fail("unexpected trailing content");
    return value;
  }

  JsonValue parse_prefix(std::size_t& consumed) {
    auto value = parse_value();
    consumed = position_;
    return value;
  }

 private:
  [[noreturn]] void fail(const std::string& message) const {
    throw SdkError("invalid JSON at byte " + std::to_string(position_) + ": " + message);
  }

  void skip_whitespace() {
    while (position_ < input_.size() &&
           (input_[position_] == ' ' || input_[position_] == '\t' ||
            input_[position_] == '\n' || input_[position_] == '\r')) {
      ++position_;
    }
  }

  bool consume(char expected) {
    skip_whitespace();
    if (position_ < input_.size() && input_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  void expect(std::string_view expected) {
    if (input_.substr(position_, expected.size()) != expected) {
      fail("expected " + std::string(expected));
    }
    position_ += expected.size();
  }

  JsonValue parse_value() {
    skip_whitespace();
    if (position_ >= input_.size()) fail("expected a value");
    switch (input_[position_]) {
      case 'n':
        expect("null");
        return {};
      case 't': {
        expect("true");
        JsonValue value;
        value.kind = JsonKind::boolean;
        value.boolean = true;
        return value;
      }
      case 'f': {
        expect("false");
        JsonValue value;
        value.kind = JsonKind::boolean;
        return value;
      }
      case '"': {
        JsonValue value;
        value.kind = JsonKind::string;
        value.scalar = parse_string();
        return value;
      }
      case '[':
        return parse_array();
      case '{':
        return parse_object();
      default:
        if (input_[position_] == '-' || std::isdigit(static_cast<unsigned char>(input_[position_]))) {
          return parse_number();
        }
        fail("unexpected token");
    }
  }

  unsigned int parse_hex4() {
    if (position_ + 4 > input_.size()) fail("incomplete unicode escape");
    unsigned int value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = input_[position_++];
      value <<= 4;
      if (c >= '0' && c <= '9') value |= static_cast<unsigned int>(c - '0');
      else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned int>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned int>(c - 'A' + 10);
      else fail("invalid unicode escape");
    }
    return value;
  }

  std::string parse_string() {
    if (input_[position_++] != '"') fail("expected string");
    std::string output;
    while (position_ < input_.size()) {
      const unsigned char c = static_cast<unsigned char>(input_[position_++]);
      if (c == '"') return output;
      if (c < 0x20) fail("unescaped control character");
      if (c != '\\') {
        output.push_back(static_cast<char>(c));
        continue;
      }
      if (position_ >= input_.size()) fail("incomplete escape");
      switch (input_[position_++]) {
        case '"': output.push_back('"'); break;
        case '\\': output.push_back('\\'); break;
        case '/': output.push_back('/'); break;
        case 'b': output.push_back('\b'); break;
        case 'f': output.push_back('\f'); break;
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        case 'u': {
          auto codepoint = parse_hex4();
          if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if (position_ + 2 > input_.size() || input_[position_] != '\\' ||
                input_[position_ + 1] != 'u') {
              fail("missing low surrogate");
            }
            position_ += 2;
            const auto low = parse_hex4();
            if (low < 0xdc00 || low > 0xdfff) fail("invalid low surrogate");
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
          } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
            fail("unexpected low surrogate");
          }
          append_utf8(output, codepoint);
          break;
        }
        default: fail("invalid escape");
      }
    }
    fail("unterminated string");
  }

  JsonValue parse_number() {
    const auto start = position_;
    if (input_[position_] == '-') ++position_;
    if (position_ >= input_.size()) fail("incomplete number");
    if (input_[position_] == '0') {
      ++position_;
    } else {
      if (!std::isdigit(static_cast<unsigned char>(input_[position_]))) fail("invalid number");
      while (position_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
    }
    if (position_ < input_.size() && input_[position_] == '.') {
      ++position_;
      const auto fraction = position_;
      while (position_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
      if (fraction == position_) fail("invalid fraction");
    }
    if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) {
        ++position_;
      }
      const auto exponent = position_;
      while (position_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
      if (exponent == position_) fail("invalid exponent");
    }
    JsonValue value;
    value.kind = JsonKind::number;
    value.scalar = std::string(input_.substr(start, position_ - start));
    return value;
  }

  JsonValue parse_array() {
    ++position_;
    JsonValue value;
    value.kind = JsonKind::array;
    if (consume(']')) return value;
    while (true) {
      value.array.push_back(parse_value());
      if (consume(']')) return value;
      if (!consume(',')) fail("expected ',' or ']'");
    }
  }

  JsonValue parse_object() {
    ++position_;
    JsonValue value;
    value.kind = JsonKind::object;
    if (consume('}')) return value;
    while (true) {
      skip_whitespace();
      if (position_ >= input_.size() || input_[position_] != '"') fail("expected object key");
      auto key = parse_string();
      if (!consume(':')) fail("expected ':'");
      value.object.insert_or_assign(std::move(key), parse_value());
      if (consume('}')) return value;
      if (!consume(',')) fail("expected ',' or '}'");
    }
  }

  std::string_view input_;
  std::size_t position_ = 0;
};

JsonValue parse_json_document(std::string_view json) {
  return JsonParser(json).parse_document();
}

std::string serialize_json(const JsonValue& value) {
  switch (value.kind) {
    case JsonKind::null_value: return "null";
    case JsonKind::boolean: return value.boolean ? "true" : "false";
    case JsonKind::number: return value.scalar;
    case JsonKind::string: return "\"" + json_escape(value.scalar) + "\"";
    case JsonKind::array: {
      std::ostringstream output;
      output << '[';
      for (std::size_t i = 0; i < value.array.size(); ++i) {
        if (i > 0) output << ',';
        output << serialize_json(value.array[i]);
      }
      output << ']';
      return output.str();
    }
    case JsonKind::object: {
      std::ostringstream output;
      output << '{';
      bool first = true;
      for (const auto& [key, member] : value.object) {
        if (!first) output << ',';
        first = false;
        output << '"' << json_escape(key) << "\":" << serialize_json(member);
      }
      output << '}';
      return output.str();
    }
  }
  return "null";
}

long extract_id(const JsonValue& root) {
  const auto* id = root.member("id");
  if (!id) return 0;
  if (id->kind == JsonKind::number || id->kind == JsonKind::string) {
    try {
      return std::stol(id->scalar);
    } catch (const std::exception&) {
      return 0;
    }
  }
  return 0;
}

int extract_error_code(const JsonValue& error) {
  const auto* code = error.member("code");
  if (!code || code->kind != JsonKind::number) return 0;
  try {
    return std::stoi(code->scalar);
  } catch (const std::exception&) {
    return 0;
  }
}

std::string json_string_member(const JsonValue& object, const std::string& key) {
  const auto* member = object.member(key);
  return member && member->kind == JsonKind::string ? member->scalar : std::string{};
}

const JsonValue& required_member(const JsonValue& object, const std::string& key, JsonKind kind) {
  const auto* member = object.member(key);
  if (!member || member->kind != kind) {
    throw SdkError("invalid RPC result: expected '" + key + "'");
  }
  return *member;
}

std::optional<std::string> optional_string_member(const JsonValue& object, const std::string& key) {
  const auto* member = object.member(key);
  if (!member || member->kind == JsonKind::null_value) return std::nullopt;
  if (member->kind != JsonKind::string) {
    throw SdkError("invalid RPC result: expected string '" + key + "'");
  }
  return member->scalar;
}

std::optional<bool> optional_bool_member(const JsonValue& object, const std::string& key) {
  const auto* member = object.member(key);
  if (!member || member->kind == JsonKind::null_value) return std::nullopt;
  if (member->kind != JsonKind::boolean) {
    throw SdkError("invalid RPC result: expected boolean '" + key + "'");
  }
  return member->boolean;
}

std::optional<long long> optional_integer_member(const JsonValue& object, const std::string& key) {
  const auto* member = object.member(key);
  if (!member || member->kind == JsonKind::null_value) return std::nullopt;
  if (member->kind != JsonKind::number) {
    throw SdkError("invalid RPC result: expected number '" + key + "'");
  }
  try {
    return std::stoll(member->scalar);
  } catch (const std::exception&) {
    throw SdkError("invalid RPC result: invalid integer '" + key + "'");
  }
}

long long required_integer_member(const JsonValue& object, const std::string& key) {
  const auto& member = required_member(object, key, JsonKind::number);
  try {
    return std::stoll(member.scalar);
  } catch (const std::exception&) {
    throw SdkError("invalid RPC result: invalid integer '" + key + "'");
  }
}

std::optional<double> optional_double_member(const JsonValue& object, const std::string& key) {
  const auto* member = object.member(key);
  if (!member || member->kind == JsonKind::null_value) return std::nullopt;
  if (member->kind != JsonKind::number) {
    throw SdkError("invalid RPC result: expected number '" + key + "'");
  }
  try {
    return std::stod(member->scalar);
  } catch (const std::exception&) {
    throw SdkError("invalid RPC result: invalid number '" + key + "'");
  }
}

std::vector<std::string> string_array_member(const JsonValue& object, const std::string& key) {
  const auto* member = object.member(key);
  if (!member) return {};
  if (member->kind != JsonKind::array) {
    throw SdkError("invalid RPC result: expected array '" + key + "'");
  }
  std::vector<std::string> values;
  values.reserve(member->array.size());
  for (const auto& value : member->array) {
    if (value.kind != JsonKind::string) {
      throw SdkError("invalid RPC result: expected string in '" + key + "'");
    }
    values.push_back(value.scalar);
  }
  return values;
}

std::map<std::string, std::string> string_map_member(const JsonValue& object, const std::string& key) {
  const auto* member = object.member(key);
  if (!member) return {};
  if (member->kind != JsonKind::object) {
    throw SdkError("invalid RPC result: expected object '" + key + "'");
  }
  std::map<std::string, std::string> values;
  for (const auto& [name, value] : member->object) {
    if (value.kind != JsonKind::string) {
      throw SdkError("invalid RPC result: expected string value in '" + key + "'");
    }
    values.emplace(name, value.scalar);
  }
  return values;
}

ResetResult parse_reset_result(const std::string& json) {
  const auto root = parse_json_document(json);
  return ResetResult{required_member(root, "sessionId", JsonKind::string).scalar};
}

BrowserHandoffCreateResult parse_browser_handoff_create_result(const std::string& json) {
  const auto root = parse_json_document(json);
  return BrowserHandoffCreateResult{
      required_member(root, "token", JsonKind::string).scalar,
      required_member(root, "sessionId", JsonKind::string).scalar,
      required_member(root, "workspaceRoot", JsonKind::string).scalar,
      required_member(root, "createdAt", JsonKind::string).scalar,
      required_member(root, "expiresAt", JsonKind::string).scalar,
      required_member(root, "url", JsonKind::string).scalar};
}

BrowserHandoffAttachResult parse_browser_handoff_attach_result(const std::string& json) {
  const auto root = parse_json_document(json);
  BrowserHandoffAttachResult result;
  result.success = required_member(root, "success", JsonKind::boolean).boolean;
  result.session_id = optional_string_member(root, "sessionId");
  result.workspace_root = optional_string_member(root, "workspaceRoot");
  result.message_count = optional_integer_member(root, "messageCount");
  return result;
}

AutomodeStartResult parse_automode_start_result(const std::string& json) {
  const auto root = parse_json_document(json);
  AutomodeStartResult result;
  result.success = required_member(root, "success", JsonKind::boolean).boolean;
  result.session_id = optional_string_member(root, "sessionId");
  result.error = optional_string_member(root, "error");
  return result;
}

AutomodeSessionStatus parse_automode_session_status(const std::string& value) {
  if (value == "running") return AutomodeSessionStatus::Running;
  if (value == "paused") return AutomodeSessionStatus::Paused;
  if (value == "completed") return AutomodeSessionStatus::Completed;
  if (value == "cancelled") return AutomodeSessionStatus::Cancelled;
  if (value == "failed") return AutomodeSessionStatus::Failed;
  throw SdkError("invalid RPC result: unknown auto-mode status '" + value + "'");
}

AutomodeStatusResult parse_automode_status_result(const std::string& json) {
  const auto root = parse_json_document(json);
  AutomodeStatusResult result;
  result.active = required_member(root, "active", JsonKind::boolean).boolean;
  result.paused = required_member(root, "paused", JsonKind::boolean).boolean;
  const auto* state_value = root.member("state");
  if (!state_value || state_value->kind == JsonKind::null_value) return result;
  if (state_value->kind != JsonKind::object) {
    throw SdkError("invalid RPC result: expected object 'state'");
  }
  AutomodeState state;
  state.session_id = required_member(*state_value, "sessionId", JsonKind::string).scalar;
  state.status = parse_automode_session_status(
      required_member(*state_value, "status", JsonKind::string).scalar);
  state.current_iteration = required_integer_member(*state_value, "currentIteration");
  state.max_iterations = required_integer_member(*state_value, "maxIterations");
  state.files_created = required_integer_member(*state_value, "filesCreated");
  state.files_modified = required_integer_member(*state_value, "filesModified");
  state.branch = optional_string_member(*state_value, "branch");
  const auto* checkpoint = state_value->member("lastCheckpoint");
  if (checkpoint && checkpoint->kind != JsonKind::null_value) {
    if (checkpoint->kind != JsonKind::object) {
      throw SdkError("invalid RPC result: expected object 'lastCheckpoint'");
    }
    state.last_checkpoint = AutomodeCheckpoint{
        required_member(*checkpoint, "commit", JsonKind::string).scalar,
        required_member(*checkpoint, "message", JsonKind::string).scalar,
        required_member(*checkpoint, "timestamp", JsonKind::string).scalar};
  }
  result.state = std::move(state);
  return result;
}

AutomodeOperationResult parse_automode_operation_result(const std::string& json) {
  const auto root = parse_json_document(json);
  AutomodeOperationResult result;
  result.success = required_member(root, "success", JsonKind::boolean).boolean;
  result.error = optional_string_member(root, "error");
  return result;
}

AutomodeGetLogResult parse_automode_get_log_result(const std::string& json) {
  const auto root = parse_json_document(json);
  AutomodeGetLogResult result;
  result.success = required_member(root, "success", JsonKind::boolean).boolean;
  result.error = optional_string_member(root, "error");
  const auto& iterations = required_member(root, "iterations", JsonKind::array);
  for (const auto& value : iterations.array) {
    if (value.kind != JsonKind::object) {
      throw SdkError("invalid RPC result: expected auto-mode log entry object");
    }
    AutomodeLogEntry entry;
    entry.iteration = required_integer_member(value, "iteration");
    entry.timestamp = required_member(value, "timestamp", JsonKind::string).scalar;
    const auto& actions = required_member(value, "actions", JsonKind::array);
    for (const auto& action : actions.array) {
      if (action.kind != JsonKind::string) {
        throw SdkError("invalid RPC result: expected string auto-mode action");
      }
      entry.actions.push_back(action.scalar);
    }
    entry.tokens_used = optional_integer_member(value, "tokensUsed");
    entry.cost = optional_double_member(value, "cost");
    const auto* checkpoint = value.member("checkpoint");
    if (checkpoint && checkpoint->kind != JsonKind::null_value) {
      if (checkpoint->kind != JsonKind::object) {
        throw SdkError("invalid RPC result: expected object 'checkpoint'");
      }
      entry.checkpoint = AutomodeLogCheckpoint{
          required_member(*checkpoint, "commit", JsonKind::string).scalar,
          required_member(*checkpoint, "message", JsonKind::string).scalar};
    }
    result.iterations.push_back(std::move(entry));
  }
  return result;
}

GetSkillsRegistryResult parse_skills_registry_result(const std::string& json) {
  const auto root = parse_json_document(json);
  GetSkillsRegistryResult result;
  result.success = required_member(root, "success", JsonKind::boolean).boolean;
  result.error = optional_string_member(root, "error");
  for (const auto& value : required_member(root, "skills", JsonKind::array).array) {
    if (value.kind != JsonKind::object) throw SdkError("invalid RPC result: expected skill object");
    CommunitySkill skill;
    skill.id = required_member(value, "id", JsonKind::string).scalar;
    skill.name = required_member(value, "name", JsonKind::string).scalar;
    skill.description = required_member(value, "description", JsonKind::string).scalar;
    skill.category = required_member(value, "category", JsonKind::string).scalar;
    skill.tags = string_array_member(value, "tags");
    skill.rating = optional_double_member(value, "rating");
    skill.download_count = optional_integer_member(value, "downloadCount");
    skill.is_featured = optional_bool_member(value, "isFeatured");
    skill.is_curated = optional_bool_member(value, "isCurated");
    result.skills.push_back(std::move(skill));
  }
  for (const auto& value : required_member(root, "categories", JsonKind::array).array) {
    if (value.kind != JsonKind::object) throw SdkError("invalid RPC result: expected category object");
    SkillCategory category;
    category.name = required_member(value, "name", JsonKind::string).scalar;
    category.count = optional_integer_member(value, "count").value_or(0);
    result.categories.push_back(std::move(category));
  }
  return result;
}

InstallSkillResult parse_install_skill_result(const std::string& json) {
  const auto root = parse_json_document(json);
  InstallSkillResult result;
  result.success = required_member(root, "success", JsonKind::boolean).boolean;
  result.skill_name = optional_string_member(root, "skillName");
  result.path = optional_string_member(root, "path");
  result.error = optional_string_member(root, "error");
  return result;
}

McpListServersResult parse_mcp_servers_result(const std::string& json) {
  const auto root = parse_json_document(json);
  McpListServersResult result;
  for (const auto& value : required_member(root, "servers", JsonKind::array).array) {
    if (value.kind != JsonKind::object) throw SdkError("invalid RPC result: expected MCP server object");
    result.servers.push_back(McpServerInfo{
        required_member(value, "name", JsonKind::string).scalar,
        required_member(value, "status", JsonKind::string).scalar,
        optional_integer_member(value, "toolCount").value_or(0)});
  }
  return result;
}

McpListToolsResult parse_mcp_tools_result(const std::string& json) {
  const auto root = parse_json_document(json);
  McpListToolsResult result;
  for (const auto& value : required_member(root, "tools", JsonKind::array).array) {
    if (value.kind != JsonKind::object) throw SdkError("invalid RPC result: expected MCP tool object");
    result.tools.push_back(McpToolInfo{
        required_member(value, "name", JsonKind::string).scalar,
        required_member(value, "description", JsonKind::string).scalar,
        required_member(value, "serverName", JsonKind::string).scalar});
  }
  return result;
}

McpGetServerConfigsResult parse_mcp_configs_result(const std::string& json) {
  const auto root = parse_json_document(json);
  McpGetServerConfigsResult result;
  for (const auto& value : required_member(root, "configs", JsonKind::array).array) {
    if (value.kind != JsonKind::object) throw SdkError("invalid RPC result: expected MCP config object");
    McpServerConfigInfo config;
    config.name = required_member(value, "name", JsonKind::string).scalar;
    const auto transport = required_member(value, "transport", JsonKind::string).scalar;
    if (transport == "stdio") config.transport = McpTransport::Stdio;
    else if (transport == "sse") config.transport = McpTransport::Sse;
    else if (transport == "http") config.transport = McpTransport::Http;
    else throw SdkError("invalid RPC result: unknown MCP transport '" + transport + "'");
    config.command = optional_string_member(value, "command");
    config.args = string_array_member(value, "args");
    config.url = optional_string_member(value, "url");
    config.env = string_map_member(value, "env");
    config.headers = string_map_member(value, "headers");
    config.auto_connect = optional_bool_member(value, "autoConnect");
    result.configs.push_back(std::move(config));
  }
  return result;
}

PermissionAcknowledgedResult parse_permission_acknowledged_result(const std::string& json) {
  const auto root = parse_json_document(json);
  return PermissionAcknowledgedResult{
      required_member(root, "success", JsonKind::boolean).boolean};
}

DirectoryAccessResponseResult parse_directory_access_response_result(
    const std::string& json) {
  const auto root = parse_json_document(json);
  return DirectoryAccessResponseResult{
      required_member(root, "success", JsonKind::boolean).boolean};
}

DirectoryAccessAcknowledgedResult parse_directory_access_acknowledged_result(
    const std::string& json) {
  const auto root = parse_json_document(json);
  return DirectoryAccessAcknowledgedResult{
      required_member(root, "success", JsonKind::boolean).boolean};
}

ChangesDecisionResult parse_changes_decision_result(const std::string& json) {
  const auto root = parse_json_document(json);
  ChangesDecisionResult result;
  result.success = required_member(root, "success", JsonKind::boolean).boolean;
  result.applied_count = required_integer_member(root, "appliedCount");
  result.skipped_count = required_integer_member(root, "skippedCount");
  const auto* errors = root.member("errors");
  if (!errors || errors->kind == JsonKind::null_value) return result;
  if (errors->kind != JsonKind::array) {
    throw SdkError("invalid RPC result: expected array 'errors'");
  }
  for (const auto& value : errors->array) {
    if (value.kind != JsonKind::object) {
      throw SdkError("invalid RPC result: expected change decision error object");
    }
    result.errors.push_back(ChangeDecisionError{
        required_member(value, "changeId", JsonKind::string).scalar,
        required_member(value, "error", JsonKind::string).scalar});
  }
  return result;
}

SessionStatus parse_session_status(const std::string& value) {
  if (value == "active") return SessionStatus::Active;
  if (value == "completed") return SessionStatus::Completed;
  if (value == "crashed") return SessionStatus::Crashed;
  throw SdkError("invalid RPC result: unknown session status '" + value + "'");
}

SessionHistoryResult parse_session_history_result(const std::string& json) {
  const auto root = parse_json_document(json);
  SessionHistoryResult result;
  for (const auto& value : required_member(root, "sessions", JsonKind::array).array) {
    if (value.kind != JsonKind::object) {
      throw SdkError("invalid RPC result: expected session history object");
    }
    result.sessions.push_back(SessionHistoryEntry{
        required_member(value, "sessionId", JsonKind::string).scalar,
        required_member(value, "createdAt", JsonKind::string).scalar,
        required_member(value, "lastActiveAt", JsonKind::string).scalar,
        required_member(value, "projectName", JsonKind::string).scalar,
        required_member(value, "model", JsonKind::string).scalar,
        required_integer_member(value, "messageCount"),
        parse_session_status(required_member(value, "status", JsonKind::string).scalar)});
  }
  result.current_page = required_integer_member(root, "currentPage");
  result.total_pages = required_integer_member(root, "totalPages");
  result.total_items = required_integer_member(root, "totalItems");
  return result;
}

SessionMessageRole parse_session_message_role(const std::string& value) {
  if (value == "user") return SessionMessageRole::User;
  if (value == "assistant") return SessionMessageRole::Assistant;
  if (value == "system") return SessionMessageRole::System;
  if (value == "tool") return SessionMessageRole::Tool;
  throw SdkError("invalid RPC result: unknown session message role '" + value + "'");
}

SessionDetailsResult parse_session_details_result(const std::string& json) {
  const auto root = parse_json_document(json);
  const auto success = required_member(root, "success", JsonKind::boolean).boolean;
  if (!success) return SessionLookupFailure{optional_string_member(root, "error")};

  SessionDetails details;
  details.session_id = required_member(root, "sessionId", JsonKind::string).scalar;
  details.project_name = required_member(root, "projectName", JsonKind::string).scalar;
  details.model = required_member(root, "model", JsonKind::string).scalar;
  details.message_count = required_integer_member(root, "messageCount");
  details.status =
      parse_session_status(required_member(root, "status", JsonKind::string).scalar);
  details.created_at = required_member(root, "createdAt", JsonKind::string).scalar;
  details.last_active_at = required_member(root, "lastActiveAt", JsonKind::string).scalar;
  details.summary = optional_string_member(root, "summary");
  details.workspace_root = required_member(root, "workspaceRoot", JsonKind::string).scalar;
  for (const auto& value : required_member(root, "messages", JsonKind::array).array) {
    if (value.kind != JsonKind::object) {
      throw SdkError("invalid RPC result: expected session message object");
    }
    SessionMessage message;
    message.id = required_member(value, "id", JsonKind::string).scalar;
    message.role = parse_session_message_role(
        required_member(value, "role", JsonKind::string).scalar);
    message.content = required_member(value, "content", JsonKind::string).scalar;
    message.timestamp = required_member(value, "timestamp", JsonKind::string).scalar;
    const auto* tool_calls = value.member("toolCalls");
    if (tool_calls && tool_calls->kind != JsonKind::null_value) {
      if (tool_calls->kind != JsonKind::array) {
        throw SdkError("invalid RPC result: expected array 'toolCalls'");
      }
      for (const auto& call : tool_calls->array) {
        if (call.kind != JsonKind::object) {
          throw SdkError("invalid RPC result: expected session tool call object");
        }
        message.tool_calls.push_back(SessionToolCall{
            required_member(call, "id", JsonKind::string).scalar,
            required_member(call, "name", JsonKind::string).scalar,
            serialize_json(required_member(call, "args", JsonKind::object))});
      }
    }
    details.messages.push_back(std::move(message));
  }
  return details;
}

std::vector<std::string> split_exec_args(const std::string& executable, const std::vector<std::string>& args) {
  std::vector<std::string> all;
  all.push_back(executable);
  all.insert(all.end(), args.begin(), args.end());
  return all;
}

ssize_t send_without_sigpipe(int fd, const void* data, std::size_t size) {
#ifdef MSG_NOSIGNAL
  return send(fd, data, size, MSG_NOSIGNAL);
#else
  return send(fd, data, size, 0);
#endif
}

}  // namespace

RpcError::RpcError(int code_value, const std::string& message, std::string data_value)
    : SdkError("RPC error " + std::to_string(code_value) + ": " + message),
      code(code_value),
      rpc_message(message),
      data(std::move(data_value)) {}

StructuredOutputError::StructuredOutputError(const std::string& message, std::string raw)
    : SdkError(message), raw_response(std::move(raw)) {}

void initialize() {
  static const bool initialized = [] {
    (void)parse_json_document("{}");
    return true;
  }();
  (void)initialized;
}

Config Config::from_environment() {
  Config config;
  if (const char* cli = std::getenv("AUTOHAND_CLI_PATH")) {
    config.cli_path = cli;
  }
  if (const char* key = std::getenv("AUTOHAND_AI_API_KEY")) {
    config.provider = "autohandai";
    config.api_key = key;
  }
  if (const char* url = std::getenv("AUTOHAND_AI_BASE_URL")) {
    config.base_url = url;
  }
  if (const char* plan = std::getenv("AUTOHAND_AI_PLAN")) {
    config.autohand_ai_plan = plan;
  }
  return config;
}

Config& Config::with_cwd(std::string value) {
  cwd = std::move(value);
  return *this;
}

Config& Config::with_cli_path(std::string value) {
  cli_path = std::move(value);
  return *this;
}

Config& Config::with_model(std::string value) {
  model = std::move(value);
  return *this;
}

Config& Config::with_skill(std::string value) {
  skills.push_back(std::move(value));
  return *this;
}

Config& Config::with_instructions(std::string value) {
  if (append_system_prompt && !append_system_prompt->empty()) {
    append_system_prompt = *append_system_prompt + "\n\n" + value;
  } else {
    append_system_prompt = std::move(value);
  }
  return *this;
}

std::vector<std::string> Config::cli_args() const {
  std::vector<std::string> args{"--mode", "rpc"};
  if (bare) args.push_back("--bare");
  if (idle_logout == false) args.push_back("--no-idle-logout");
  if (unrestricted) args.push_back("--unrestricted");
  if (auto_mode) args.push_back("--auto-mode");
  if (auto_skill) args.push_back("--auto-skill");
  if (auto_commit) args.push_back("-c");
  if (persist_session) args.push_back("--persist-session");
  if (resume) args.push_back("--resume");
  if (continue_session) args.push_back("--continue");
  if (agents_md_create) args.push_back("--agents-md-create");
  if (agents_md_auto_update) args.push_back("--agents-md-auto-update");
  if (agents_md_enable == true) args.push_back("--agents-md");
  if (agents_md_enable == false) args.push_back("--no-agents-md");
  if (context_compact == true) args.push_back("--context-compact");
  if (context_compact == false) args.push_back("--no-context-compact");
  append_value(args, "--max-iterations", max_iterations);
  append_value(args, "--max-runtime", max_runtime_minutes);
  append_value(args, "--max-cost", max_cost);
  append_value(args, "--session-id", session_id);
  append_value(args, "--session-path", session_path);
  append_value(args, "--auto-save-interval", auto_save_interval);
  append_value(args, "--max-tokens", max_tokens);
  append_value(args, "--compression-threshold", compression_threshold);
  append_value(args, "--summarization-threshold", summarization_threshold);
  append_value(args, "--agents-md-path", agents_md_path);
  append_value(args, "--model", model);
  append_value(args, "--temperature", temperature);
  append_value(args, "--sys-prompt", system_prompt);
  append_value(args, "--append-sys-prompt", append_system_prompt);
  append_value(args, "--fork", fork_session);
  append_value(args, "--display-language", display_language);
  append_value(args, "--system-prompt-file", system_prompt_file);
  append_value(args, "--append-system-prompt-file", append_system_prompt_file);
  append_value(args, "--mcp-config", mcp_config);
  append_value(args, "--agents", agents);
  append_value(args, "--plugin-dir", plugin_dir);
  append_value(args, "--yolo", yolo);
  append_value(args, "--yolo-timeout", yolo_timeout_seconds);
  append_joined(args, "--skills", skills);
  append_joined(args, "--skill-sources", skill_sources);
  if (install_missing_skills) args.push_back("--install-missing-skills");
  for (const auto& dir : additional_directories) {
    args.push_back("--add-dir");
    args.push_back(dir);
  }
  args.insert(args.end(), extra_args.begin(), extra_args.end());
  return args;
}

std::string GoalParams::to_json() const {
  std::ostringstream out;
  bool first = true;
  out << '{';
  if (objective) append_json_string(out, first, "objective", *objective);
  if (status) append_json_string(out, first, "status", *status);
  if (token_budget) append_json_number(out, first, "token_budget", *token_budget);
  if (time_budget_seconds) append_json_number(out, first, "time_budget_seconds", *time_budget_seconds);
  if (min_tokens_before_wrap_up) {
    append_json_number(out, first, "min_tokens_before_wrap_up", *min_tokens_before_wrap_up);
  }
  if (min_time_seconds_before_wrap_up) {
    append_json_number(out, first, "min_time_seconds_before_wrap_up", *min_time_seconds_before_wrap_up);
  }
  out << '}';
  return out.str();
}

std::string AutoresearchSubagentOptions::to_json() const {
  std::ostringstream out;
  bool first = true;
  out << '{';
  if (idea_generation) append_json_bool(out, first, "ideaGeneration", *idea_generation);
  if (measurement_analysis) append_json_bool(out, first, "measurementAnalysis", *measurement_analysis);
  if (finalization) append_json_bool(out, first, "finalization", *finalization);
  out << '}';
  return out.str();
}

std::string AutoresearchStartParams::to_json() const {
  if (objective.empty()) throw SdkError("autoresearch objective must not be empty");
  std::ostringstream out;
  bool first = true;
  out << '{';
  append_json_string(out, first, "objective", objective);
  if (max_iterations) append_json_number(out, first, "maxIterations", *max_iterations);
  if (timeout_ms) append_json_number(out, first, "timeoutMs", *timeout_ms);
  if (metric_name) append_json_string(out, first, "metricName", *metric_name);
  if (metric_unit) append_json_string(out, first, "metricUnit", *metric_unit);
  if (direction) append_json_string(out, first, "direction", *direction);
  if (measure_command) append_json_string(out, first, "measureCommand", *measure_command);
  if (measure_script) append_json_string(out, first, "measureScript", *measure_script);
  if (checks_command) append_json_string(out, first, "checksCommand", *checks_command);
  if (checks_script) append_json_string(out, first, "checksScript", *checks_script);
  append_json_strings(out, first, "filesInScope", files_in_scope);
  if (subagents) append_json_raw(out, first, "subagents", subagents->to_json());
  if (secondary_objectives_json) {
    append_json_raw(out, first, "secondaryObjectives", *secondary_objectives_json);
  }
  if (constraints_json) append_json_raw(out, first, "constraints", *constraints_json);
  if (sampling_json) append_json_raw(out, first, "sampling", *sampling_json);
  if (retention_json) append_json_raw(out, first, "retention", *retention_json);
  append_json_strings(out, first, "environmentAllowlist", environment_allowlist);
  out << '}';
  return out.str();
}

std::string PromptOptions::to_json(std::string_view message) const {
  std::ostringstream out;
  out << "{\"message\":\"" << json_escape(message) << "\"";
  if (!context_json.empty()) {
    out << ",\"context\":" << context_json;
  }
  if (!image_json.empty()) {
    out << ",\"images\":[";
    for (size_t i = 0; i < image_json.size(); ++i) {
      if (i > 0) out << ",";
      out << image_json[i];
    }
    out << "]";
  }
  if (thinking_level) {
    out << ",\"thinkingLevel\":\"" << json_escape(*thinking_level) << "\"";
  }
  for (const auto& [key, value] : extra_json) {
    out << ",\"" << json_escape(key) << "\":" << value;
  }
  out << "}";
  return out.str();
}

std::string GetSkillsRegistryParams::to_json() const {
  if (!force_refresh) return "{}";
  return std::string("{\"forceRefresh\":") + (*force_refresh ? "true}" : "false}");
}

std::string BrowserHandoffCreateParams::to_json() const {
  std::ostringstream out;
  bool first = true;
  out << '{';
  if (extension_id) append_json_string(out, first, "extensionId", *extension_id);
  if (install_url) append_json_string(out, first, "installUrl", *install_url);
  out << '}';
  return out.str();
}

std::string BrowserHandoffAttachParams::to_json() const {
  if (token.empty()) throw SdkError("browser handoff token must not be empty");
  return "{\"token\":\"" + json_escape(token) + "\"}";
}

std::string AutomodeStartParams::to_json() const {
  if (prompt.empty()) throw SdkError("auto-mode prompt must not be empty");
  std::ostringstream out;
  bool first = true;
  out << '{';
  append_json_string(out, first, "prompt", prompt);
  if (max_iterations) append_json_number(out, first, "maxIterations", *max_iterations);
  if (completion_promise) append_json_string(out, first, "completionPromise", *completion_promise);
  if (use_worktree) append_json_bool(out, first, "useWorktree", *use_worktree);
  if (checkpoint_interval) append_json_number(out, first, "checkpointInterval", *checkpoint_interval);
  if (max_runtime) append_json_number(out, first, "maxRuntime", *max_runtime);
  if (max_cost) append_json_number(out, first, "maxCost", *max_cost);
  out << '}';
  return out.str();
}

std::string AutomodeCancelParams::to_json() const {
  if (!reason) return "{}";
  return "{\"reason\":\"" + json_escape(*reason) + "\"}";
}

std::string AutomodeGetLogParams::to_json() const {
  if (!limit) return "{}";
  return "{\"limit\":" + std::to_string(*limit) + "}";
}

std::string InstallSkillParams::to_json() const {
  if (skill_name.empty()) throw SdkError("skill_name must not be empty");
  std::ostringstream out;
  out << "{\"skillName\":\"" << json_escape(skill_name) << "\",\"scope\":\""
      << (scope == SkillInstallScope::User ? "user" : "project") << '"';
  if (force) out << ",\"force\":" << (*force ? "true" : "false");
  out << '}';
  return out.str();
}

std::string McpListToolsParams::to_json() const {
  if (!server_name) return "{}";
  return "{\"serverName\":\"" + json_escape(*server_name) + "\"}";
}

std::string SdkEvent::text_delta() const { return json_get_string(raw_json, "delta"); }
std::string SdkEvent::message_content() const { return json_get_string(raw_json, "content"); }
std::string SdkEvent::tool_name() const {
  auto name = json_get_string(raw_json, "toolName");
  return name.empty() ? json_get_string(raw_json, "tool_name") : name;
}
std::string SdkEvent::request_id() const {
  auto id = json_get_string(raw_json, "requestId");
  return id.empty() ? json_get_string(raw_json, "request_id") : id;
}
std::string SdkEvent::description() const { return json_get_string(raw_json, "description"); }
std::string SdkEvent::autoresearch_phase() const { return json_get_string(raw_json, "phase"); }
std::string SdkEvent::autoresearch_operation() const { return json_get_string(raw_json, "operation"); }

class AutohandSdk::Impl {
 public:
  explicit Impl(Config config) : config_(std::move(config)) {}
  ~Impl() { stop(); }

  void start() {
    if (started_) return;
    if (pid_ > 0 || stdin_fd_ >= 0 || stdout_fd_ >= 0 || stderr_fd_ >= 0 ||
        stdout_thread_.joinable() || stderr_thread_.joinable()) {
      stop();
    }

    int stdin_socket[2]{-1, -1};
    int stdout_pipe[2]{-1, -1};
    int stderr_pipe[2]{-1, -1};
    int exec_error_pipe[2]{-1, -1};
    const auto close_pair = [](int pair[2]) {
      if (pair[0] >= 0) close(pair[0]);
      if (pair[1] >= 0) close(pair[1]);
    };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, stdin_socket) != 0 || pipe(stdout_pipe) != 0 ||
        pipe(stderr_pipe) != 0 || pipe(exec_error_pipe) != 0) {
      const auto error = errno;
      close_pair(stdin_socket);
      close_pair(stdout_pipe);
      close_pair(stderr_pipe);
      close_pair(exec_error_pipe);
      throw SdkError(std::string("transport pipe failed: ") + std::strerror(error));
    }
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    (void)setsockopt(stdin_socket[0], SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
    const auto descriptor_flags = fcntl(exec_error_pipe[1], F_GETFD);
    if (descriptor_flags < 0 ||
        fcntl(exec_error_pipe[1], F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
      const auto error = errno;
      close_pair(stdin_socket);
      close_pair(stdout_pipe);
      close_pair(stderr_pipe);
      close_pair(exec_error_pipe);
      throw SdkError(std::string("configure exec status pipe failed: ") + std::strerror(error));
    }

    const auto executable = config_.cli_path.empty() ? std::string("autohand") : config_.cli_path;
    const auto args = split_exec_args(executable, config_.cli_args());

    pid_ = fork();
    if (pid_ < 0) {
      const auto error = errno;
      close_pair(stdin_socket);
      close_pair(stdout_pipe);
      close_pair(stderr_pipe);
      close_pair(exec_error_pipe);
      throw SdkError(std::string("fork failed: ") + std::strerror(error));
    }

    if (pid_ == 0) {
      close(exec_error_pipe[0]);
      const auto child_fail = [&](int error) {
        while (::write(exec_error_pipe[1], &error, sizeof(error)) < 0 && errno == EINTR) {
        }
        _exit(127);
      };
      if (dup2(stdin_socket[1], STDIN_FILENO) < 0 || dup2(stdout_pipe[1], STDOUT_FILENO) < 0 ||
          dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
        child_fail(errno);
      }
      close(stdin_socket[0]);
      close(stdin_socket[1]);
      close(stdout_pipe[0]);
      close(stdout_pipe[1]);
      close(stderr_pipe[0]);
      close(stderr_pipe[1]);
      if (!config_.cwd.empty() && chdir(config_.cwd.c_str()) != 0) {
        child_fail(errno);
      }
      setenv("AUTOHAND_STREAM_TOOL_OUTPUT", "1", 1);
      if (config_.provider == "autohandai") {
        setenv("AUTOHAND_AI_PLAN", config_.autohand_ai_plan.value_or("cloud").c_str(), 1);
        if (config_.api_key) setenv("AUTOHAND_AI_API_KEY", config_.api_key->c_str(), 1);
        if (config_.base_url) setenv("AUTOHAND_AI_BASE_URL", config_.base_url->c_str(), 1);
      }
      for (const auto& [key, value] : config_.environment) {
        setenv(key.c_str(), value.c_str(), 1);
      }
      std::vector<char*> argv;
      argv.reserve(args.size() + 1);
      for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
      }
      argv.push_back(nullptr);
      execvp(executable.c_str(), argv.data());
      child_fail(errno);
    }

    close(stdin_socket[1]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    close(exec_error_pipe[1]);
    int child_error = 0;
    ssize_t exec_status = 0;
    do {
      exec_status = read(exec_error_pipe[0], &child_error, sizeof(child_error));
    } while (exec_status < 0 && errno == EINTR);
    const auto exec_read_error = errno;
    close(exec_error_pipe[0]);
    if (exec_status != 0) {
      close(stdin_socket[0]);
      close(stdout_pipe[0]);
      close(stderr_pipe[0]);
      if (exec_status < 0) (void)kill(pid_, SIGKILL);
      int status = 0;
      while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
      }
      pid_ = -1;
      if (exec_status < 0) {
        throw SdkError(std::string("read exec status failed: ") + std::strerror(exec_read_error));
      }
      throw SdkError(std::string("start CLI failed: ") + std::strerror(child_error));
    }

    stdin_fd_ = stdin_socket[0];
    stdout_fd_ = stdout_pipe[0];
    stderr_fd_ = stderr_pipe[0];
    started_ = true;
    stdout_thread_ = std::thread([this] { read_stdout(); });
    stderr_thread_ = std::thread([this] { read_stderr(); });
    try {
      (void)request("autohand.getState", "{}");
      if (config_.feature_settings_json) {
        (void)request("autohand.applyFlagSettings", "{\"settings\":" + *config_.feature_settings_json + "}");
      }
    } catch (...) {
      stop();
      throw;
    }
  }

  void stop() {
    started_ = false;
    {
      std::lock_guard lock(write_mutex_);
      if (stdin_fd_ >= 0) {
        (void)shutdown(stdin_fd_, SHUT_RDWR);
        close(stdin_fd_);
        stdin_fd_ = -1;
      }
    }
    if (pid_ > 0) {
      (void)kill(pid_, SIGTERM);
      int status = 0;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
      while (true) {
        const auto result = waitpid(pid_, &status, WNOHANG);
        if (result == pid_ || (result < 0 && errno == ECHILD)) break;
        if (result < 0 && errno != EINTR) break;
        if (std::chrono::steady_clock::now() >= deadline) {
          (void)kill(pid_, SIGKILL);
          while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
          }
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      pid_ = -1;
    }
    if (stdout_thread_.joinable()) stdout_thread_.join();
    if (stderr_thread_.joinable()) stderr_thread_.join();
    if (stdout_fd_ >= 0) {
      close(stdout_fd_);
      stdout_fd_ = -1;
    }
    if (stderr_fd_ >= 0) {
      close(stderr_fd_);
      stderr_fd_ = -1;
    }
    fail_pending("transport stopped");
  }

  bool is_started() const { return started_; }

  std::string request(const std::string& method, const std::string& params_json) {
    if (!started_) throw SdkError("transport has not been started");
    const auto id = next_id_++;
    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();
    {
      std::lock_guard lock(pending_mutex_);
      pending_[id] = promise;
    }

    std::ostringstream payload;
    payload << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"method\":\"" << json_escape(method)
            << "\",\"params\":" << (params_json.empty() ? "{}" : params_json) << "}\n";
    const auto line = payload.str();
    try {
      std::lock_guard lock(write_mutex_);
      std::size_t written = 0;
      while (written < line.size()) {
        const auto count = send_without_sigpipe(stdin_fd_, line.data() + written, line.size() - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
          const auto error = count == 0 ? EPIPE : errno;
          throw SdkError(std::string("write failed: ") + std::strerror(error));
        }
        written += static_cast<std::size_t>(count);
      }
    } catch (...) {
      std::lock_guard lock(pending_mutex_);
      pending_.erase(id);
      throw;
    }

    if (future.wait_for(config_.timeout) == std::future_status::timeout) {
      std::lock_guard lock(pending_mutex_);
      pending_.erase(id);
      throw RequestTimeoutError(method);
    }
    return future.get();
  }

  void clear_events() {
    std::lock_guard lock(event_mutex_);
    events_.clear();
  }

  std::vector<SdkEvent> drain_events() {
    std::lock_guard lock(event_mutex_);
    auto events = std::move(events_);
    events_.clear();
    return events;
  }

  void wait_for_event(std::chrono::milliseconds timeout) {
    std::unique_lock lock(event_mutex_);
    if (events_.empty()) {
      event_cv_.wait_for(lock, timeout);
    }
  }

  std::unique_lock<std::mutex> lock_stream() {
    return std::unique_lock<std::mutex>(stream_mutex_);
  }

 private:
  void read_stdout() {
    const auto reader_fd = dup(stdout_fd_);
    FILE* file = reader_fd >= 0 ? fdopen(reader_fd, "r") : nullptr;
    if (!file) {
      if (reader_fd >= 0) close(reader_fd);
      started_ = false;
      fail_pending("failed to open CLI stdout");
      return;
    }
    char* line = nullptr;
    size_t size = 0;
    while (started_ && getline(&line, &size, file) != -1) {
      std::string text(line);
      if (!text.empty() && text.back() == '\n') text.pop_back();
      handle_line(text);
    }
    free(line);
    fclose(file);
    started_ = false;
    fail_pending("CLI stdout closed");
  }

  void read_stderr() {
    const auto reader_fd = dup(stderr_fd_);
    FILE* file = reader_fd >= 0 ? fdopen(reader_fd, "r") : nullptr;
    if (!file) {
      if (reader_fd >= 0) close(reader_fd);
      return;
    }
    char* line = nullptr;
    size_t size = 0;
    while (started_ && getline(&line, &size, file) != -1) {
      if (config_.debug) {
        std::cerr << "[autohand] " << line;
      }
    }
    free(line);
    fclose(file);
  }

  void fail_pending(const std::string& message) {
    std::map<long, std::shared_ptr<std::promise<std::string>>> pending;
    {
      std::lock_guard lock(pending_mutex_);
      pending.swap(pending_);
    }
    for (const auto& [id, promise] : pending) {
      (void)id;
      try {
        promise->set_exception(std::make_exception_ptr(SdkError(message)));
      } catch (const std::future_error&) {
      }
    }
  }

  void handle_line(const std::string& line) {
    JsonValue root;
    try {
      root = parse_json_document(line);
    } catch (const std::exception& error) {
      if (config_.debug) std::cerr << "[autohand] invalid JSON-RPC line: " << error.what() << '\n';
      return;
    }
    const auto id = extract_id(root);
    if (id > 0) {
      std::shared_ptr<std::promise<std::string>> promise;
      {
        std::lock_guard lock(pending_mutex_);
        auto it = pending_.find(id);
        if (it != pending_.end()) {
          promise = it->second;
          pending_.erase(it);
        }
      }
      if (promise) {
        const auto* error = root.member("error");
        if (error && error->kind == JsonKind::object) {
          const auto error_json = serialize_json(*error);
          promise->set_exception(std::make_exception_ptr(RpcError(
              extract_error_code(*error), json_string_member(*error, "message"), error_json)));
        } else {
          const auto* result = root.member("result");
          promise->set_value(result ? serialize_json(*result) : "null");
        }
      }
      return;
    }

    const auto method = json_string_member(root, "method");
    if (method.empty()) return;
    const auto* params_value = root.member("params");
    const auto params = params_value ? serialize_json(*params_value) : "null";
    auto event = sdk_event_from_notification(method, params);
    {
      std::lock_guard lock(event_mutex_);
      events_.push_back(std::move(event));
    }
    event_cv_.notify_one();
  }

  Config config_;
  std::atomic<bool> started_{false};
  std::atomic<long> next_id_{1};
  pid_t pid_ = -1;
  int stdin_fd_ = -1;
  int stdout_fd_ = -1;
  int stderr_fd_ = -1;
  std::thread stdout_thread_;
  std::thread stderr_thread_;
  std::mutex write_mutex_;
  std::mutex pending_mutex_;
  std::mutex stream_mutex_;
  std::mutex event_mutex_;
  std::condition_variable event_cv_;
  std::map<long, std::shared_ptr<std::promise<std::string>>> pending_;
  std::vector<SdkEvent> events_;
};

AutohandSdk::AutohandSdk(Config config) : impl_(std::make_unique<Impl>(std::move(config))) {}
AutohandSdk::~AutohandSdk() = default;
AutohandSdk::AutohandSdk(AutohandSdk&&) noexcept = default;
AutohandSdk& AutohandSdk::operator=(AutohandSdk&&) noexcept = default;

void AutohandSdk::start() { impl_->start(); }
void AutohandSdk::stop() { impl_->stop(); }
bool AutohandSdk::is_started() const { return impl_->is_started(); }
std::string AutohandSdk::request(const std::string& method, const std::string& params_json) {
  return impl_->request(method, params_json);
}
std::string AutohandSdk::prompt(const std::string& message, const PromptOptions& options) {
  return request("autohand.prompt", options.to_json(message));
}
void AutohandSdk::stream_prompt(
    const std::string& message,
    const std::function<void(const SdkEvent&)>& on_event,
    const PromptOptions& options) {
  auto stream_guard = impl_->lock_stream();
  impl_->clear_events();
  auto prompt_future = std::async(std::launch::async, [this, &message, &options] {
    return prompt(message, options);
  });

  while (prompt_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    impl_->wait_for_event(std::chrono::milliseconds(25));
    for (const auto& event : impl_->drain_events()) {
      on_event(event);
    }
  }

  for (const auto& event : impl_->drain_events()) {
    on_event(event);
  }

  (void)prompt_future.get();
}
std::string AutohandSdk::interrupt() { return request("autohand.abort"); }
std::string AutohandSdk::set_plan_mode(bool enabled) {
  return request("autohand.planModeSet", std::string("{\"enabled\":") + (enabled ? "true}" : "false}"));
}
std::string AutohandSdk::set_permission_mode(const std::string& mode) {
  return request("autohand.permissionModeSet", "{\"mode\":\"" + json_escape(mode) + "\"}");
}
std::string AutohandSdk::set_model(const std::string& model) {
  return request("autohand.modelSet", "{\"model\":\"" + json_escape(model) + "\"}");
}
std::string AutohandSdk::get_state() { return request("autohand.getState"); }
std::string AutohandSdk::get_messages() { return request("autohand.getMessages"); }
ResetResult AutohandSdk::reset() {
  return parse_reset_result(request("autohand.reset"));
}
BrowserHandoffCreateResult AutohandSdk::create_browser_handoff(
    const BrowserHandoffCreateParams& params) {
  return parse_browser_handoff_create_result(
      request("autohand.browserHandoff.create", params.to_json()));
}
BrowserHandoffAttachResult AutohandSdk::attach_browser_handoff(
    const BrowserHandoffAttachParams& params) {
  return parse_browser_handoff_attach_result(
      request("autohand.browserHandoff.attach", params.to_json()));
}
BrowserHandoffAttachResult AutohandSdk::attach_latest_browser_handoff() {
  return parse_browser_handoff_attach_result(
      request("autohand.browserHandoff.attachLatest"));
}
AutomodeStartResult AutohandSdk::start_automode(const AutomodeStartParams& params) {
  return parse_automode_start_result(
      request("autohand.automode.start", params.to_json()));
}
AutomodeStatusResult AutohandSdk::get_automode_status() {
  return parse_automode_status_result(request("autohand.automode.status"));
}
AutomodeOperationResult AutohandSdk::pause_automode() {
  return parse_automode_operation_result(request("autohand.automode.pause"));
}
AutomodeOperationResult AutohandSdk::resume_automode() {
  return parse_automode_operation_result(request("autohand.automode.resume"));
}
AutomodeOperationResult AutohandSdk::cancel_automode(const AutomodeCancelParams& params) {
  return parse_automode_operation_result(
      request("autohand.automode.cancel", params.to_json()));
}
AutomodeGetLogResult AutohandSdk::get_automode_log(const AutomodeGetLogParams& params) {
  return parse_automode_get_log_result(
      request("autohand.automode.getLog", params.to_json()));
}
GetSkillsRegistryResult AutohandSdk::get_skills_registry(const GetSkillsRegistryParams& params) {
  return parse_skills_registry_result(request("autohand.getSkillsRegistry", params.to_json()));
}
InstallSkillResult AutohandSdk::install_skill(const InstallSkillParams& params) {
  return parse_install_skill_result(request("autohand.installSkill", params.to_json()));
}
McpListServersResult AutohandSdk::list_mcp_servers() {
  return parse_mcp_servers_result(request("autohand.mcp.listServers"));
}
McpListToolsResult AutohandSdk::list_mcp_tools(const McpListToolsParams& params) {
  return parse_mcp_tools_result(request("autohand.mcp.listTools", params.to_json()));
}
McpGetServerConfigsResult AutohandSdk::get_mcp_server_configs() {
  return parse_mcp_configs_result(request("autohand.mcp.getServerConfigs"));
}
std::string AutohandSdk::get_supported_commands() { return request("autohand.getSupportedCommands"); }
bool AutohandSdk::supports_command(const std::string& command) {
  const auto normalized = format_slash_command(command);
  const auto plain = normalized.substr(1);
  const auto result = get_supported_commands();
  return result.find("\"" + normalized + "\"") != std::string::npos ||
         result.find("\"" + plain + "\"") != std::string::npos;
}
void AutohandSdk::stream_command(
    const std::string& command,
    const std::string& args,
    const std::function<void(const SdkEvent&)>& on_event,
    const PromptOptions& options) {
  stream_prompt(format_slash_command(command, args), on_event, options);
}
std::string AutohandSdk::apply_flag_settings(const std::string& settings_json) {
  return request("autohand.applyFlagSettings", "{\"settings\":" + settings_json + "}");
}
std::string AutohandSdk::get_goal() { return request("autohand.goal.get"); }
std::string AutohandSdk::create_goal(const GoalParams& params) {
  return request("autohand.goal.create", params.to_json());
}
std::string AutohandSdk::update_goal(const GoalParams& params) {
  return request("autohand.goal.update", params.to_json());
}
std::string AutohandSdk::clear_goal() { return request("autohand.goal.clear"); }
std::string AutohandSdk::queue_goal(const GoalParams& params) {
  return request("autohand.goal.queue", params.to_json());
}
std::string AutohandSdk::start_queued_goal() { return request("autohand.goal.startQueued"); }
std::string AutohandSdk::list_goal_templates() { return request("autohand.goal.listTemplates"); }
std::string AutohandSdk::start_autoresearch(const AutoresearchStartParams& params) {
  return request("autohand.autoresearch.start", params.to_json());
}
std::string AutohandSdk::get_autoresearch_status() {
  return request("autohand.autoresearch.status");
}
std::string AutohandSdk::stop_autoresearch() { return request("autohand.autoresearch.stop"); }
std::string AutohandSdk::get_autoresearch_history() {
  return request("autohand.autoresearch.history");
}
std::string AutohandSdk::replay_autoresearch(const std::string& attempt_id, const std::string& evaluator) {
  return request("autohand.autoresearch.replay",
                 "{\"attemptId\":\"" + json_escape(attempt_id) + "\",\"evaluator\":\"" +
                     json_escape(evaluator) + "\"}");
}
std::string AutohandSdk::rescore_autoresearch(const std::string& attempt_id) {
  return request("autohand.autoresearch.rescore", "{\"attemptId\":\"" + json_escape(attempt_id) + "\"}");
}
std::string AutohandSdk::rescore_all_autoresearch() {
  return request("autohand.autoresearch.rescore", "{\"all\":true}");
}
std::string AutohandSdk::compare_autoresearch(
    const std::string& left_attempt_id, const std::string& right_attempt_id) {
  return request("autohand.autoresearch.compare",
                 "{\"leftAttemptId\":\"" + json_escape(left_attempt_id) + "\",\"rightAttemptId\":\"" +
                     json_escape(right_attempt_id) + "\"}");
}
std::string AutohandSdk::get_autoresearch_pareto() {
  return request("autohand.autoresearch.pareto");
}
std::string AutohandSdk::pin_autoresearch(const std::string& attempt_id, bool pinned) {
  return request("autohand.autoresearch.pin",
                 "{\"attemptId\":\"" + json_escape(attempt_id) + "\",\"pinned\":" +
                     (pinned ? "true}" : "false}"));
}
std::string AutohandSdk::prune_autoresearch(bool dry_run, bool yes) {
  return request("autohand.autoresearch.prune",
                 std::string("{\"dryRun\":") + (dry_run ? "true" : "false") +
                     ",\"yes\":" + (yes ? "true}" : "false}"));
}
std::string AutohandSdk::permission_response(const std::string& request_id, const std::string& decision) {
  return request("autohand.permissionResponse",
                 "{\"requestId\":\"" + json_escape(request_id) + "\",\"decision\":\"" +
                     json_escape(decision) + "\"}");
}

PermissionAcknowledgedResult AutohandSdk::acknowledge_permission(
    const std::string& request_id) {
  if (request_id.empty() || is_blank(request_id)) {
    throw SdkError("permission request_id must not be blank");
  }
  return parse_permission_acknowledged_result(request(
      "autohand.permissionAcknowledged",
      "{\"requestId\":\"" + json_escape(request_id) + "\"}"));
}

std::string DirectoryAccessResponseParams::to_json() const {
  if (request_id.empty() || is_blank(request_id)) {
    throw SdkError("directory access request_id must not be blank");
  }
  return "{\"requestId\":\"" + json_escape(request_id) + "\",\"granted\":" +
         (granted ? "true}" : "false}");
}

DirectoryAccessResponseResult AutohandSdk::respond_to_directory_access(
    const DirectoryAccessResponseParams& params) {
  return parse_directory_access_response_result(
      request("autohand.directoryAccessResponse", params.to_json()));
}

DirectoryAccessAcknowledgedResult AutohandSdk::acknowledge_directory_access(
    const std::string& request_id) {
  if (request_id.empty() || is_blank(request_id)) {
    throw SdkError("directory access request_id must not be blank");
  }
  return parse_directory_access_acknowledged_result(request(
      "autohand.directoryAccessAcknowledged",
      "{\"requestId\":\"" + json_escape(request_id) + "\"}"));
}

std::string ChangesDecisionParams::to_json() const {
  if (batch_id.empty() || is_blank(batch_id)) {
    throw SdkError("changes decision batch_id must not be blank");
  }
  std::ostringstream output;
  output << "{\"batchId\":\"" << json_escape(batch_id) << "\",\"action\":\"";
  if (std::holds_alternative<AcceptAllChanges>(decision)) {
    output << "accept_all\"}";
  } else if (std::holds_alternative<RejectAllChanges>(decision)) {
    output << "reject_all\"}";
  } else {
    const auto& selected = std::get<AcceptSelectedChanges>(decision).selected_change_ids;
    if (selected.empty()) {
      throw SdkError("accept_selected requires at least one selected change ID");
    }
    output << "accept_selected\",\"selectedChangeIds\":[";
    for (std::size_t index = 0; index < selected.size(); ++index) {
      if (selected[index].empty() || is_blank(selected[index])) {
        throw SdkError("selected change IDs must not be blank");
      }
      if (index > 0) output << ',';
      output << '"' << json_escape(selected[index]) << '"';
    }
    output << "]}";
  }
  return output.str();
}

ChangesDecisionResult AutohandSdk::decide_changes(
    const ChangesDecisionParams& params) {
  return parse_changes_decision_result(
      request("autohand.changesDecision", params.to_json()));
}

std::string SessionHistoryParams::to_json() const {
  if ((page && *page < 1) || (page_size && *page_size < 1)) {
    throw SdkError("session history page values must be positive");
  }
  std::ostringstream output;
  output << '{';
  bool first = true;
  if (page) append_json_number(output, first, "page", *page);
  if (page_size) append_json_number(output, first, "pageSize", *page_size);
  output << '}';
  return output.str();
}

SessionHistoryResult AutohandSdk::get_session_history(
    const SessionHistoryParams& params) {
  return parse_session_history_result(request("autohand.getHistory", params.to_json()));
}

SessionDetailsResult AutohandSdk::get_session(const std::string& session_id) {
  if (session_id.empty() || is_blank(session_id)) {
    throw SdkError("session_id must not be blank");
  }
  return parse_session_details_result(request(
      "autohand.getSession", "{\"sessionId\":\"" + json_escape(session_id) + "\"}"));
}

Run::Run(AutohandSdk& sdk, std::string prompt, PromptOptions options)
    : sdk_(&sdk), id_(now_id()), prompt_(std::move(prompt)), options_(std::move(options)) {}

const std::string& Run::id() const { return id_; }

void Run::stream(const std::function<void(const SdkEvent&)>& on_event) {
  if (streamed_) {
    if (stream_error_) std::rethrow_exception(stream_error_);
    return;
  }
  streamed_ = true;
  result_.id = id_;
  result_.status = "running";
  try {
    sdk_->stream_prompt(prompt_, [&](const SdkEvent& event) {
      result_.events.push_back(event);
      if (event.type == "message_update") result_.text += event.text_delta();
      if (event.type == "message_end") {
        const auto content = event.message_content();
        if (!content.empty()) result_.text = content;
      }
      on_event(event);
    }, options_);
    result_.status = "completed";
  } catch (...) {
    result_.status = "failed";
    stream_error_ = std::current_exception();
    throw;
  }
}

RunResult Run::wait() {
  if (!streamed_) {
    stream([](const SdkEvent&) {});
  }
  if (stream_error_) std::rethrow_exception(stream_error_);
  return result_;
}

std::string Run::json_text() { return parse_json_text(wait().text); }

void Run::abort() { (void)sdk_->interrupt(); }

Agent::Agent(Config config) : sdk_(std::move(config)) { sdk_.start(); }
Run Agent::send(std::string prompt, PromptOptions options) {
  return Run(sdk_, std::move(prompt), std::move(options));
}
Run Agent::command(std::string command_name, std::string args, PromptOptions options) {
  return send(format_slash_command(command_name, args), std::move(options));
}
Run Agent::deep_research(std::string topic, PromptOptions options) {
  return command("/deep-research", std::move(topic), std::move(options));
}
Run Agent::autoresearch(std::string objective, PromptOptions options) {
  return command("/autoresearch", std::move(objective), std::move(options));
}
RunResult Agent::run(std::string prompt, PromptOptions options) {
  auto run = send(std::move(prompt), std::move(options));
  return run.wait();
}
std::string Agent::run_json(std::string prompt, std::string schema_json) {
  return parse_json_text(run(with_json_instruction(prompt, schema_json)).text);
}
void Agent::allow_permission(const std::string& request_id) {
  (void)sdk_.permission_response(request_id, "allow_once");
}
void Agent::deny_permission(const std::string& request_id) {
  (void)sdk_.permission_response(request_id, "deny_once");
}
void Agent::set_plan_mode(bool enabled) { (void)sdk_.set_plan_mode(enabled); }
bool Agent::supports_command(const std::string& command_name) { return sdk_.supports_command(command_name); }
std::string Agent::get_goal() { return sdk_.get_goal(); }
std::string Agent::create_goal(const GoalParams& params) { return sdk_.create_goal(params); }
std::string Agent::update_goal(const GoalParams& params) { return sdk_.update_goal(params); }
std::string Agent::clear_goal() { return sdk_.clear_goal(); }
std::string Agent::queue_goal(const GoalParams& params) { return sdk_.queue_goal(params); }
std::string Agent::start_queued_goal() { return sdk_.start_queued_goal(); }
std::string Agent::list_goal_templates() { return sdk_.list_goal_templates(); }
ResetResult Agent::reset() { return sdk_.reset(); }
BrowserHandoffCreateResult Agent::create_browser_handoff(
    const BrowserHandoffCreateParams& params) {
  return sdk_.create_browser_handoff(params);
}
BrowserHandoffAttachResult Agent::attach_browser_handoff(
    const BrowserHandoffAttachParams& params) {
  return sdk_.attach_browser_handoff(params);
}
BrowserHandoffAttachResult Agent::attach_latest_browser_handoff() {
  return sdk_.attach_latest_browser_handoff();
}
AutomodeStartResult Agent::start_automode(const AutomodeStartParams& params) {
  return sdk_.start_automode(params);
}
AutomodeStatusResult Agent::get_automode_status() {
  return sdk_.get_automode_status();
}
AutomodeOperationResult Agent::pause_automode() { return sdk_.pause_automode(); }
AutomodeOperationResult Agent::resume_automode() { return sdk_.resume_automode(); }
AutomodeOperationResult Agent::cancel_automode(const AutomodeCancelParams& params) {
  return sdk_.cancel_automode(params);
}
AutomodeGetLogResult Agent::get_automode_log(const AutomodeGetLogParams& params) {
  return sdk_.get_automode_log(params);
}
GetSkillsRegistryResult Agent::get_skills_registry(const GetSkillsRegistryParams& params) {
  return sdk_.get_skills_registry(params);
}
InstallSkillResult Agent::install_skill(const InstallSkillParams& params) {
  return sdk_.install_skill(params);
}
McpListServersResult Agent::list_mcp_servers() { return sdk_.list_mcp_servers(); }
McpListToolsResult Agent::list_mcp_tools(const McpListToolsParams& params) {
  return sdk_.list_mcp_tools(params);
}
McpGetServerConfigsResult Agent::get_mcp_server_configs() {
  return sdk_.get_mcp_server_configs();
}
std::string Agent::start_autoresearch(const AutoresearchStartParams& params) {
  return sdk_.start_autoresearch(params);
}
std::string Agent::get_autoresearch_status() { return sdk_.get_autoresearch_status(); }
std::string Agent::stop_autoresearch() { return sdk_.stop_autoresearch(); }
std::string Agent::get_autoresearch_history() { return sdk_.get_autoresearch_history(); }
std::string Agent::replay_autoresearch(const std::string& attempt_id, const std::string& evaluator) {
  return sdk_.replay_autoresearch(attempt_id, evaluator);
}
std::string Agent::rescore_autoresearch(const std::string& attempt_id) {
  return sdk_.rescore_autoresearch(attempt_id);
}
std::string Agent::rescore_all_autoresearch() { return sdk_.rescore_all_autoresearch(); }
std::string Agent::compare_autoresearch(
    const std::string& left_attempt_id, const std::string& right_attempt_id) {
  return sdk_.compare_autoresearch(left_attempt_id, right_attempt_id);
}
std::string Agent::get_autoresearch_pareto() { return sdk_.get_autoresearch_pareto(); }
std::string Agent::pin_autoresearch(const std::string& attempt_id, bool pinned) {
  return sdk_.pin_autoresearch(attempt_id, pinned);
}
std::string Agent::prune_autoresearch(bool dry_run, bool yes) {
  return sdk_.prune_autoresearch(dry_run, yes);
}
void Agent::close() { sdk_.stop(); }

std::string parse_json_text(const std::string& text) {
  auto trimmed = text;
  trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
  trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
  if (trimmed.empty()) throw StructuredOutputError("Expected JSON output, received an empty response.", text);
  if (trimmed.front() == '{' || trimmed.front() == '[') {
    try {
      (void)parse_json_document(trimmed);
      return trimmed;
    } catch (const SdkError&) {
    }
  }
  const auto fence = trimmed.find("```");
  if (fence != std::string::npos) {
    auto start = trimmed.find('\n', fence);
    auto end = trimmed.find("```", start == std::string::npos ? fence + 3 : start + 1);
    if (start != std::string::npos && end != std::string::npos) {
      auto candidate = trimmed.substr(start + 1, end - start - 1);
      candidate.erase(0, candidate.find_first_not_of(" \t\r\n"));
      candidate.erase(candidate.find_last_not_of(" \t\r\n") + 1);
      if (!candidate.empty() && (candidate.front() == '{' || candidate.front() == '[')) {
        try {
          (void)parse_json_document(candidate);
          return candidate;
        } catch (const SdkError&) {
        }
      }
    }
  }
  const auto object = trimmed.find('{');
  const auto array = trimmed.find('[');
  auto start = std::min(object == std::string::npos ? trimmed.size() : object,
                        array == std::string::npos ? trimmed.size() : array);
  while (start < trimmed.size()) {
    try {
      std::size_t consumed = 0;
      (void)JsonParser(std::string_view(trimmed).substr(start)).parse_prefix(consumed);
      return trimmed.substr(start, consumed);
    } catch (const SdkError&) {
      const auto next_object = trimmed.find('{', start + 1);
      const auto next_array = trimmed.find('[', start + 1);
      start = std::min(next_object == std::string::npos ? trimmed.size() : next_object,
                       next_array == std::string::npos ? trimmed.size() : next_array);
      if (start >= trimmed.size()) {
        break;
      }
    }
  }
  throw StructuredOutputError("Expected valid JSON output from the agent.", text);
}

std::string with_json_instruction(const std::string& prompt, const std::string& schema_json) {
  return prompt + "\n\nReturn only valid JSON.\nDo not wrap the response in Markdown.\n"
                  "Do not include commentary outside the JSON value.\nUse this JSON schema or example shape:\n" +
         schema_json;
}

std::string json_escape(std::string_view value) {
  std::ostringstream out;
  for (const unsigned char c : value) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      default:
        if (c < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(c) << std::dec;
        } else {
          out << static_cast<char>(c);
        }
        break;
    }
  }
  return out.str();
}

std::string format_slash_command(const std::string& command, const std::string& args) {
  auto normalized = command;
  normalized.erase(0, normalized.find_first_not_of(" \t\r\n"));
  const auto last = normalized.find_last_not_of(" \t\r\n");
  if (last != std::string::npos) normalized.erase(last + 1);
  if (normalized.empty() || normalized.front() != '/' || normalized.find_first_of(" \t\r\n") != std::string::npos) {
    throw SdkError("invalid slash command: " + command);
  }
  auto normalized_args = args;
  normalized_args.erase(0, normalized_args.find_first_not_of(" \t\r\n"));
  const auto args_last = normalized_args.find_last_not_of(" \t\r\n");
  if (args_last != std::string::npos) normalized_args.erase(args_last + 1);
  return normalized_args.empty() ? normalized : normalized + " " + normalized_args;
}

std::string json_get_string(const std::string& json, const std::string& key) {
  try {
    return json_string_member(parse_json_document(json), key);
  } catch (const SdkError&) {
    return {};
  }
}

std::string event_type_from_method(const std::string& method, const std::string& params_json) {
  const auto explicit_type = json_get_string(params_json, "type");
  if (!explicit_type.empty()) return explicit_type;
  if (method == "autohand.agentStart") return "agent_start";
  if (method == "autohand.agentEnd") return "agent_end";
  if (method == "autohand.turnStart") return "turn_start";
  if (method == "autohand.turnEnd") return "turn_end";
  if (method == "autohand.messageStart") return "message_start";
  if (method == "autohand.messageUpdate") return "message_update";
  if (method == "autohand.messageEnd") return "message_end";
  if (method == "autohand.toolStart") return "tool_start";
  if (method == "autohand.toolUpdate") return "tool_update";
  if (method == "autohand.toolEnd") return "tool_end";
  if (method == "autohand.permissionRequest") return "permission_request";
  if (method.rfind("autohand.autoresearch.", 0) == 0) return "autoresearch";
  if (method == "autohand.error") return "error";
  constexpr std::string_view prefix = "autohand.";
  if (method.rfind(prefix, 0) == 0) return method.substr(prefix.size());
  return method;
}

SdkEvent sdk_event_from_notification(const std::string& method, const std::string& params_json) {
  auto normalized_params = params_json;
  const auto phase = autoresearch_phase_for_method(method);
  if (!phase.empty() && json_get_string(params_json, "phase").empty()) {
    if (params_json == "{}" || params_json.empty()) {
      normalized_params = "{\"phase\":\"" + phase + "\"}";
    } else if (params_json.front() == '{') {
      normalized_params = "{\"phase\":\"" + phase + "\"," + params_json.substr(1);
    }
  }
  return SdkEvent{event_type_from_method(method, normalized_params), normalized_params};
}

}  // namespace autohand
