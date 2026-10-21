#include <autohand/sdk.hpp>

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

int run_fixture() {
  int prompt_count = 0;
  int step_number = 0;
  const auto end = [](const std::string& reason) {
    std::cout << "{\"jsonrpc\":\"2.0\",\"method\":\"autohand.turnEnd\",\"params\":{\"reason\":\""
              << reason << "\",\"timestamp\":\"now\"}}\n" << std::flush;
  };
  const auto step = [&] {
    ++step_number;
    if (const auto* malformed = std::getenv("AUTOHAND_CPP_BAD_STEP")) {
      std::cout << "{\"jsonrpc\":\"2.0\",\"method\":\"autohand.stepEnd\",\"params\":"
                << malformed << "}\n" << std::flush;
      return;
    }
    std::cout << "{\"jsonrpc\":\"2.0\",\"method\":\"autohand.stepEnd\",\"params\":{\"stepId\":\"step-"
              << step_number << "\",\"timestamp\":\"now\",\"step\":{\"stepNumber\":"
              << step_number << ",\"toolCalls\":[{\"tool\":\"read_file\",\"args\":{\"path\":\"evidence.txt\"}}],"
              << "\"toolResults\":[{\"tool\":\"read_file\",\"success\":true,\"output\":\"evidence-"
              << step_number << "\"}]}}}\n" << std::flush;
  };
  std::string line;
  while (std::getline(std::cin, line)) {
    if (const auto* log = std::getenv("AUTOHAND_CPP_STEPS_LOG")) std::ofstream(log, std::ios::app) << line << '\n';
    const auto marker = line.find("\"id\":");
    const auto id = std::stol(line.substr(marker + 5));
    const auto method = autohand::json_get_string(line, "method");
    const auto* decision_result = std::getenv("AUTOHAND_CPP_BAD_DECISION");
    const auto result = method == "autohand.stepDecision" && decision_result
        ? decision_result : "{\"success\":true}";
    std::cout << "{\"jsonrpc\":\"2.0\",\"id\":" << id
              << ",\"result\":" << result << "}\n" << std::flush;
    if (method == "autohand.prompt") {
      ++prompt_count;
      if (prompt_count == 1 && line.find("stopWhen") != std::string::npos) {
        step();
        if (std::getenv("AUTOHAND_CPP_EXIT_AT_STEP")) return 0;
        continue;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(80));
      std::ofstream(std::getenv("AUTOHAND_CPP_STEPS_COMPLETED")).close();
      std::cout << R"({"jsonrpc":"2.0","method":"autohand.messageEnd","params":{"content":"continued"}})" << '\n';
      end("completed");
    } else if (method == "autohand.stepDecision" && !decision_result) {
      if (line.find("\"stop\":true") != std::string::npos) end("stop_condition");
      else step();
    } else if (method == "autohand.abort") {
      end("aborted");
    }
  }
  return 0;
}

struct Fixture {
  std::filesystem::path directory;
  autohand::Config config;

  explicit Fixture(const std::string& executable) {
    auto pattern = (std::filesystem::temp_directory_path() / "autohand-cpp-steps-XXXXXX").string();
    const auto* created = ::mkdtemp(pattern.data());
    assert(created);
    directory = created;
    config.cli_path = executable;
    config.timeout = std::chrono::seconds(2);
    config.environment["AUTOHAND_CPP_STEPS_FIXTURE"] = "1";
    config.environment["AUTOHAND_CPP_STEPS_COMPLETED"] = (directory / "completed").string();
    config.environment["AUTOHAND_CPP_STEPS_LOG"] = (directory / "requests").string();
  }

  ~Fixture() { std::filesystem::remove_all(directory); }

  std::string log() const {
    std::ifstream input(directory / "requests");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }
};

void test_stop_and_continue(const std::string& executable) {
  Fixture fixture(executable);
  autohand::Agent agent(fixture.config);
  autohand::PromptOptions options;
  options.stop_when = {autohand::is_step_count(2), autohand::has_tool_call(" never "),
      [](const autohand::StopConditionContext& context, autohand::CancellationToken) {
        assert(context.steps.back().tool_results.front().output == "evidence-" + std::to_string(context.steps.size()));
        return false;
      }};
  auto result = agent.run("Read evidence", options);
  assert(result.status == "stopped" && result.steps.size() == 2);
  assert(result.steps.front().tool_calls.front().args_json == R"({"path":"evidence.txt"})");
  auto continued = agent.run("Continue");
  assert(continued.status == "completed" && continued.text == "continued" && continued.steps.empty());
  assert(fixture.log().find(R"("stopWhen":{"mode":"host"})") != std::string::npos);
}

void test_has_tool_call(const std::string& executable) {
  Fixture fixture(executable);
  autohand::Agent agent(fixture.config);
  autohand::PromptOptions options;
  options.stop_when = {autohand::has_tool_call(" read_file ")};
  const auto result = agent.run("Read evidence", options);
  assert(result.status == "stopped" && result.steps.size() == 1);
}

