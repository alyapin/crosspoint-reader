#include "ReaderActivity.h"

#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <KOReaderDocumentId.h>
#include <Memory.h>
#include <TrustedTime.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "XtcReaderActivity.h"
#include "stats/ReadingStats.h"
#include "util/PluginEvents.h"

#if CROSSPOINT_BRIGHTNESS_SWIPE
#include <HalFrontlight.h>

#include "BrightnessOverlay.h"
#include "util/SlideSwitch.h"
#endif

namespace {
// After a failed statistics write the card is left alone this long.
constexpr uint32_t STATS_RETRY_MS = 30000;
}  // namespace

ReaderActivity::ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               std::string bookPath, const bool allowFastInitialRefresh)
    : Activity(name, renderer, mappedInput), bookPath(std::move(bookPath)) {
  if (allowFastInitialRefresh) {
    const int refreshFrequency = SETTINGS.getRefreshFrequency();
    pagesUntilFullRefresh = refreshFrequency > 1 ? refreshFrequency : 2;
  }
}

std::unique_ptr<ReaderActivity> ReaderActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string path, const bool allowFastInitialRefresh) {
  // ActivityManager requires heap ownership; each branch allocates exactly one screen-lifetime object.
  std::unique_ptr<ReaderActivity> activity;
  if (FsHelpers::hasXtcExtension(path)) {
    activity = makeUniqueNoThrow<XtcReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else {
    activity = makeUniqueNoThrow<EpubReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  }

  if (!activity) {
    LOG_ERR("READER", "OOM: reader activity");
  }
  return activity;
}

void ReaderActivity::applyInitialOrientation() { ReaderUtils::applyOrientation(renderer, SETTINGS.orientation); }

void ReaderActivity::disableFastInitialRefresh() { pagesUntilFullRefresh = 0; }

void ReaderActivity::notePageTurn(const bool forward, const bool succeeded) {
  RenderLock lock(*this);
  readerSession.noteTurn(forward, succeeded);
  readingTracker.noteTurn(forward, succeeded);
}

void ReaderActivity::onEnter() {
  Activity::onEnter();

  // Heap ledger for field crash reports: free vs largest block distinguishes a
  // leak (free falls) from fragmentation (free stable, largest collapses).
  LOG_INF("MEM", "reader enter: free=%u max_block=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  if (!Storage.exists(bookPath.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", bookPath.c_str());
    finish();
    return;
  }

  // Clear remembered book after opening it
  if (!APP_STATE.openEpubPath.empty()) {
    APP_STATE.openEpubPath.clear();
    APP_STATE.saveToFile();
  }

  sdFontSystem.ensureLoaded(renderer);
  applyInitialOrientation();

  if (!loadBook()) {
    if (!handleLoadFailure()) finish();
    return;
  }

  requestUpdate();
}

void ReaderActivity::rememberBookOnceRendered() {
  if (bookRemembered || !pageRendered.load(std::memory_order_acquire)) return;
  bookRemembered = true;
  APP_STATE.openEpubPath = bookPath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(bookPath, getBookTitle(), getBookAuthor(), getBookThumbBmpPath());
  statsBookId =
      READING_STATS.openBook(bookPath, getBookTitle(), getBookAuthor(), stats::today(), getProgressBasisPoints());
  READING_STATS.compactIfNeeded();
  const pluginevents::Var openVars[] = {{"book", bookPath.c_str()}};
  pluginevents::emit(pluginevents::Event::ReaderOpen, openVars, 1);
}

