#include <autohand/sdk.hpp>

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unistd.h>

namespace {

long request_id(const std::string& line) {
  const auto marker = line.find("\"id\"");
  if (marker == std::string::npos) return 0;
  auto position = line.find(':', marker);
  if (position == std::string::npos) return 0;
  ++position;
  while (position < line.size() &&
         std::isspace(static_cast<unsigned char>(line[position]))) {
    ++position;
  }
  return std::stol(line.substr(position));
}

int run_fixture() {
  const auto* log_path = std::getenv("AUTOHAND_CPP_CURRENT_LOG");
  const auto* configured_result = std::getenv("AUTOHAND_CPP_CURRENT_RESULT");
  const auto* notification = std::getenv("AUTOHAND_CPP_CURRENT_NOTIFICATION");
  std::string line;
  while (std::getline(std::cin, line)) {
    if (log_path) std::ofstream(log_path, std::ios::app) << line << '\n';
    const auto id = request_id(line);
    const auto method = autohand::json_get_string(line, "method");
    if (method == "autohand.prompt" && notification && *notification) {
      std::cout << notification << '\n';
    }
    const auto result = method == "autohand.getState"
                            ? std::string("{}")
                            : std::string(configured_result ? configured_result : "{\"success\":true}");
    std::cout << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":" << result
              << "}\n"
              << std::flush;
  }
  return 0;
}

struct Fixture {
  std::filesystem::path log_path;
  autohand::AutohandSdk sdk;

  Fixture(
      const std::string& executable,
      std::string_view name,
      std::string result,
      std::string notification = {})
      : log_path(std::filesystem::temp_directory_path() /
                 ("autohand-cpp-current-" + std::string(name) + "-" +
                  std::to_string(::getpid()) + ".log")),
        sdk([&] {
          std::filesystem::remove(log_path);
          autohand::Config config;
          config.cli_path = executable;
          config.timeout = std::chrono::seconds(2);
          config.environment["AUTOHAND_CPP_CURRENT_FIXTURE"] = "1";
          config.environment["AUTOHAND_CPP_CURRENT_LOG"] = log_path.string();
          config.environment["AUTOHAND_CPP_CURRENT_RESULT"] = std::move(result);
          config.environment["AUTOHAND_CPP_CURRENT_NOTIFICATION"] = std::move(notification);
          return config;
        }()) {
    sdk.start();
  }

  ~Fixture() {
    sdk.stop();
    std::filesystem::remove(log_path);
  }

