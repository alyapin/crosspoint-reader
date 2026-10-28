#pragma once
#include <I18n.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/setka/SetkaStyle.h"

// Modal option picker drawn over the current screen (no full clear) as a Setka
// dialog, see render(). Touch hit-testing is the SDK's InteractionBuffer: each
// render registers the option buttons (plus a chrome guard rect) on the render
// task, and handleInput routes touch snapshots against that table on the loop
// task, gated by the uiReady handshake (same pattern as UiListActivity).
// render() builds into InteractionBuffer's non-published generation
// (beginPublishCycle()) and publishes it only once every hit() call for the
// frame is done (publish()), so handleInput()'s routePublished()/
// publishedData() reads on the loop task always see a complete table, never
// one render is mid-rebuilding. uiReady closes when show() replaces the
// popup's data, then stays open across ordinary repaints after the first
// publication so a release cannot be dropped during a highlight repaint.
class OptionPopup {
 public:
  void show(StrId titleId, const StrId* optionIds, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    headline.clear();
    ownedStrings.resize(optionCount);
    for (int i = 0; i < optionCount; i++) {
      ownedStrings[i] = I18N.get(optionIds[i]);
    }
    activate(currentIndex, std::move(onSelect));
  }

  void show(const char* titleStr, const char* const* options, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = titleStr;
    headline.clear();
    ownedStrings.resize(optionCount);
    for (int i = 0; i < optionCount; i++) {
      ownedStrings[i] = options[i];
    }
    activate(currentIndex, std::move(onSelect));
  }

  // As above, plus a subject line inside the dialog (a book or event title).
  // It wraps to several lines under the caption; the dialog grows to fit.
  void show(const char* titleStr, const char* headlineStr, const char* const* options, int optionCount,
            int currentIndex, std::function<void(int)> onSelect) {
    show(titleStr, options, optionCount, currentIndex, std::move(onSelect));
    headline = headlineStr ? headlineStr : "";
  }

  // Message dialog: a wrapped body under the (optional) title, like the
  // Wi-Fi forget-network prompt. Pass an empty title for a message-only look.
  void showMessage(const char* titleStr, const char* messageStr, const char* const* options, int optionCount,
                   int currentIndex, std::function<void(int)> onSelect) {
    show(titleStr, options, optionCount, currentIndex, std::move(onSelect));
    message = messageStr ? messageStr : "";
  }

