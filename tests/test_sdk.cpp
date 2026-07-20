#include <autohand/sdk.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

std::filesystem::path make_fake_cli() {
  auto dir = std::filesystem::temp_directory_path() /
             ("autohand-cpp-sdk-test-" + std::to_string(::getpid()));
  std::filesystem::create_directories(dir);
  auto cli = dir / "fake-autohand";
  std::ofstream out(cli);
  out << R"FAKE(#!/bin/sh
while IFS= read -r line; do
  id=$(printf '%s\n' "$line" | sed -n 's/.*"id":\([0-9][0-9]*\).*/\1/p')
  method=$(printf '%s\n' "$line" | sed -n 's/.*"method":"\([^"]*\)".*/\1/p')
  case "$line" in
    *autohand.prompt*)
      prompt_id="$id"
      printf '%s\n' '{"jsonrpc":"2.0","method":"autohand.permissionRequest","params":{"type":"permission_request","requestId":"perm-1","tool":"bash","description":"list files"}}'
      IFS= read -r permission_line || exit 1
      permission_id=$(printf '%s\n' "$permission_line" | sed -n 's/.*"id":\([0-9][0-9]*\).*/\1/p')
      printf '{"jsonrpc":"2.0","id":%s,"result":{"ok":true}}\n' "$permission_id"
      printf '%s\n' '{"jsonrpc":"2.0","method":"autohand.messageUpdate","params":{"type":"message_update","delta":"hello"}}'
      printf '%s\n' '{"jsonrpc":"2.0","method":"autohand.messageEnd","params":{"type":"message_end","content":"hello"}}'
      printf '{"jsonrpc":"2.0","id":%s,"result":{"ok":true}}\n' "$prompt_id"
      ;;
    *autohand.getSupportedCommands*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"commands":["deep-research","/autoresearch"]}}\n' "$id"
      ;;
    *autohand.env*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"plan":"%s","apiKey":"%s","baseUrl":"%s"}}\n' "$id" "$AUTOHAND_AI_PLAN" "$AUTOHAND_AI_API_KEY" "$AUTOHAND_AI_BASE_URL"
      ;;
    *autohand.reset*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"sessionId":"reset-session"}}\n' "$id"
      ;;
    *autohand.browserHandoff.create*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"token":"handoff-token","sessionId":"browser-session","workspaceRoot":"/workspace","createdAt":"2026-07-20T00:00:00.000Z","expiresAt":"2026-07-20T00:10:00.000Z","url":"https://example.test/handoff"}}\n' "$id"
      ;;
    *autohand.browserHandoff.attachLatest*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"success":true,"sessionId":"latest-session","workspaceRoot":"/workspace","messageCount":5}}\n' "$id"
      ;;
    *autohand.browserHandoff.attach*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"success":true,"sessionId":"browser-session","workspaceRoot":"/workspace","messageCount":3}}\n' "$id"
      ;;
    *autohand.automode.start*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"success":true,"sessionId":"automode-session"}}\n' "$id"
      ;;
    *autohand.automode.status*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"active":true,"paused":false,"state":{"sessionId":"automode-session","status":"running","currentIteration":4,"maxIterations":8,"filesCreated":2,"filesModified":7,"branch":"automode/session","lastCheckpoint":{"commit":"checkpoint-1","message":"iteration 3","timestamp":"2026-07-20T00:03:00.000Z"}}}}\n' "$id"
      ;;
    *autohand.automode.pause*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"success":true}}\n' "$id"
      ;;
    *autohand.automode.resume*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"success":true}}\n' "$id"
      ;;
    *autohand.automode.cancel*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"success":true}}\n' "$id"
      ;;
    *autohand.getSkillsRegistry*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"success":true,"skills":[{"id":"skill-1","name":"review","description":"Review code","category":"quality","tags":["cpp"],"rating":4.5,"downloadCount":8,"isFeatured":true}],"categories":[{"name":"quality","count":1}]}}\n' "$id"
      ;;
    *autohand.installSkill*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"success":true,"skillName":"review","path":"/skills/review"}}\n' "$id"
      ;;
    *autohand.mcp.listServers*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"servers":[{"name":"github","status":"connected","toolCount":2}]}}\n' "$id"
      ;;
    *autohand.mcp.listTools*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"tools":[{"name":"search","description":"Search issues","serverName":"github"}]}}\n' "$id"
      ;;
    *autohand.mcp.getServerConfigs*)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"configs":[{"name":"github","transport":"stdio","command":"mcp-github","args":["--stdio"],"env":{"TOKEN":"test"},"headers":{},"autoConnect":true}]}}\n' "$id"
      ;;
    *)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"ok":true,"method":"%s"}}\n' "$id" "$method"
      ;;
  esac
