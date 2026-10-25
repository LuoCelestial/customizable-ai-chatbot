<p align="right"><a href="deepseek-voice.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# DeepSeek voice assistant

This application replaces the hardware-test startup with a dedicated voice
conversation screen. The ESP32-C3 records microphone input and displays the
transcript and DeepSeek's answer. It supports Chinese text and two connection
modes in the same firmware. Replies are displayed as text, with optional Tencent
Cloud TTS playback and configurable volume. Wake-word detection is not implemented.

Tencent TTS splits long replies at sentence or word boundaries into segments of
at most 120 Unicode characters, without splitting UTF-8 sequences. Segments are
synthesized and played sequentially, streaming PCM without retaining the entire
audio in RAM. Stopping playback cancels the remaining segments. A failed segment
stops playback and displays the service error code (or the local/HTTP error),
rather than assuming that the service has not been activated.

| Mode | Network path | Persistent memory | API keys |
| --- | --- | --- | --- |
| Computer gateway | Device → computer → ASR → DeepSeek | SQLite on the computer; last 12 turns sent as context, up to 128 retained | On the computer |
| Direct cloud | Device → ASR → DeepSeek | Last 3 turns in device NVS; each stored question/answer has a UTF-8 byte limit | In device NVS |

Both modes use online speech recognition. The default is Tencent Cloud's
SentenceRecognition with the `16k_zh` engine. Existing compatible multipart
transcription endpoints remain supported. DeepSeek defaults to `deepseek-flash`
with thinking disabled and a short answer limit. Cloud calls can incur provider
charges. No provider keys are compiled into the supplied firmware.

## Controls

Both connection modes use the shared Luo Tianyi roleplay prompt in
`main/assistant_persona.txt`. It keeps the supplied character's gentle,
energetic and food-loving style, limits replies for the small screen, and
uses only the conversation history actually supplied. The device does not
claim to sing. When asked about its identity, it identifies
itself as an unofficial AI assistant. The supplied full character reference
is preserved in [Luo Tianyi character profile](luo-tianyi-persona.md).

- Click **OK** to start a request. Wait for the recording indication before
  speaking. The microphone streams 16 kHz, mono, signed 16-bit audio for up to
  8 seconds. Click **OK** again to finish early; the remaining upload is padded
  with silence to keep its length fixed.
- Click **UP/DOWN** to scroll the conversation.
- During TTS playback, click **OK** to stop or **UP/DOWN** to adjust volume.
- Hold **OK** while idle to restart into settings.
- Hold **DOWN** while idle to request clearing the current mode's memory.
  Click **OK** to confirm, or **UP/DOWN** to cancel. Gateway clearing needs the
  computer service to be reachable; direct memory can be cleared offline.
- Other commands are ignored during a network request. No automatic request
  retry is performed, avoiding duplicate paid requests after ambiguous failures.

The vivid interface uses `#66CCFF` as its primary color and labels assistant
messages as Luo Tianyi. It includes connection/recording/recognition/thinking/error
states, a scrollable conversation, a memory-clear confirmation, and a battery
reading at the upper right. Unknown battery readings are shown as unavailable.

## First setup and switching modes

1. After an explicitly approved firmware flash, start the device. With no valid
   settings it creates a WPA2 hotspot named `Passport-XXXXXX`; its fixed setup
   password is displayed on the screen.
2. Connect a phone or computer to that hotspot and open `http://192.168.4.1`.
   Remain connected even if the phone reports that this network has no Internet.
3. Enter the 2.4 GHz Wi-Fi name and password and select a mode. For gateway mode,
   enter the computer's LAN base URL, for example `http://192.168.1.100:8765`,
   and the same device token configured on the computer. Do not enter
   `localhost` or append `/v1/voice` here. For direct mode, enter the DeepSeek
   API key plus Tencent SecretId and SecretKey; advanced settings contain the full endpoint URLs and
   model names.
4. Save. The device restarts, closes its setup hotspot and joins Wi-Fi. Restore
   the phone's usual Wi-Fi connection. Click OK to test a short sentence.
5. Hold OK while idle to reopen setup or switch modes. Saved Wi-Fi can be reused; API settings must be entered again.
   The page never reads back stored keys. Power-cycling before
   saving exits a requested settings session and resumes the previous settings.

The setup page also accepts a local wallpaper image. The browser crops it to
the portrait screen, converts it to 240×320 RGB565, and uploads only the converted
pixels; the original file is not stored on the device. Applying or removing a
wallpaper restarts the device. Interrupted uploads remain invalid and the UI
falls back to the built-in `#66CCFF` theme.

