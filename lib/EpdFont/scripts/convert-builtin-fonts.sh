#!/bin/bash

set -e

cd "$(dirname "$0")"

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
NOTOSANS_FONT_SIZES=(12 14 16 18)

for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum --zopfli > $output_path
    echo "Generated $output_path"
  done
done

UI_FONT_SIZES=(10 12)
UI_FONT_STYLES=("Regular" "Bold")

# Arabic glyphs for UI text (menus, file browser titles). The built-in fonts
# must cover the *output* of MiniBidi's do_shape() — contextual presentation
# forms — not base letters, or shaped UI text silently drops glyphs.
# Curated for firmware-size budget: core Arabic (Presentation Forms-B,
# incl. the Lam-Alef ligature forms) plus the Farsi/Urdu extra letters'
# Presentation Forms-A blocks, the few characters shaping leaves at their
# base codepoint, Arabic punctuation, and both digit sets. No harakat and
# no Sindhi/Pashto/Kurdish forms — book text gets those from SD-card fonts.
ARABIC_INTERVALS=(
  --additional-intervals 0x060C,0x060C  # Arabic comma
  --additional-intervals 0x061B,0x061B  # Arabic semicolon
  --additional-intervals 0x061F,0x061F  # Arabic question mark
  --additional-intervals 0x0621,0x0621  # hamza (non-joining, never shaped)
  --additional-intervals 0x0640,0x0640  # tatweel
  --additional-intervals 0x0654,0x0654  # Persian/Urdu ezafe hamza, Arabic hamza carriers
  --additional-intervals 0x0660,0x0669  # Arabic-Indic digits
  --additional-intervals 0x06BA,0x06BA  # noon ghunna base (initial/medial keep base cp)
  --additional-intervals 0x06D4,0x06D4  # Urdu full stop
  --additional-intervals 0x06D5,0x06D5  # ae (isolated; Kurdish/Uyghur/Ottoman) — has no presentation form
  --additional-intervals 0x06F0,0x06F9  # extended Arabic-Indic digits (Farsi/Urdu)
  --additional-intervals 0xFB56,0xFB59  # peh (Farsi)
  --additional-intervals 0xFB66,0xFB69  # tteh (Urdu)
  --additional-intervals 0xFB7A,0xFB7D  # tcheh (Farsi)
  --additional-intervals 0xFB88,0xFB95  # ddal, jeh, rreh (Urdu), keheh, gaf (Farsi/Urdu)
  --additional-intervals 0xFB9E,0xFB9F  # noon ghunna isolated/final (Urdu)
  --additional-intervals 0xFBA6,0xFBB1  # heh goal, heh doachashmee, yeh barree(+hamza) (Urdu)
  --additional-intervals 0xFBFC,0xFBFF  # farsi yeh (Farsi/Urdu)
  --additional-intervals 0xFE80,0xFEFC  # Presentation Forms-B: core Arabic + Lam-Alef
)

for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/Ubuntu/Ubuntu-${style}.ttf"
    latin_fallback_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    hebrew_path="../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-${style}.ttf"
    arabic_path="../builtinFonts/source/NotoSansArabic/NotoSansArabic-${style}.ttf"
    # Ubuntu lacks the Latin Extended Additional block (U+1EA0-U+1EF9) used for
    # Vietnamese tone marks. Append a Vietnamese-only Ubuntu cut so those glyphs
    # are filled from it while every glyph Ubuntu already has stays unchanged
    # (fontstack is ordered by descending priority).
    viet_path="../builtinFonts/source/Ubuntu/Ubuntu-Vietnamese-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path $latin_fallback_path $hebrew_path $arabic_path $viet_path \
      --additional-intervals 0x05D0,0x05EA "${ARABIC_INTERVALS[@]}" > $output_path
    echo "Generated $output_path"
  done
done

# The small Ubuntu face, so Ubuntu UI text stays in one family at every size.
python fontconvert.py ubuntu_8_regular 8 \
  ../builtinFonts/source/Ubuntu/Ubuntu-Regular.ttf \
  ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf \
  ../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-Regular.ttf \
  ../builtinFonts/source/NotoSansArabic/NotoSansArabic-Regular.ttf \
  ../builtinFonts/source/Ubuntu/Ubuntu-Vietnamese-Regular.ttf \
  --additional-intervals 0x05D0,0x05EA "${ARABIC_INTERVALS[@]}" > ../builtinFonts/ubuntu_8_regular.h

