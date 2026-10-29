#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "components/setka/SetkaStyle.h"
#include "network/LocalSendReceiver.h"

// «Приём книг»: receives books from the LocalSend app. Wi-Fi and the receiver
// run only while this screen is open; leaving it, or sleep, stops both.
// Buttons: Confirm runs the focused action (Принять by default), Back declines
// a pending request or leaves, Left/Right move the focus.
class LocalSendActivity final : public Activity, private UiAppHost {
 public:
  LocalSendActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Full speed only while bytes flow, no auto-sleep while a request is open; an
  // idle screen keeps the normal loop delay and may sleep, which stops the receiver.
  bool skipLoopDelay() override { return receiver && receiver->streaming(); }
  bool preventAutoSleep() override { return receiver && receiver->busy(); }

 private:
  static constexpr int kMaxRows = 5;
  enum class Stage : uint8_t { Connecting, Ready, Failed };

  // What render() draws; the loop task copies it from the receiver.
  struct Row {
    char name[localsend::FILE_NAME_MAX + 1];
    uint8_t percent;
    localsend::FileState state;
    localsend::Reject reason;
    bool toCards;
  };
  struct View {
    Stage stage = Stage::Connecting;
    localsend::Phase phase = localsend::Phase::Idle;
    char sender[localsend::ALIAS_MAX + 1] = {};
    char network[33] = {};
    char pin[5] = {};
    int files = 0;  // in the request, including rows that do not fit
    int rowCount = 0;
    Row rows[kMaxRows] = {};
    bool acceptFocused = true;
    bool pinLocked = false;
  };

  std::unique_ptr<LocalSendReceiver> receiver;
  std::mutex viewLock;  // view: written by the loop task, drawn by the render task
  View view;
  uint32_t seenRevision = 0;
  uint32_t layoutKey = 0;  // changes with everything but the percentages
  uint32_t lastPaintMs = 0;
  bool progressDirty = false;
  bool viewStale = false;  // the focus moved: sync even without a receiver change
  bool acceptFocused = true;
  setka::RefreshCadence cadence;  // render task only
  std::atomic<bool> entryRefresh{true};

  // Back to Ещё; the Wi-Fi reboot in onExit() lands there too.
  void leave();
  bool leftToMore = false;
  void onWifiResult(bool connected, const char* ssid);
  void setStage(Stage stage);
  void syncView();
  void decide(bool accept);

  static void screenTrampoline(UiScreen& screen, void* user);
  static void onAction(const freeink::ui::ActionEvent& event, void* user);
  void buildScreen(UiScreen& screen);
  void drawRequest(int y, int width);
  void drawActions(UiScreen& screen, int width, int height);
};