Configuration is stored only after validation. Station-mode setup never
publishes the configuration server onto the normal Wi-Fi network. Direct cloud
URLs require HTTPS and use certificate verification; the device synchronizes
its clock with NTP before HTTPS requests. Gateway HTTP is intended for a trusted
private LAN. Use an HTTPS reverse proxy and a publicly trusted certificate when
access crosses an untrusted network. A router's client-isolation setting can
prevent the device from reaching the computer.

The setup page defaults to direct cloud mode. Its main view shows saved Wi-Fi,
the DeepSeek key, Tencent SecretId/SecretKey and one primary save button. Computer-gateway mode,
endpoint URLs and model names are under **More settings**. A short three-step
setup guide and the device button instructions appear on the same page.

## Primary and backup Wi-Fi

The web setup page supports a primary network and an optional backup network.
Enable **Backup Wi-Fi**, enter a different 2.4 GHz SSID and password, then use
**Save Wi-Fi settings only** to preserve the existing API keys and profile.
Passwords are never returned by `GET /wifi`. The page can reuse the saved backup,
replace it, or delete it by disabling the backup option and saving. Empty passwords
mean open networks; SSIDs are limited to 32 UTF-8 bytes and passwords to 8–63
bytes when nonempty. Saving other settings preserves the backup configuration.

Every reboot starts with the primary network. After three consecutive connection
failures, the device switches to the backup; after three failures there, it tries
the primary again. Obtaining an IP resets the failure count and retains the
connected network. A working backup is not interrupted to probe the primary.
This policy reacts to Wi-Fi disconnections, not cloud-provider errors or an
Internet outage while Wi-Fi remains associated. The existing 30-second warning
does not itself switch networks.

Backup credentials now live in the settings NVS blob, rather than macros in
`main.c`. Existing blobs keep their Wi-Fi, API keys, character profile, TTS and
volume values; the new backup is disabled until configured once in the web UI.
The formerly hardcoded backup is not silently copied into persistent settings.
The failover policy lives in `assistant_wifi_policy.c`; the main application
task applies driver changes and commits the selected target only on success.

## Source map for the current application

| Files | Responsibility |
| --- | --- |
| `main/main.c`, `assistant_state.*` | Startup, button/event dispatch, network recovery, voice worker and TTS handoff |
| `assistant_settings.*`, `assistant_storage.c` | Validation, defaults and version-compatible NVS settings |
| `assistant_setup.c`, `setup.html` | Hotspot-only authenticated configuration, profile, Wi-Fi and wallpaper endpoints |
| `assistant_client.c`, `assistant_search_prompt.txt` | Streamed ASR, search planning, bounded web summaries and chat requests |
| `assistant_memory.*`, `services/voice_gateway/gateway.py` | Direct-mode recent history and gateway SQLite history |
| `assistant_tencent.*`, `assistant_tts.*` | Tencent request signing and streamed speech decoding/playback |
| `assistant_weather.*` | Network-derived weather cache and local time formatting |
| `assistant_ui.*`, `assistant_wallpaper.*`, `assets/fonts/` | Locked LVGL rendering, wallpaper persistence and Chinese/symbol glyphs |

## Tencent Cloud setup and migration

