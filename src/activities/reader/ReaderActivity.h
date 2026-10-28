#pragma once

#include <BoardConfig.h>

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "EndOfBookOptions.h"
#include "ReaderSession.h"
#include "activities/Activity.h"
#include "stats/ReadingTracker.h"

// Frontlight brightness by a vertical drag along the right edge of the page.
#define CROSSPOINT_BRIGHTNESS_SWIPE (FREEINK_CAP_TOUCH && FREEINK_CAP_FRONTLIGHT)
#if CROSSPOINT_BRIGHTNESS_SWIPE
#include "BrightnessSwipe.h"
#endif

class ReaderActivity : public Activity {
  friend class ActivityManager;  // turns pages for a Bluetooth remote (ActivityManager::remoteTurn)

#if CROSSPOINT_BRIGHTNESS_SWIPE
  brightness_swipe::Gesture brightnessGesture;
  int swipeStartLevel = 0;
  int swipeLevel = 0;
  int swipeAppliedLevel = 0;
  unsigned long swipeAppliedAt = 0;
  uint8_t swipeOriginalBrightness = 0;
  bool swipeStartedOn = false;
  // Level the render task paints in the overlay, -1 = no overlay.
  std::atomic<int> brightnessOverlayLevel{-1};
  // Raised by every page repaint request, so a coalesced overlay paint never
  // swallows one.
  std::atomic<bool> pageRepaintWanted{false};
  void applySwipeLevel(bool force);
  void finishBrightnessSwipe();
  bool renderBrightnessOverlay();
#endif

 protected:
  std::string bookPath;
  int pagesUntilFullRefresh = 0;
  bool forcedRefreshPending = false;

  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  std::atomic<bool> endOfBookOptionsReady{false};
  ReaderSession readerSession;
  // Reading statistics: active time, pages and status of this visit (stats/ReadingStats.h).
  ReadingTracker readingTracker;
  int statsBookId = -1;
  // Taken from the tracker but not written yet: a failed write is retried, not dropped.
  ReadingSegment unsavedSegment;
  bool segmentUnsaved = false;
  bool finishUnsaved = false;
  uint32_t statsWriteFailedMs = 0;
  std::atomic<bool> pageRendered{false};
  bool bookRemembered = false;
  void markPageRendered() { pageRendered.store(true, std::memory_order_release); }
  void rememberBookOnceRendered();

  explicit ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                          std::string bookPath, bool allowFastInitialRefresh);

  virtual bool loadBook() = 0;
  // Called when loadBook() failed. Return true to keep the activity alive
  // (e.g. showing a dialog); false finishes it (the default).
  virtual bool handleLoadFailure() { return false; }
  virtual std::string getBookTitle() const = 0;
  virtual std::string getBookAuthor() const { return ""; }
  virtual std::string getBookThumbBmpPath() const { return ""; }
  // Whole-book progress for the reader.exit plugin event, reusing the
  // per-reader ScreenshotInfo implementations.
  //
  // Upstream added this helper in d3e55c53 with an earlier slice of this
  // feature, then removed it in c1e1fec3 as dead code once no caller remained
  // ("removes unnecessary wrapper structs"). It is reinstated here because this
  // change reintroduces the callers: ReaderActivity.cpp uses it for the progress
  // string and for the session's render-complete basis points, and
  // EpubReaderActivity overrides getProgressBasisPoints() and falls back to it.
  int getProgressPercent() const { return getScreenshotInfo().progressPercent; }
  virtual int getProgressBasisPoints() const { return getProgressPercent() * 100; }

  virtual bool handleFormatInput() { return false; }
  virtual bool pageTurn(bool isForward) = 0;
  virtual bool skipPages(int amount) { return pageTurn(amount > 0); }
  // A Bluetooth remote asks for the reader menu (true) or a new clipping (false).
  virtual bool remoteShortcut(bool /*readerMenu*/) { return false; }
  virtual bool isAtEndOfBook() const = 0;
  virtual void onReturnFromEndOfBook() {}

  virtual void renderBook() = 0;
  virtual void applyInitialOrientation();
  virtual void onEndOfBookRendered() {}

  bool handleBackNavigation();
  /** True while the end-of-book suggestion menu is on screen and owning input. */
  bool endOfBookMenuActive() const;
  /** True while a toolbar, panel or menu over the page owns the device keys: a remote turn is refused. */
  virtual bool inputOverPage() const { return endOfBookMenuActive(); }
  bool handleEndOfBookMenu(bool suppressConfirmRelease = false);
  bool handleEndOfBookPageTurn(bool prevTriggered, bool nextTriggered);
  void clearEndOfBookOptionsIfNeeded();
  void disableFastInitialRefresh();
  void notePageTurn(bool forward, bool succeeded);
  void flushReaderSession();
#if CROSSPOINT_BRIGHTNESS_SWIPE
  // Readers call this first in loop(); true = a brightness drag owns this
  // frame's touch input, so the reader must not act on it.
  bool handleBrightnessSwipe();
  // Whether a drag may start now: the page is on screen and in the
  // framebuffer, and nothing else is painted over it.
  virtual bool brightnessSwipeAllowed() const;
#endif
  // Writes reading-statistics segments the tracker closed; call from loop().
  void serviceReadingStats();

 public:
  ~ReaderActivity() override = default;

  static std::unique_ptr<ReaderActivity> create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                std::string path, bool allowFastInitialRefresh);

  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override;
  void loop() override;
  void render(RenderLock&& lock) override;
#if CROSSPOINT_BRIGHTNESS_SWIPE
  void requestUpdate(bool immediate = false) override;
#endif

  bool isReaderActivity() const final { return true; }
  bool acceptsRemoteInput() const final { return pageRendered.load(std::memory_order_acquire); }
  bool handleForcedRefresh() final;

 private:
  void notePageShown(bool atEnd);
  void takeReadingStats();
  bool writeReadingStats();
};
