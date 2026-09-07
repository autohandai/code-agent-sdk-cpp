#include <autohand/sdk.hpp>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>

namespace {

std::string required_environment(const char* key) {
  const auto* value = std::getenv(key);
  if (!value || !*value) throw std::runtime_error(std::string("Missing ") + key);
  return value;
}

void verify_harness() {
  const auto base_url = required_environment("AUTOHAND_CPP_HARNESS_BASE_URL");
  autohand::Config config;
  config.cli_path = required_environment("AUTOHAND_TEST_CLI_PATH");
  config.cwd = required_environment("AUTOHAND_CPP_HARNESS_WORKSPACE");
  config.timeout = std::chrono::seconds(30);
  config.provider = "autohandai";
  config.model = "fantail";
  config.api_key = "inference-key";
  config.base_url = base_url + "/v1";
  config.unrestricted = true;
  config.agents = R"({"inline-helper":{"description":"Inline helper","prompt":"Private inline instructions.","tools":["read_file"]}})";
  config.environment = {
      {"AUTOHAND_HOME", required_environment("AUTOHAND_CPP_HARNESS_HOME")},
      {"AUTOHAND_CONFIG", required_environment("AUTOHAND_CPP_HARNESS_CONFIG")},
      {"AUTOHAND_AUTH_API_URL", base_url + "/auth"},
      {"AUTOHAND_SKIP_PING", "1"}, {"AUTOHAND_SKIP_UPDATE_CHECK", "1"},
      {"AUTOHAND_NO_IDLE_LOGOUT", "1"}, {"AUTOHAND_DISABLE_AUTO_REPORT", "1"}};
  autohand::AutohandSdk sdk(config);
  sdk.start();
  const auto agents = sdk.get_supported_agents();
  assert(std::any_of(agents.begin(), agents.end(), [](const auto& agent) { return agent.name == "reviewer" && agent.source == "builtin"; }));
  assert(std::any_of(agents.begin(), agents.end(), [](const auto& agent) { return agent.name == "inline-helper" && agent.source == "session"; }));
  const auto extension_available = [&] {
    const auto current = sdk.get_supported_agents();
    return std::any_of(current.begin(), current.end(), [](const auto& agent) { return agent.extension_id == "sdk.cpp"; });
  };
  assert(extension_available());
  assert(autohand::Run(sdk, "/extensions disable sdk.cpp --scope project").wait().text.find("Disabled sdk.cpp") != std::string::npos);
  assert(!extension_available());
  assert(autohand::Run(sdk, "/extensions enable sdk.cpp --scope project").wait().text.find("Enabled sdk.cpp") != std::string::npos);
  assert(extension_available());

  autohand::PromptOptions options;
  options.stop_when = {autohand::is_step_count(1)};
  const auto stopped = autohand::Run(sdk, "Read evidence.txt using read_file", options).wait();
  assert(stopped.status == "stopped" && stopped.steps.size() == 1);
  const auto& result = stopped.steps.front().tool_results.front();
  assert(result.success && result.output && result.output->find("cpp-persisted-marker") != std::string::npos);
  const auto continued = autohand::Run(sdk, "Continue using the saved tool result").wait();
  assert(continued.status == "completed" && continued.text == "continued from persisted evidence");
}

}  // namespace

int main() {
  try {
    verify_harness();
    std::cout << "actual CLI provider, extension and step control checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