void ReaderActivity::onExit() {
#if CROSSPOINT_BRIGHTNESS_SWIPE
  // Left mid-drag (Home key, sleep): keep the level the light is at.
  if (brightnessGesture.dragging()) {
    brightnessGesture.cancel();
    finishBrightnessSwipe();
  }
#endif
  Activity::onExit();

  // Keep rebuildable font buffers from pinning the heap between reading sessions.
  if (auto* fontCache = renderer.getFontCacheManager()) {
    fontCache->releaseSdFontCaches();
  }

  LOG_INF("MEM", "reader exit: free=%u max_block=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  // Flush BEFORE the ReaderExit event: the session's final progress must be
  // durable before a subscriber can act on the exit notification.
  flushReaderSession();

  if (pluginevents::anySubscriber(pluginevents::Event::ReaderExit)) {
    char percent[8];
    snprintf(percent, sizeof(percent), "%d", getScreenshotInfo().progressPercent);
    const pluginevents::Var vars[] = {{"book", bookPath.c_str()}, {"percent", percent}};
    pluginevents::emit(pluginevents::Event::ReaderExit, vars, 2);
  }

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();

  endOfBookOptions.reset();
  endOfBookOptionsReady.store(false, std::memory_order_release);
}

void ReaderActivity::prepareForSleep() { flushReaderSession(); }

void ReaderActivity::serviceReadingStats() {
  if (!bookRemembered) return;
  if ((segmentUnsaved || finishUnsaved) && millis() - statsWriteFailedMs < STATS_RETRY_MS) return;
  {
    // Never wait for a page being drawn; the segment keeps until the next loop.
    RenderLock lock(RenderLock::Mode::Try);
    if (!lock.ownsLock()) return;
    takeReadingStats();
  }
  writeReadingStats();
}

// While an older segment is unsaved, the next one waits in the tracker.
void ReaderActivity::takeReadingStats() {
  if (!segmentUnsaved && readingTracker.hasReady()) {
    unsavedSegment = readingTracker.takeReady();
    segmentUnsaved = true;
  }
  if (readingTracker.takeFinished()) finishUnsaved = true;
}

// False while something taken from the tracker is still unsaved.
bool ReaderActivity::writeReadingStats() {
  if (segmentUnsaved && READING_STATS.appendSegment(statsBookId, unsavedSegment)) segmentUnsaved = false;
  if (finishUnsaved && (statsBookId < 0 || READING_STATS.markFinished(statsBookId, stats::today()))) {
    finishUnsaved = false;
  }
  if (!segmentUnsaved && !finishUnsaved) return true;
  statsWriteFailedMs = millis();
  return false;
}

void ReaderActivity::flushReaderSession() {
  readingTracker.close(millis());
  if (bookRemembered) {
    // A second pass writes the segment the tracker kept behind an unsaved one.
    for (int pass = 0; pass < 2; pass++) {
      takeReadingStats();
      writeReadingStats();
      if (segmentUnsaved) break;
    }
  }

  if (!readerSession.isEmitWorthy() || !pluginevents::anySubscriber(pluginevents::Event::ReaderSession)) {
    readerSession.reset();
    return;
  }

  const std::string document = KOReaderDocumentId::calculate(bookPath);
  const bool validDocument =
      document.size() == 32 && std::all_of(document.begin(), document.end(), [](const unsigned char c) {
        return std::isdigit(c) || (c >= 'a' && c <= 'f');
      });
  if (validDocument) {
    char startTime[24];
    char endTime[24];
    char duration[16];
    char startProgress[8];
    char endProgress[8];
    snprintf(startTime, sizeof(startTime), "%lld", static_cast<long long>(readerSession.startTime()));
    snprintf(endTime, sizeof(endTime), "%lld", static_cast<long long>(readerSession.endTime()));
    snprintf(duration, sizeof(duration), "%lu", static_cast<unsigned long>(readerSession.durationSeconds()));
    snprintf(startProgress, sizeof(startProgress), "%u", readerSession.startProgressBp());
    snprintf(endProgress, sizeof(endProgress), "%u", readerSession.endProgressBp());
    const pluginevents::Var vars[] = {{"book", bookPath.c_str()},       {"document", document.c_str()},
                                      {"start_time", startTime},        {"end_time", endTime},
                                      {"duration_seconds", duration},   {"start_progress_bp", startProgress},
                                      {"end_progress_bp", endProgress}, {"progress_scale", "10000"}};
    pluginevents::emit(pluginevents::Event::ReaderSession, vars, 8);
  }
  readerSession.reset();
}

bool ReaderActivity::handleBackNavigation() {
  return ReaderUtils::handleBackNavigation(mappedInput, activityManager, bookPath.c_str(),
                                           {this, [](void* ctx) { static_cast<ReaderActivity*>(ctx)->onGoHome(); }});
}

void ReaderActivity::clearEndOfBookOptionsIfNeeded() {
  if (isAtEndOfBook() || !endOfBookOptionsReady.load(std::memory_order_acquire)) return;

  RenderLock lock(*this);
  endOfBookOptionsReady.store(false, std::memory_order_release);
  endOfBookOptions.reset();
}

bool ReaderActivity::endOfBookMenuActive() const {
  return isAtEndOfBook() && endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive();
}

bool ReaderActivity::handleEndOfBookMenu(const bool suppressConfirmRelease) {
  if (suppressConfirmRelease || !endOfBookMenuActive()) {
    return false;
  }

  std::string openPath;
  switch (endOfBookOptions->handleMenuInput(mappedInput, &openPath)) {
    case EndOfBookOptions::Action::OpenBook:
      activityManager.goToReader(openPath);
      return true;
    case EndOfBookOptions::Action::GoHome:
      onGoHome();
      return true;
    case EndOfBookOptions::Action::LastPage:
      onReturnFromEndOfBook();
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::Redraw:
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::None:
      return false;
  }

  return false;
}

bool ReaderActivity::handleEndOfBookPageTurn(const bool prevTriggered, const bool nextTriggered) {
  if (!isAtEndOfBook()) return false;

  if (endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive()) {
    return true;
  }
  if (nextTriggered) {
    onGoHome();
  } else if (prevTriggered) {
    onReturnFromEndOfBook();
    requestUpdate();
  }
  return true;
}

void ReaderActivity::loop() {
  rememberBookOnceRendered();
  serviceReadingStats();
#if CROSSPOINT_BRIGHTNESS_SWIPE
  if (handleBrightnessSwipe()) return;
#endif
  clearEndOfBookOptionsIfNeeded();
  if (handleEndOfBookMenu()) return;
  if (handleFormatInput()) return;
  if (handleBackNavigation()) return;

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) return;
  if (handleEndOfBookPageTurn(prevTriggered, nextTriggered)) return;

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool skip =
      !fromTilt && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP && heldMs >= ReaderUtils::SKIP_HOLD_MS;

  const bool changed = skip ? skipPages(prevTriggered ? -10 : 10) : pageTurn(!prevTriggered);
  // A skip is navigation, not reading: it never counts toward session dwell.
  notePageTurn(!skip && !prevTriggered, changed);
  if (changed && (touch.prev || touch.next)) haptic_feedback::touchAction(skip);
  requestUpdate();
}

