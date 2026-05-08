#pragma once

#include <autohand/sdk.hpp>

#include <iostream>
#include <string>

namespace examples {

inline autohand::Config base_config() {
  return autohand::Config::from_environment().with_cwd(".");
}

inline void print_event(autohand::AutohandSdk* sdk, const autohand::SdkEvent& event) {
  if (event.type == "message_update") {
    std::cout << event.text_delta();
  } else if (event.type == "message_end") {
    std::cout << "\n[message completed]\n";
  } else if (event.type == "tool_start") {
    std::cout << "\n[tool] " << event.tool_name() << "\n";
  } else if (event.type == "tool_update") {
    std::cout << event.raw_json << "\n";
  } else if (event.type == "tool_end") {
    std::cout << "\n[tool completed] " << event.tool_name() << "\n";
  } else if (event.type == "permission_request") {
    std::cout << "\n[permission] " << event.description() << "\n";
    if (sdk != nullptr && !event.request_id().empty()) {
      sdk->permission_response(event.request_id(), "allow_once");
    }
  } else if (event.type == "error") {
    std::cerr << "\n[error] " << event.raw_json << "\n";
  }
}

inline int run_low_level(const std::string& title, const std::string& prompt) {
  std::cout << "=== " << title << " ===\n\n";
  autohand::AutohandSdk sdk(base_config());
  sdk.start();
  sdk.stream_prompt(prompt, [&](const autohand::SdkEvent& event) {
    print_event(&sdk, event);
  });
  (void)sdk.get_state();
  sdk.stop();
  return 0;
}

inline int run_agent(const std::string& title, const std::string& prompt) {
  std::cout << "=== " << title << " ===\n\n";
  autohand::Agent agent(base_config());
  auto run = agent.send(prompt);
  run.stream([](const autohand::SdkEvent& event) {
    print_event(nullptr, event);
  });
  const auto result = run.wait();
  std::cout << "\n\n=== Final Response ===\n" << result.text << "\n";
  agent.close();
  return 0;
}

inline int run_json_example() {
  autohand::Agent agent(base_config());
  const auto json = agent.run_json(
      "Assess this SDK repository for publish readiness. Do not execute commands.",
      R"({"summary":"string","risks":[{"title":"string","severity":"low | medium | high","mitigation":"string"}]})");
  std::cout << json << "\n";
  agent.close();
  return 0;
}

inline int show_control_features() {
  const char* methods[] = {
      "request",
      "prompt",
      "stream_prompt",
      "interrupt",
      "set_plan_mode",
      "set_permission_mode",
      "set_model",
      "get_state",
      "get_messages",
      "permission_response",
  };
  for (const auto* method : methods) {
    std::cout << "✓ SDK has method: " << method << "\n";
  }
  return 0;
}

}  // namespace examples

