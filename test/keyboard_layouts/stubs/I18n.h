#pragma once

#include <cstdint>

enum class Language : uint8_t { EN, FR, DE, ES, RU, UK, BE, KK, HE, AR };

struct I18nStub {
  Language language = Language::EN;
  Language getLanguage() const { return language; }
};
extern I18nStub I18N;
