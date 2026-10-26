// SPDX-License-Identifier: BSD-3-Clause
#include "ime_core.hpp"
#include "trusted_inputs.hpp"
#include "input_schemas.hpp"
#include "../third_party/picosha2/picosha2.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace ime {
namespace {
namespace fs = std::filesystem;

void NoSymlink(const fs::path& path) {
  for (auto p = path; !p.empty(); p = p.parent_path()) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(p.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
      throw std::runtime_error("Refusing reparse point in protected data path.");
#endif
    if (fs::is_symlink(fs::symlink_status(p)))
      throw std::runtime_error("Refusing symlink in protected data path.");
    if (p == p.parent_path()) break;
  }
}

std::string Sha256(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Required trusted data file is missing.");
  const auto value = picosha2::hash256_hex_string(
      std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  if (file.bad()) throw std::runtime_error("Cannot read trusted data file.");
  return value;
}

template <size_t N>
void VerifyInputs(const fs::path& root, const policy::Input (&expected)[N]) {
  for (const auto& input : expected) {
    const auto file = root / input.name;
    NoSymlink(file);
    if (!fs::is_regular_file(file) || Sha256(file) != input.sha256)
      throw std::runtime_error("Refusing profile: trusted source or no-learning patch was modified.");
  }
}

void VerifyProfile(const fs::path& shared, const fs::path& user) {
  NoSymlink(shared);
  NoSymlink(user);
  if (shared == user || !fs::is_directory(shared) || !fs::is_directory(user))
    throw std::runtime_error("Use separate, pre-staged shared and user directories.");
  std::set<std::string> shared_names, user_names = {"user.yaml", "installation.yaml", "build"};
  for (const auto& input : policy::kShared) shared_names.insert(input.name);
  for (const auto& input : policy::kUser) user_names.insert(input.name);
  for (const auto& entry : fs::directory_iterator(shared)) {
    if (entry.is_symlink() || !entry.is_regular_file() || !shared_names.count(entry.path().filename().string()))
      throw std::runtime_error("Refusing unexpected shared data, configuration, or script.");
  }
  std::set<std::string> build_names = {"default.yaml"};
  for (const char* id : kDeployedSchemas)
    for (const char* suffix : {".schema.yaml", ".table.bin", ".prism.bin", ".reverse.bin"})
      build_names.insert(std::string(id) + suffix);
  for (const auto& entry : fs::directory_iterator(user)) {
    const auto name = entry.path().filename().string();
    if (entry.is_symlink() || !user_names.count(name))
      throw std::runtime_error("Refusing unexpected user schema, script, or saved dictionary.");
    if (name == "build") {
      if (!entry.is_directory()) throw std::runtime_error("Invalid compiled data directory.");
      for (const auto& cache : fs::directory_iterator(entry.path()))
        if (cache.is_symlink() || !cache.is_regular_file() || !build_names.count(cache.path().filename().string()))
          throw std::runtime_error("Refusing unexpected compiled data or script.");
    } else if (!entry.is_regular_file()) {
      throw std::runtime_error("Invalid user configuration file type.");
    }
  }
  VerifyInputs(shared, policy::kShared);
  VerifyInputs(user, policy::kUser);
}

bool StringEquals(RimeApi* api, RimeConfig* c, const std::string& path, const std::string& value) {
  const char* actual = api->config_get_cstring(c, path.c_str());
  return actual && value == actual;
}

bool ListEquals(RimeApi* api, RimeConfig* c, const std::string& path,
                const std::vector<std::string>& values) {
  if (api->config_list_size(c, path.c_str()) != values.size()) return false;
  for (size_t i = 0; i < values.size(); ++i)
    if (!StringEquals(api, c, path + "/@" + std::to_string(i), values[i])) return false;
  return true;
}

bool OnlyKeys(RimeApi* api, RimeConfig* c, const std::string& path,
              const std::set<std::string>& allowed) {
  RimeConfigIterator it{};
  if (!api->config_begin_map(&it, c, path.c_str())) return false;
  bool valid = true;
  while (api->config_next(&it))
    if (!it.key || !allowed.count(it.key)) valid = false;
  api->config_end(&it);
  return valid;
}

bool SafeKeyBindings(RimeApi* api, RimeConfig* c) {
  const auto count = api->config_list_size(c, "key_binder/bindings");
  for (size_t i = 0; i < count; ++i) {
    const std::string path = "key_binder/bindings/@" + std::to_string(i) + "/select";
    if (const char* target = api->config_get_cstring(c, path.c_str())) {
      const std::string id = target;
      if (id != ".next" && !ApprovedSchema(id)) return false;
    }
  }
  return true;
}
}  // namespace

Engine::Engine(const std::string& shared_dir, const std::string& user_dir)
    : shared_dir_(fs::absolute(fs::u8path(shared_dir)).lexically_normal().u8string()),
      user_dir_(fs::absolute(fs::u8path(user_dir)).lexically_normal().u8string()) {
  // Reject imported executable configuration before Rime maintenance can
  // write metadata, upgrade user dictionaries, or construct any session.
  VerifyProfile(fs::u8path(shared_dir_), fs::u8path(user_dir_));
  api_ = rime_get_api();
  RimeTraits traits{};
  RIME_STRUCT_INIT(RimeTraits, traits);
  traits.shared_data_dir = shared_dir_.c_str();
  traits.user_data_dir = user_dir_.c_str();
  traits.app_name = "rime.yanvy";
  traits.distribution_name = "YANVY Input";
  traits.distribution_code_name = "yanvy";
  traits.distribution_version = "0.1.0";
  // No file logs; only fatal engine diagnostics may reach stderr.
  traits.min_log_level = 3;
  traits.log_dir = "";
  const char* modules[] = {"default", nullptr};
  traits.modules = modules;
  api_->setup(&traits);
  api_->initialize(&traits);
  initialized_ = true;
  try {
    // Windows maintenance can rebuild a modified cache from safe sources.
    // Inspect an existing cache before maintenance as well, so tampering is
    // consistently rejected instead of silently repaired on one platform.
    if (fs::is_directory(fs::u8path(user_dir_) / "build")) {
      CheckDefaultPolicy();
      for (const char* id : kDeployedSchemas) CheckPrivacy(id);
    }
    if (api_->start_maintenance(True)) api_->join_maintenance_thread();
    CheckDefaultPolicy();
    // Validate before create_session(): an unsafe schema must never get a
    // chance to instantiate a user dictionary, even for the first session.
    for (const char* id : kDeployedSchemas) CheckPrivacy(id);
    session_ = api_->create_session();
    if (!session_) throw std::runtime_error("Rime session creation failed.");
    SelectSchema("wubi86");
  } catch (...) {
    if (session_) api_->destroy_session(session_);
    api_->finalize();
    initialized_ = false;
    throw;
  }
}

Engine::~Engine() {
  if (session_) api_->destroy_session(session_);
  if (initialized_) api_->finalize();
}

void Engine::CheckPrivacy(const std::string& id) {
  RimeConfig config{};
  if (!api_->schema_open(id.c_str(), &config))
    throw std::runtime_error("Required schema did not deploy.");
  const auto disabled = [&](const char* key) {
    Bool value = True;
    return api_->config_get_bool(&config, key, &value) && !value;
  };
  const std::vector<std::string> processors = {"ascii_composer", "recognizer", "key_binder", "speller", "punctuator", "selector", "navigator", "express_editor"};
  const std::vector<std::string> segmentors = {"ascii_segmentor", "matcher", "abc_segmentor", "punct_segmentor", "fallback_segmentor"};
  const bool pinyin=id=="pinyin_simp"||IsDoublePinyin(id);
  const std::vector<std::string> translators = pinyin
      ? std::vector<std::string>{"punct_translator", "script_translator", "reverse_lookup_translator"}
      : id == "wubi86" ? std::vector<std::string>{"punct_translator", "reverse_lookup_translator", "table_translator"}
      : std::vector<std::string>{"punct_translator", "table_translator"};
  const std::vector<std::string> dependencies = id == "wubi86" ? std::vector<std::string>{"pinyin_simp"}
      : pinyin ? std::vector<std::string>{"stroke"} : std::vector<std::string>{};
  const std::string reverse_dictionary = id == "wubi86" ? "pinyin_simp" : "stroke";
  const bool ok = disabled("translator/enable_user_dict") &&
      disabled("translator/enable_encoder") &&
      disabled("translator/encode_commit_history") &&
      OnlyKeys(api_, &config, "", {"__build_info", "abc_segmentor", "engine", "key_binder", "menu", "punctuator", "recognizer", "reverse_lookup", "schema", "speller", "switches", "translator"}) &&
      OnlyKeys(api_, &config, "engine", {"processors", "segmentors", "translators", "filters"}) &&
      ListEquals(api_, &config, "engine/processors", processors) &&
      ListEquals(api_, &config, "engine/segmentors", segmentors) &&
      ListEquals(api_, &config, "engine/translators", translators) &&
      ListEquals(api_, &config, "engine/filters", {}) &&
      ListEquals(api_, &config, "schema/dependencies", dependencies) &&
      StringEquals(api_, &config, "schema/schema_id", id) &&
      StringEquals(api_, &config, "translator/dictionary", IsDoublePinyin(id)?"pinyin_simp":id) &&
      StringEquals(api_, &config, "reverse_lookup/dictionary", reverse_dictionary) &&
      SafeKeyBindings(api_, &config);
  api_->config_close(&config);
  if (!ok) throw std::runtime_error("Refusing schema: approved components or no-learning policy differ.");
}

void Engine::CheckDefaultPolicy() {
  RimeConfig config{};
  if (!api_->config_open("default", &config)) throw std::runtime_error("Missing default configuration.");
  Bool fixed_order = False;
  bool schemas_ok=api_->config_list_size(&config,"schema_list")==kSchemaCount;
  for(uint32_t index=0;index<kSchemaCount;++index){
    auto path="schema_list/@"+std::to_string(index);
    schemas_ok=schemas_ok&&OnlyKeys(api_,&config,path,{"schema"})&&StringEquals(api_,&config,path+"/schema",kSchemaIds[index]);
  }
  const bool ok = schemas_ok &&
      ListEquals(api_, &config, "switcher/save_options", {}) &&
      api_->config_get_bool(&config, "switcher/fix_schema_list_order", &fixed_order) && fixed_order &&
      OnlyKeys(api_, &config, "", {"__build_info", "ascii_composer", "config_version", "key_binder", "menu", "punctuator", "recognizer", "schema_list", "selector", "switcher"}) &&
      SafeKeyBindings(api_, &config);
  api_->config_close(&config);
  if (!ok) throw std::runtime_error("Refusing default configuration: approved schema list or startup policy differ.");
}

void Engine::SelectSchema(const std::string& id) {
  if (!ApprovedSchema(id))
    throw std::runtime_error("Schema is not in the approved offline set.");
  CheckDefaultPolicy();
  CheckPrivacy(id);
  // Cancel before switching so composition is never committed accidentally.
  api_->clear_composition(session_);
  if (!api_->select_schema(session_, id.c_str()))
    throw std::runtime_error("Schema selection failed.");
  api_->set_option(session_, "ascii_mode", False);
}

Snapshot Engine::State() {
  Snapshot result;
  char schema[128]{};
  api_->get_current_schema(session_, schema, sizeof(schema));
  result.schema = schema;
  if (const char* input = api_->get_input(session_)) result.input = input;
  RimeContext context{};
  RIME_STRUCT_INIT(RimeContext, context);
  if (api_->get_context(session_, &context)) {
    if (context.composition.preedit) result.preedit = context.composition.preedit;
    result.page = context.menu.page_no;
    result.highlighted = context.menu.highlighted_candidate_index;
    result.last_page = context.menu.is_last_page;
    result.cursor = context.composition.cursor_pos;
    for (int i = 0; i < context.menu.num_candidates; ++i)
      result.candidates.emplace_back(context.menu.candidates[i].text);
    api_->free_context(&context);
  }
  RimeStatus status{};
  RIME_STRUCT_INIT(RimeStatus, status);
  if (api_->get_status(session_, &status)) {
    result.ascii_mode = status.is_ascii_mode;
    api_->free_status(&status);
  }
  RimeCommit commit{};
  RIME_STRUCT_INIT(RimeCommit, commit);
  if (api_->get_commit(session_, &commit)) {
    if (commit.text) result.commit = commit.text;
    api_->free_commit(&commit);
  }
  return result;
}

Snapshot Engine::Key(int keycode, int modifiers) {
  const bool handled = api_->process_key(session_, keycode, modifiers);
  auto state = State();
  state.handled = handled;
  return state;
}

Snapshot Engine::TypeAscii(const std::string& keys) {
  if (keys.size() > 4096) throw std::runtime_error("Input batch is too long.");
  for (unsigned char c : keys)
    if (c < 32 || c > 126) throw std::runtime_error("keys requires printable ASCII.");
  std::string commits;
  auto state = State();
  for (unsigned char c : keys) {
    state = Key(c);
    commits += state.commit;
  }
  state.commit = commits;
  return state;
}

Snapshot Engine::Clear() {
  api_->clear_composition(session_);
  return State();
}

const char* Engine::Version() const { return api_->get_version(); }
}  // namespace ime
