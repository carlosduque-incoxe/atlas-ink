#pragma once

#include <PersistableStore.h>

#include <mutex>
#include <optional>
#include <string>

struct AtlasConfig {
  std::string url;
  std::string token;
};

enum class AtlasConfigStatus : uint8_t {
  NotLoaded,
  Ok,
  Missing,
  InvalidJson,
  InvalidUrl,
  InvalidToken,
};

class AtlasConfigStore : public PersistableStore<AtlasConfigStore> {
 private:
  std::string url;
  std::string token;
  AtlasConfigStatus status = AtlasConfigStatus::NotLoaded;
  mutable std::mutex configMutex;

  AtlasConfigStore() = default;

  friend class PersistableStore<AtlasConfigStore>;

 public:
  static constexpr const char* DEFAULT_FEED_URL = "http://10.10.1.111:3456/api/v2/atlas-ink/feed";
  static const char* getFilePath() { return "/.crosspoint/atlas.json"; }

  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool load();
  std::optional<AtlasConfig> getConfig() const;
  AtlasConfigStatus getStatus() const;
};

#define ATLAS_CONFIG AtlasConfigStore::getInstance()
