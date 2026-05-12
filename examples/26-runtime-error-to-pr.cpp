#include "example_common.hpp"

#include <cstdlib>
#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Customer {
  std::string loyalty_tier;
};

struct Cart {
  double subtotal;
  const Customer* customer;
};

double checkout_discount(const Cart& cart) {
  try {
    if (cart.customer == nullptr) {
      throw std::runtime_error("customer is null");
    }
    return cart.customer->loyalty_tier == "gold" ? cart.subtotal * 0.15 : cart.subtotal * 0.05;
  } catch (const std::exception& error) {
    throw std::runtime_error(std::string("checkout discount failed: ") + error.what());
  }
}

std::string capture_runtime_error() {
  try {
    (void)checkout_discount(Cart{129.0, nullptr});
  } catch (const std::exception& error) {
    std::ostringstream report;
    report << "RuntimeError: " << error.what() << "\n"
           << "    at checkout::discounts::checkout_discount (src/checkout/discounts.cpp:42)\n"
           << "    at checkout::session::create_checkout_session (src/checkout/session.cpp:88)\n"
           << "Request: POST /checkout\n"
           << R"(Payload: {"subtotal":129,"customer":null})";
    return report.str();
  }

  return "RuntimeError: checkout discount failed when customer was null";
}

}  // namespace

int main() {
  const char* target_repo = std::getenv("AUTOHAND_TARGET_REPO");
  const std::string captured_error = capture_runtime_error();
  const std::string prompt =
      "You are a QA engineering agent that turns production error reports into small repair pull requests.\n"
      "Reproduce the failure when the repository makes that possible.\n"
      "Fix the root cause, add or update a focused regression test, run the relevant validation command, commit the fix, push a branch, and create a pull request.\n"
      "Keep the pull request description concise and include the error signature, the fix summary, and the validation result.\n\n"
      "A runtime error was captured by the application error boundary.\n\n"
      "Captured error:\n```text\n" +
      captured_error +
      "\n```\n\n"
      "Expected user impact:\n"
      "A checkout session should still calculate a safe default discount when the customer object is missing.\n\n"
      "Please create a pull request with the fix.";

  std::cout << "=== 26 Runtime Error to Pull Request ===\n\n";
  auto config = autohand::Config::from_environment()
                    .with_cwd(target_repo != nullptr && std::string(target_repo).size() > 0 ? target_repo : ".")
                    .with_instructions(
                        "You are a QA engineering agent that turns production error reports into small repair pull requests.\n"
                        "Reproduce the failure when the repository makes that possible.\n"
                        "Fix the root cause, add or update a focused regression test, run the relevant validation command, commit the fix, push a branch, and create a pull request.\n"
                        "Keep the pull request description concise and include the error signature, the fix summary, and the validation result.");
  autohand::Agent agent(config);
  auto run = agent.send(prompt);
  run.stream([](const autohand::SdkEvent& event) {
    examples::print_event(nullptr, event);
  });
  const auto result = run.wait();
  std::cout << "\n\nRun " << result.id << " " << result.status << ".\n";
  agent.close();
  return 0;
}
