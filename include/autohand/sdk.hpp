#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace autohand {

// Performs idempotent eager initialization of the public SDK runtime.
void initialize();

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
  bool bare = false;
  std::optional<bool> idle_logout;
  std::optional<bool> context_compact;
  std::optional<bool> agents_md_enable;
  bool agents_md_create = false;
  std::optional<std::string> agents_md_path;
  bool agents_md_auto_update = false;
  bool persist_session = false;
  std::optional<std::string> session_id;
  bool resume = false;
  bool continue_session = false;
  std::optional<std::string> session_path;
  std::optional<int> auto_save_interval;
  std::optional<int> max_tokens;
  std::optional<double> compression_threshold;
  std::optional<double> summarization_threshold;
  std::optional<int> max_iterations;
  std::optional<int> max_runtime_minutes;
  std::optional<double> max_cost;
  std::optional<std::string> model;
  std::optional<double> temperature;
  std::optional<std::string> system_prompt;
  std::optional<std::string> append_system_prompt;
  std::optional<std::string> fork_session;
  std::optional<std::string> display_language;
  std::optional<std::string> system_prompt_file;
  std::optional<std::string> append_system_prompt_file;
  std::optional<std::string> mcp_config;
  std::optional<std::string> agents;
  std::optional<std::string> plugin_dir;
  std::optional<std::string> feature_settings_json;
  std::optional<std::string> yolo;
  std::optional<int> yolo_timeout_seconds;
  std::vector<std::string> additional_directories;
  std::vector<std::string> skills;
  std::vector<std::string> skill_sources;
  bool install_missing_skills = false;
  std::vector<std::string> extra_args;
  std::map<std::string, std::string> environment;
  std::optional<std::string> provider;
  std::optional<std::string> api_key;
  std::optional<std::string> base_url;
  std::optional<std::string> autohand_ai_plan;

  static Config from_environment();
  Config& with_cwd(std::string value);
  Config& with_cli_path(std::string value);
  Config& with_model(std::string value);
  Config& with_skill(std::string value);
  Config& with_instructions(std::string value);
  std::vector<std::string> cli_args() const;
};

struct GoalParams {
  std::optional<std::string> objective;
  std::optional<std::string> status;
  std::optional<long long> token_budget;
  std::optional<long long> time_budget_seconds;
  std::optional<long long> min_tokens_before_wrap_up;
  std::optional<long long> min_time_seconds_before_wrap_up;

  std::string to_json() const;
};

struct AutoresearchSubagentOptions {
  std::optional<bool> idea_generation;
  std::optional<bool> measurement_analysis;
  std::optional<bool> finalization;

  std::string to_json() const;
};

struct AutoresearchStartParams {
  std::string objective;
  std::optional<int> max_iterations;
  std::optional<long long> timeout_ms;
  std::optional<std::string> metric_name;
  std::optional<std::string> metric_unit;
  std::optional<std::string> direction;
  std::optional<std::string> measure_command;
  std::optional<std::string> measure_script;
  std::optional<std::string> checks_command;
  std::optional<std::string> checks_script;
  std::vector<std::string> files_in_scope;
  std::optional<AutoresearchSubagentOptions> subagents;
  std::optional<std::string> secondary_objectives_json;
  std::optional<std::string> constraints_json;
  std::optional<std::string> sampling_json;
  std::optional<std::string> retention_json;
  std::vector<std::string> environment_allowlist;

  std::string to_json() const;
};

struct PromptOptions {
  std::string context_json;
  std::vector<std::string> image_json;
  std::optional<std::string> thinking_level;
  std::map<std::string, std::string> extra_json;

  std::string to_json(std::string_view message) const;
};

struct CommunitySkill {
  std::string id;
  std::string name;
  std::string description;
  std::string category;
  std::vector<std::string> tags;
  std::optional<double> rating;
  std::optional<long long> download_count;
  std::optional<bool> is_featured;
  std::optional<bool> is_curated;
};

struct SkillCategory {
  std::string name;
  long long count = 0;
};

struct GetSkillsRegistryParams {
  std::optional<bool> force_refresh;
  std::string to_json() const;
};

struct GetSkillsRegistryResult {
  bool success = false;
  std::vector<CommunitySkill> skills;
  std::vector<SkillCategory> categories;
  std::optional<std::string> error;
};

enum class SkillInstallScope { User, Project };

struct InstallSkillParams {
  std::string skill_name;
  SkillInstallScope scope = SkillInstallScope::User;
  std::optional<bool> force;
  std::string to_json() const;
};

struct InstallSkillResult {
  bool success = false;
  std::optional<std::string> skill_name;
  std::optional<std::string> path;
  std::optional<std::string> error;
};

struct McpServerInfo {
  std::string name;
  std::string status;
  long long tool_count = 0;
};

struct McpListServersResult {
  std::vector<McpServerInfo> servers;
};

struct McpListToolsParams {
  std::optional<std::string> server_name;
  std::string to_json() const;
};

struct McpToolInfo {
  std::string name;
  std::string description;
  std::string server_name;
};

