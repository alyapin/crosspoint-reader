#include "SetkaStyle.h"

#include <FreeInkUIGfxRenderer.h>

#include <algorithm>

#include "components/UIScale.h"
#include "components/UITheme.h"

namespace setka {

int drawStatusLine(const GfxRenderer& renderer) {
  namespace fui = freeink::ui;
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const auto spec = uiScaleSpec();
  fui::GfxRendererFrame<1> ui(renderer, spec.smallFontId, spec.bodyFontId, spec.titleFontId);
  ui.target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  ui.target.setFont(fui::GfxRendererTarget::FONT_LABEL, SMALL_FONT_ID);
  fui::HeaderProps props;
  BaseTheme::applyHeaderStatus(renderer, props);
  props.styles = fui::plainStyles();  // no band fill over what the screen already drew
  const int strip = metrics.batteryBarHeight;
  // headerBand lowers the band's top by the board's header offset; keep the strip whole.
  const int height = strip + std::max(0, static_cast<int>(BoardConfig::ACTIVE.headerOffsetY));
  const fui::Rect band =
      BaseTheme::headerBand(renderer,
                            fui::Rect{0, static_cast<int16_t>(metrics.topPadding),
                                      static_cast<int16_t>(renderer.getScreenWidth()), static_cast<int16_t>(height)},
                            props);
  fui::header(ui.frame, band, props);
  return band.y + strip;
}

}  // namespace setka
