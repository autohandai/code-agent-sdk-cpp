#include "example_common.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct GitHubCredentials {
  std::string token_env_name;
  std::string remote;
  std::string base_branch;
  std::string repository;
};

std::string getenv_or_default(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  return value != nullptr && std::string(value).size() > 0 ? std::string(value) : fallback;
}

GitHubCredentials github_credentials_from_env() {
  std::string token_env_name;
  if (const char* token = std::getenv("GITHUB_TOKEN"); token != nullptr && std::string(token).size() > 0) {
    token_env_name = "GITHUB_TOKEN";
  } else if (const char* token = std::getenv("GH_TOKEN"); token != nullptr && std::string(token).size() > 0) {
    token_env_name = "GH_TOKEN";
  } else {
    throw std::runtime_error("Set GITHUB_TOKEN or GH_TOKEN before running this example.");
  }

  return GitHubCredentials{
      token_env_name,
      getenv_or_default("AUTOHAND_GITHUB_REMOTE", "origin"),
      getenv_or_default("AUTOHAND_GITHUB_BASE_BRANCH", "main"),
      getenv_or_default("GITHUB_REPOSITORY", ""),
  };
}

std::string incident_packet_json() {
  return R"JSON({
  "id": "INC-2026-05-12-0417",
  "severity": "sev2",
  "service": "checkout-api",
  "first_seen": "2026-05-12T09:14:22Z",
  "release": "checkout-api@2026.05.12.3",
  "error_signature": "RuntimeError: checkout discount failed while replaying coupon idempotency key",
  "user_impact": "Checkout returns HTTP 500 for guest customers using coupon replay from mobile clients.",
  "stack_trace": "RuntimeError: checkout discount failed while replaying coupon idempotency key\n    at checkout::discounts::calculate_discount (src/checkout/discounts.cpp:42)\n    at checkout::payments::build_payment_intent (src/checkout/payment_intent.cpp:118)\n    at checkout::session::create_checkout_session (src/checkout/session.cpp:88)",
  "logs": [
    "level=error trace=trk_94 request_id=req_7f2 route=POST /checkout status=500 duration_ms=184",
    "level=warn trace=trk_94 idempotency_key=checkout:cart_live_9834:attempt_2 cache_status=miss",
    "level=info trace=trk_94 feature_flags=discount-v2,coupon-replay"
  ],
  "request": {
    "method": "POST",
    "path": "/checkout",
    "payload": {"cartId":"cart_live_9834","subtotal":129,"customer":null,"coupon":{"code":"SPRING25","source":"mobile-v5"},"idempotencyKey":"checkout:cart_live_9834:attempt_2"}
  },
  "suspected_files": [
    "src/checkout/discounts.cpp",
    "src/checkout/payment_intent.cpp",
    "src/checkout/session.cpp",
    "tests/checkout_session_test.cpp"
  ],
  "reproduction_command": "ctest -R guest_coupon_replay --output-on-failure",
  "validation_commands": [
    "ctest -R guest_coupon_replay --output-on-failure",
    "cmake --build build",
    "ctest --test-dir build --output-on-failure"
  ]
})JSON";
}

std::string build_prompt(const GitHubCredentials& github) {
  const std::string repo_hint = github.repository.empty()
                                    ? "- Discover the GitHub repository from git remote output."
                                    : "- GitHub repository hint: " + github.repository + ".";
  std::ostringstream prompt;
  prompt << "You are a senior QA engineering agent responsible for converting production incidents into verified repair pull requests.\n\n"
         << "GitHub credentials:\n"
         << "- A GitHub token is available in the " << github.token_env_name
         << " environment variable. Do not print or commit the token.\n"
         << "- Use git remote " << github.remote << ".\n"
         << "- Open the pull request against " << github.base_branch << ".\n"
         << repo_hint << "\n"
         << "- Before pushing, run gh auth status or an equivalent non-secret auth check.\n\n"
         << "Incident packet:\n```json\n"
         << incident_packet_json()
         << "\n```\n\n"
         << "Required workflow:\n"
         << "1. Inspect the target repository and confirm the likely failing path.\n"
         << "2. Reproduce the incident using the provided payload or nearest existing test harness.\n"
         << "3. Fix the root cause, not just the thrown exception.\n"
         << "4. Add a regression test covering guest checkout, coupon replay, and idempotency behavior.\n"
         << "5. Run the focused test first, then the relevant validation commands.\n"
         << "6. Create a branch named autohand/fix-checkout-incident-inc-2026-05-12-0417.\n"
         << "7. Commit the fix with a clear message.\n"
         << "8. Push the branch and open a pull request.\n"
         << "9. In the PR body, include the incident id, error signature, files changed, tests run, and any residual risk.";
  return prompt.str();
}

}  // namespace

int main() {
  const auto github = github_credentials_from_env();
  const auto target_repo = getenv_or_default("AUTOHAND_TARGET_REPO", ".");

  auto config = autohand::Config::from_environment()
                    .with_cwd(target_repo)
                    .with_instructions("Work like a careful senior QA engineer. Keep secrets out of logs and pull request text.");
  autohand::Agent agent(config);
  auto run = agent.send(build_prompt(github));
  run.stream([](const autohand::SdkEvent& event) {
    examples::print_event(nullptr, event);
  });
  const auto result = run.wait();
  std::cout << "\n\nRun " << result.id << " " << result.status << ".\n";
  agent.close();
  return 0;
}
