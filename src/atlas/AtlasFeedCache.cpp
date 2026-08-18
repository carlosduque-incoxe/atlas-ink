#include "AtlasFeedCache.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

namespace {

bool loadPath(const char* path, atlas_feed::AtlasFeedJsonParser& parser) {
  parser.reset();

  HalFile file;
  if (!Storage.openFileForRead("ATLAS", path, file)) return false;

  char buffer[256];
  while (true) {
    const int read = file.read(buffer, sizeof(buffer));
    if (read < 0) {
      LOG_ERR("ATLAS", "Failed reading Atlas cache: %s", path);
      file.close();
      return false;
    }
    if (read == 0) break;
    if (!parser.feed(buffer, static_cast<size_t>(read))) {
      LOG_ERR("ATLAS", "Atlas cache is invalid or too large: %s", path);
      file.close();
      return false;
    }
  }
  file.close();
  return parser.finish();
}

}  // namespace

void AtlasFeedCache::recoverInterruptedWrite() {
  if (!Storage.exists(CACHE_PATH) && Storage.exists(BACKUP_PATH)) {
    if (!Storage.rename(BACKUP_PATH, CACHE_PATH)) {
      LOG_ERR("ATLAS", "Failed to restore Atlas cache backup");
    }
  }
  if (Storage.exists(TEMP_PATH)) {
    Storage.remove(TEMP_PATH);
  }
}

bool AtlasFeedCache::load(atlas_feed::AtlasFeedJsonParser& parser) {
  recoverInterruptedWrite();
  if (loadPath(CACHE_PATH, parser)) return true;

  // A reset during the rename window can leave both files behind. If the main
  // cache is present but corrupt, validate and restore the last known-good copy.
  if (!Storage.exists(BACKUP_PATH) || !loadPath(BACKUP_PATH, parser)) return false;

  if (Storage.exists(CACHE_PATH)) Storage.remove(CACHE_PATH);
  if (!Storage.rename(BACKUP_PATH, CACHE_PATH)) {
    // The already-parsed backup is still usable for this session even when the
    // repair write fails; retain it so a later boot can retry.
    LOG_ERR("ATLAS", "Loaded Atlas backup but failed to restore cache path");
  }
  return true;
}

bool AtlasFeedCache::saveValidatedRaw(const char* data, const size_t len) {
  if (!data || len == 0 || len > atlas_feed::MAX_BODY_BYTES) return false;

  auto checkedFeed = makeUniqueNoThrow<atlas_feed::Feed>();
  if (!checkedFeed) {
    LOG_ERR("ATLAS", "Not enough memory to validate Atlas cache");
    return false;
  }
  atlas_feed::ParseError parseError = atlas_feed::ParseError::None;
  if (!atlas_feed::parseFeedJson(data, len, *checkedFeed, parseError)) {
    LOG_ERR("ATLAS", "Refusing invalid Atlas cache: %s", atlas_feed::AtlasFeedJsonParser::errorName(parseError));
    return false;
  }

  Storage.mkdir("/.crosspoint");
  recoverInterruptedWrite();
  if (Storage.exists(TEMP_PATH)) Storage.remove(TEMP_PATH);

  HalFile file;
  if (!Storage.openFileForWrite("ATLAS", TEMP_PATH, file)) {
    LOG_ERR("ATLAS", "Failed opening Atlas cache temp");
    return false;
  }
  if (file.write(data, len) != len) {
    LOG_ERR("ATLAS", "Failed writing Atlas cache temp");
    file.close();
    Storage.remove(TEMP_PATH);
    return false;
  }
  file.close();

  if (Storage.exists(BACKUP_PATH)) Storage.remove(BACKUP_PATH);
  if (Storage.exists(CACHE_PATH) && !Storage.rename(CACHE_PATH, BACKUP_PATH)) {
    LOG_ERR("ATLAS", "Failed backing up Atlas cache");
    Storage.remove(TEMP_PATH);
    return false;
  }

  if (!Storage.rename(TEMP_PATH, CACHE_PATH)) {
    LOG_ERR("ATLAS", "Failed committing Atlas cache");
    Storage.remove(TEMP_PATH);
    if (Storage.exists(BACKUP_PATH)) {
      Storage.rename(BACKUP_PATH, CACHE_PATH);
    }
    return false;
  }

  if (Storage.exists(BACKUP_PATH)) Storage.remove(BACKUP_PATH);
  return true;
}