void ReaderActivity::render(RenderLock&&) {
#if CROSSPOINT_BRIGHTNESS_SWIPE
  if (renderBrightnessOverlay()) return;
#endif
  if (isAtEndOfBook()) {
    if (!endOfBookOptions) {
      endOfBookOptions = makeUniqueNoThrow<EndOfBookOptions>(renderer);
      if (!endOfBookOptions) LOG_ERR("READER", "OOM: EndOfBookOptions");
    }
    renderer.clearScreen();
    if (endOfBookOptions) {
      endOfBookOptions->loadOnce(bookPath);
      // Release-publish AFTER loadOnce() so the main task's acquire load can't
      // observe an object whose names/selector are still being populated.
      endOfBookOptionsReady.store(true, std::memory_order_release);
      endOfBookOptions->render(renderer, mappedInput);
    }
    renderer.displayBuffer();
    onEndOfBookRendered();
    markPageRendered();
    notePageShown(true);
    return;
  }

  renderBook();
  notePageShown(false);
}

void ReaderActivity::notePageShown(const bool atEnd) {
  const uint32_t now = millis();
  const int64_t epoch = trustedtime::trustedNow();
  const int progressBp = getProgressBasisPoints();
  readerSession.onRenderComplete(now, epoch, progressBp);
  readingTracker.onPageShown(now, epoch, progressBp, atEnd);
}

bool ReaderActivity::handleForcedRefresh() {
  {
    RenderLock lock(*this);
    pagesUntilFullRefresh = 1;
    forcedRefreshPending = true;
  }
  requestUpdate();
  return true;
}

#if CROSSPOINT_BRIGHTNESS_SWIPE
namespace {
// Frontlight writes while dragging, at most this often; the last level always lands.
constexpr unsigned long kLightApplyIntervalMs = 25;
// Same floor as the control center slider: 0 is "off", which is the lamp's job.
constexpr int kMinOnLevel = 1;
}  // namespace

void ReaderActivity::requestUpdate(const bool immediate) {
  pageRepaintWanted.store(true, std::memory_order_release);
  Activity::requestUpdate(immediate);
}

bool ReaderActivity::brightnessSwipeAllowed() const {
  return SETTINGS.brightnessSwipe && SETTINGS.touchReaderControls != CrossPointSettings::TOUCH_READER_OFF &&
         mappedInput.hasTouch() && Frontlight.present() && pageRendered.load(std::memory_order_acquire) &&
         !isAtEndOfBook() && !inputOverPage() && (Frontlight.isOn() || !slideSwitchOwnsLight());
}

