#include "example_common.hpp"

int main() {
  autohand::Agent agent(
      examples::base_config().with_instructions("You are a concise C++ SDK reviewer. Prefer actionable notes."));
  const auto result = agent.run("Summarize the public API in three bullets.");
  std::cout << result.text << "\n";
  agent.close();
}

