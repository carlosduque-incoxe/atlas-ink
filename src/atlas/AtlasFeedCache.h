#pragma once

#include <cstddef>

#include "AtlasFeedJsonParser.h"

class AtlasFeedCache {
 public:
  static constexpr const char* CACHE_PATH = "/.crosspoint/atlas-feed.json";
  static constexpr const char* TEMP_PATH = "/.crosspoint/atlas-feed.json.tmp";
  static constexpr const char* BACKUP_PATH = "/.crosspoint/atlas-feed.json.bak";

  static bool load(atlas_feed::AtlasFeedJsonParser& parser);
  static bool saveValidatedRaw(const char* data, size_t len);

 private:
  static void recoverInterruptedWrite();
};
