#include "SleepImagePickerActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SleepImagePreviewActivity.h"
#include "components/UITheme.h"
#include "util/SleepImagePick.h"
#include "util/SleepThumbCache.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_TILE = 1;  // value: tile index
constexpr int kColumns = 3;
constexpr int kGap = 16;  // between tiles, and between the grid and the rules
constexpr int kFooterPadding = 18;
constexpr int kTileTextInset = 8;
constexpr int kFrameKeyline = 3;
constexpr size_t kNameBytes = 256;

const char* baseName(const std::string& path) { return path.c_str() + path.rfind('/') + 1; }

bool cropMode() { return SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP; }
}  // namespace

SleepImagePickerActivity::SleepImagePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("SleepImagePicker", renderer, mappedInput), UiAppHost(renderer) {}

void SleepImagePickerActivity::onEnter() {
  Activity::onEnter();
  layout();
  scan();
  chosen = findChosen();
  focus = chosen;
  page = chosen / geo.perPage;
  resetUi();
  app.on(ACTION_TILE, &SleepImagePickerActivity::onTile, this);
  app.setScreen(&SleepImagePickerActivity::screenFn, this);
  entryRefresh = true;
  requestUpdate();
}

void SleepImagePickerActivity::layout() {
  using namespace setka;
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Button boards keep the bottom band for the front-button hints.
  const Rect content = UITheme::getInstance().getScreenSafeArea(renderer, !mappedInput.hasTouch());
  const int width = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(kText12Font);
  geo.gridTop = metrics.topPadding + metrics.headerHeight + kGap;
  // Portrait tiles in the screen's own aspect: a thumbnail is the whole sleep screen.
  geo.tileW = (width - 2 * kSidePadding - (kColumns - 1) * kGap) / kColumns;
  geo.tileH = geo.tileW * renderer.getScreenHeight() / width;
  const int footerHeight = lineHeight + 2 * kFooterPadding;
  geo.footerRuleY = content.y + content.height - footerHeight;
  geo.footerTextTop = geo.footerRuleY + kRuleWidth + (footerHeight - kRuleWidth - lineHeight) / 2;
  const int rows = std::max(1, (geo.footerRuleY - geo.gridTop) / (geo.tileH + kGap));
  geo.perPage = rows * kColumns;
}

void SleepImagePickerActivity::scan() {
  entries.clear();
  entries.reserve(kMaxImages);
  auto name = makeUniqueNoThrow<char[]>(kNameBytes);
  if (!name) {
    LOG_ERR("SIP", "OOM: file name buffer");
    return;
  }
  const bool crop = cropMode();
  for (const auto folder : sleep_pick::kFolders) {
    const std::string dirPath(folder);
    auto dir = Storage.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) continue;
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (entries.size() >= kMaxImages) break;
      if (file.isDirectory()) continue;
      file.getName(name.get(), kNameBytes);
      std::string path = dirPath + "/" + name.get();
      // The same files the random pick accepts: visible BMPs with valid headers.
      if (!sleep_pick::isChoicePath(path)) continue;
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() != BmpReaderError::Ok) continue;
      Entry entry;
      entry.key = sleep_pick::thumbKey(path, static_cast<uint32_t>(file.fileSize()), file.modificationTime(), geo.tileW,
                                       geo.tileH, crop);
      entry.path = std::move(path);
      entries.push_back(std::move(entry));
    }
  }
  if (entries.size() >= kMaxImages) LOG_INF("SIP", "Showing the first %u sleep images", unsigned(kMaxImages));
  // Sort by file name through an index, building each name once.
  std::vector<std::string> names;
  names.reserve(entries.size());
  for (const auto& entry : entries) names.emplace_back(baseName(entry.path));
  std::vector<uint16_t> order(entries.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](const uint16_t a, const uint16_t b) {
    if (names[a] != names[b]) return FsHelpers::naturalLess(names[a], names[b]);
    return entries[a].path < entries[b].path;
  });
  std::vector<Entry> sorted;
  sorted.reserve(entries.size());
  for (const auto index : order) sorted.push_back(std::move(entries[index]));
  entries = std::move(sorted);

  std::vector<uint32_t> keys;
  keys.reserve(entries.size());
  for (const auto& entry : entries) keys.push_back(entry.key);
  std::sort(keys.begin(), keys.end());
  sleep_thumbs::prune(keys);
}

int SleepImagePickerActivity::findChosen() const {
  for (size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].path == SETTINGS.sleepImagePath) return static_cast<int>(i) + 1;
  }
  // Nothing chosen, or the chosen file is gone: the sleep screen picks at random.
  return 0;
}

