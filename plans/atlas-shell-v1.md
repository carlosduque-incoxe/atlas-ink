# Atlas Shell v1 — full-device vertical slice

## Product correction
The whole XTEINK X4 belongs to Atlas Ink. Do not add Atlas as a module inside an ebook-oriented shell. Home, menus, sleep/lock, navigation and data hierarchy are Atlas-owned. Existing reader code may remain reachable as a secondary capability but must not shape the primary UX.

## Stable base and safety
- Stable hardware-validated rollback: Atlas Ink 1.5.9.
- Work only on `feat/atlas-ink-next`.
- No automatic/background network, boot/partition/OTA changes, credential-format changes or destructive storage migration.
- Current compact task feed/cache remains the only live data source in this slice.

## First vertical slice (no placeholders)

### 1. Replace ebook home with Atlas Home
Refactor `HomeActivity` into the Atlas-owned home surface. The class may retain its name to minimize routing risk, but its behavior/layout must no longer be driven by recent books or cover buffers.

Home must load the validated Atlas feed cache without network and render:
- Atlas identity/header;
- freshness (`DD/MM HH:MM` from cached `generated_at`, explicitly labelled as last update, never fake live clock);
- critical summary count;
- up to two most important task titles, priority >=4 as black cards with white text;
- a compact action menu containing only working destinations in this slice: **Tareas**, **Biblioteca**, **Transferencia**, **Ajustes**.

Do not add Hoy/Agentes/Estado/Consulta buttons until their data and destinations work. The architecture/menu enum should make later insertion straightforward.

Input contract:
- Up/Down and vertical swipe move among action rows.
- Confirm/tap opens the selected working destination.
- Back does not silently open a recent ebook; on Home it is inert or returns selection to Tareas.
- Tareas opens the existing physically validated `AtlasActivity`.
- Biblioteca opens the existing file browser; reading stays secondary.

Remove Home's recent-cover loading, cover framebuffer cache and dynamic recent-book menu from the hot path. Do not delete reader/storage code in this slice.

### 2. Atlas task presentation
Improve `AtlasActivity` without changing refresh/auth/cache worker semantics:
- at most three task cards per page;
- priority >=4 solid black/white text; priority 3 white with black rail;
- selected marker visible on both backgrounds;
- readable cached/updated timestamp;
- no agent-space reservation when `agentCount == 0`;
- task position and navigation retained for buttons/touch.

### 3. Replace default sleep/lock with offline Atlas glance
Add an Atlas sleep renderer in `SleepActivity` using only validated `AtlasFeedCache`:
- Atlas logo/identity;
- last update timestamp, clearly labelled;
- highest-priority cached task, black if critical;
- cached/offline marker;
- fallback to current Atlas logo sleep screen when cache is absent or invalid.

No Wi-Fi, HTTP, token/config load or writes from sleep rendering. Use one HALF refresh as existing sleep behavior. Do not claim a live clock on X4 base; it has no dedicated RTC.

For this first slice, route the normal default sleep path to Atlas glance. Preserve quick-resume/reader-specific paths only if changing them creates a larger wake/resume risk; document the remaining legacy path. A later release may remove all legacy sleep modes after physical wake/sleep QA.

### 4. Pure formatting module and tests
Create a small pure `lib/Atlas/AtlasDashboardFormat.*` if useful. Test:
- canonical and malformed generated_at formatting;
- page ranges for 0..5 tasks;
- priority threshold exactly 4;
- fixed buffers always terminated;
- sleep-card selection is deterministic and respects feed order/priority.

### 5. i18n and docs
Use i18n for all visible labels. Update English and Spanish; generated language handling must stay buildable. Update `ATLAS_INK_PRODUCT.md` checkboxes only for actually completed behavior.

## Resource constraints
- ESP32-C3, no PSRAM, ~380KB usable RAM, single 48KB framebuffer.
- No stack local >256 bytes.
- Use `nothrow` for fallible allocations.
- Avoid `std::vector`/`std::string` construction in repeated hot loops where fixed buffers suffice.
- Home should reduce RAM by removing recent-cover caching rather than increase it.

## Verification gates
1. Format and `git diff --check`.
2. Native tests including new pure helpers.
3. Clean isolated PlatformIO `gh_release` build; record platform/compiler, RAM, flash and image bytes.
4. Inspect binary metadata/version.
5. Independent review: input routing, cache-only sleep, no network on home/sleep, heap/stack and reader fallback.
6. Do not publish until Carlos sees a visual candidate or explicit hardware acceptance plan.
7. Physical X4 gates: boot to Atlas Home; buttons/touch; Tareas; Biblioteca; Transferencia; Ajustes; sleep image; wake; cached tasks without network; 30/60/120 s; rollback.
