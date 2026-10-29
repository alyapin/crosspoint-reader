#include <gtest/gtest.h>

#include "CrossPointSettings.h"
#include "activities/util/KeyboardLayoutSet.h"

I18nStub I18N;
CrossPointSettings SETTINGS;

namespace {
using freeink::ui::KeyboardLayoutId;
using keyboard_layouts::bitAt;

class KeyboardLayoutsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    I18N.language = Language::EN;
    SETTINGS.keyboardLayouts = 0;
  }
};

TEST_F(KeyboardLayoutsTest, LatinStartKeepsALatinUiLayout) {
  I18N.language = Language::FR;
  EXPECT_EQ(keyboard_layouts::startingLatinLayout(), KeyboardLayoutId::AzertyFr);
}

TEST_F(KeyboardLayoutsTest, CyrillicUiStartsLatinFieldsOnEnglish) {
  I18N.language = Language::RU;
  EXPECT_EQ(keyboard_layouts::startingLayout(), KeyboardLayoutId::CyrillicRu);
  EXPECT_EQ(keyboard_layouts::startingLatinLayout(), KeyboardLayoutId::QwertyEn);
}

TEST_F(KeyboardLayoutsTest, LatinStartUsesTheEnabledLatinLayout) {
  I18N.language = Language::RU;
  SETTINGS.keyboardLayouts = bitAt(2) | bitAt(4);  // German and Russian
  EXPECT_EQ(keyboard_layouts::startingLatinLayout(), KeyboardLayoutId::QwertzDe);
}

TEST_F(KeyboardLayoutsTest, CyrillicOnlyMaskStillStartsLatinFieldsOnEnglish) {
  I18N.language = Language::RU;
  SETTINGS.keyboardLayouts = bitAt(4);
  EXPECT_EQ(keyboard_layouts::startingLatinLayout(), KeyboardLayoutId::QwertyEn);
}

}  // namespace