void SleepImagePickerActivity::screenFn(UiScreen& screen, void* user) {
  static_cast<SleepImagePickerActivity*>(user)->buildScreen(screen);
}

void SleepImagePickerActivity::buildScreen(UiScreen& screen) {
  using namespace setka;
  auto& frame = screen.frame();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();

  // The CrossPoint 2.0 header: status line, back arrow (a tap arrives as Back) and title over a rule.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_SLEEP_IMAGE));

  // Touch boards frame the chosen picture; button boards frame the cursor.
  const int marked = mappedInput.hasTouch() ? chosen : focus;
  const int first = page * geo.perPage;
  for (int slot = 0; slot < geo.perPage && first + slot < tileCount(); ++slot) {
    const int tile = first + slot;
    const int x = kSidePadding + (slot % kColumns) * (geo.tileW + kGap);
    const int y = geo.gridTop + (slot / kColumns) * (geo.tileH + kGap);
    drawTile(tile, x, y);
    if (tile == marked) {
      drawCellFrame(renderer, x, y, geo.tileW, geo.tileH);
      // A white keyline inside the frame keeps it visible on a dark picture's edge.
      renderer.drawRect(x + kCellFrameWidth, y + kCellFrameWidth, geo.tileW - 2 * kCellFrameWidth,
                        geo.tileH - 2 * kCellFrameWidth, kFrameKeyline, false);
    }
    frame.hit(fui::makeRect(x, y, geo.tileW, geo.tileH), ACTION_TILE, static_cast<int16_t>(tile), fui::InputTouch);
  }

  drawRule(renderer, 0, geo.footerRuleY, width);
  int nameWidth = width - 2 * kSidePadding;
  if (pageCount() > 1) {
    char pages[16];
    snprintf(pages, sizeof(pages), "%d/%d", page + 1, pageCount());
    const int pagesWidth = renderer.getTextWidth(kText12Font, pages);
    renderer.drawText(kText12Font, width - kSidePadding - pagesWidth, geo.footerTextTop, pages);
    nameWidth -= pagesWidth + kGap;
  }
  const char* name = entries.empty() ? tr(STR_SLEEP_IMAGE_EMPTY)
                     : marked == 0   ? tr(STR_SLEEP_IMAGE_RANDOM)
                                     : baseName(entries[marked - 1].path);
  renderer.drawText(kText12Font, kSidePadding, geo.footerTextTop,
                    renderer.truncatedText(kText12Font, name, nameWidth).c_str());
}

void SleepImagePickerActivity::drawTile(const int tile, const int x, const int y) {
  using namespace setka;
  const int w = geo.tileW;
  const int h = geo.tileH;
  const auto drawLabel = [&](const char* text) {
    const std::string fitted = renderer.truncatedText(kText10Font, text, w - 2 * kTileTextInset);
    renderer.drawText(kText10Font, x + (w - renderer.getTextWidth(kText10Font, fitted.c_str())) / 2,
                      y + (h - renderer.getLineHeight(kText10Font)) / 2, fitted.c_str());
  };
  if (tile == 0) {
    drawLabel(tr(STR_SLEEP_IMAGE_RANDOM));
    renderer.drawRect(x, y, w, h);
    return;
  }

  auto& entry = entries[tile - 1];
  const std::string thumbPath = sleep_thumbs::pathFor(entry.key);
  if (entry.thumb == Thumb::Unknown) {
    entry.thumb = Storage.exists(thumbPath.c_str()) ? Thumb::Ready : Thumb::Missing;
  }
  if (entry.thumb == Thumb::Ready) {
    HalFile file;
    bool drawn = false;
    if (Storage.openFileForRead("SIP", thumbPath, file)) {
      Bitmap bitmap(file);
      drawn = bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() == w && bitmap.getHeight() == h &&
              renderer.drawBitmap(bitmap, x, y, w, h);
    }
    if (!drawn) {
      // A broken cache file: drop it so the next visit regenerates it.
      LOG_ERR("SIP", "Bad thumbnail %s", thumbPath.c_str());
      file.close();
      Storage.remove(thumbPath.c_str());
      entry.thumb = Thumb::Failed;
    }
  }
  if (entry.thumb == Thumb::Missing) renderer.fillRectDither(x, y, w, h, Color::LightGray);
  if (entry.thumb == Thumb::Failed) drawLabel(baseName(entry.path));
  renderer.drawRect(x, y, w, h);
}

