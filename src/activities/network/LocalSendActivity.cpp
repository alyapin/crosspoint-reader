#include "LocalSendActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_random.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "network/FirmwareBoardTag.h"

namespace fui = freeink::ui;
using localsend::FileState;
using localsend::Phase;
using localsend::Reject;

namespace {
constexpr char kAlias[] = "Сова";
constexpr fui::ActionId ACTION_DECLINE = 1;
constexpr fui::ActionId ACTION_ACCEPT = 2;
// Progress repaints at most this often; state changes repaint at once.
constexpr uint32_t kProgressPaintMs = 1000;
constexpr int kActionBarHeight = 72;
constexpr int kBarGap = 8;  // between the selection bar and its label

// Russian plural forms; English uses the "one" and "few" strings.
const char* filesWord(const int n) {
  const int mod10 = n % 10;
  const int mod100 = n % 100;
  if (mod10 == 1 && mod100 != 11) return tr(STR_LOCALSEND_FILE_ONE);
  if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return tr(STR_LOCALSEND_FILE_FEW);
  return tr(STR_LOCALSEND_FILE_MANY);
}

const char* rejectText(const Reject reason) {
  switch (reason) {
    case Reject::Format:
      return tr(STR_LOCALSEND_NOT_A_BOOK);
    case Reject::TooLarge:
      return tr(STR_LOCALSEND_TOO_LARGE);
    case Reject::Empty:
      return tr(STR_LOCALSEND_EMPTY);
    case Reject::NoSpace:
      return tr(STR_LOCALSEND_NO_SPACE);
    case Reject::Declined:
      return tr(STR_LOCALSEND_DECLINED);
    case Reject::BadName:
    case Reject::Duplicate:
    case Reject::None:
      break;
  }
  return tr(STR_LOCALSEND_BAD_FILE);
}
}  // namespace

LocalSendActivity::LocalSendActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("LocalSend", renderer, mappedInput), UiAppHost(renderer) {}

void LocalSendActivity::onEnter() {
  // The render task may run as soon as ActivityManager drops its lock; keep it
  // out until the state below is in place (same as LibraryListActivity).
  RenderLock renderLock(*this);
  Activity::onEnter();
  resetUi();
  app.on(ACTION_DECLINE, &LocalSendActivity::onAction, this);
  app.on(ACTION_ACCEPT, &LocalSendActivity::onAction, this);
  app.setScreen(&LocalSendActivity::screenTrampoline, this);
  entryRefresh = true;

  // Wi-Fi needs ~45 KB; rebuildable SD-font caches go first, as for File Transfer.
  if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();
  // One ~13 KB block, taken before Wi-Fi fragments the heap.
  receiver = makeUniqueNoThrow<LocalSendReceiver>();
  {
    std::lock_guard<std::mutex> lock(viewLock);
    snprintf(view.pin, sizeof(view.pin), "%04u", static_cast<unsigned>(esp_random() % 10000));
  }
  LOG_DBG("LSACT", "code %s", view.pin);  // shown on screen too; lets a scripted sender pair
  if (!receiver) {
    LOG_ERR("LSACT", "OOM: receiver (%u bytes)", static_cast<unsigned>(sizeof(LocalSendReceiver)));
    setStage(Stage::Failed);
    return;
  }

  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           entryRefresh = true;
                           if (result.isCancelled) {
                             leave();
                             return;
                           }
                           const auto& wifi = std::get<WifiResult>(result.data);
                           onWifiResult(wifi.connected, wifi.ssid.c_str());
                         });
}

void LocalSendActivity::onExit() {
  Activity::onExit();
  receiver.reset();  // stops the receiver and drops a partial file
  // Like File Transfer: Wi-Fi off, then a reboot to undo its heap fragmentation.
  // silentRestart() returns without rebooting when this exit is the way to sleep.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    if (leftToMore) {
      silentRestartToMore();
    } else {
      silentRestart();
    }
  }
}

void LocalSendActivity::onWifiResult(const bool connected, const char* ssid) {
  if (!connected) {
    leave();
    return;
  }
  char model[24];
  snprintf(model, sizeof(model), "%.*s", static_cast<int>(board_tag::boardNameLen()), board_tag::boardName());
  LocalSendReceiver::Config config;
  config.alias = kAlias;
  config.deviceModel = model;
  config.pin = view.pin;  // written once in onEnter
  if (!receiver->start(config)) {
    setStage(Stage::Failed);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(viewLock);
    snprintf(view.network, sizeof(view.network), "%s", ssid);
  }
  setStage(Stage::Ready);
}

void LocalSendActivity::leave() {
  leftToMore = true;
  finish();
}

void LocalSendActivity::setStage(const Stage stage) {
  {
    std::lock_guard<std::mutex> lock(viewLock);
    view.stage = stage;
  }
  requestUpdate();
}

