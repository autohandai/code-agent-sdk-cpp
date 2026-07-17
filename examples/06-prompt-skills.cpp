#include "example_common.hpp"

int main() {
  auto config = examples::base_config().with_model("moa").with_skill("cpp").with_skill("testing");
  autohand::AutohandSdk sdk(config);
  sdk.start();
  sdk.stream_prompt(
      "Review this C++ code using /skill cpp best practices and suggest improvements.",
      [&](const autohand::SdkEvent& event) { examples::print_event(&sdk, event); });
  sdk.stop();
}