bool SleepImagePickerActivity::generateNextThumb() {
  const int first = std::max(page * geo.perPage, 1);
  const int last = std::min(page * geo.perPage + geo.perPage, tileCount());
  for (int tile = first; tile < last; ++tile) {
    auto& entry = entries[tile - 1];
    if (entry.thumb != Thumb::Missing) continue;
    const bool ok = sleep_thumbs::generate(entry.path, entry.key, geo.tileW, geo.tileH, renderer.getScreenWidth(),
                                           renderer.getScreenHeight(), cropMode());
    entry.thumb = ok ? Thumb::Ready : Thumb::Failed;
    return true;
  }
  return false;
}

void SleepImagePickerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), focus == 0 ? tr(STR_SELECT) : tr(STR_OPEN), tr(STR_DIR_LEFT),
                                            tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);  // no-op on touch boards
  renderer.displayBuffer(entryRefresh.exchange(false) ? cadence.entry() : cadence.update());
  // Fill the visible page lazily: one thumbnail per pass, each shown as it lands.
  if (generateNextThumb()) requestUpdate();
}

void SleepImagePickerActivity::onTile(const fui::ActionEvent& event, void* user) {
  static_cast<SleepImagePickerActivity*>(user)->openTile(event.value);
}

void SleepImagePickerActivity::openTile(const int tile) {
  app.clearTapFlash();
  if (tile <= 0) {
    choose(0);
    return;
  }
  if (tile >= tileCount()) return;
  auto preview = makeUniqueNoThrow<SleepImagePreviewActivity>(renderer, mappedInput, entries[tile - 1].path);
  if (!preview) {
    LOG_ERR("SIP", "OOM: SleepImagePreviewActivity");
    return;
  }
  startActivityForResult(std::move(preview), [this, tile](const ActivityResult& result) {
    if (!result.isCancelled) choose(tile);
    entryRefresh = true;  // a clean repaint over the preview's grays
  });
}

void SleepImagePickerActivity::choose(const int tile) {
  const char* path = tile > 0 ? entries[tile - 1].path.c_str() : "";
  strncpy(SETTINGS.sleepImagePath, path, sizeof(SETTINGS.sleepImagePath) - 1);
  SETTINGS.sleepImagePath[sizeof(SETTINGS.sleepImagePath) - 1] = '\0';
  // Any pick here is a sleep picture: modes that show no folder picture switch to Custom.
  using Mode = CrossPointSettings::SLEEP_SCREEN_MODE;
  if (SETTINGS.sleepScreen != Mode::CUSTOM && SETTINGS.sleepScreen != Mode::COVER_CUSTOM) {
    SETTINGS.sleepScreen = Mode::CUSTOM;
  }
  SETTINGS.saveToFile();
  chosen = tile;
  focus = tile;
  requestUpdate();
}

void SleepImagePickerActivity::showPage(const int newPage) {
  if (newPage < 0 || newPage >= pageCount() || newPage == page) return;
  page = newPage;
  if (!mappedInput.hasTouch()) focus = page * geo.perPage;
  requestUpdate();
}

void SleepImagePickerActivity::moveFocus(const int tile) {
  focus = tile;
  page = focus / geo.perPage;
  requestUpdate();
}

void SleepImagePickerActivity::loop() {
  if (routeTouch(mappedInput)) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    app.clearTapFlash();
    finish();
    return;
  }
  if (!mappedInput.hasTouch() && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openTile(focus);
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    showPage(page + (swipe == MappedInputManager::SwipeDir::Up ? 1 : -1));
    return;
  }
  if (mappedInput.hasTouch()) {
    buttonNavigator.onNextPress([this] { showPage(page + 1); });
    buttonNavigator.onPreviousPress([this] { showPage(page - 1); });
    return;
  }
  // Button boards: a press steps the frame (side buttons and Left/Right), a hold jumps a page.
  buttonNavigator.onNextPress([this] { moveFocus(ButtonNavigator::nextIndex(focus, tileCount())); });
  buttonNavigator.onPreviousPress([this] { moveFocus(ButtonNavigator::previousIndex(focus, tileCount())); });
  buttonNavigator.onNextContinuous(
      [this] { moveFocus(ButtonNavigator::nextPageIndex(focus, tileCount(), geo.perPage)); });
  buttonNavigator.onPreviousContinuous(
      [this] { moveFocus(ButtonNavigator::previousPageIndex(focus, tileCount(), geo.perPage)); });
}