void LocalSendActivity::loop() {
  if (receiver && receiver->running()) {
    receiver->loop();
    syncView();
  }

  const auto route = routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;  // onAction ran

  const bool pending = receiver && receiver->session().phase() == Phase::AwaitingDecision;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (pending) {
      decide(false);
    } else {
      app.clearTapFlash();
      leave();
    }
    return;
  }
  if (!pending) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    decide(acceptFocused);
    return;
  }
  // Decline sits on the left, Accept on the right.
  using Button = MappedInputManager::Button;
  if (mappedInput.wasPressed(Button::Left) || mappedInput.wasPressed(Button::Up)) {
    acceptFocused = false;
    viewStale = true;
  } else if (mappedInput.wasPressed(Button::Right) || mappedInput.wasPressed(Button::Down)) {
    acceptFocused = true;
    viewStale = true;
  }
}

void LocalSendActivity::decide(const bool accept) {
  if (accept) {
    receiver->accept();
  } else {
    receiver->decline();
  }
  acceptFocused = true;
  viewStale = true;
}

void LocalSendActivity::syncView() {
  const auto& session = receiver->session();
  const uint32_t now = millis();
  const bool locked = session.pinLocked(now);
  if (viewStale || session.revision() != seenRevision || locked != view.pinLocked) {
    viewStale = false;
    seenRevision = session.revision();
    uint32_t key = static_cast<uint32_t>(session.phase()) * 31u + session.entryCount() * 7u + (locked ? 1u : 0u) +
                   (acceptFocused ? 2u : 0u);
    std::lock_guard<std::mutex> lock(viewLock);
    view.phase = session.phase();
    view.pinLocked = locked;
    view.acceptFocused = acceptFocused;
    snprintf(view.sender, sizeof(view.sender), "%s", session.senderAlias());
    view.files = static_cast<int>(session.entryCount() + session.droppedCount());
    view.rowCount = 0;
    for (size_t i = 0; i < session.entryCount() && view.rowCount < kMaxRows; i++) {
      const localsend::FileEntry& e = session.entry(i);
      Row& row = view.rows[view.rowCount++];
      snprintf(row.name, sizeof(row.name), "%s", e.name[0] ? e.name : "?");
      row.percent = static_cast<uint8_t>(e.size ? e.received * 100 / e.size : 0);
      row.state = e.state;
      row.reason = e.reason;
      key = key * 31u + static_cast<uint32_t>(e.state) * 5u + static_cast<uint32_t>(e.reason);
    }
    if (key != layoutKey) {
      layoutKey = key;
      progressDirty = false;
      lastPaintMs = now;
      requestUpdate();
      return;
    }
    progressDirty = true;
  }
  if (progressDirty && now - lastPaintMs >= kProgressPaintMs) {
    progressDirty = false;
    lastPaintMs = now;
    requestUpdate();
  }
}

void LocalSendActivity::onAction(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<LocalSendActivity*>(user);
  if (!self->receiver || self->receiver->session().phase() != Phase::AwaitingDecision) return;
  self->decide(event.action == ACTION_ACCEPT);
}

void LocalSendActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<LocalSendActivity*>(user)->buildScreen(screen);
}