done
)FAKE";
  out.close();
  chmod(cli.c_str(), 0755);
  return cli;
}

long request_id(const std::string& line) {
  const auto marker = line.find("\"id\"");
  if (marker == std::string::npos) return 0;
  auto position = line.find(':', marker);
  if (position == std::string::npos) return 0;
  ++position;
  while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position]))) {
    ++position;
  }
  return std::stol(line.substr(position));
}

int run_fixture(std::string_view mode) {
  bool exit_after_readiness = false;
  if (mode == "eof-once") {
    const auto* marker = std::getenv("AUTOHAND_CPP_TEST_MARKER");
    if (marker && !std::filesystem::exists(marker)) {
      std::ofstream(marker).close();
      exit_after_readiness = true;
    }
  }
  int stream_count = 0;
  std::string line;
  while (std::getline(std::cin, line)) {
    const auto id = request_id(line);
    const auto method = autohand::json_get_string(line, "method");
    if (method == "autohand.prompt") {
      if (const auto* prompt_log = std::getenv("AUTOHAND_CPP_PROMPT_LOG")) {
        std::ofstream(prompt_log, std::ios::app) << id << '\n';
      }
    }
    if (mode == "readiness-error") {
      std::cout << "{\"jsonrpc\":\"2.0\",\"id\":" << id
                << ",\"error\":{\"code\":-32000,\"message\":\"not ready\"}}\n"
                << std::flush;
      continue;
    }
    if (mode == "stream" && method == "autohand.prompt") {
      ++stream_count;
      std::cout << "{\"jsonrpc\":\"2.0\",\"method\":\"autohand.messageUpdate\","
                   "\"params\":{\"type\":\"message_update\",\"delta\":\"stream-"
                << stream_count << "\"}}\n"
                << "{\"jsonrpc\":\"2.0\",\"id\":" << id
                << ",\"result\":{\"ok\":true}}\n"
                << std::flush;
      continue;
    }
    if (mode == "eof" && method != "autohand.getState") return 0;
    std::cout << " { \"jsonrpc\" : \"2.0\", \"id\" : " << id
              << ", \"result\" : { \"ready\" : true, \"method\" : \""
              << autohand::json_escape(method) << "\", \"unicode\" : \"\\uD83D\\uDE80\" } } \n"
              << std::flush;
    if (exit_after_readiness && method == "autohand.getState") return 0;
    if (mode == "hang" && method == "autohand.getState") {
      signal(SIGTERM, SIG_IGN);
      while (true) pause();
    }
  }
  return 0;
}

std::chrono::nanoseconds run_public_import_probe(const std::string& executable) {
  int output_pipe[2];
  assert(pipe(output_pipe) == 0);
  const auto pid = fork();
  assert(pid >= 0);
  if (pid == 0) {
    close(output_pipe[0]);
    dup2(output_pipe[1], STDOUT_FILENO);
    close(output_pipe[1]);
    setenv("AUTOHAND_CPP_PUBLIC_IMPORT_PROBE", "1", 1);
    unsetenv("AUTOHAND_CPP_TEST_FIXTURE");
    execl(executable.c_str(), executable.c_str(), nullptr);
    _exit(127);
  }
  close(output_pipe[1]);
  std::string output;
  char buffer[256];
  ssize_t count = 0;
  while ((count = read(output_pipe[0], buffer, sizeof(buffer))) > 0) {
    output.append(buffer, static_cast<std::size_t>(count));
  }
  close(output_pipe[0]);
  int status = 0;
  assert(waitpid(pid, &status, 0) == pid);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  const auto marker = output.find("PUBLIC_IMPORT_NS=");
  assert(marker != std::string::npos);
  return std::chrono::nanoseconds(std::stoll(output.substr(marker + 17)));
}

