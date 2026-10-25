<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

### Voice assistant font

[`fonts/passport_noto_sans_sc_16.c`](fonts/passport_noto_sans_sc_16.c) is a
16 px, 2 bpp, uncompressed bitmap font generated with `lv_font_conv` 1.5.3.
Its source is Noto Sans SC Regular, version `2.004-H2`, bundled in the pinned
[LVGL 9.5.0 test assets](https://github.com/lvgl/lvgl/tree/v9.5.0/tests/src/test_files/fonts/noto).
The original SIL OFL 1.1 is retained as
[`fonts/NotoSansSC-OFL.txt`](fonts/NotoSansSC-OFL.txt). The source TTF SHA-256 is
`ae82f4e2a55e1316a55bcc1d05e9555ce08d8bda07e893b486896b626fd852ff`.

Coverage includes printable ASCII, Latin-1, selected general punctuation,
U+3000–301F, every ideograph U+4E00–9FEF, and full-width U+FF01–FF60.
U+9FF0–9FFF, CJK extensions and emoji are outside this asset's contract.
The UI shows `?` plus a notice for unsupported characters. Glyph coverage is
checked by `tests/test_assistant_font.py`, including negative probes.

After managed dependencies are installed, regenerate from the repository root
with the pinned converter:

```bash
lv_font_conv --font managed_components/lvgl__lvgl/tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf \
  --range 0x20-0x7E,0xA0-0xFF,0x2010-0x2027,0x2030-0x203B,0x3000-0x301F,0x4E00-0x9FEF,0xFF01-0xFF60 \
  --size 16 --bpp 2 --no-compress --no-kerning --format lvgl \
  --lv-font-name passport_noto_sans_sc_16 --lv-include lvgl.h \
  --output assets/fonts/passport_noto_sans_sc_16.c
```

The generated C source is compiled once by `main/CMakeLists.txt`; all Chinese
and dynamic device labels select it explicitly. `CONFIG_LV_FONT_FMT_TXT_LARGE=y`
is required because the bitmap exceeds the small format's index limit. The
bitmap occupies approximately 1.28 MB in Flash; it is not copied into RAM.
The line height is 21 px. Native UI previews use the same 24 KiB LVGL pool;
on-board glyph rendering and peak internal heap still require device tests.

### Voice assistant symbol fallback

`fonts/passport_kaomoji_16.c` adds 16 px, 2 bpp, uncompressed symbols to the
Chinese font through an application-owned LVGL fallback descriptor. It includes
available Greek/Cyrillic, kana, IPA, arrows, math, box drawing and decorative
symbols, plus selected Thai characters used in kaomoji. The verified inventory
is `fonts/kaomoji-samples.txt`; this does not provide arbitrary Unicode or color
emoji. Both descriptors have a 5 px baseline; mixed text uses a 22 px line height.

Sources: the pinned LVGL Noto Sans SC and DejaVu Sans test fonts, plus
[Noto Sans Regular](https://github.com/notofonts/noto-fonts/tree/main/hinted/ttf/NotoSans)
and [Noto Sans Thai Regular](https://github.com/notofonts/noto-fonts/tree/main/hinted/ttf/NotoSansThai)
stored in `fonts/`. Their respective SHA-256 values are
`b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5` and
`404ddfb5ed0aaa6b6ec8a85700d682978992062d67da93903967b56cbd9a4acc`.
Licenses are retained as `fonts/DejaVuSans-LICENSE.txt`, `fonts/NotoSans-OFL.txt`
and the existing Noto Sans SC license. The exact `lv_font_conv` 1.5.3 command and
source ranges are recorded in the generated C file header. Its read-only bitmap
stays in Flash. Run `python tests/test_assistant_font.py` after regeneration;
inspect the `kaomoji.ppm` UI preview and verify device rendering separately.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.
