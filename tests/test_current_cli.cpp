#include <autohand/sdk.hpp>

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unistd.h>

namespace {

long request_id(const std::string& line) {
  const auto marker = line.find("\"id\"");
  if (marker == std::string::npos) return 0;
  auto position = line.find(':', marker);
  if (position == std::string::npos) return 0;
  ++position;
  while (position < line.size() &&
         std::isspace(static_cast<unsigned char>(line[position]))) {
    ++position;
  }
  return std::stol(line.substr(position));
}

int run_fixture() {
  const auto* log_path = std::getenv("AUTOHAND_CPP_CURRENT_LOG");
  const auto* configured_result = std::getenv("AUTOHAND_CPP_CURRENT_RESULT");
  const auto* notification = std::getenv("AUTOHAND_CPP_CURRENT_NOTIFICATION");
  std::string line;
  while (std::getline(std::cin, line)) {
    if (log_path) std::ofstream(log_path, std::ios::app) << line << '\n';
    const auto id = request_id(line);
    const auto method = autohand::json_get_string(line, "method");
    if (method == "autohand.prompt" && notification && *notification) {
      std::cout << notification << '\n';
    }
    const auto result = method == "autohand.getState"
                            ? std::string("{}")
                            : std::string(configured_result ? configured_result : "{\"success\":true}");
    std::cout << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":" << result
              << "}\n"
              << std::flush;
  }
  return 0;
}

struct Fixture {
  std::filesystem::path log_path;
  autohand::AutohandSdk sdk;

  Fixture(
      const std::string& executable,
      std::string_view name,
      std::string result,
      std::string notification = {})
      : log_path(std::filesystem::temp_directory_path() /
                 ("autohand-cpp-current-" + std::string(name) + "-" +
                  std::to_string(::getpid()) + ".log")),
        sdk([&] {
          std::filesystem::remove(log_path);
          autohand::Config config;
          config.cli_path = executable;
          config.timeout = std::chrono::seconds(2);
          config.environment["AUTOHAND_CPP_CURRENT_FIXTURE"] = "1";
          config.environment["AUTOHAND_CPP_CURRENT_LOG"] = log_path.string();
          config.environment["AUTOHAND_CPP_CURRENT_RESULT"] = std::move(result);
          config.environment["AUTOHAND_CPP_CURRENT_NOTIFICATION"] = std::move(notification);
          return config;
        }()) {
    sdk.start();
  }

  ~Fixture() {
    sdk.stop();
    std::filesystem::remove(log_path);
  }

  std::string log() const {
    std::ifstream input(log_path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }

  void assert_request(const std::string& method, std::initializer_list<std::string> fragments) const {
    const auto contents = log();
    assert(contents.find("\"method\":\"" + method + "\"") != std::string::npos);
    for (const auto& fragment : fragments) {
      assert(contents.find(fragment) != std::string::npos);
    }
  }
};

void test_permission_acknowledgement(const std::string& executable) {
  Fixture fixture(executable, "permission-ack", R"({"success":true})");
  const auto result = fixture.sdk.acknowledge_permission("permission-1");
  assert(result.success);
  fixture.assert_request(
      "autohand.permissionAcknowledged", {R"("requestId":"permission-1")"});
  bool rejected = false;
  try {
    (void)fixture.sdk.acknowledge_permission("  ");
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

void test_directory_access_response(const std::string& executable) {
  Fixture fixture(executable, "directory-response", R"({"success":true})");
  const auto result = fixture.sdk.respond_to_directory_access(
      autohand::DirectoryAccessResponseParams{"directory-1", true});
  assert(result.success);
  fixture.assert_request(
      "autohand.directoryAccessResponse",
      {R"("requestId":"directory-1")", R"("granted":true)"});
  bool rejected = false;
  try {
    (void)fixture.sdk.respond_to_directory_access({"", false});
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

void test_directory_access_acknowledgement(const std::string& executable) {
  Fixture fixture(executable, "directory-ack", R"({"success":true})");
  const auto result = fixture.sdk.acknowledge_directory_access("directory-2");
  assert(result.success);
  fixture.assert_request(
      "autohand.directoryAccessAcknowledged", {R"("requestId":"directory-2")"});
  bool rejected = false;
  try {
    (void)fixture.sdk.acknowledge_directory_access("\t");
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

void test_multi_file_change_decisions(const std::string& executable) {
  Fixture fixture(
      executable,
      "changes-decision",
      R"({"success":false,"appliedCount":1,"skippedCount":1,"errors":[{"changeId":"change-2","error":"conflict"}]})");
  const auto result = fixture.sdk.decide_changes(
      {"batch-1", autohand::AcceptSelectedChanges{{"change-1", "change-2"}}});
  assert(!result.success);
  assert(result.applied_count == 1);
  assert(result.skipped_count == 1);
  assert(result.errors.size() == 1);
  assert(result.errors.front().change_id == "change-2");
  assert(result.errors.front().error == "conflict");
  fixture.assert_request(
      "autohand.changesDecision",
      {R"("batchId":"batch-1")", R"("action":"accept_selected")",
       R"("selectedChangeIds":["change-1","change-2"])"});

  bool rejected = false;
  try {
    (void)fixture.sdk.decide_changes(
        {"batch-2", autohand::AcceptSelectedChanges{{}}});
  } catch (const autohand::SdkError&) {
    rejected = true;
  }
  assert(rejected);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  if (std::getenv("AUTOHAND_CPP_CURRENT_FIXTURE")) return run_fixture();
  const auto executable = std::filesystem::absolute(argv[0]).string();
  test_permission_acknowledgement(executable);
  test_directory_access_response(executable);
  test_directory_access_acknowledgement(executable);
  test_multi_file_change_decisions(executable);
  return 0;
}