struct McpListToolsResult {
  std::vector<McpToolInfo> tools;
};

enum class McpTransport { Stdio, Sse, Http };

struct McpServerConfigInfo {
  std::string name;
  McpTransport transport = McpTransport::Stdio;
  std::optional<std::string> command;
  std::vector<std::string> args;
  std::optional<std::string> url;
  std::map<std::string, std::string> env;
  std::map<std::string, std::string> headers;
  std::optional<bool> auto_connect;
};

struct McpGetServerConfigsResult {
  std::vector<McpServerConfigInfo> configs;
};

struct SdkEvent {
  std::string type;
  std::string raw_json;

  std::string text_delta() const;
  std::string message_content() const;
  std::string tool_name() const;
  std::string request_id() const;
  std::string description() const;
  std::string autoresearch_phase() const;
  std::string autoresearch_operation() const;
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
  GetSkillsRegistryResult get_skills_registry(const GetSkillsRegistryParams& params = {});
  InstallSkillResult install_skill(const InstallSkillParams& params);
  McpListServersResult list_mcp_servers();
  McpListToolsResult list_mcp_tools(const McpListToolsParams& params = {});
  McpGetServerConfigsResult get_mcp_server_configs();
  std::string get_supported_commands();
  bool supports_command(const std::string& command);
  void stream_command(
      const std::string& command,
      const std::string& args,
      const std::function<void(const SdkEvent&)>& on_event,
      const PromptOptions& options = {});
  std::string apply_flag_settings(const std::string& settings_json);
  std::string get_goal();
  std::string create_goal(const GoalParams& params);
  std::string update_goal(const GoalParams& params);
  std::string clear_goal();
  std::string queue_goal(const GoalParams& params);
  std::string start_queued_goal();
  std::string list_goal_templates();
  std::string start_autoresearch(const AutoresearchStartParams& params);
  std::string get_autoresearch_status();
  std::string stop_autoresearch();
  std::string get_autoresearch_history();
  std::string replay_autoresearch(const std::string& attempt_id, const std::string& evaluator = "original");
  std::string rescore_autoresearch(const std::string& attempt_id);
  std::string rescore_all_autoresearch();
  std::string compare_autoresearch(const std::string& left_attempt_id, const std::string& right_attempt_id);
  std::string get_autoresearch_pareto();
  std::string pin_autoresearch(const std::string& attempt_id, bool pinned);
  std::string prune_autoresearch(bool dry_run = true, bool yes = false);
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
  std::exception_ptr stream_error_;
  RunResult result_;
};

class Agent {
 public:
  explicit Agent(Config config = {});

  Run send(std::string prompt, PromptOptions options = {});
  Run command(std::string command, std::string args = {}, PromptOptions options = {});
  Run deep_research(std::string topic, PromptOptions options = {});
  Run autoresearch(std::string objective, PromptOptions options = {});
  RunResult run(std::string prompt, PromptOptions options = {});
  std::string run_json(std::string prompt, std::string schema_json = "{}");
  void allow_permission(const std::string& request_id);
  void deny_permission(const std::string& request_id);
  void set_plan_mode(bool enabled);
  bool supports_command(const std::string& command);
  std::string get_goal();
  std::string create_goal(const GoalParams& params);
  std::string update_goal(const GoalParams& params);
  std::string clear_goal();
  std::string queue_goal(const GoalParams& params);
  std::string start_queued_goal();
  std::string list_goal_templates();
  GetSkillsRegistryResult get_skills_registry(const GetSkillsRegistryParams& params = {});
  InstallSkillResult install_skill(const InstallSkillParams& params);
  McpListServersResult list_mcp_servers();
  McpListToolsResult list_mcp_tools(const McpListToolsParams& params = {});
  McpGetServerConfigsResult get_mcp_server_configs();
  std::string start_autoresearch(const AutoresearchStartParams& params);
  std::string get_autoresearch_status();
  std::string stop_autoresearch();
  std::string get_autoresearch_history();
  std::string replay_autoresearch(const std::string& attempt_id, const std::string& evaluator = "original");
  std::string rescore_autoresearch(const std::string& attempt_id);
  std::string rescore_all_autoresearch();
  std::string compare_autoresearch(const std::string& left_attempt_id, const std::string& right_attempt_id);
  std::string get_autoresearch_pareto();
  std::string pin_autoresearch(const std::string& attempt_id, bool pinned);
  std::string prune_autoresearch(bool dry_run = true, bool yes = false);
  void close();

 private:
  AutohandSdk sdk_;
};

std::string parse_json_text(const std::string& text);
std::string with_json_instruction(const std::string& prompt, const std::string& schema_json = "{}");
std::string json_escape(std::string_view value);
std::string format_slash_command(const std::string& command, const std::string& args = {});
std::string json_get_string(const std::string& json, const std::string& key);
std::string event_type_from_method(const std::string& method, const std::string& params_json);
SdkEvent sdk_event_from_notification(const std::string& method, const std::string& params_json);

}  // namespace autohand