autohand::Config fixture_config(const std::string& executable, const std::string& mode = "normal") {
  autohand::Config config;
  config.cli_path = executable;
  config.environment["AUTOHAND_CPP_TEST_FIXTURE"] = mode;
  config.timeout = std::chrono::seconds(2);
  return config;
}

std::chrono::nanoseconds measure_sdk_start(const std::string& executable) {
  autohand::AutohandSdk sdk(fixture_config(executable));
  const auto started = std::chrono::steady_clock::now();
  sdk.start();
  const auto elapsed = std::chrono::steady_clock::now() - started;
  sdk.stop();
  return elapsed;
}

std::chrono::nanoseconds measure_fixture_first_rpc(const std::string& executable) {
  autohand::AutohandSdk sdk(fixture_config(executable));
  const auto started = std::chrono::steady_clock::now();
  sdk.start();
  const auto elapsed = std::chrono::steady_clock::now() - started;
  sdk.stop();
  return elapsed;
}

std::chrono::nanoseconds percentile(std::vector<std::chrono::nanoseconds> samples, std::size_t percent) {
  std::sort(samples.begin(), samples.end());
  const auto index = ((samples.size() * percent + 99) / 100) - 1;
  return samples[index];
}

std::chrono::nanoseconds median(std::vector<std::chrono::nanoseconds> samples) {
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}

double milliseconds(std::chrono::nanoseconds value) {
  return static_cast<double>(value.count()) / 1'000'000.0;
}

