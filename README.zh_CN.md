<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# 洛天依 AI Passport 语音助手

基于 FoloToy AI Passport、ESP-IDF 和 LVGL 的可定制语音伙伴。
在 ESP32-C3 设备上按键说话，即可查看文字回答并收听语音。

本项目是基于 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport)
开发的非官方同人项目，与洛天依角色权利方没有关联。
默认合成音色是腾讯云提供的通用音色，并非洛天依官方声库或声音克隆。

## 主要功能

- 语音对话：显示识别文字和回答，并支持语音朗读。
- 鲜艳的 `#66CCFF` 主题，可自定义助手名称与主题色。
- 网页设置角色预设、自定义提示词、语音播放开关和音量。
- 浏览器上传壁纸，居中裁切并转换为 240×320 RGB565 后保存在设备中。
- 网页配置主 Wi-Fi 和可选备用 Wi-Fi，连续连接失败三次切换网络；启动先尝试主网络。
- 根据当前问题和近期对话规划搜索词，限制参考内容长度，搜索不可用时正常降级。
- 中文显示与符号后备字库，支持字库覆盖的颜文字。
- 持久化对话记忆与明确的清除操作。
- 长回答按每段最多 120 个 Unicode 字符分段合成、依次播放，失败显示真实错误码。
- 支持设备直连云端，也可使用电脑网关。

## 硬件与服务

| 项目 | 要求 |
| --- | --- |
| 开发板 | FoloToy AI Passport，ESP32-C3 |
| Flash / PSRAM | 8 MB / 无 PSRAM |
| 开发环境 | ESP-IDF 5.5.3 |
| 屏幕 | 240×320，LVGL |
| 网络 | 可访问互联网的 2.4 GHz Wi-Fi |
| 语音服务 | 腾讯云语音识别与语音合成 |
| 对话服务 | 可配置的 Chat Completions 接口与模型 |

默认对话配置使用 DeepSeek。可在高级设置中修改兼容接口及模型名称，实际兼容性
取决于服务商。云服务需要自行配置账号密钥，并可能按用量收费。
语音识别与语音合成需要分别在服务商处开通。

## 首次使用

1. 为支持的开发板编译并烧录固件。
2. 手机或电脑连接设备创建的 `Passport-XXXXXX` 热点，密码显示在设备屏幕上。
3. 保持连接该热点，在浏览器打开 `http://192.168.4.1`。
4. 填写主 Wi-Fi，可选填写另一个备用网络。
5. 直连模式填写对话 API Key 和腾讯云 SecretId、SecretKey。
   高级设置支持修改接口地址与模型名称。
6. 设置名称、主题色、人设、语音开关和音量后保存，设备重启并连接 Wi-Fi。

空闲时长按**确定**可重新进入设置。上传和清除壁纸也会触发重启。
配置服务器仅在设置模式开放，不在日常连接的路由器网络中开放。
密钥保存在设备本地 NVS 中，网页不会读回已保存的 API 密钥和 Wi-Fi 密码。

## 按键操作

| 操作 | 功能 |
| --- | --- |
| 空闲时短按确定 | 开始录音，出现录音提示后说话 |
| 录音时短按确定 | 提前结束，录音最长 8 秒 |
| 短按上 / 下 | 滚动对话 |
| 播报时短按确定 | 停止播放并取消后续段落 |
| 播报时短按上 / 下 | 调节音量 |
| 空闲时长按确定 | 重启进入网页设置 |
| 空闲时长按下 | 请求清除记忆，短按确定确认 |

## 编译与烧录

激活已安装的 ESP-IDF **5.5.3** 环境，在项目根目录运行：

```bash
idf.py set-target esp32c3
idf.py build
```

运行完整仓库检查并生成经过校验的固件包：

```bash
./tools/validate.sh
```

完整检查还需要兼容的 shell 和主机 C 编译器。参见
[构建说明](docs/development/engineering/build-and-test.zh_CN.md)与
[环境安装](docs/development/engineering/environment-setup.zh_CN.md)。
生成的固件与配套调试文件保留在 Git 忽略的 `build/` 目录中。

烧录当前构建，将 `PORT` 替换为设备实际串口：

```bash
idf.py -p PORT flash
```

本项目分区包含壁纸区。分区兼容时，分段烧录可保留设置与壁纸；
从 `0x0` 写入合并固件可能重置存储数据。日常升级不要直接擦除整片 Flash。
详见[固件及数据政策](docs/development/engineering/firmware-layout.zh_CN.md)。

## 可选：电脑网关

Python 网关仅使用标准库。参考
[`services/voice_gateway/.env.example`](services/voice_gateway/.env.example)
配置环境变量后运行：

```bash
python -m services.voice_gateway
```

程序不会自动加载 `.env` 文件，需要先在当前 shell 中导出相应变量。
设备填写电脑可访问的局域网地址与相同网关令牌，不要填写 `localhost`。
明文 HTTP 网关仅用于可信局域网。

## 源码目录

| 路径 | 内容 |
| --- | --- |
| `main/` | 语音流程、网页配置、界面、Wi-Fi 策略、记忆及 TTS |
| `components/bsp/` | 显示、音频、按键与电源等板级支持 |
| `assets/fonts/` | 中文和符号字库及许可说明 |
| `services/voice_gateway/` | 可选 Python 语音网关 |
| `tests/` | 主机检查与服务测试 |
| `tools/` | 校验、固件归档和界面预览工具 |
| `docs/` | 应用、硬件和开发文档 |

详细行为见[应用说明](docs/applications/deepseek-voice.zh_CN.md)，
硬件资料见[文档索引](docs/README.zh_CN.md)。

## 使用限制与隐私

- 本项目是在线助手，需要联网并配置相应服务。
- 搜索结果与模型回答可能出错。内置字库不支持彩色 emoji 或任意 Unicode 字符。
- 录音和对话文字会发送至你配置的服务商；可选电脑网关将历史保存在本地 SQLite 中。
- 公开版本的 Wi-Fi 预设为空。通过网页输入凭据，或在私有构建中使用被忽略的本地预设。
- 不要公开 API 密钥、Wi-Fi 密码、`.env`、设备记忆、原始日志或含私人配置的固件及调试包。

## 许可与致谢

项目代码遵循 [MIT 许可证](LICENSE)，保留上游 FoloToy 版权声明。
字库及其他第三方资源保留各自许可证，见[资源清单](assets/README.zh_CN.md)。
角色相关权利归其各自权利方所有。
