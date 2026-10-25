<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Luo Tianyi AI Passport

A customizable voice companion for the FoloToy AI Passport, built with ESP-IDF
and LVGL. Press a button, speak, and read or listen to the answer on a compact
ESP32-C3 device.

This is an unofficial fan project derived from
[FoloToy AI Passport](https://github.com/FoloToy/ai-passport). It is not affiliated
with the owners of the Luo Tianyi character. The default synthesized voice is a
Tencent Cloud stock voice, not an official Luo Tianyi voice or voice clone.

## Features

- Voice conversations with on-screen transcripts and spoken replies.
- A bright `#66CCFF` theme with a configurable assistant name and accent color.
- Web settings for character presets, custom instructions, TTS, and volume.
- Upload wallpaper from a browser; the image is cropped and converted to
  240 x 320 RGB565 before being stored on the device.
- Primary and optional backup Wi-Fi configured from the web page. Three
  consecutive connection failures switch networks; startup tries the primary.
- Search planning based on the question and recent conversation, with bounded
  search context and graceful fallback when search is unavailable.
- Chinese text and a supplementary symbol font for supported kaomoji.
- Persistent conversation memory and an explicit memory-clear action.
- Long TTS replies split into at most 120 Unicode characters per request and
  played sequentially. Service errors are shown as their actual error codes.
- Direct cloud operation or an optional computer gateway.

## Hardware and services

| Item | Requirement |
| --- | --- |
| Board | FoloToy AI Passport, ESP32-C3 |
| Flash / PSRAM | 8 MB / no PSRAM |
| Toolchain | ESP-IDF 5.5.3 |
| Display | 240 x 320, LVGL |
| Network | 2.4 GHz Wi-Fi with internet access |
| Speech | Tencent Cloud speech recognition and speech synthesis |
| Conversation | Configurable chat-completions endpoint and model |

The default chat configuration uses DeepSeek. You can configure another
compatible endpoint and model in advanced settings; compatibility depends on
that provider. Cloud services require your own credentials and may charge for
usage. Activate speech recognition and TTS separately with the provider.

## First-time setup

1. Build and flash the firmware for the supported board.
2. Connect your phone or computer to the device's `Passport-XXXXXX` hotspot.
   The hotspot password is displayed on the screen.
3. Open `http://192.168.4.1` while connected to that hotspot.
4. Enter the primary Wi-Fi settings and, optionally, a different backup network.
5. For direct mode, enter the chat API key and Tencent Cloud SecretId/SecretKey.
   Advanced settings expose the endpoint and model configuration.
6. Choose the name, theme color, persona, TTS setting, and volume, then save.
   The device restarts and connects to your Wi-Fi.

Hold **OK** while idle to return to settings. Wallpaper upload and removal also
restart the device. The configuration server operates in setup mode, not on the
normal router connection. Credentials are stored locally in device NVS; the web
page does not read back saved API secrets or Wi-Fi passwords.

## Buttons

| Action | Behavior |
| --- | --- |
| Click OK while idle | Start recording; speak after the recording prompt |
| Click OK during recording | Finish early; recording is limited to 8 seconds |
| Click UP / DOWN | Scroll the conversation |
| Click OK during TTS | Stop playback and cancel remaining segments |
| Click UP / DOWN during TTS | Adjust volume |
| Hold OK while idle | Restart into web setup |
| Hold DOWN while idle | Request memory clearing; confirm with OK |

## Build

Activate an installed ESP-IDF **5.5.3** environment and run from the repository
root:

```bash
idf.py set-target esp32c3
idf.py build
```

For the repository's complete validation and verified firmware package:

```bash
./tools/validate.sh
```

The complete gate requires a compatible shell and host C compiler as well as
ESP-IDF. See [build instructions](docs/development/engineering/build-and-test.md)
and [environment setup](docs/development/engineering/environment-setup.md).
Generated firmware and matching debug artifacts remain under the ignored
`build/` directory.

To flash the current build, replacing `PORT` with the actual device port:

```bash
idf.py -p PORT flash
```

The partition layout includes a wallpaper partition. A compatible segmented
flash can preserve settings and wallpaper; flashing a merged image from `0x0`
can reset stored data. Do not erase the whole chip as a routine upgrade step.
See the [firmware and data policy](docs/development/engineering/firmware-layout.md).

## Optional computer gateway

The Python gateway uses the standard library. Configure your environment using
[`services/voice_gateway/.env.example`](services/voice_gateway/.env.example),
then run:

```bash
python -m services.voice_gateway
```

The `.env` file is not loaded automatically. Export its values in your shell
before starting the service. The device must use the computer's reachable LAN
address and the same gateway token. Do not use `localhost` on the device.
Keep plain HTTP gateway access within a trusted LAN.

## Source layout

| Path | Contents |
| --- | --- |
| `main/` | Voice workflow, web setup, UI, Wi-Fi policy, memory, TTS |
| `components/bsp/` | Board support for display, audio, buttons, and power |
| `assets/fonts/` | Chinese and symbol fonts with their license notices |
| `services/voice_gateway/` | Optional Python voice gateway |
| `tests/` | Host-side checks and service tests |
| `tools/` | Validation, firmware packaging, and UI preview tools |
| `docs/` | Application, hardware, and development documentation |

See the [application guide](docs/applications/deepseek-voice.md) for detailed
behavior and the [documentation index](docs/README.md) for board information.

## Limits and privacy

- This is an online assistant; network access and configured services are needed.
- Search results and model answers can be wrong. Colored emoji and arbitrary
  Unicode are not supported by the bundled font set.
- Recorded speech and conversation text are sent to your configured providers.
  The optional gateway stores conversation history in a local SQLite database.
- The published Wi-Fi preset is empty. Enter credentials through setup, or use
  the ignored local preset workflow when building private firmware.
- Do not publish API keys, Wi-Fi passwords, `.env` files, device memory, raw
  logs, or private firmware/debug bundles.

## License and acknowledgements

Project code follows the [MIT license](LICENSE), retaining the upstream FoloToy
copyright notice. Fonts and other third-party assets retain their respective
licenses; see the [asset inventory](assets/README.md). Character-related rights
remain with their respective owners.
