#include <autohand/sdk.hpp>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

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

}  // namespace

int main() {
  assert(autohand::parse_json_text(R"({"ok":true})") == R"({"ok":true})");
  assert(autohand::parse_json_text("```json\n{\"ok\":true}\n```") == R"({"ok":true})");
  assert(autohand::parse_json_text("Result: {\"ok\":true} done.") == R"({"ok":true})");

  auto config = autohand::Config::from_environment()
                    .with_cli_path(make_fake_cli().string())
                    .with_skill("cpp")
                    .with_model("fantail2");
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

  autohand::GoalParams goal;
  goal.objective = "Finish parity";
  goal.token_budget = 20000;
  assert(goal.to_json() == R"({"objective":"Finish parity","token_budget":20000})");

  autohand::AutoresearchStartParams autoresearch{"Reduce test runtime"};
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

  autohand::AutohandSdk sdk(config);
  sdk.start();
  assert(sdk.supports_command("/autoresearch"));
  assert(autohand::json_get_string(sdk.create_goal(goal), "method") == "autohand.goal.create");
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

  std::cout << "autohand_sdk_tests passed\n";
}
