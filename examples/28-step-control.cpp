#include <autohand/sdk.hpp>

#include <iostream>

int main() {
  auto config = autohand::Config::from_environment();
  config.provider = "autohandai";
  config.model = "fantail";
  autohand::Agent agent(config);
  autohand::PromptOptions options;
  options.stop_when = {autohand::is_step_count(1)};
  const auto result = agent.run("Read README.md using read_file", options);
  std::cout << result.status << ": " << result.steps.size() << " completed steps\n";
  for (const auto& step : result.steps) {
    for (const auto& output : step.tool_results) {
      std::cout << output.output.value_or(output.error.value_or("")) << '\n';
    }
  }
  std::cout << agent.run("Summarize the saved result.").text << '\n';
}