void test_startup_budgets(const std::string& executable) {
  for (int i = 0; i < 5; ++i) {
    (void)run_public_import_probe(executable);
    (void)measure_sdk_start(executable);
    (void)measure_fixture_first_rpc(executable);
  }
  std::vector<std::chrono::nanoseconds> public_import;
  std::vector<std::chrono::nanoseconds> sdk_start;
  std::vector<std::chrono::nanoseconds> fixture_first_rpc;
  for (int i = 0; i < 50; ++i) {
    public_import.push_back(run_public_import_probe(executable));
    sdk_start.push_back(measure_sdk_start(executable));
    fixture_first_rpc.push_back(measure_fixture_first_rpc(executable));
  }
  const auto public_p95 = percentile(public_import, 95);
  const auto sdk_p95 = percentile(sdk_start, 95);
  const auto fixture_p95 = percentile(fixture_first_rpc, 95);
  const auto public_max = *std::max_element(public_import.begin(), public_import.end());
  const auto sdk_max = *std::max_element(sdk_start.begin(), sdk_start.end());
  const auto fixture_max = *std::max_element(fixture_first_rpc.begin(), fixture_first_rpc.end());
  const auto budget = std::chrono::milliseconds(50);
  const auto public_passed = public_p95 < budget;
  const auto sdk_passed = sdk_p95 < budget;
  const auto fixture_passed = fixture_p95 < budget;
  const auto passed = public_passed && sdk_passed && fixture_passed;
  std::cout << std::fixed << std::setprecision(6)
            << "{\"language\":\"cpp\",\"budgetMs\":50,\"metrics\":{"
            << "\"publicImportMs\":{\"samples\":50,\"medianMs\":"
            << milliseconds(median(public_import)) << ",\"p95Ms\":" << milliseconds(public_p95)
            << ",\"maxMs\":" << milliseconds(public_max) << ",\"passed\":"
            << (public_passed ? "true" : "false") << "},"
            << "\"sdkStartReturnMs\":{\"samples\":50,\"medianMs\":"
            << milliseconds(median(sdk_start)) << ",\"p95Ms\":" << milliseconds(sdk_p95)
            << ",\"maxMs\":" << milliseconds(sdk_max) << ",\"passed\":"
            << (sdk_passed ? "true" : "false") << "},"
            << "\"fixtureSpawnToFirstRpcMs\":{\"samples\":50,\"medianMs\":"
            << milliseconds(median(fixture_first_rpc)) << ",\"p95Ms\":" << milliseconds(fixture_p95)
            << ",\"maxMs\":" << milliseconds(fixture_max) << ",\"passed\":"
            << (fixture_passed ? "true" : "false") << "}},\"passed\":"
            << (passed ? "true" : "false") << "}\n";
  assert(passed);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  if (const auto* mode = std::getenv("AUTOHAND_CPP_TEST_FIXTURE")) return run_fixture(mode);
  if (std::getenv("AUTOHAND_CPP_PUBLIC_IMPORT_PROBE")) {
    const auto started = std::chrono::steady_clock::now();
    autohand::initialize();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    std::cout << "PUBLIC_IMPORT_NS="
              << std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() << '\n';
    return 0;
  }
  const auto executable = std::filesystem::absolute(argv[0]).string();
  assert(autohand::parse_json_text(R"({"ok":true})") == R"({"ok":true})");
  assert(autohand::parse_json_text("```json\n{\"ok\":true}\n```") == R"({"ok":true})");
  assert(autohand::parse_json_text("Result: {\"ok\":true} done.") == R"({"ok":true})");
  assert(autohand::json_get_string(R"( { "value" : "\uD83D\uDE80" } )", "value") == "🚀");
  bool invalid_json_rejected = false;
  try {
    (void)autohand::parse_json_text(R"({"unterminated":true)");
  } catch (const autohand::StructuredOutputError&) {
    invalid_json_rejected = true;
  }
  assert(invalid_json_rejected);

  auto config = autohand::Config::from_environment()
                    .with_cli_path(make_fake_cli().string())
                    .with_skill("cpp")
                    .with_model("moa");
  auto args = config.cli_args();
  assert(!args.empty());

  autohand::Config current_config;
  current_config.bare = true;
  current_config.idle_logout = false;
  current_config.fork_session = "session-1";
  current_config.display_language = "en-NZ";
  current_config.system_prompt_file = "SYSTEM.md";
  current_config.append_system_prompt_file = "EXTRA.md";
  current_config.mcp_config = "mcp.json";
  current_config.agents = "agents.json";
  current_config.plugin_dir = ".autohand/plugins";
  current_config.feature_settings_json = R"({"features":{"slashGoal":true}})";
  current_config.persist_session = true;
  current_config.session_id = "session-2";
  current_config.resume = true;
  current_config.agents_md_enable = true;
  current_config.agents_md_create = true;
  current_config.agents_md_path = "AGENTS.md";
  current_config.agents_md_auto_update = true;
  current_config.max_tokens = 40000;
  current_config.skill_sources = {"team", "local"};
  current_config.install_missing_skills = true;
  const auto current_args = current_config.cli_args();
  const auto has_arg = [&](const std::string& value) {
    return std::find(current_args.begin(), current_args.end(), value) != current_args.end();
  };
  assert(has_arg("--bare"));
  assert(has_arg("--no-idle-logout"));
  assert(has_arg("--fork"));
  assert(has_arg("--display-language"));
  assert(has_arg("--mcp-config"));
  assert(has_arg("--plugin-dir"));
  assert(has_arg("--persist-session"));
  assert(has_arg("--resume"));
  assert(has_arg("--agents-md"));
  assert(has_arg("--agents-md-create"));
  assert(has_arg("--agents-md-auto-update"));
  assert(has_arg("--agents-md-path"));
  assert(has_arg("--max-tokens"));
  assert(has_arg("--skill-sources"));
  assert(has_arg("--install-missing-skills"));

  autohand::GoalParams goal;
  goal.objective = "Finish parity";
  goal.token_budget = 20000;
  assert(goal.to_json() == R"({"objective":"Finish parity","token_budget":20000})");

  autohand::AutoresearchStartParams autoresearch;
  autoresearch.objective = "Reduce test runtime";
  autoresearch.metric_name = "total_ms";
  autoresearch.max_iterations = 12;
  autoresearch.subagents = autohand::AutoresearchSubagentOptions{true, std::nullopt, std::nullopt};
  autoresearch.secondary_objectives_json = R"([{"name":"memory","unit":"mb","direction":"lower"}])";
  const auto autoresearch_json = autoresearch.to_json();
  assert(autoresearch_json.find(R"("metricName":"total_ms")") != std::string::npos);
  assert(autoresearch_json.find(R"("ideaGeneration":true)") != std::string::npos);
  assert(autoresearch_json.find(R"("secondaryObjectives":[)") != std::string::npos);
  assert(autohand::format_slash_command(" /deep-research ", " C++ RPC reliability ") ==
         "/deep-research C++ RPC reliability");

  config.provider = "autohandai";
  config.api_key = "test-key";
  config.base_url = "https://example.test";
  config.autohand_ai_plan = "cloud";
  autohand::AutohandSdk sdk(config);
  sdk.start();
  assert(sdk.supports_command("/autoresearch"));
  const auto environment = sdk.request("autohand.env");
  assert(autohand::json_get_string(environment, "plan") == "cloud");
  assert(autohand::json_get_string(environment, "apiKey") == "test-key");
  assert(autohand::json_get_string(environment, "baseUrl") == "https://example.test");
  assert(sdk.reset().session_id == "reset-session");
  autohand::BrowserHandoffCreateParams handoff_create;
  handoff_create.extension_id = "extension-1";
  handoff_create.install_url = "https://example.test/install";
  assert(handoff_create.to_json() ==
         R"({"extensionId":"extension-1","installUrl":"https://example.test/install"})");
  const auto handoff = sdk.create_browser_handoff(handoff_create);
  assert(handoff.token == "handoff-token");
  assert(handoff.session_id == "browser-session");
  assert(handoff.url == "https://example.test/handoff");
  autohand::BrowserHandoffAttachParams handoff_attach{"handoff-token"};
  assert(handoff_attach.to_json() == R"({"token":"handoff-token"})");
  const auto attached = sdk.attach_browser_handoff(handoff_attach);
  assert(attached.success);
  assert(attached.session_id == "browser-session");
  assert(attached.message_count == 3);
  const auto latest = sdk.attach_latest_browser_handoff();
  assert(latest.success);
  assert(latest.session_id == "latest-session");
  assert(latest.message_count == 5);
  autohand::AutomodeStartParams automode_start;
  automode_start.prompt = "Ship the SDK";
  automode_start.max_iterations = 8;
  automode_start.completion_promise = "SHIPPED";
  automode_start.use_worktree = false;
  automode_start.checkpoint_interval = 2;
  automode_start.max_runtime = 45;
  automode_start.max_cost = 4.5;
  assert(automode_start.to_json() ==
         R"({"prompt":"Ship the SDK","maxIterations":8,"completionPromise":"SHIPPED","useWorktree":false,"checkpointInterval":2,"maxRuntime":45,"maxCost":4.5})");
  const auto automode_started = sdk.start_automode(automode_start);
  assert(automode_started.success);
  assert(automode_started.session_id == "automode-session");
  const auto automode_status = sdk.get_automode_status();
  assert(automode_status.active && !automode_status.paused);
  assert(automode_status.state);
  assert(automode_status.state->status == autohand::AutomodeSessionStatus::Running);
  assert(automode_status.state->current_iteration == 4);
  assert(automode_status.state->last_checkpoint);
  assert(automode_status.state->last_checkpoint->commit == "checkpoint-1");
  const auto paused = sdk.pause_automode();
  assert(paused.success && !paused.error);
  const auto resumed = sdk.resume_automode();
  assert(resumed.success && !resumed.error);
  autohand::AutomodeCancelParams automode_cancel{std::string("release window closed")};
  assert(automode_cancel.to_json() == R"({"reason":"release window closed"})");
  const auto cancelled = sdk.cancel_automode(automode_cancel);
  assert(cancelled.success && !cancelled.error);
  assert(autohand::json_get_string(sdk.create_goal(goal), "method") == "autohand.goal.create");
  assert(autohand::json_get_string(sdk.get_goal(), "method") == "autohand.goal.get");
  autohand::GoalParams update;
  update.status = "paused";
  assert(autohand::json_get_string(sdk.update_goal(update), "method") == "autohand.goal.update");
  assert(autohand::json_get_string(sdk.clear_goal(), "method") == "autohand.goal.clear");
  assert(autohand::json_get_string(sdk.queue_goal(goal), "method") == "autohand.goal.queue");
  assert(autohand::json_get_string(sdk.start_queued_goal(), "method") == "autohand.goal.startQueued");
  assert(autohand::json_get_string(sdk.list_goal_templates(), "method") ==
         "autohand.goal.listTemplates");
  autohand::GetSkillsRegistryParams registry_params{true};
  assert(registry_params.to_json() == R"({"forceRefresh":true})");
  const auto registry = sdk.get_skills_registry(registry_params);
  assert(registry.success && registry.skills.size() == 1);
  assert(registry.skills[0].id == "skill-1");
  assert(registry.skills[0].rating == 4.5);
  autohand::InstallSkillParams install_params;
  install_params.skill_name = "review";
  install_params.scope = autohand::SkillInstallScope::Project;
  assert(install_params.to_json() == R"({"skillName":"review","scope":"project"})");
  const auto installed = sdk.install_skill(install_params);
  assert(installed.success && installed.path == "/skills/review");
  assert(sdk.list_mcp_servers().servers[0].tool_count == 2);
  autohand::McpListToolsParams tools_params{std::string("github")};
  assert(tools_params.to_json() == R"({"serverName":"github"})");
  assert(sdk.list_mcp_tools(tools_params).tools[0].server_name == "github");
  const auto server_configs = sdk.get_mcp_server_configs();
  assert(server_configs.configs[0].transport == autohand::McpTransport::Stdio);
  assert(server_configs.configs[0].env.at("TOKEN") == "test");
  assert(autohand::json_get_string(sdk.start_autoresearch(autoresearch), "method") ==
         "autohand.autoresearch.start");
  assert(autohand::json_get_string(sdk.get_autoresearch_status(), "method") ==
         "autohand.autoresearch.status");
  assert(autohand::json_get_string(sdk.get_autoresearch_history(), "method") ==
         "autohand.autoresearch.history");
  assert(autohand::json_get_string(sdk.replay_autoresearch("attempt-1", "current"), "method") ==
         "autohand.autoresearch.replay");
  assert(autohand::json_get_string(sdk.rescore_all_autoresearch(), "method") ==
         "autohand.autoresearch.rescore");
  assert(autohand::json_get_string(sdk.compare_autoresearch("attempt-1", "attempt-2"), "method") ==
         "autohand.autoresearch.compare");
  assert(autohand::json_get_string(sdk.get_autoresearch_pareto(), "method") ==
         "autohand.autoresearch.pareto");
  assert(autohand::json_get_string(sdk.pin_autoresearch("attempt-1", true), "method") ==
         "autohand.autoresearch.pin");
  assert(autohand::json_get_string(sdk.prune_autoresearch(true), "method") ==
         "autohand.autoresearch.prune");
  assert(autohand::json_get_string(sdk.stop_autoresearch(), "method") ==
         "autohand.autoresearch.stop");
  assert(autohand::event_type_from_method("autohand.autoresearch.event", R"({"operation":"replay"})") ==
         "autoresearch");
  const auto lifecycle_event = autohand::sdk_event_from_notification(
      "autohand.autoresearch.status", R"({"active":true,"runsLogged":3})");
  assert(lifecycle_event.type == "autoresearch");
  assert(lifecycle_event.autoresearch_phase() == "status");
  const auto operation_event = autohand::sdk_event_from_notification(
      "autohand.autoresearch.event", R"({"operation":"replay","phase":"complete"})");
  assert(operation_event.autoresearch_operation() == "replay");
  assert(operation_event.autoresearch_phase() == "complete");
  std::string text;
  bool answered_permission = false;
  sdk.stream_prompt("hello", [&](const autohand::SdkEvent& event) {
    if (event.type == "permission_request") {
      (void)sdk.permission_response(event.request_id(), "allow_once");
      answered_permission = true;
    }
    if (event.type == "message_update") {
      text += event.text_delta();
    }
  });
  sdk.stop();
  assert(text == "hello");
  assert(answered_permission);

  {
    autohand::Config missing;
    missing.cli_path = "/definitely/missing/autohand";
    autohand::AutohandSdk missing_sdk(missing);
    bool rejected = false;
    try {
      missing_sdk.start();
    } catch (const autohand::SdkError&) {
      rejected = true;
    }
    assert(rejected && !missing_sdk.is_started());
  }
  {
    auto invalid_cwd = fixture_config(executable);
    invalid_cwd.cwd = "/definitely/missing/autohand-cwd";
    autohand::AutohandSdk invalid_cwd_sdk(invalid_cwd);
    bool rejected = false;
    try {
      invalid_cwd_sdk.start();
    } catch (const autohand::SdkError&) {
      rejected = true;
    }
    assert(rejected && !invalid_cwd_sdk.is_started());
  }
  {
    autohand::AutohandSdk readiness_sdk(fixture_config(executable, "readiness-error"));
    bool rejected = false;
    try {
      readiness_sdk.start();
    } catch (const autohand::RpcError&) {
      rejected = true;
    }
    assert(rejected && !readiness_sdk.is_started());
  }
  {
    autohand::AutohandSdk large_sdk(fixture_config(executable));
    large_sdk.start();
    const std::string large_value(2 * 1024 * 1024, 'x');
    const auto result = large_sdk.request(
        "autohand.large", "{\"value\":\"" + large_value + "\"}");
    assert(autohand::json_get_string(result, "method") == "autohand.large");
    large_sdk.stop();
  }
  {
    autohand::AutohandSdk eof_sdk(fixture_config(executable, "eof"));
    eof_sdk.start();
    const auto started = std::chrono::steady_clock::now();
    bool rejected = false;
    try {
      (void)eof_sdk.request("autohand.afterEof");
    } catch (const autohand::SdkError&) {
      rejected = true;
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    assert(rejected);
    assert(elapsed < std::chrono::seconds(1));
    eof_sdk.stop();
  }
  {
    autohand::AutohandSdk stream_sdk(fixture_config(executable, "stream"));
    stream_sdk.start();
    std::vector<std::string> first_events;
    std::vector<std::string> second_events;
    std::thread first([&] {
      stream_sdk.stream_prompt("first", [&](const autohand::SdkEvent& event) {
        first_events.push_back(event.text_delta());
      });
    });
    std::thread second([&] {
      stream_sdk.stream_prompt("second", [&](const autohand::SdkEvent& event) {
        second_events.push_back(event.text_delta());
      });
    });
    first.join();
    second.join();
    assert(first_events.size() == 1);
    assert(second_events.size() == 1);
    assert(first_events[0] != second_events[0]);
    stream_sdk.stop();
  }
  {
    const auto prompt_log = std::filesystem::temp_directory_path() /
                            ("autohand-cpp-run-prompts-" + std::to_string(::getpid()));
    std::filesystem::remove(prompt_log);
    auto run_config = fixture_config(executable, "stream");
    run_config.environment["AUTOHAND_CPP_PROMPT_LOG"] = prompt_log.string();
    autohand::Agent agent(run_config);
    auto run = agent.send("throw once");
    bool stream_rejected = false;
    try {
      run.stream([](const autohand::SdkEvent&) { throw std::runtime_error("callback failed"); });
    } catch (const std::runtime_error& error) {
      stream_rejected = std::string(error.what()) == "callback failed";
    }
    assert(stream_rejected);
    bool wait_rejected = false;
    try {
      (void)run.wait();
    } catch (const std::runtime_error& error) {
      wait_rejected = std::string(error.what()) == "callback failed";
    }
    assert(wait_rejected);
    std::ifstream prompt_log_stream(prompt_log);
    std::size_t prompt_count = 0;
    std::string prompt_line;
    while (std::getline(prompt_log_stream, prompt_line)) ++prompt_count;
    assert(prompt_count == 1);
    agent.close();
    std::filesystem::remove(prompt_log);
  }
  {
    const auto marker = std::filesystem::temp_directory_path() /
                        ("autohand-cpp-eof-once-" + std::to_string(::getpid()));
    std::filesystem::remove(marker);
    auto restart_config = fixture_config(executable, "eof-once");
    restart_config.environment["AUTOHAND_CPP_TEST_MARKER"] = marker.string();
    autohand::AutohandSdk restart_sdk(restart_config);
    restart_sdk.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (restart_sdk.is_started() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(!restart_sdk.is_started());
    restart_sdk.start();
    assert(restart_sdk.is_started());
    assert(autohand::json_get_string(restart_sdk.get_state(), "method") == "autohand.getState");
    restart_sdk.stop();
    std::filesystem::remove(marker);
  }
  {
    autohand::AutohandSdk hanging_sdk(fixture_config(executable, "hang"));
    hanging_sdk.start();
    const auto started = std::chrono::steady_clock::now();
    hanging_sdk.stop();
    assert(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
  }

  test_startup_budgets(executable);

  std::cout << "autohand_sdk_tests passed\n";
}
