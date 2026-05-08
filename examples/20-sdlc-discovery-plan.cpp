#include "example_common.hpp"

int main() {
  autohand::AutohandSdk sdk(examples::base_config());
  sdk.start();
  (void)sdk.set_plan_mode(true);
  sdk.stream_prompt(
      "Inspect this package and produce an implementation plan. Do not edit files.",
      [&](const autohand::SdkEvent& event) { examples::print_event(&sdk, event); });
  sdk.stop();
}

