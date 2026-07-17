#include <autohand/sdk.hpp>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <future>
#include <iostream>
#include <mutex>
#include <regex>
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

std::string make_prompt_json(const std::string& message) {
  return "{\"message\":\"" + json_escape(message) + "\"}";
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

std::string extract_raw_property(const std::string& json, const std::string& key) {
  const std::string marker = "\"" + key + "\":";
  const auto start = json.find(marker);
  if (start == std::string::npos) {
    return {};
  }
  auto value_start = start + marker.size();
  while (value_start < json.size() && std::isspace(static_cast<unsigned char>(json[value_start]))) {
    ++value_start;
  }
  if (value_start >= json.size()) {
    return {};
  }
  if (json[value_start] == '{' || json[value_start] == '[') {
    const char opener = json[value_start];
    const char closer = opener == '{' ? '}' : ']';
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (auto i = value_start; i < json.size(); ++i) {
      const char c = json[i];
      if (in_string) {
        if (escaped) {
          escaped = false;
        } else if (c == '\\') {
          escaped = true;
        } else if (c == '"') {
          in_string = false;
        }
        continue;
      }
      if (c == '"') {
        in_string = true;
        continue;
      }
      if (c == opener) {
        ++depth;
      } else if (c == closer) {
        --depth;
        if (depth == 0) {
          return json.substr(value_start, i - value_start + 1);
        }
      }
    }
  }
  auto end = json.find_first_of(",}\n", value_start);
  return json.substr(value_start, end == std::string::npos ? std::string::npos : end - value_start);
}

long extract_id(const std::string& json) {
  static const std::regex id_regex("\"id\"\\s*:\\s*\"?([0-9]+)\"?");
  std::smatch match;
  if (std::regex_search(json, match, id_regex)) {
    return std::stol(match[1].str());
  }
  return 0;
}

int extract_error_code(const std::string& error_json) {
  static const std::regex code_regex("\"code\"\\s*:\\s*(-?[0-9]+)");
  std::smatch match;
  if (std::regex_search(error_json, match, code_regex)) {
    return std::stoi(match[1].str());
  }
  return 0;
}

std::vector<std::string> split_exec_args(const std::string& executable, const std::vector<std::string>& args) {
  std::vector<std::string> all;
  all.push_back(executable);
  all.insert(all.end(), args.begin(), args.end());
  return all;
}

}  // namespace

RpcError::RpcError(int code_value, const std::string& message, std::string data_value)
    : SdkError("RPC error " + std::to_string(code_value) + ": " + message),
      code(code_value),
      rpc_message(message),
      data(std::move(data_value)) {}