1. Enable **SentenceRecognition** in the [Tencent ASR console](https://console.cloud.tencent.com/asr).
   The [current billing policy](https://cloud.tencent.com/document/product/1093/35686)
   provides 5,000 successful requests per month, valid only that month. Keep
   postpaid disabled to stop when free/prepaid resources run out. These are
   standard one-sentence quotas, not the large-model realtime quotas.
2. Create API credentials in [Cloud Access Management](https://console.cloud.tencent.com/cam/capi),
   preferably for a sub-account limited to SentenceRecognition. Enter SecretId
   and SecretKey in the device setup page along with the DeepSeek key. Keys are
   never read back into the browser. Keep the default ASR URL
   `https://asr.tencentcloudapi.com/` and engine `16k_zh`.
3. Existing firmware settings load without erasing Wi-Fi, DeepSeek credentials
   or conversation memory. An old multipart provider remains active until you
   hold OK, enter the new service credentials and save. API credentials must
   be re-entered when saving the full form. Wi-Fi-only saves preserve them.

Direct mode streams base64 PCM in small chunks using TC3-HMAC-SHA256 and the
HTTPS unsigned-payload option implemented by the
[official Tencent SDK](https://github.com/TencentCloud/tencentcloud-sdk-python/blob/master/tencentcloud/common/abstract_client.py).
It hashes the unsigned-payload marker as that SDK does. TLS certificate
verification stays enabled; no recording is buffered in full or written to
Flash. The computer gateway signs its complete WAV request body instead.
Tencent API errors may arrive with HTTP 200; they stop the conversation before
DeepSeek or history updates. Provider messages and credentials are not displayed.
Live account acceptance, recognition quality and device memory/timing still
require on-device testing with locally entered credentials.

## Wi-Fi-only setup and private factory presets

Wi-Fi can be saved before API keys are available. Select **Save Wi-Fi only**
on the setup page. The device restarts and joins the saved network; hold OK to
add API settings later. **Keep saved Wi-Fi** reuses the network without reading
its password back into the browser. Wi-Fi-only updates preserve existing API
settings and conversation memory. Protected networks require WPA2 or newer.
A disconnect schedules retries every 5 seconds; after 30 seconds without an IP,
the screen offers setup while retries continue.

For a private preconfigured firmware, run `python3 tools/configure_assistant_wifi.py`
and enter the SSID and hidden password, or use `--input` with a local JSON file
containing only `ssid` and `password`. The tool validates UTF-8 byte limits and
writes `.local/assistant-wifi.json` with owner-only permissions. This directory
is Git-ignored. `--check` validates it without printing credentials.

The normal validation build includes that local preset when present. Its values
are imported into NVS only when no saved Wi-Fi exists. Existing configuration
and corrupt NVS records are never overwritten by a preset. Wi-Fi-only startup
shows network status until service setup is complete. Factory credentials remain
embedded in this private firmware: keep its binary and matching debug archive
private. Removing the local preset before a fresh build produces the generic
firmware again. Changing a preset does not override an existing device's NVS;
change Wi-Fi through setup or explicitly approve a provisioning flash that resets
NVS. This mechanism never flashes a device automatically.

## Run the computer gateway

Use Python 3.10 or newer; the gateway uses the standard library and SQLite and
needs no Python package installation. From the repository root:

```bash
cp services/voice_gateway/.env.example .env
# Edit .env locally: set provider keys, token and LAN binding.
# Generate a device token with: python3 -c 'import secrets; print(secrets.token_urlsafe(32))'
set -a
. ./.env
set +a
python3 -m services.voice_gateway
```

The environment file is a shell file, so quote values and use only local,
trusted contents. `.env` and SQLite files are Git-ignored. Do not put keys in
chat messages, screenshots or committed files.

| Variable | Meaning/default |
| --- | --- |
| `DEEPSEEK_API_KEY` | Required DeepSeek key |
| `TENCENT_SECRET_ID` | Required Tencent SecretId for the default ASR service |
| `ASR_API_KEY` | Required Tencent SecretKey (or the configured compatible provider key) |
| `VOICE_GATEWAY_TOKEN` | Required device token, at least 32 printable ASCII characters |
| `VOICE_GATEWAY_HOST` | `127.0.0.1`; set `0.0.0.0` to accept LAN devices |
| `VOICE_GATEWAY_PORT` | `8765` |
| `VOICE_GATEWAY_MEMORY_DB` | `~/.local/share/ai-passport/voice-memory.sqlite3` |
| `ASR_BASE_URL` | `https://asr.tencentcloudapi.com` |
| `ASR_MODEL` | `16k_zh` |
| `DEEPSEEK_BASE_URL` | `https://api.deepseek.com` |
| `DEEPSEEK_MODEL` | `deepseek-flash` |
| `BING_SEARCH_ENABLED` | `0`; set `1` to add optional Bing public-web context in gateway mode |
| `BING_SEARCH_URL` | `https://www.bing.com/search` |

For separate memories on several devices, use `VOICE_GATEWAY_TOKENS`, a JSON
object mapping device identifiers to distinct tokens, instead of the single
token variable. Device identity comes from the authenticated token. Keep the
computer awake, allow the chosen port through its local firewall if needed,
and enter the computer's LAN address in device setup.

### Optional Bing search context

Gateway mode can search Bing's public web RSS page before sending a question to
DeepSeek. Set `BING_SEARCH_ENABLED='1'`; the gateway adds at most five bounded
HTTPS results as untrusted reference context, and continues with a normal answer
if Bing is unavailable. No Bing API key is stored on the device. The legacy Bing
Search APIs were retired by Microsoft on August 11, 2025, so this feature does
not use the retired API; Azure AI Agents Grounding with Bing Search is the
Microsoft-supported alternative for deployments that need a supported managed
grounding service.

## Memory and privacy

Memory means bounded, persistent conversation context, not an unlimited fact
store or a summary of every conversation. Direct mode retains 3 successful
question/answer pairs. Stored questions are limited to 512 UTF-8 bytes and
answers to 1024 bytes, always cut on character boundaries. Gateway mode retains
128 successful pairs and sends the latest 12 to DeepSeek; older pairs expire.
Failed provider calls do not replace existing conversation memory.

Restarting the device or computer preserves its stored memory. The two modes
have separate histories; changing modes or provider settings neither merges
nor clears them. Clear the active history explicitly when changing users.
Clearing direct memory preserves Wi-Fi and API settings. Clearing gateway
memory affects only the authenticated device's database rows. Neither action
deletes copies already processed by upstream providers or external backups.

This application keeps 24 KiB NVS and PHY data, resizes the factory application
to `0x7B0000`, and reserves a 256 KiB custom data partition for one wallpaper.
The wallpaper is memory-mapped for display instead of copied into internal RAM.
NVS credentials/history are not encrypted by this prototype, so
physical access to the device can expose them. Gateway history is local SQLite;
protect the computer account and database. Audio is streamed from the device
with a small buffer and is not saved as audio files by the gateway. Application
logs omit keys and conversation content.

## Limits and failure recovery

The screen shows at most 768 UTF-8 bytes of recognized text and 1536 bytes of
reply text. Long responses may be rejected or truncated with a visible notice,
depending on mode. Short questions and concise answers suit this screen and
memory budget. Unsupported characters such as emoji are explicitly marked;
font coverage is documented with the [font assets](../../assets/README.md).

For an offline router or incorrect credentials, hold OK and reconfigure. For
cloud failures, verify account access, quota, model names and endpoint URLs.
For TLS clock errors, check NTP access and retry. For NVS errors, the application
preserves data and never automatically erases the partition. Diagnose or
explicitly clear the affected history rather than erasing all settings.

## Build and acceptance

Activate ESP-IDF 5.5.3 and run `./tools/validate.sh`. It includes pure state,
configuration and memory tests, mocked gateway tests, the existing BSP/demo
checks, an isolated firmware build and merged-image verification. The startup
path compiles only the assistant application, while baseline demo sources remain
available as references. Matching binary/ELF/MAP artifacts are archived under
`build/firmware/`.

An optional native preview renders the actual application UI with a 24 KiB
LVGL pool, including repeated maximum-length conversations. It requires the
managed LVGL sources downloaded by the firmware build:

```bash
cmake -S tools/assistant_ui_preview -B build/ui-preview
cmake --build build/ui-preview
(cd build/ui-preview && ./assistant_ui_preview)
```

The preview writes PPM images and reports pool usage. It exercises software
rendering and layout; it does not validate display hardware or microphone input.

Hardware and live-provider acceptance remains separate:

1. Verify the Chinese interface, Luo Tianyi labels, vivid `#66CCFF` theme,
   punctuation, long-text scrolling, all three buttons, battery display and
   unsupported-character notice. Upload portrait and landscape wallpapers,
   confirm center cropping and restart persistence, then restore the default theme.
2. Provision both modes, including wrong Wi-Fi credentials, reconnect, reboot,
   repeated setup entry and mode switching.
3. Test microphone recognition in quiet/noisy conditions, early stop, the
   8-second limit, service timeout, rejected keys and recovery after failure.
4. Tell the assistant a detail, ask it to recall the detail after power-cycling,
   clear memory with confirmation, then confirm the detail is absent. Repeat
   for the computer mode after restarting its service.
5. Measure free internal heap/largest block during TLS, recording, display and
   repeated requests. Confirm power and audio behavior on the actual board.

Flashing always requires approval for the exact validated build. A merged
image written at `0x0` may reset NVS; compatible segmented flashing can preserve
it. Follow the [firmware/data policy](../development/engineering/firmware-layout.md).

## Context-aware web search and kaomoji

Direct mode plans retrieval with the configured chat provider before answering.
The planner receives recent conversation and the UTC date, but no character
persona. It returns a bounded JSON query, or an empty query for ordinary chat
or an unresolved reference. Gateway mode uses the same policy when
`BING_SEARCH_ENABLED` is enabled. Planning adds one short provider call, with
its associated latency and API usage. Planning failure skips retrieval instead
of searching the raw speech transcript. The answer receives the query, retrieval
status and bounded summaries as untrusted reference data; it must check entity
and date relevance and acknowledge missing evidence. This is a prompt-based
relevance check, not a guarantee that search results or answers are correct.

The character font now has a symbol fallback for the verified examples in
`assets/fonts/kaomoji-samples.txt`. Built-in prompts allow brief kaomoji;
color emoji and arbitrary Unicode remain unsupported. Check mixed Chinese,
symbols, wrapping and restart behavior on the board. Details and licenses are
in the [asset inventory](../../assets/README.md#voice-assistant-symbol-fallback).

## Interface references

- [DeepSeek chat completions](https://api-docs.deepseek.com/api/create-chat-completion/)
- [Tencent SentenceRecognition](https://cloud.tencent.com/document/api/1093/35646)
- [Optional BLUFI reference](../development/engineering/wifi-provisioning.md)
  reviewed at `9c039cc5127f22072afa83bedb7fa3d8efe635ad`; this application uses
  its own local Wi-Fi setup page and does not claim BLUFI mini-program support.
