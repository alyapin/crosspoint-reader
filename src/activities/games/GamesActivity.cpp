#include "GamesActivity.h"

#include <I18n.h>

#include <iterator>
#include <memory>

namespace {
struct Game {
  StrId name;
  std::unique_ptr<Activity> (*open)(GfxRenderer&, MappedInputManager&);
};

template <typename T>
std::unique_ptr<Activity> openGame(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<T>(renderer, mappedInput);
}

// A game registers with one line: {StrId::STR_<NAME>, &openGame<GameActivity>},
// The trailing empty entry keeps the array valid while no game is registered.
constexpr Game kGames[] = {
    {},
};
constexpr int kGameCount = static_cast<int>(std::size(kGames)) - 1;
}  // namespace

int GamesActivity::listCount() const { return kGameCount; }

const char* GamesActivity::headerTitle() const { return tr(STR_GAMES); }

const char* GamesActivity::rowLabel(const int index) const { return I18N.get(kGames[index].name); }

const char* GamesActivity::emptyText() const { return tr(STR_NO_GAMES); }

void GamesActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (auto game = kGames[index].open(renderer, mappedInput)) openChild(std::move(game));
}