  void show(StrId titleId, const std::vector<std::string>& options, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    headline.clear();
    ownedStrings = options;
    activate(currentIndex, std::move(onSelect));
  }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active) return false;

    const freeink::ui::InputSnapshot snap = touchSnapshotFrom(input);
    if (snap.touchPressed || snap.touchReleased || snap.touchHeld) {
      // Interactions are registered on the render task; only route once the
      // first render after show() has populated the table (uiReady handshake).
      if (uiReady) {
        const freeink::ui::ActionEvent event = interactions.routePublished(snap);
        if (event && event.action == ACTION_OPTION) {
          // Tap released on an option: select it, fire, dismiss.
          selectedIndex = event.value;
          active = false;
          if (onSelectCallback) onSelectCallback(selectedIndex);
          haptic_feedback::touchAction();
          requestUpdate();
          return true;
        }
        if (event && event.action == ACTION_CHROME) {
          // Taps on the dialog chrome (title, padding) keep the popup open.
          return true;
        }
        if (snap.touchReleased && snap.touchX >= 0) {
          // Tap released outside the dialog: dismiss without firing. Swipe-end
          // releases arrive with -1,-1 coords and fall through (no dismiss).
          active = false;
          haptic_feedback::touchAction();
          requestUpdate();
          return true;
        }
        if (snap.touchPressed) {
          // Touch-down on an option moves the highlight (route() latched the
          // hit as the active interaction; read it back, no re-hit-testing).
          const int16_t idx = interactions.activeIndex();
          if (idx >= 0) {
            const freeink::ui::Interaction& hit = interactions.publishedData()[idx];
            if (hit.action == ACTION_OPTION && selectedIndex != hit.value) {
              selectedIndex = hit.value;
              requestUpdate();
            }
          }
        }
      }
      return true;
    }

    return handleButtons(input.wasPressed(MappedInputManager::Button::NavPrevious),
                         input.wasPressed(MappedInputManager::Button::NavNext),
                         input.wasReleased(MappedInputManager::Button::Confirm),
                         input.wasReleased(MappedInputManager::Button::Back), requestUpdate);
  }

  bool handleButtons(const bool previous, const bool next, const bool confirm, const bool back,
                     const std::function<void()>& requestUpdate) {
    if (!active) return false;
    const int count = std::min<int>(ownedStrings.size(), MAX_OPTIONS);
    if (previous) {
      selectedIndex = (selectedIndex - 1 + count) % count;
      requestUpdate();
      return true;
    } else if (next) {
      selectedIndex = (selectedIndex + 1) % count;
      requestUpdate();
      return true;
    } else if (confirm) {
      active = false;
      if (onSelectCallback) onSelectCallback(selectedIndex);
      requestUpdate();
      return true;
    } else if (back) {
      active = false;
      requestUpdate();
      return true;
    }
    return true;
  }

  bool processRender(GfxRenderer& renderer, const MappedInputManager& input) const {
    if (!active) return false;
    const bool row = buttonRow(renderer);
    const auto popupLabels = input.mapLabels(tr(STR_BACK), tr(STR_SELECT), row ? tr(STR_DIR_LEFT) : tr(STR_DIR_UP),
                                             row ? tr(STR_DIR_RIGHT) : tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, popupLabels.btn1, popupLabels.btn2, popupLabels.btn3, popupLabels.btn4);
    render(renderer);
    renderer.displayBuffer();
    return true;
  }

  // Setka dialog (sova-fonts-unbounded-v2 "Обновить прошивку?"): a white card
  // with a 2 px frame, the title SemiBold and the body Regular, both left
  // aligned and wrapped; one or two options sit in a row ("Отмена ▌Удалить"),
  // more become rows like the Library book menu. The focused option carries
  // the bar and SemiBold. A full-width band around the card is cleared and
  // ruled so the screen underneath never shows through at its edges.
  void render(const GfxRenderer& renderer) const {
    if (!active) return;
    namespace fui = freeink::ui;
    using namespace setka;

    // Per-render target: tracks the live orientation; holds no state.
    fui::GfxRendererTarget target = makeUiTarget(renderer);
    // Frame stores a const DeviceContext&; keep it in a local that outlives
    // the frame (a deviceContext() temporary would dangle).
    const fui::DeviceContext device = target.deviceContext();
    // Routing happens on the loop task against the member buffer; the frame
    // itself never dispatches, so it gets an empty snapshot.
    const fui::InputSnapshot noInput{};

    // Builds into the generation handleInput()'s routePublished()/
    // publishedData() aren't currently reading, so the loop task never sees
    // this table mid-rebuild — see publish() below and
    // InteractionBuffer::beginPublishCycle().
    interactions.beginPublishCycle();
    fui::Frame<INTERACTION_CAPACITY> frame(target, device, noInput, interactions);

    const int screenWidth = renderer.getScreenWidth();
    const int screenHeight = renderer.getScreenHeight();
    const int count = std::min<int>(ownedStrings.size(), MAX_OPTIONS);
    const int cardX = kSidePadding;
    const int cardWidth = screenWidth - 2 * kSidePadding;
    const int textX = cardX + kDialogSidePadding;
    const int textWidth = cardWidth - 2 * kDialogSidePadding;
    const int titleLine = renderer.getLineHeight(kText12Font);
    const int bodyLine = renderer.getLineHeight(kText10Font);

    const auto titleLines = title.empty() ? std::vector<std::string>{}
                                          : renderer.wrappedText(kText12Font, title.c_str(), textWidth, 3, kSemiBold);
    const auto headlineLines = headline.empty() ? std::vector<std::string>{}
                                                : renderer.wrappedText(kText10Font, headline.c_str(), textWidth, 3);
    const auto messageLines =
        message.empty() ? std::vector<std::string>{} : renderer.wrappedText(kText10Font, message.c_str(), textWidth, 6);
    const int bodyLines = static_cast<int>(headlineLines.size() + messageLines.size());
    const int textHeight = static_cast<int>(titleLines.size()) * titleLine +
                           (!titleLines.empty() && bodyLines > 0 ? kBodyGap : 0) + bodyLines * bodyLine;

    const bool inRow = buttonRow(renderer);
    const int maxCardHeight = screenHeight - 2 * kBandMargin - 2 * kRuleWidth;
    const int textBlock = kDialogPadding + textHeight + (textHeight > 0 ? kOptionsGap : 0);
    const int rowHeight = inRow ? kButtonRowHeight
                                : std::clamp((maxCardHeight - textBlock - kRowsBottomPadding) / std::max(1, count),
                                             kMinTouch, kListRowHeight);
    const int optionsHeight = inRow ? kButtonRowHeight : count * rowHeight + kRowsBottomPadding;
    const int cardHeight = std::min(maxCardHeight, textBlock + optionsHeight);
    const int cardY = std::max(kBandMargin + kRuleWidth, (screenHeight - cardHeight) / 2);

    if (!standalone) {
      const int bandTop = std::max(0, cardY - kBandMargin);
      const int bandBottom = std::min(screenHeight, cardY + cardHeight + kBandMargin);
      renderer.fillRect(0, bandTop, screenWidth, bandBottom - bandTop, false);
      if (bandTop > 0) drawRule(renderer, 0, bandTop - kRuleWidth, screenWidth);
      if (bandBottom < screenHeight) drawRule(renderer, 0, bandBottom, screenWidth);
    } else {
      renderer.fillRect(cardX, cardY, cardWidth, cardHeight, false);
    }
    renderer.drawRect(cardX, cardY, cardWidth, cardHeight, kRuleWidth, true);

    // Chrome guard first, options after: route() scans newest-first, so the
    // option buttons win inside the dialog and the guard absorbs the rest.
    frame.hit(fui::makeRect(cardX, cardY, cardWidth, cardHeight), ACTION_CHROME, 0, fui::InputTouch);

    int y = cardY + kDialogPadding;
    for (const auto& line : titleLines) {
      renderer.drawText(kText12Font, textX, y, line.c_str(), true, kSemiBold);
      y += titleLine;
    }
    if (!titleLines.empty() && bodyLines > 0) y += kBodyGap;
    for (const auto* lines : {&headlineLines, &messageLines}) {
      for (const auto& line : *lines) {
        renderer.drawText(kText10Font, textX, y, line.c_str());
        y += bodyLine;
      }
    }

    const int optionsTop = cardY + textBlock;
    if (inRow) {
      // The last option is the action and sits on the right; a lone one too.
      const int textTop = optionsTop + (kButtonRowHeight - titleLine) / 2;
      for (int i = 0; i < count; ++i) {
        const bool focused = i == selectedIndex;
        const auto style = focused ? kSemiBold : EpdFontFamily::REGULAR;
        const char* label = ownedStrings[i].c_str();
        const bool right = i == count - 1;
        const int labelX = right ? textX + textWidth - renderer.getTextWidth(kText12Font, label, style) : textX;
        if (focused) drawSelectionBar(renderer, labelX - kButtonBarSpan, textTop, titleLine);
        renderer.drawText(kText12Font, labelX, textTop, label, true, style);
        const int hitX = right && count == 2 ? cardX + cardWidth / 2 : cardX;
        const int hitWidth = count == 2 ? cardWidth / 2 : cardWidth;
        frame.hit(fui::makeRect(hitX, optionsTop, hitWidth, kButtonRowHeight), ACTION_OPTION, static_cast<int16_t>(i),
                  fui::InputTouch);
      }
    } else {
      const int labelX = textX + kRowTextX - kSelectionBarX;
      const int labelWidth = textX + textWidth - labelX;
      // Rows that do not fit scroll with the focus.
      const int shown =
          std::max(1, std::min(count, (cardY + cardHeight - optionsTop - kRowsBottomPadding) / rowHeight));
      const int first = std::clamp(selectedIndex - shown + 1, 0, count - shown);
      for (int i = first; i < first + shown; ++i) {
        const int top = optionsTop + (i - first) * rowHeight;
        const bool focused = i == selectedIndex;
        const auto style = focused ? kSemiBold : EpdFontFamily::REGULAR;
        if (focused) drawSelectionBar(renderer, textX, top, rowHeight);
        renderer.drawText(kText12Font, labelX, top + (rowHeight - titleLine) / 2,
                          renderer.truncatedText(kText12Font, ownedStrings[i].c_str(), labelWidth, style).c_str(), true,
                          style);
        frame.hit(fui::makeRect(cardX, top, cardWidth, rowHeight), ACTION_OPTION, static_cast<int16_t>(i),
                  fui::InputTouch);
      }
    }
    // Atomically make this generation the one handleInput() reads, now that
    // every hit() call for this frame is done.
    interactions.publish();
    uiReady = true;
  }

  // One or two options share a row when both labels fit side by side.
  bool buttonRow(const GfxRenderer& renderer) const {
    using namespace setka;
    const int count = std::min<int>(ownedStrings.size(), MAX_OPTIONS);
    if (count > 2) return false;
    int labelsWidth = kButtonGap;
    for (int i = 0; i < count; ++i) {
      labelsWidth += renderer.getTextWidth(kText12Font, ownedStrings[i].c_str(), kSemiBold) + kButtonBarSpan;
    }
    return labelsWidth <= renderer.getScreenWidth() - 2 * (kSidePadding + kDialogSidePadding);
  }

  // The dialog is the only thing on a cleared screen (ConfirmationActivity):
  // no band to clear around it.
  void setStandalone(const bool value) { standalone = value; }

  bool isActive() const { return active; }

  // Close without firing the callback (the surface under the popup is going
  // away, e.g. its host screen closes from outside the popup's own input).
  void dismiss() {
    active = false;
    onSelectCallback = nullptr;
  }

 private:
  // The dialog has no scrolling, so options past MAX_OPTIONS would render off
  // screen anyway; a fixed cap keeps the interaction table small. +1 slot for
  // the chrome guard rect.
  static constexpr int MAX_OPTIONS = 16;
  static constexpr size_t INTERACTION_CAPACITY = MAX_OPTIONS + 1;
  static constexpr freeink::ui::ActionId ACTION_OPTION = 1;
  static constexpr freeink::ui::ActionId ACTION_CHROME = 2;
  static constexpr int kDialogPadding = 24;                             // above the title
  static constexpr int kDialogSidePadding = 32;                         // clears the bar of a left option
  static constexpr int kBodyGap = 4;                                    // title to body
  static constexpr int kOptionsGap = 8;                                 // text to the options
  static constexpr int kButtonRowHeight = 56;                           // one row of 1-2 options
  static constexpr int kButtonGap = 24;                                 // least air between two option labels
  static constexpr int kButtonBarSpan = setka::kSelectionBarWidth + 6;  // bar + gap before an option label
  static constexpr int kRowsBottomPadding = 8;
  static constexpr int kBandMargin = 20;  // cleared band around the card

  void activate(int currentIndex, std::function<void(int)> onSelect) {
    const int count = std::min<int>(ownedStrings.size(), MAX_OPTIONS);
    selectedIndex = currentIndex >= 0 && currentIndex < count ? currentIndex : 0;
    onSelectCallback = std::move(onSelect);
    message.clear();
    uiReady = false;
    active = count > 0;
  }

  bool active = false;
  bool standalone = false;
  std::string title;
  std::string headline;
  std::string message;
  std::vector<std::string> ownedStrings;
  int selectedIndex = 0;
  std::function<void(int)> onSelectCallback;
  // Written by the render task (frame registration), routed by the loop task;
  // uiReady closes the rebuild window exactly like UiListActivity::uiReady.
  mutable freeink::ui::InteractionBuffer<INTERACTION_CAPACITY> interactions;
  mutable std::atomic<bool> uiReady{false};
};
