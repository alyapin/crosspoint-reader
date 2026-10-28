#include "GamesActivity.h"

#include <I18n.h>

#include <iterator>
#include <memory>

namespace {
struct Game {
  StrId name;
  const freeink::Icon* icon;  // 24 px Lucide icon
  std::unique_ptr<Activity> (*open)(GfxRenderer&, MappedInputManager&);
  // Right-aligned row status (e.g. an unfinished game); nullptr or "" for none.
  const char* (*status)();
};

template <typename T>
std::unique_ptr<Activity> openGame(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<T>(renderer, mappedInput);
}

// A game registers with one line:
//   {StrId::STR_<NAME>, &icon_<name>_24, &openGame<GameActivity>, &statusFn or nullptr},
// The trailing empty entry keeps the array valid while no game is registered.
constexpr Game kGames[] = {
    {},
};
constexpr int kGameCount = static_cast<int>(std::size(kGames)) - 1;
}  // namespace

int GamesActivity::listCount() const { return kGameCount; }

const char* GamesActivity::headerTitle() const { return tr(STR_GAMES); }

const char* GamesActivity::rowLabel(const int index) const { return I18N.get(kGames[index].name); }

const freeink::Icon* GamesActivity::rowIcon(const int index) const { return kGames[index].icon; }

const char* GamesActivity::rowStatus(const int index) const {
  const auto status = kGames[index].status;
  return status ? status() : nullptr;
}

const char* GamesActivity::emptyText() const { return tr(STR_NO_GAMES); }

void GamesActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (auto game = kGames[index].open(renderer, mappedInput)) openChild(std::move(game));
}
