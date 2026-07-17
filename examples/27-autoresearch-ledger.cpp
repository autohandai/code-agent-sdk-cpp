#include <autohand/sdk.hpp>

#include <iostream>

int main() {
  autohand::Agent agent(autohand::Config::from_environment().with_cwd("."));
  if (!agent.supports_command("/autoresearch")) {
    std::cerr << "The connected Autohand CLI does not expose /autoresearch.\n";
    return 0;
  }

  autohand::AutoresearchStartParams params{"Reduce C++ SDK test runtime without failures"};
  params.metric_name = "total_ms";
  params.metric_unit = "ms";
  params.direction = "lower";
  params.measure_command = "ctest --test-dir build";
  params.checks_command = "cmake --build build";
  params.max_iterations = 8;
  params.sampling_json = R"({"minSamples":3,"maxSamples":7})";
  params.constraints_json = R"([{"metricName":"failures","operator":"<=","threshold":0}])";

  std::cout << agent.start_autoresearch(params) << '\n';
  std::cout << agent.get_autoresearch_status() << '\n';
  std::cout << agent.get_autoresearch_history() << '\n';
  std::cout << agent.get_autoresearch_pareto() << '\n';
  std::cout << agent.prune_autoresearch(true) << '\n';
  (void)agent.stop_autoresearch();
  agent.close();
}