bool ReaderActivity::handleBrightnessSwipe() {
  using Event = brightness_swipe::Gesture::Event;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTouchPressed(x, y)) {
    // A new contact while still dragging: the old release went unseen.
    if (brightnessGesture.dragging()) finishBrightnessSwipe();
    if (brightnessSwipeAllowed()) {
      brightnessGesture.press(x, y, screenW, screenH);
    } else {
      brightnessGesture.cancel();
    }
  }
  const bool held = mappedInput.isScreenTouchHeld(x, y);
  const Event event = brightnessGesture.track(held, x, y);
  switch (event) {
    case Event::None:
      return false;
    case Event::Start:
      // Re-checked at the claim: the page may have left the framebuffer since
      // touch-down (a background build step can lend it out).
      if (!brightnessSwipeAllowed()) {
        brightnessGesture.cancel();
        return false;
      }
      // An unlit light starts from 0, so the drag up is what switches it on.
      swipeStartedOn = Frontlight.isOn();
      swipeOriginalBrightness = Frontlight.brightness();
      swipeStartLevel = swipeStartedOn ? std::max<int>(kMinOnLevel, swipeOriginalBrightness) : 0;
      swipeAppliedLevel = swipeStartLevel;
      [[fallthrough]];
    case Event::Move: {
      const int level = brightness_swipe::levelFor(swipeStartLevel, brightnessGesture.travel(), screenH,
                                                   swipeStartedOn ? kMinOnLevel : 0);
      if (event == Event::Start || level != swipeLevel) {
        swipeLevel = level;
        brightnessOverlayLevel.store(level, std::memory_order_release);
        // Overlay only: the page repaint flag stays down.
        Activity::requestUpdate();
      }
      applySwipeLevel(false);
      return true;
    }
    case Event::Hold:
      applySwipeLevel(false);
      return true;
    case Event::End:
      finishBrightnessSwipe();
      return true;
  }
  return true;
}

void ReaderActivity::applySwipeLevel(const bool force) {
  if (swipeLevel == swipeAppliedLevel) return;
  const unsigned long now = millis();
  if (!force && now - swipeAppliedAt < kLightApplyIntervalMs) return;
  swipeAppliedLevel = swipeLevel;
  swipeAppliedAt = now;
  if (swipeLevel <= 0) {
    // Only reachable when the drag started with the light off.
    Frontlight.setOn(false);
    return;
  }
  Frontlight.setBrightness(static_cast<uint8_t>(swipeLevel));
  if (!slideSwitchOwnsLight()) Frontlight.setOn(true);
}

void ReaderActivity::finishBrightnessSwipe() {
  applySwipeLevel(true);
  // Dragged back to 0 from off: nothing changed, keep the remembered level.
  if (swipeLevel <= 0) Frontlight.setBrightness(swipeOriginalBrightness);
  const bool lightOn = Frontlight.isOn();
  bool changed = false;
  if (SETTINGS.frontlightBrightness != Frontlight.brightness()) {
    SETTINGS.frontlightBrightness = Frontlight.brightness();
    changed = true;
  }
  if (lightOn != swipeStartedOn && SETTINGS.frontlightOn != (lightOn ? 1 : 0)) {
    SETTINGS.frontlightOn = lightOn ? 1 : 0;
    changed = true;
  }
  if (changed) SETTINGS.saveToFile();
  brightnessOverlayLevel.store(-1, std::memory_order_release);
  requestUpdate();  // repaint the page under the overlay, on the reader's refresh cadence
}

bool ReaderActivity::renderBrightnessOverlay() {
  const bool pageWanted = pageRepaintWanted.exchange(false, std::memory_order_acq_rel);
  const int level = brightnessOverlayLevel.load(std::memory_order_acquire);
  if (level < 0) return false;
  if (pageWanted) {
    // The page comes first; the overlay goes back over it on the next pass.
    Activity::requestUpdate();
    return false;
  }
  brightness_overlay::draw(renderer, level);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  // Each overlay refresh counts toward the reader's ghost-cleanup cadence, so
  // the repaint on release takes the HALF refresh once it falls due.
  if (pagesUntilFullRefresh > 1) --pagesUntilFullRefresh;
  return true;
}
#endif
