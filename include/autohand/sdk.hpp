#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace autohand {

class SdkError : public std::runtime_error {
 public:
  explicit SdkError(const std::string& message) : std::runtime_error(message) {}
};

class RequestTimeoutError : public SdkError {
 public:
  explicit RequestTimeoutError(const std::string& method)
      : SdkError("request timed out: " + method) {}
};

class RpcError : public SdkError {
 public:
  RpcError(int code, const std::string& message, std::string data = {});

  int code;
  std::string rpc_message;
  std::string data;
};

class StructuredOutputError : public SdkError {
 public:
  StructuredOutputError(const std::string& message, std::string raw_response);

  std::string raw_response;
};

struct Config {
  std::string cwd = ".";
  std::string cli_path;
  bool debug = false;
  std::chrono::milliseconds timeout{300000};
  bool unrestricted = false;
  bool auto_mode = false;
  bool auto_skill = false;
  bool auto_commit = false;
  std::optional<bool> context_compact;
  std::optional<int> max_iterations;
  std::optional<int> max_runtime_minutes;
  std::optional<double> max_cost;
  std::optional<std::string> model;
  std::optional<double> temperature;
  std::optional<std::string> system_prompt;
  std::optional<std::string> append_system_prompt;
  std::optional<std::string> yolo;
  std::optional<int> yolo_timeout_seconds;
  std::vector<std::string> additional_directories;
  std::vector<std::string> skills;
  std::vector<std::string> extra_args;
  std::map<std::string, std::string> environment;

  static Config from_environment();
  Config& with_cwd(std::string value);
  Config& with_cli_path(std::string value);
  Config& with_model(std::string value);
  Config& with_skill(std::string value);
  Config& with_instructions(std::string value);
  std::vector<std::string> cli_args() const;
};

struct PromptOptions {
  std::string context_json;
  std::vector<std::string> image_json;
  std::optional<std::string> thinking_level;
  std::map<std::string, std::string> extra_json;

  std::string to_json(std::string_view message) const;
};

struct SdkEvent {
  std::string type;
  std::string raw_json;

  std::string text_delta() const;
  std::string message_content() const;
  std::string tool_name() const;
  std::string request_id() const;
  std::string description() const;
};

struct RunResult {
  std::string id;
  std::string status;
  std::string text;
  std::vector<SdkEvent> events;
};

class AutohandSdk {
 public:
  explicit AutohandSdk(Config config = {});
  ~AutohandSdk();

  AutohandSdk(const AutohandSdk&) = delete;
  AutohandSdk& operator=(const AutohandSdk&) = delete;
  AutohandSdk(AutohandSdk&&) noexcept;
  AutohandSdk& operator=(AutohandSdk&&) noexcept;

  void start();
  void stop();
  bool is_started() const;

  std::string request(const std::string& method, const std::string& params_json = "{}");
  std::string prompt(const std::string& message, const PromptOptions& options = {});
  void stream_prompt(
      const std::string& message,
      const std::function<void(const SdkEvent&)>& on_event,
      const PromptOptions& options = {});
  std::string interrupt();
  std::string set_plan_mode(bool enabled);
  std::string set_permission_mode(const std::string& mode);
  std::string set_model(const std::string& model);
  std::string get_state();
  std::string get_messages();
  std::string permission_response(const std::string& request_id, const std::string& decision);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class Run {
 public:
  Run(AutohandSdk& sdk, std::string prompt, PromptOptions options = {});

  const std::string& id() const;
  void stream(const std::function<void(const SdkEvent&)>& on_event);
  RunResult wait();
  std::string json_text();
  void abort();

 private:
  AutohandSdk* sdk_;
  std::string id_;
  std::string prompt_;
  PromptOptions options_;
  bool streamed_ = false;
  RunResult result_;
};

class Agent {
 public:
  explicit Agent(Config config = {});

  Run send(std::string prompt, PromptOptions options = {});
  RunResult run(std::string prompt, PromptOptions options = {});
  std::string run_json(std::string prompt, std::string schema_json = "{}");
  void allow_permission(const std::string& request_id);
  void deny_permission(const std::string& request_id);
  void set_plan_mode(bool enabled);
  void close();

 private:
  AutohandSdk sdk_;
};

std::string parse_json_text(const std::string& text);
std::string with_json_instruction(const std::string& prompt, const std::string& schema_json = "{}");
std::string json_escape(std::string_view value);
std::string json_get_string(const std::string& json, const std::string& key);
std::string event_type_from_method(const std::string& method, const std::string& params_json);

}  // namespace autohand

