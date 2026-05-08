#include <autohand/sdk.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::filesystem::path make_fake_cli() {
  auto dir = std::filesystem::temp_directory_path() /
             ("autohand-cpp-sdk-test-" + std::to_string(::getpid()));
  std::filesystem::create_directories(dir);
  auto cli = dir / "fake-autohand";
  std::ofstream out(cli);
  out << R"(#!/bin/sh
while IFS= read -r line; do
  id=$(printf '%s\n' "$line" | sed -n 's/.*"id":\([0-9][0-9]*\).*/\1/p')
  case "$line" in
    *autohand.prompt*)
      prompt_id="$id"
      printf '%s\n' '{"jsonrpc":"2.0","method":"autohand.permissionRequest","params":{"type":"permission_request","requestId":"perm-1","tool":"bash","description":"list files"}}'
      IFS= read -r permission_line || exit 1
      permission_id=$(printf '%s\n' "$permission_line" | sed -n 's/.*"id":\([0-9][0-9]*\).*/\1/p')
      printf '{"jsonrpc":"2.0","id":%s,"result":{"ok":true}}\n' "$permission_id"
      printf '%s\n' '{"jsonrpc":"2.0","method":"autohand.messageUpdate","params":{"type":"message_update","delta":"hello"}}'
      printf '%s\n' '{"jsonrpc":"2.0","method":"autohand.messageEnd","params":{"type":"message_end","content":"hello"}}'
      printf '{"jsonrpc":"2.0","id":%s,"result":{"ok":true}}\n' "$prompt_id"
      ;;
    *)
      printf '{"jsonrpc":"2.0","id":%s,"result":{"ok":true}}\n' "$id"
      ;;
  esac
done
)";
  out.close();
  chmod(cli.c_str(), 0755);
  return cli;
}

}  // namespace

int main() {
  assert(autohand::parse_json_text(R"({"ok":true})") == R"({"ok":true})");
  assert(autohand::parse_json_text("```json\n{\"ok\":true}\n```") == R"({"ok":true})");
  assert(autohand::parse_json_text("Result: {\"ok\":true} done.") == R"({"ok":true})");

  auto config = autohand::Config::from_environment()
                    .with_cli_path(make_fake_cli().string())
                    .with_skill("cpp")
                    .with_model("fantail2");
  auto args = config.cli_args();
  assert(!args.empty());

  autohand::AutohandSdk sdk(config);
  sdk.start();
  std::string text;
  bool answered_permission = false;
  sdk.stream_prompt("hello", [&](const autohand::SdkEvent& event) {
    if (event.type == "permission_request") {
      (void)sdk.permission_response(event.request_id(), "allow_once");
      answered_permission = true;
    }
    if (event.type == "message_update") {
      text += event.text_delta();
    }
  });
  sdk.stop();
  assert(text == "hello");
  assert(answered_permission);

  std::cout << "autohand_sdk_tests passed\n";
}
