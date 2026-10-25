<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

### 语音助手字库

[`fonts/passport_noto_sans_sc_16.c`](fonts/passport_noto_sans_sc_16.c) 是使用
`lv_font_conv` 1.5.3 生成的 16 px、2 bpp、不压缩点阵字库。源字体为锁定的
[LVGL 9.5.0 测试资源](https://github.com/lvgl/lvgl/tree/v9.5.0/tests/src/test_files/fonts/noto)
中的 Noto Sans SC Regular `2.004-H2`，原始 SIL OFL 1.1 许可保存在
[`fonts/NotoSansSC-OFL.txt`](fonts/NotoSansSC-OFL.txt)。源 TTF 的 SHA-256 为
`ae82f4e2a55e1316a55bcc1d05e9555ce08d8bda07e893b486896b626fd852ff`。

覆盖可打印 ASCII、Latin-1、部分通用标点、U+3000–301F、U+4E00–9FEF 的每个汉字及
全角 U+FF01–FF60。不承诺覆盖 U+9FF0–9FFF、汉字扩展区及表情符号。不支持的字符
显示为 `?` 并给出提示。`tests/test_assistant_font.py` 检查实际字形映射，包含缺字反例。

安装受管理依赖后，在仓库根目录使用固定版本转换器重新生成：

```bash
lv_font_conv --font managed_components/lvgl__lvgl/tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf \
  --range 0x20-0x7E,0xA0-0xFF,0x2010-0x2027,0x2030-0x203B,0x3000-0x301F,0x4E00-0x9FEF,0xFF01-0xFF60 \
  --size 16 --bpp 2 --no-compress --no-kerning --format lvgl \
  --lv-font-name passport_noto_sans_sc_16 --lv-include lvgl.h \
  --output assets/fonts/passport_noto_sans_sc_16.c
```

生成源码由 `main/CMakeLists.txt` 编译一次，所有中文及动态设备标签均明确选择此字体。
位图超过小格式索引上限，必须启用 `CONFIG_LV_FONT_FMT_TXT_LARGE=y`。位图约占用
1.28 MB Flash，不会完整复制到 RAM。行高为 21 px。本机界面预览使用相同的
24 KiB LVGL 内存池，实际板载显示及内部堆峰值仍需实机测试。

### 语音助手符号后备字库

`fonts/passport_kaomoji_16.c` 通过应用持有的 LVGL 后备字体描述符为中文字库补充
16 px、2 bpp、不压缩的符号。覆盖源字体实际提供的希腊字母、西里尔字母、假名、
音标、箭头、数学、制表及装饰符号，以及颜文字常用的部分泰文字符。已验证样例见
`fonts/kaomoji-samples.txt`；不承诺任意 Unicode 或彩色 emoji。两个字库基线均为
5 px，混合文字使用 22 px 行高。

来源为锁定的 LVGL Noto Sans SC、DejaVu Sans 字体，以及保存在 `fonts/` 的
[Noto Sans Regular](https://github.com/notofonts/noto-fonts/tree/main/hinted/ttf/NotoSans)
和 [Noto Sans Thai Regular](https://github.com/notofonts/noto-fonts/tree/main/hinted/ttf/NotoSansThai)。
后两者 SHA-256 分别为
`b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5` 和
`404ddfb5ed0aaa6b6ec8a85700d682978992062d67da93903967b56cbd9a4acc`。
许可保存在 `fonts/DejaVuSans-LICENSE.txt`、`fonts/NotoSans-OFL.txt` 及已有的
Noto Sans SC 许可文件中。生成的 C 文件头部记录完整的 `lv_font_conv` 1.5.3 命令及
源字符范围。只读位图保存在 Flash。重新生成后运行
`python tests/test_assistant_font.py`，检查 `kaomoji.ppm` 界面预览，并单独验证实机显示。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