# X4 Pro faces, compiled into X4 Pro builds only (roles in src/fontIds.h and
# src/components/setka/SetkaStyle.h, registered in src/main.cpp).
# google/fonts ships Golos Text and Unbounded as variable fonts; the committed
# static cuts come from:
#   fonttools varLib.instancer 'Unbounded[wght].ttf' wght=400 --static --update-name-table -o Unbounded-Regular.ttf
# (wght=300 for Unbounded-Light, 600 for Unbounded-SemiBold, 400 for
# GolosText-Regular). Both are unhinted, so the text faces use the auto-hinter
# for even 1-bit stems.
#
# X4 Pro UI text (the UI_10 / UI_12 / SMALL roles in fontIds.h): Unbounded
# Regular with SemiBold in the bold slot, one size below the Ubuntu roles since
# it runs wide, and Light for captions. Latin, Cyrillic and the punctuation the
# UI uses only; the renderer routes any other string to Ubuntu.
UI_TEXT_INTERVALS=(
  --only-additional-intervals
  --additional-intervals 0x20,0x7E      # ASCII
  --additional-intervals 0xA0,0x17F     # Latin-1, Latin Extended-A
  --additional-intervals 0x18F,0x18F    # Azerbaijani schwa
  --additional-intervals 0x218,0x21B    # Romanian comma-below letters
  --additional-intervals 0x259,0x259    # schwa
  --additional-intervals 0x400,0x45F    # Cyrillic
  --additional-intervals 0x490,0x491    # Ukrainian ghe with upturn
  --additional-intervals 0x2010,0x2027  # dashes, quotes, bullet, ellipsis
  --additional-intervals 0x2030,0x2030  # per mille
  --additional-intervals 0x2039,0x203A  # single guillemets
  --additional-intervals 0x20AC,0x20AC  # euro
  --additional-intervals 0x20BD,0x20BD  # ruble
  --additional-intervals 0x2116,0x2116  # numero sign
  --additional-intervals 0x2122,0x2122  # trade mark
  --additional-intervals 0x2190,0x2193  # arrows
  --additional-intervals 0x2212,0x2212  # minus sign
  --additional-intervals 0x2248,0x2248  # almost equal
)
for face in 9:Regular 9:SemiBold 11:Regular 11:SemiBold 7:Light; do
  size=${face%%:*}
  style=${face##*:}
  font_name="unbounded_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
  output_path="../builtinFonts/${font_name}.h"
  python fontconvert.py $font_name $size ../builtinFonts/source/Unbounded/Unbounded-${style}.ttf \
    --force-autohint "${UI_TEXT_INTERVALS[@]}" > $output_path
  echo "Generated $output_path"
done

# Dense game text (scanword clue cells) keeps the narrower Golos Text.
python fontconvert.py golostext_8_regular 8 ../builtinFonts/source/GolosText/GolosText-Regular.ttf \
  --force-autohint "${UI_TEXT_INTERVALS[@]}" > ../builtinFonts/golostext_8_regular.h
echo "Generated ../builtinFonts/golostext_8_regular.h"

# Hero numbers: digits-only Unbounded SemiBold. Cap height is 0.75 em and the
# converter rasterizes at 150 DPI, so 61 pt is ~96 px, 31 pt ~48 px, 19 pt ~30 px,
# 14 pt ~22 px and 11 pt ~17 px.
HERO_INTERVALS=(
  --only-additional-intervals
  --additional-intervals 0x20,0x20      # space
  --additional-intervals 0x25,0x25      # %
  --additional-intervals 0x2B,0x2E      # + , - .
  --additional-intervals 0x30,0x3A      # 0-9 :
  --additional-intervals 0x2212,0x2212  # minus sign
)
# The 31 pt cut also spells the device name «Сова» for the receive screen.
SOVA_INTERVALS=(
  --additional-intervals 0x0421,0x0421  # С
  --additional-intervals 0x0430,0x0430  # а
  --additional-intervals 0x0432,0x0432  # в
  --additional-intervals 0x043E,0x043E  # о
)
for size in 61 31 19 14 11; do
  output_path="../builtinFonts/unbounded_${size}_digits.h"
  extra=()
  [ "$size" = 31 ] && extra=("${SOVA_INTERVALS[@]}")
  python fontconvert.py unbounded_${size}_digits $size ../builtinFonts/source/Unbounded/Unbounded-SemiBold.ttf \
    "${HERO_INTERVALS[@]}" "${extra[@]}" > $output_path
  echo "Generated $output_path"
done

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
