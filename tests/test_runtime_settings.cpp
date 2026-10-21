#include <algorithm>
#include <autohand/sdk.hpp>
#include <cassert>
#include <iostream>

int main() {
 {autohand::Config config; auto args=config.cli_args(); assert(std::find(args.begin(),args.end(),"--ephemeral")==args.end());
 config.ephemeral=true; args=config.cli_args(); assert(std::find(args.begin(),args.end(),"--ephemeral")!=args.end());}

 { autohand::Config legacy; legacy.cli_path="/does-not-exist"; legacy.auto_save_interval=30; bool rejected=false;
   try {autohand::AutohandSdk sdk(legacy); sdk.start();} catch(const autohand::SdkError& error) {rejected=std::string(error.what()).find("unsupported by the current CLI")!=std::string::npos;}
   assert(rejected);
 }

 for (const auto& provider : {"anthropic", "openai", "xai", "azure", "bedrock", "vertexai", "custom:My.Provider", "extension:team.provider:model"}) {
  autohand::Config config;
  config.provider = provider; config.api_key = "secret"; config.base_url = "https://fixture.test";
  const auto settings = config.run_settings_json(); assert(settings);
  const auto& json = *settings;
  assert(json.find(provider == std::string("vertexai") ? "\"authToken\":\"secret\"" : "\"apiKey\":\"secret\"") != std::string::npos);
  assert(json.find(provider == std::string("vertexai") ? "\"endpoint\":\"fixture.test\"" : provider == std::string("bedrock") ? "\"endpoint\":\"https://fixture.test\"" : "\"baseUrl\":\"https://fixture.test\"") != std::string::npos);
  if (provider == std::string("openai") || provider == std::string("xai")) assert(json.find("\"authMode\":\"api-key\"") != std::string::npos);
  if (provider == std::string("azure")) assert(json.find("\"authMethod\":\"api-key\"") != std::string::npos);
  if (provider == std::string("bedrock")) assert(json.find("\"authMode\":\"bedrock-api-key\"") != std::string::npos);
  for (const auto& argument : config.cli_args()) assert(argument.find("secret") == std::string::npos);
 }
 autohand::Config saved; saved.provider="autohandai";
 assert(!saved.run_settings_json());
 saved.feature_settings_json="{\"slashGoal\":false}";
 saved.run_settings="{\"customProviders\":{\"My.Provider\":{\"models\":[null,\"one\"]}}}";
 assert(saved.run_settings_json()->find("\"slashGoal\":false") != std::string::npos);
 assert(saved.run_settings_json()->find("My.Provider") != std::string::npos);
 assert(saved.run_settings_json()->find("cloud") == std::string::npos);
 for (int kind=0; kind<3; ++kind) {
  autohand::Config invalid;
  if(kind==0) invalid.api_key="secret";
  if(kind==1) { invalid.provider="vertexai"; invalid.base_url="https://user:secret@fixture.test/v1"; }
  if(kind==2) invalid.run_settings="{\"features\":{\"constructor\":true}}";
  bool rejected=false; try { (void)invalid.run_settings_json(); } catch(const autohand::SdkError&) { rejected=true; }
  assert(rejected);
 }
 autohand::Config resume; resume.session_id="saved"; resume.persist_session=true;
 assert((resume.cli_args() == std::vector<std::string>{"--mode","rpc","--sdk-session-reference","saved"}));
 autohand::Config latest; latest.continue_session=true;
 assert((latest.cli_args() == std::vector<std::string>{"--mode","rpc","--sdk-resume-last"}));
 std::cout << "Provider startup settings passed\n";
}