StructuredOutputError::StructuredOutputError(const std::string& message, std::string raw)
    : SdkError(message), raw_response(std::move(raw)) {}

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

    int stdin_pipe[2];
    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
      throw SdkError(std::string("pipe failed: ") + std::strerror(errno));
    }

    const auto executable = config_.cli_path.empty() ? std::string("autohand") : config_.cli_path;
    const auto args = split_exec_args(executable, config_.cli_args());

    pid_ = fork();
    if (pid_ < 0) {
      throw SdkError(std::string("fork failed: ") + std::strerror(errno));
    }

    if (pid_ == 0) {
      dup2(stdin_pipe[0], STDIN_FILENO);
      dup2(stdout_pipe[1], STDOUT_FILENO);
      dup2(stderr_pipe[1], STDERR_FILENO);
      close(stdin_pipe[0]);
      close(stdin_pipe[1]);
      close(stdout_pipe[0]);
      close(stdout_pipe[1]);
      close(stderr_pipe[0]);
      close(stderr_pipe[1]);
      if (!config_.cwd.empty()) {
        chdir(config_.cwd.c_str());
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
      _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    stdin_fd_ = stdin_pipe[1];
    stdout_fd_ = stdout_pipe[0];
    stderr_fd_ = stderr_pipe[0];
    started_ = true;
    stdout_thread_ = std::thread([this] { read_stdout(); });
    stderr_thread_ = std::thread([this] { read_stderr(); });
    if (config_.feature_settings_json) {
      (void)request("autohand.applyFlagSettings", "{\"settings\":" + *config_.feature_settings_json + "}");
    }
  }

  void stop() {
    if (!started_) return;
    started_ = false;
    if (stdin_fd_ >= 0) {
      close(stdin_fd_);
      stdin_fd_ = -1;
    }
    if (pid_ > 0) {
      kill(pid_, SIGTERM);
      int status = 0;
      waitpid(pid_, &status, 0);
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
    {
      std::lock_guard lock(write_mutex_);
      if (write(stdin_fd_, line.data(), line.size()) < 0) {
        throw SdkError(std::string("write failed: ") + std::strerror(errno));
      }
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

 private:
  void read_stdout() {
    FILE* file = fdopen(stdout_fd_, "r");
    if (!file) return;
    char* line = nullptr;
    size_t size = 0;
    while (started_ && getline(&line, &size, file) != -1) {
      std::string text(line);
      if (!text.empty() && text.back() == '\n') text.pop_back();
      handle_line(text);
    }
    free(line);
    fclose(file);
  }

  void read_stderr() {
    FILE* file = fdopen(stderr_fd_, "r");
    if (!file) return;
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

  void handle_line(const std::string& line) {
    const auto id = extract_id(line);
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
        const auto error_json = extract_raw_property(line, "error");
        if (!error_json.empty()) {
          promise->set_exception(std::make_exception_ptr(RpcError(
              extract_error_code(error_json), json_get_string(error_json, "message"), error_json)));
        } else {
          promise->set_value(extract_raw_property(line, "result"));
        }
      }
      return;
    }

    const auto method = json_get_string(line, "method");
    if (method.empty()) return;
    const auto params = extract_raw_property(line, "params");
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

Run::Run(AutohandSdk& sdk, std::string prompt, PromptOptions options)
    : sdk_(&sdk), id_(now_id()), prompt_(std::move(prompt)), options_(std::move(options)) {}

const std::string& Run::id() const { return id_; }

void Run::stream(const std::function<void(const SdkEvent&)>& on_event) {
  result_.id = id_;
  result_.status = "completed";
  sdk_->stream_prompt(prompt_, [&](const SdkEvent& event) {
    result_.events.push_back(event);
    if (event.type == "message_update") result_.text += event.text_delta();
    if (event.type == "message_end") {
      const auto content = event.message_content();
      if (!content.empty()) result_.text = content;
    }
    on_event(event);
  }, options_);
  streamed_ = true;
}

RunResult Run::wait() {
  if (!streamed_) {
    stream([](const SdkEvent&) {});
  }
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
  if (trimmed.front() == '{' || trimmed.front() == '[') return trimmed;
  const auto fence = trimmed.find("```");
  if (fence != std::string::npos) {
    auto start = trimmed.find('\n', fence);
    auto end = trimmed.find("```", start == std::string::npos ? fence + 3 : start + 1);
    if (start != std::string::npos && end != std::string::npos) {
      auto candidate = trimmed.substr(start + 1, end - start - 1);
      candidate.erase(0, candidate.find_first_not_of(" \t\r\n"));
      candidate.erase(candidate.find_last_not_of(" \t\r\n") + 1);
      if (!candidate.empty() && (candidate.front() == '{' || candidate.front() == '[')) return candidate;
    }
  }
  const auto object = trimmed.find('{');
  const auto array = trimmed.find('[');
  auto start = std::min(object == std::string::npos ? trimmed.size() : object,
                        array == std::string::npos ? trimmed.size() : array);
  if (start < trimmed.size()) {
    const char opener = trimmed[start];
    const char closer = opener == '{' ? '}' : ']';
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (auto i = start; i < trimmed.size(); ++i) {
      const char c = trimmed[i];
      if (in_string) {
        if (escaped) escaped = false;
        else if (c == '\\') escaped = true;
        else if (c == '"') in_string = false;
        continue;
      }
      if (c == '"') {
        in_string = true;
      } else if (c == opener) {
        ++depth;
      } else if (c == closer && --depth == 0) {
        return trimmed.substr(start, i - start + 1);
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
  for (const char c : value) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default: out << c; break;
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
  const std::regex re("\"" + key + "\"\\s*:\\s*\"((?:\\\\.|[^\"])*)\"");
  std::smatch match;
  if (std::regex_search(json, match, re)) {
    auto value = match[1].str();
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
      if (value[i] == '\\' && i + 1 < value.size()) {
        const char n = value[++i];
        if (n == 'n') out.push_back('\n');
        else if (n == 'r') out.push_back('\r');
        else if (n == 't') out.push_back('\t');
        else out.push_back(n);
      } else {
        out.push_back(value[i]);
      }
    }
    return out;
  }
  return {};
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