  std::string log() const {
    std::ifstream input(log_path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }

  void assert_request(const std::string& method, std::initializer_list<std::string> fragments) const {
    const auto contents = log();
    assert(contents.find("\"method\":\"" + method + "\"") != std::string::npos);
    for (const auto& fragment : fragments) {
      assert(contents.find(fragment) != std::string::npos);
    }
  }
};

void test_permission_acknowledgement(const std::string& executable) {
  Fixture fixture(executable, "permission-ack", R"({"success":true})");
  const auto result = fixture.sdk.acknowledge_permission("permission-1");
  assert(result.success);
  fixture.assert_request(
      "autohand.permissionAcknowledged", {R"("requestId":"permission-1")"});
  bool rejected = false;
  try {
    (void)fixture.sdk.acknowledge_permission("  ");
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

void test_directory_access_response(const std::string& executable) {
  Fixture fixture(executable, "directory-response", R"({"success":true})");
  const auto result = fixture.sdk.respond_to_directory_access(
      autohand::DirectoryAccessResponseParams{"directory-1", true});
  assert(result.success);
  fixture.assert_request(
      "autohand.directoryAccessResponse",
      {R"("requestId":"directory-1")", R"("granted":true)"});
  bool rejected = false;
  try {
    (void)fixture.sdk.respond_to_directory_access({"", false});
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

void test_directory_access_acknowledgement(const std::string& executable) {
  Fixture fixture(executable, "directory-ack", R"({"success":true})");
  const auto result = fixture.sdk.acknowledge_directory_access("directory-2");
  assert(result.success);
  fixture.assert_request(
      "autohand.directoryAccessAcknowledged", {R"("requestId":"directory-2")"});
  bool rejected = false;
  try {
    (void)fixture.sdk.acknowledge_directory_access("\t");
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

void test_multi_file_change_decisions(const std::string& executable) {
  Fixture fixture(
      executable,
      "changes-decision",
      R"({"success":false,"appliedCount":1,"skippedCount":1,"errors":[{"changeId":"change-2","error":"conflict"}]})");
  const auto result = fixture.sdk.decide_changes(
      {"batch-1", autohand::AcceptSelectedChanges{{"change-1", "change-2"}}});
  assert(!result.success);
  assert(result.applied_count == 1);
  assert(result.skipped_count == 1);
  assert(result.errors.size() == 1);
  assert(result.errors.front().change_id == "change-2");
  assert(result.errors.front().error == "conflict");
  fixture.assert_request(
      "autohand.changesDecision",
      {R"("batchId":"batch-1")", R"("action":"accept_selected")",
       R"("selectedChangeIds":["change-1","change-2"])"});

  bool rejected = false;
  try {
    (void)fixture.sdk.decide_changes(
        {"batch-2", autohand::AcceptSelectedChanges{{}}});
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

void test_session_history(const std::string& executable) {
  Fixture fixture(
      executable,
      "session-history",
      R"({"sessions":[{"sessionId":"session-1","createdAt":"2026-07-20T00:00:00Z","lastActiveAt":"2026-07-21T00:00:00Z","projectName":"tin","model":"gpt-5","messageCount":7,"status":"completed"}],"currentPage":2,"totalPages":3,"totalItems":21})");
  const auto result = fixture.sdk.get_session_history({2, 10});
  assert(result.sessions.size() == 1);
  assert(result.sessions.front().session_id == "session-1");
  assert(result.sessions.front().status == autohand::SessionStatus::Completed);
  assert(result.current_page == 2);
  assert(result.total_pages == 3);
  assert(result.total_items == 21);
  fixture.assert_request(
      "autohand.getHistory", {R"("page":2)", R"("pageSize":10)"});

  bool rejected = false;
  try {
    (void)fixture.sdk.get_session_history({0, std::nullopt});
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

void test_session_details(const std::string& executable) {
  Fixture fixture(
      executable,
      "session-details",
      R"({"success":true,"sessionId":"session-2","projectName":"tin","model":"gpt-5","messageCount":1,"status":"active","createdAt":"2026-07-20T00:00:00Z","lastActiveAt":"2026-07-21T00:00:00Z","summary":"summary","messages":[{"id":"message-1","role":"assistant","content":"done","timestamp":"2026-07-21T00:00:00Z","toolCalls":[{"id":"call-1","name":"read","args":{"path":"README.md"}}]}],"workspaceRoot":"/workspace"})");
  const auto result = fixture.sdk.get_session("session-2");
  assert(std::holds_alternative<autohand::SessionDetails>(result));
  const auto& details = std::get<autohand::SessionDetails>(result);
  assert(details.session_id == "session-2");
  assert(details.status == autohand::SessionStatus::Active);
  assert(details.messages.size() == 1);
  assert(details.messages.front().role == autohand::SessionMessageRole::Assistant);
  assert(details.messages.front().tool_calls.front().args_json ==
         R"({"path":"README.md"})");
  fixture.assert_request("autohand.getSession", {R"("sessionId":"session-2")"});

  bool rejected = false;
  try {
    (void)fixture.sdk.get_session(" ");
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

void test_session_attachment(const std::string& executable) {
  Fixture fixture(
      executable,
      "session-attach",
      R"({"success":true,"sessionId":"session-3","workspaceRoot":"/workspace","messageCount":12})");
  const auto result = fixture.sdk.attach_session({"session-3"});
  assert(result.success);
  assert(result.session_id == "session-3");
  assert(result.workspace_root == "/workspace");
  assert(result.message_count == 12);
  assert(!result.error);
  fixture.assert_request("autohand.session.attach", {R"("sessionId":"session-3")"});

  bool rejected = false;
  try {
    (void)fixture.sdk.attach_session({""});
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

void test_timed_yolo_mode(const std::string& executable) {
  Fixture fixture(
      executable, "timed-yolo", R"({"success":true,"expiresIn":90})");
  const auto result = fixture.sdk.set_yolo({"*", 90});
  assert(result.success);
  assert(result.expires_in == 90);
  fixture.assert_request(
      "autohand.yoloSet", {R"("pattern":"*")", R"("timeoutSeconds":90)"});

  (void)fixture.sdk.set_yolo_compat({"", std::nullopt});
  fixture.assert_request("autohand.yolo.set", {R"("pattern":"")"});

  bool rejected = false;
  try {
    (void)fixture.sdk.set_yolo({"*", 0});
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

void test_vscode_mcp_tool_registration(const std::string& executable) {
  Fixture fixture(executable, "vscode-tools", R"({"success":true})");
  autohand::SetVscodeMcpToolsParams params;
  params.tools.push_back(autohand::VscodeMcpTool{
      "workspace.read",
      "Read a workspace file",
      "vscode",
      autohand::McpObjectInputSchema{
          R"({"path":{"type":"string"}})", {"path"}}});
  const auto result = fixture.sdk.set_vscode_mcp_tools(params);
  assert(result.success);
  fixture.assert_request(
      "autohand.mcp.setVscodeTools",
      {R"("name":"workspace.read")", R"("serverName":"vscode")",
       R"("inputSchema":{"type":"object")", R"("required":["path"])"});

  params.tools.front().input_schema->properties_json = "[]";
  bool rejected = false;
  try {
    (void)fixture.sdk.set_vscode_mcp_tools(params);
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

void test_mcp_invocation_responses(const std::string& executable) {
  Fixture fixture(executable, "mcp-response", R"({"success":true})");
  const auto result = fixture.sdk.respond_to_mcp_invocation(
      {"invoke-1", autohand::McpInvocationSuccess{R"({"content":"ok"})"}});
  assert(result.success);
  fixture.assert_request(
      "autohand.mcp.invokeResponse",
      {R"("requestId":"invoke-1")", R"("success":true)",
       R"("result":{"content":"ok"})"});

  const auto failure = fixture.sdk.respond_to_mcp_invocation(
      {"invoke-2", autohand::McpInvocationFailure{"tool failed"}});
  assert(failure.success);
  fixture.assert_request(
      "autohand.mcp.invokeResponse",
      {R"("requestId":"invoke-2")", R"("success":false)",
       R"("error":"tool failed")"});

  bool rejected = false;
  try {
    (void)fixture.sdk.respond_to_mcp_invocation(
        {"invoke-3", autohand::McpInvocationSuccess{"{"}});
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

void test_project_learning_recommendations(const std::string& executable) {
  Fixture fixture(
      executable,
      "learn-recommend",
      R"({"success":true,"projectSummary":"C++ SDK","audit":[{"skill":"legacy","status":"outdated","reason":"Old API"}],"recommendations":[{"slug":"cpp-testing","score":0.95,"reason":"Test coverage"}],"gapAnalysis":null})");
  const auto result = fixture.sdk.recommend_project_skills({true});
  assert(result.success);
  assert(result.project_summary == "C++ SDK");
  assert(result.audit.front().status == autohand::SkillAuditStatus::Outdated);
  assert(result.recommendations.front().slug == "cpp-testing");
  assert(result.recommendations.front().score == 0.95);
  assert(!result.gap_analysis);
  fixture.assert_request("autohand.learn.recommend", {R"("deep":true)"});
}

void test_project_learning_updates(const std::string& executable) {
  Fixture fixture(
      executable,
      "learn-update",
      R"({"success":true,"updated":1,"unchanged":1,"results":[{"name":"cpp-testing","status":"updated"},{"name":"cmake","status":"unchanged"}]})");
  const auto result = fixture.sdk.update_project_skills();
  assert(result.success);
  assert(result.updated == 1);
  assert(result.unchanged == 1);
  assert(result.results.size() == 2);
  assert(result.results.front().status == autohand::SkillUpdateStatus::Updated);
  fixture.assert_request("autohand.learn.update", {});
}

void test_skill_generation(const std::string& executable) {
  Fixture fixture(
      executable,
      "learn-generate",
      R"({"success":true,"skillName":"cpp-testing","skillPath":"/workspace/.autohand/skills/cpp-testing/SKILL.md"})");
  const auto result = fixture.sdk.generate_skill(
      {autohand::SkillGenerationScope::Project});
  assert(result.success);
  assert(result.skill_name == "cpp-testing");
  assert(result.skill_path == "/workspace/.autohand/skills/cpp-testing/SKILL.md");
  fixture.assert_request("autohand.learn.generate", {R"("scope":"project")"});
}

void test_tools_registry(const std::string& executable) {
  Fixture fixture(
      executable,
      "tools-registry",
      R"json({"tools":[{"name":"release","description":"Prepare release","requiresApproval":true,"approvalMessage":"Publish?","source":"extension","scope":"project","disabled":false,"createdAt":"2026-07-21T00:00:00Z","schemaVersion":2,"handlerPreview":"run()","reuseHint":"Use for releases","extensionId":"release-tools","extensionVersion":"1.2.3"}],"diagnostics":[{"file":"bad-tool.json","reason":"invalid schema"}]})json");
  const auto result = fixture.sdk.get_tools_registry();
  assert(result.tools.size() == 1);
  const auto& tool = result.tools.front();
  assert(tool.name == "release");
  assert(tool.source == autohand::ToolRegistrySource::Extension);
  assert(tool.scope == autohand::ToolRegistryScope::Project);
  assert(tool.requires_approval == true);
  assert(tool.schema_version == 2);
  assert(tool.extension_version == "1.2.3");
  assert(result.diagnostics.front().file == "bad-tool.json");
  fixture.assert_request("autohand.getToolsRegistry", {});
}

void test_context_compaction_control(const std::string& executable) {
  Fixture fixture(executable, "context-compact", R"({"enabled":true})");
  const auto result = fixture.sdk.set_context_compact(true);
  assert(result.enabled);
  fixture.assert_request("autohand.setContextCompact", {R"("enabled":true)"});
}

void test_automode_iteration_events(const std::string& executable) {
  Fixture fixture(
      executable,
      "automode-iteration",
      "{}",
      R"({"jsonrpc":"2.0","method":"autohand.automode.iteration","params":{"sessionId":"session-auto","iteration":3,"actions":["read","write"],"tokensUsed":400,"timestamp":"2026-07-21T00:00:00Z"}})");
  std::vector<autohand::SdkEvent> events;
  fixture.sdk.stream_prompt("continue", [&](const auto& event) { events.push_back(event); });
  assert(events.size() == 1);
  assert(events.front().type == "automode_iteration");
  const auto* iteration =
      std::get_if<autohand::AutomodeIterationEvent>(&events.front().payload);
  assert(iteration != nullptr);
  assert(iteration->session_id == "session-auto");
  assert(iteration->iteration == 3);
  assert(iteration->actions.size() == 2);
  assert(iteration->tokens_used == 400);

  const auto unknown = autohand::sdk_event_from_notification(
      "autohand.futureNotification", R"({"value":1})");
  assert(unknown.type == "futureNotification");
  assert(std::holds_alternative<std::monostate>(unknown.payload));

  bool rejected = false;
  try {
    (void)autohand::sdk_event_from_notification(
        "autohand.automode.iteration", R"({"sessionId":"missing-fields"})");
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);

  Fixture malformed_fixture(
      executable,
      "automode-iteration-malformed",
      "{}",
      R"({"jsonrpc":"2.0","method":"autohand.automode.iteration","params":{"sessionId":"missing-fields"}})");
  std::vector<autohand::SdkEvent> malformed_events;
  malformed_fixture.sdk.stream_prompt(
      "continue", [&](const auto& event) { malformed_events.push_back(event); });
  assert(malformed_events.size() == 1);
  assert(malformed_events.front().type == "error");
  assert(std::holds_alternative<std::monostate>(malformed_events.front().payload));
}

void test_automode_completion_events(const std::string& executable) {
  Fixture fixture(
      executable,
      "automode-complete",
      "{}",
      R"({"jsonrpc":"2.0","method":"autohand.automode.complete","params":{"sessionId":"session-auto","iterations":8,"filesCreated":2,"filesModified":5,"timestamp":"2026-07-21T00:01:00Z"}})");
  std::vector<autohand::SdkEvent> events;
  fixture.sdk.stream_prompt("continue", [&](const auto& event) { events.push_back(event); });
  assert(events.size() == 1);
  assert(events.front().type == "automode_complete");
  const auto* complete =
      std::get_if<autohand::AutomodeCompleteEvent>(&events.front().payload);
  assert(complete != nullptr);
  assert(complete->session_id == "session-auto");
  assert(complete->iterations == 8);
  assert(complete->files_created == 2);
  assert(complete->files_modified == 5);
}

void test_automode_error_events(const std::string& executable) {
  Fixture fixture(
      executable,
      "automode-error",
      "{}",
      R"({"jsonrpc":"2.0","method":"autohand.automode.error","params":{"sessionId":"session-auto","error":"iteration limit reached","timestamp":"2026-07-21T00:02:00Z"}})");
  std::vector<autohand::SdkEvent> events;
  fixture.sdk.stream_prompt("continue", [&](const auto& event) { events.push_back(event); });
  assert(events.size() == 1);
  assert(events.front().type == "automode_error");
  const auto* error = std::get_if<autohand::AutomodeErrorEvent>(&events.front().payload);
  assert(error != nullptr);
  assert(error->session_id == "session-auto");
  assert(error->error == "iteration limit reached");
}

void test_pre_tool_hook_events(const std::string& executable) {
  Fixture fixture(
      executable,
      "hook-pre-tool",
      "{}",
      R"({"jsonrpc":"2.0","method":"autohand.hook.preTool","params":{"toolId":"tool-1","toolName":"read_file","args":{"path":"README.md"},"timestamp":"2026-07-21T00:03:00Z"}})");
  std::vector<autohand::SdkEvent> events;
  fixture.sdk.stream_prompt("continue", [&](const auto& event) { events.push_back(event); });
  assert(events.size() == 1);
  assert(events.front().type == "hook_pre_tool");
  const auto* hook = std::get_if<autohand::PreToolHookEvent>(&events.front().payload);
  assert(hook != nullptr);
  assert(hook->tool_id == "tool-1");
  assert(hook->tool_name == "read_file");
  assert(hook->args_json == R"({"path":"README.md"})");
}

void test_post_tool_hook_events(const std::string& executable) {
  Fixture fixture(
      executable,
      "hook-post-tool",
      "{}",
      R"({"jsonrpc":"2.0","method":"autohand.hook.postTool","params":{"toolId":"tool-1","toolName":"read_file","success":true,"duration":12.5,"output":"contents","timestamp":"2026-07-21T00:04:00Z"}})");
  std::vector<autohand::SdkEvent> events;
  fixture.sdk.stream_prompt("continue", [&](const auto& event) { events.push_back(event); });
  assert(events.size() == 1);
  assert(events.front().type == "hook_post_tool");
  const auto* hook = std::get_if<autohand::PostToolHookEvent>(&events.front().payload);
  assert(hook != nullptr);
  assert(hook->tool_id == "tool-1");
  assert(hook->success);
  assert(hook->duration == 12.5);
  assert(hook->output == "contents");
}

void test_pre_prompt_hook_events(const std::string& executable) {
  Fixture fixture(
      executable,
      "hook-pre-prompt",
      "{}",
      R"({"jsonrpc":"2.0","method":"autohand.hook.prePrompt","params":{"instruction":"Use the SDK contract","mentionedFiles":["README.md","src/sdk.cpp"],"timestamp":"2026-07-21T00:05:00Z"}})");
  std::vector<autohand::SdkEvent> events;
  fixture.sdk.stream_prompt("continue", [&](const auto& event) { events.push_back(event); });
  assert(events.size() == 1);
  assert(events.front().type == "hook_pre_prompt");
  const auto* hook = std::get_if<autohand::PrePromptHookEvent>(&events.front().payload);
  assert(hook != nullptr);
  assert(hook->instruction == "Use the SDK contract");
  assert(hook->mentioned_files.size() == 2);
  assert(hook->mentioned_files.back() == "src/sdk.cpp");
}

void test_post_response_hook_events(const std::string& executable) {
  Fixture fixture(
      executable,
      "hook-post-response",
      "{}",
      R"({"jsonrpc":"2.0","method":"autohand.hook.postResponse","params":{"tokensUsed":1300,"tokensUsageStatus":"actual","toolCallsCount":4,"duration":225.5,"timestamp":"2026-07-21T00:06:00Z"}})");
  std::vector<autohand::SdkEvent> events;
  fixture.sdk.stream_prompt("continue", [&](const auto& event) { events.push_back(event); });
  assert(events.size() == 1);
  assert(events.front().type == "hook_post_response");
  const auto* hook =
      std::get_if<autohand::PostResponseHookEvent>(&events.front().payload);
  assert(hook != nullptr);
  assert(hook->tokens_used == 1300);
  assert(hook->tokens_usage_status == autohand::TokensUsageStatus::Actual);
  assert(hook->tool_calls_count == 4);
  assert(hook->duration == 225.5);
}

void test_mcp_invocation_request_events(const std::string& executable) {
  Fixture fixture(
      executable,
      "mcp-invoke-request",
      "{}",
      R"({"jsonrpc":"2.0","method":"autohand.mcp.invokeRequest","params":{"requestId":"invoke-4","toolName":"workspace.read","args":{"path":"README.md"},"timestamp":"2026-07-21T00:07:00Z"}})");
  std::vector<autohand::SdkEvent> events;
  fixture.sdk.stream_prompt("continue", [&](const auto& event) { events.push_back(event); });
  assert(events.size() == 1);
  assert(events.front().type == "mcp_invoke_request");
  const auto* request =
      std::get_if<autohand::McpInvocationRequestEvent>(&events.front().payload);
  assert(request != nullptr);
  assert(request->request_id == "invoke-4");
  assert(request->tool_name == "workspace.read");
  assert(request->args_json == R"({"path":"README.md"})");
}

void test_mcp_tools_changed_events(const std::string& executable) {
  Fixture fixture(
      executable,
      "mcp-tools-changed",
      "{}",
      R"({"jsonrpc":"2.0","method":"autohand.mcp.toolsChanged","params":{"tools":[{"name":"workspace.read","description":"Read a file","serverName":"vscode"}],"timestamp":"2026-07-21T00:08:00Z"}})");
  std::vector<autohand::SdkEvent> events;
  fixture.sdk.stream_prompt("continue", [&](const auto& event) { events.push_back(event); });
  assert(events.size() == 1);
  assert(events.front().type == "mcp_tools_changed");
  const auto* changed =
      std::get_if<autohand::McpToolsChangedEvent>(&events.front().payload);
  assert(changed != nullptr);
  assert(changed->tools.size() == 1);
  assert(changed->tools.front().name == "workspace.read");
  assert(changed->tools.front().server_name == "vscode");
}

void test_learning_progress_events(const std::string& executable) {
  Fixture fixture(
      executable,
      "learn-progress",
      "{}",
      R"({"jsonrpc":"2.0","method":"autohand.learn.progress","params":{"status":"loading-registry","timestamp":"2026-07-21T00:09:00Z"}})");
  std::vector<autohand::SdkEvent> events;
  fixture.sdk.stream_prompt("continue", [&](const auto& event) { events.push_back(event); });
  assert(events.size() == 1);
  assert(events.front().type == "learn_progress");
  const auto* progress =
      std::get_if<autohand::LearningProgressEvent>(&events.front().payload);
  assert(progress != nullptr);
  assert(progress->status == autohand::LearningProgressStatus::LoadingRegistry);
  assert(progress->timestamp == "2026-07-21T00:09:00Z");

  bool rejected = false;
  try {
    (void)autohand::sdk_event_from_notification(
        "autohand.learn.progress",
        R"({"status":"unknown","timestamp":"2026-07-21T00:09:00Z"})");
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  if (std::getenv("AUTOHAND_CPP_CURRENT_FIXTURE")) return run_fixture();
  const auto executable = std::filesystem::absolute(argv[0]).string();
  test_permission_acknowledgement(executable);
  test_directory_access_response(executable);
  test_directory_access_acknowledgement(executable);
  test_multi_file_change_decisions(executable);
  test_session_history(executable);
  test_session_details(executable);
  test_session_attachment(executable);
  test_timed_yolo_mode(executable);
  test_vscode_mcp_tool_registration(executable);
  test_mcp_invocation_responses(executable);
  test_project_learning_recommendations(executable);
  test_project_learning_updates(executable);
  test_skill_generation(executable);
  test_tools_registry(executable);
  test_context_compaction_control(executable);
  test_automode_iteration_events(executable);
  test_automode_completion_events(executable);
  test_automode_error_events(executable);
  test_pre_tool_hook_events(executable);
  test_post_tool_hook_events(executable);
  test_pre_prompt_hook_events(executable);
  test_post_response_hook_events(executable);
  test_mcp_invocation_request_events(executable);
  test_mcp_tools_changed_events(executable);
  test_learning_progress_events(executable);
  return 0;
}