void test_cancel_and_queued_ownership(const std::string& executable) {
  Fixture fixture(executable);
  autohand::Agent agent(fixture.config);
  auto entered = std::make_shared<std::promise<void>>();
  auto verdict = std::make_shared<std::promise<bool>>();
  auto decision = verdict->get_future().share();
  autohand::PromptOptions options;
  options.stop_when = {[entered, decision](const autohand::StopConditionContext&, autohand::CancellationToken) {
    entered->set_value();
    return decision;
  }};
  auto active = agent.send("Read evidence", options);
  auto result = std::async(std::launch::async, [&] { return active.wait(); });
  assert(entered->get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  auto queued = agent.send("Queued prompt");
  auto queued_result = std::async(std::launch::async, [&] { return queued.wait(); });
  queued.abort();
  assert(queued_result.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  assert(queued_result.get().status == "aborted");
  assert(fixture.log().find("autohand.abort") == std::string::npos);
  assert(fixture.log().find("Queued prompt") == std::string::npos);
  active.abort();
  assert(result.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  assert(result.get().status == "aborted");
  assert(agent.run("Continue").text == "continued");
  verdict->set_value(true);
  assert(fixture.log().find("autohand.stepDecision") == std::string::npos);
}

void test_failure_and_recovery(const std::string& executable) {
  Fixture fixture(executable);
  autohand::Agent agent(fixture.config);
  auto unresolved = std::make_shared<std::promise<bool>>();
  auto decision = unresolved->get_future().share();
  autohand::PromptOptions options;
  options.stop_when = {[decision](const autohand::StopConditionContext&, autohand::CancellationToken) { return decision; },
      [](const autohand::StopConditionContext&, autohand::CancellationToken) -> bool { throw std::runtime_error("predicate failed"); }};
  auto run = agent.send("Read evidence", options);
  auto result = std::async(std::launch::async, [&] { return run.wait(); });
  assert(result.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  bool rejected = false;
  try { (void)result.get(); } catch (const std::runtime_error& error) { rejected = std::string(error.what()) == "predicate failed"; }
  assert(rejected);
  unresolved->set_value(false);
  assert(fixture.log().find(R"("stop":true)") != std::string::npos);
  assert(agent.run("Continue").text == "continued");
}

void test_protocol_failure(const std::string& executable, const std::string& key, const std::string& value) {
  Fixture fixture(executable);
  fixture.config.environment[key] = value;
  autohand::Agent agent(fixture.config);
  autohand::PromptOptions options;
  options.stop_when = {autohand::is_step_count(1)};
  bool rejected = false;
  try { (void)agent.run("Read evidence", options); } catch (const autohand::SdkError&) { rejected = true; }
  assert(rejected);
  if (key != "AUTOHAND_CPP_EXIT_AT_STEP") assert(agent.run("Continue").text == "continued");
}

void test_callback_failure_and_recovery(const std::string& executable) {
  Fixture fixture(executable);
  autohand::Agent agent(fixture.config);
  autohand::PromptOptions options;
  options.stop_when = {autohand::is_step_count(1)};
  auto run = agent.send("Read evidence", options);
  bool rejected = false;
  try {
    run.stream([](const auto&) { throw std::runtime_error("observer failed"); });
  } catch (const std::runtime_error& error) { rejected = std::string(error.what()) == "observer failed"; }
  assert(rejected);
  rejected = false;
  try { (void)run.wait(); }
  catch (const std::runtime_error& error) { rejected = std::string(error.what()) == "observer failed"; }
  assert(rejected);
  assert(agent.run("Continue").text == "continued");
}

void test_abort_before_and_after_completion(const std::string& executable) {
  Fixture fixture(executable);
  autohand::Agent agent(fixture.config);
  auto unstarted = agent.send("Unstarted");
  unstarted.abort();
  assert(unstarted.wait().status == "aborted");
  assert(fixture.log().find("autohand.prompt") == std::string::npos);
  auto completed = agent.send("Ordinary prompt");
  assert(completed.wait().status == "completed");
  completed.abort();
  assert(completed.wait().status == "completed");
  assert(fixture.log().find("autohand.abort") == std::string::npos);
  assert(agent.run("Continue").text == "continued");
}

void test_prompt_waits_for_terminal(const std::string& executable) {
  const auto marker = std::filesystem::temp_directory_path() /
      ("autohand-cpp-step-completed-" + std::to_string(::getpid()));
  std::filesystem::remove(marker);
  autohand::Config config;
  config.cli_path = executable;
  config.timeout = std::chrono::seconds(2);
  config.environment["AUTOHAND_CPP_STEPS_FIXTURE"] = "1";
  config.environment["AUTOHAND_CPP_STEPS_COMPLETED"] = marker.string();
  autohand::AutohandSdk sdk(config);
  sdk.start();
  (void)sdk.prompt("Read evidence");
  const auto completed = std::filesystem::exists(marker);
  sdk.stop();
  std::filesystem::remove(marker);
  assert(completed && "Prompt acknowledgement is not terminal completion");
}

}  // namespace

int main(int argc, char** argv) {
  if (std::getenv("AUTOHAND_CPP_STEPS_FIXTURE")) return run_fixture();
  assert(argc > 0);
  test_prompt_waits_for_terminal(std::filesystem::absolute(argv[0]).string());
  const auto executable = std::filesystem::absolute(argv[0]).string();
  test_stop_and_continue(executable);
  test_has_tool_call(executable);
  test_cancel_and_queued_ownership(executable);
  test_failure_and_recovery(executable);
  test_callback_failure_and_recovery(executable);
  test_abort_before_and_after_completion(executable);
  test_protocol_failure(executable, "AUTOHAND_CPP_BAD_STEP", "{}");
  test_protocol_failure(executable, "AUTOHAND_CPP_BAD_DECISION", "{}");
  test_protocol_failure(executable, "AUTOHAND_CPP_BAD_DECISION", R"({"success":false})");
  test_protocol_failure(executable, "AUTOHAND_CPP_BAD_DECISION", R"({"success":"true"})");
  test_protocol_failure(executable, "AUTOHAND_CPP_EXIT_AT_STEP", "1");
  assert(autohand::event_type_from_method("autohand.stepEnd", "{}") == "step_end");
  std::cout << "step control tests passed\n";
}