void LocalSendActivity::render(RenderLock&&) {
  {
    std::lock_guard<std::mutex> lock(viewLock);
    const auto& metrics = UITheme::getInstance().getMetrics();
    renderer.clearScreen();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                   tr(STR_LOCALSEND_TITLE));
    renderUi();
    const bool deciding = view.stage == Stage::Ready && view.phase == Phase::AwaitingDecision;
    // Back declines a pending offer, so it says so.
    const auto labels =
        mappedInput.mapLabels(deciding ? tr(STR_LOCALSEND_DECLINE) : tr(STR_BACK), deciding ? tr(STR_SELECT) : "",
                              deciding ? tr(STR_DIR_LEFT) : "", deciding ? tr(STR_DIR_RIGHT) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer(entryRefresh.exchange(false) ? cadence.entry() : cadence.update());
}

void LocalSendActivity::buildScreen(UiScreen& screen) {
  using namespace setka;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int line10 = renderer.getLineHeight(kText10Font);
  int y = metrics.topPadding + metrics.headerHeight + 20;

  if (view.stage != Stage::Ready) {
    const char* text = view.stage == Stage::Failed ? tr(STR_LOCALSEND_START_FAILED) : tr(STR_CONNECTING);
    renderer.drawText(kText12Font, kSidePadding, y, text);
    return;
  }

  renderer.drawText(kText10Font, kSidePadding, y, tr(STR_LOCALSEND_VISIBLE_AS));
  y += line10 + 8;
  drawHeroNumber(renderer, kHeroSmallFont, kSidePadding, y, kAlias);
  y += heroCapHeight(renderer, kHeroSmallFont) + 16;
  char network[96];
  snprintf(network, sizeof(network), tr(STR_LOCALSEND_NETWORK), view.network);
  renderer.drawText(kText10Font, kSidePadding, y, network);
  // Text width ends at the last glyph, so the gap before the bold code is added here.
  const int pinX = kSidePadding + renderer.getTextWidth(kText10Font, network) + renderer.getSpaceWidth(kText10Font);
  renderer.drawText(kText10Font, pinX, y, view.pin, true, kSemiBold);
  y += line10 + 18;
  drawRule(renderer, 0, y, width);
  y += kRuleWidth + 18;

  drawRequest(y, width);
  if (view.phase == Phase::AwaitingDecision) drawActions(screen, width, height);
}

void LocalSendActivity::drawRequest(int y, const int width) {
  using namespace setka;
  const int textWidth = width - 2 * kSidePadding;
  const int line12 = renderer.getLineHeight(kText12Font);

  if (view.phase == Phase::Idle || view.phase == Phase::Preparing) {
    const char* text = view.pinLocked ? tr(STR_LOCALSEND_PIN_LOCKED) : tr(STR_LOCALSEND_HINT);
    for (const auto& line : renderer.wrappedText(kText12Font, text, textWidth, 4)) {
      renderer.drawText(kText12Font, kSidePadding, y, line.c_str());
      y += line12;
    }
    return;
  }

  char title[128];
  snprintf(title, sizeof(title), tr(STR_LOCALSEND_SENDING), view.sender[0] ? view.sender : "LocalSend", view.files,
           filesWord(view.files));
  const std::string fitted = renderer.truncatedText(kText12Font, title, textWidth, kSemiBold);
  renderer.drawText(kText12Font, kSidePadding, y, fitted.c_str(), true, kSemiBold);
  y += line12 + 14;

  const int rowPitch = line12 + 4 + kProgressHeight + 18;
  const int hints = mappedInput.hasTouch() ? 0 : UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int bottom = renderer.getScreenHeight() - hints - kActionBarHeight - 8;
  int shown = 0;
  for (; shown < view.rowCount && y + rowPitch <= bottom; shown++) {
    const Row& row = view.rows[shown];
    char right[32];
    bool bar = false;
    switch (row.state) {
      case FileState::Rejected:
        snprintf(right, sizeof(right), "%s", rejectText(row.reason));
        break;
      case FileState::Failed:
        snprintf(right, sizeof(right), "%s", tr(STR_LOCALSEND_FILE_FAILED));
        break;
      case FileState::Accepted:
        right[0] = 0;
        bar = true;
        break;
      case FileState::Receiving:
      case FileState::Done:
        snprintf(right, sizeof(right), "%u%%", row.state == FileState::Done ? 100u : row.percent);
        bar = true;
        break;
    }
    const int rightWidth = right[0] ? renderer.getTextWidth(kText12Font, right) : 0;
    const std::string name = renderer.truncatedText(kText12Font, row.name, textWidth - rightWidth - 12);
    renderer.drawText(kText12Font, kSidePadding, y, name.c_str());
    if (right[0]) renderer.drawText(kText12Font, width - kSidePadding - rightWidth, y, right);
    if (bar) {
      drawProgressBar(renderer, kSidePadding, y + line12 + 4, textWidth,
                      row.state == FileState::Done ? 100 : row.percent);
    }
    y += bar ? rowPitch : line12 + 12;
  }
  const int hidden = view.files - shown;
  if (hidden > 0) {
    char more[48];
    snprintf(more, sizeof(more), tr(STR_LOCALSEND_MORE_FILES), hidden);
    renderer.drawText(kText10Font, kSidePadding, y, more);
  }
}

void LocalSendActivity::drawActions(UiScreen& screen, const int width, const int height) {
  using namespace setka;
  const int hints = mappedInput.hasTouch() ? 0 : UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int top = height - hints - kActionBarHeight;
  const int line12 = renderer.getLineHeight(kText12Font);
  const int textTop = top + (kActionBarHeight - line12) / 2;
  drawRule(renderer, 0, top, width);

  const char* decline = tr(STR_LOCALSEND_DECLINE);
  const char* accept = tr(STR_LOCALSEND_ACCEPT);
  const bool acceptOn = view.acceptFocused;
  const int declineX = kSidePadding + kSelectionBarWidth + kBarGap;
  const int acceptX =
      width - kSidePadding - renderer.getTextWidth(kText12Font, accept, acceptOn ? kSemiBold : EpdFontFamily::REGULAR);
  drawSelectionBar(renderer, (acceptOn ? acceptX : declineX) - kBarGap - kSelectionBarWidth, textTop, line12);
  renderer.drawText(kText12Font, declineX, textTop, decline, true, acceptOn ? EpdFontFamily::REGULAR : kSemiBold);
  renderer.drawText(kText12Font, acceptX, textTop, accept, true, acceptOn ? kSemiBold : EpdFontFamily::REGULAR);

  screen.frame().hit(fui::makeRect(0, top, width / 2, kActionBarHeight), ACTION_DECLINE, 0, fui::InputTouch);
  screen.frame().hit(fui::makeRect(width / 2, top, width - width / 2, kActionBarHeight), ACTION_ACCEPT, 0,
                     fui::InputTouch);
}
