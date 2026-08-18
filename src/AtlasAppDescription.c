#include <stdint.h>

#include "esp_app_desc.h"
#include "sdkconfig.h"

// The precompiled Arduino-ESP32 archive carries a weak app descriptor that can
// retain metadata from the machine where the archive was prepared. Define the
// release descriptor in the application so OTA metadata is deterministic and
// always agrees with the version shown by Atlas Ink.
const __attribute__((section(".rodata_desc"), used)) esp_app_desc_t esp_app_desc = {
    .magic_word = ESP_APP_DESC_MAGIC_WORD,
    .secure_version = 0,
    .reserv1 = {0, 0},
    .version = CROSSPOINT_VERSION,
    .project_name = "atlas-ink",
    .time = "",
    .date = "",
    .idf_ver = IDF_VER,
    .app_elf_sha256 = {0},
    .min_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MIN_FULL,
    .max_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MAX_FULL,
    .mmu_page_size = 31 - __builtin_clz(CONFIG_MMU_PAGE_SIZE),
    .reserv3 = {0},
};

_Static_assert(sizeof(CROSSPOINT_VERSION) <= sizeof(esp_app_desc.version), "release version exceeds app descriptor");
_Static_assert(sizeof("atlas-ink") <= sizeof(esp_app_desc.project_name), "project name exceeds app descriptor");
