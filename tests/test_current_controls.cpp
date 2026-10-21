#include "autohand/sdk.hpp"
#include <cassert>
#include <filesystem>
#include <unistd.h>

int main() {
 const auto directory = std::filesystem::temp_directory_path() / ("autohand-controls-" + std::to_string(getpid()));
 std::filesystem::create_directories(directory);
 const auto cli = directory / "autohand";
 std::filesystem::copy_file(std::filesystem::path(__FILE__).parent_path() / "fixtures/current-cli-controls.py", cli);
 std::filesystem::permissions(cli, std::filesystem::perms::owner_all);
 autohand::Config config;
 config.cli_path=cli.string(); config.cwd=directory.string();
 autohand::AutohandSdk sdk(config); sdk.start();
 auto check=[](const std::string& result, const std::string& method) { assert(result.find(method) != std::string::npos); };
 auto result=sdk.set_browser_capabilities(2,"1.0.0",{"browser_navigate"});
 check(result,"autohand.browserCapabilities.set"); assert(result.find("browser_navigate") != std::string::npos);
 result=sdk.set_max_thinking_tokens(std::nullopt); check(result,"autohand.maxThinkingTokensSet"); assert(result.find("null") != std::string::npos);
 check(sdk.get_supported_models(),"autohand.getSupportedModels");
 check(sdk.get_context_usage(),"autohand.getContextUsage");
 check(sdk.reload_plugins(),"autohand.reloadPlugins");
 check(sdk.get_account_info(),"autohand.getAccountInfo");
 result=sdk.toggle_mcp_server("files",false);check(result,"autohand.mcp.toggleServer");assert(result.find("false")!=std::string::npos);
 check(sdk.reconnect_mcp_server("files"),"autohand.mcp.reconnectServer");
 check(sdk.set_mcp_servers(R"({"files":{"command":"server","args":[]}})"),"autohand.mcp.setServers");
 sdk.stop();std::filesystem::remove_all(directory);
}
