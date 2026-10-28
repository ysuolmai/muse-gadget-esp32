# Muse Gadget ESP32

这是面向自有设备的 Muse Gadget 固件仓库，当前自动构建并维护以下三款板子：

- ALIENTEK ATK-DNESP32S3-BOX0
- ESP32-S3-N16R8
- ESP32-S3 Super Mini

N16R8 和 Super Mini 使用 INMP441 麦克风及 MAX98357A 喇叭功放，组成无屏语音设备。固件基于 [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk)，本仓库保留上游关系，后续可同步 SDK 更新。

## 设备规格

| 设备 | 存储规格 | 音频与控制 | 烧录/串口接口 |
|---|---|---|---|
| ATK-DNESP32S3-BOX0 | 8 MB Flash、8 MB octal PSRAM | 屏幕和 ES8311 音频已集成；GPIO0 语音键，GPIO4 菜单 | 芯片原生 USB Serial/JTAG |
| ESP32-S3-N16R8 | 16 MB QIO Flash、8 MB octal PSRAM | 无屏；外接 INMP441 和 MAX98357A；BOOT/GPIO0 语音键 | CH343P COM 用于命令行烧录；另有原生 USB 口 |
| ESP32-S3 Super Mini | 4 MB QIO Flash、2 MB quad PSRAM | 无屏；外接 INMP441 和 MAX98357A；BOOT/GPIO0 语音键 | 芯片原生 USB Serial/JTAG |

规格参考：[N16R8 用户手册](https://github.com/microrobotics/ESP32-S3-N16R8/blob/main/ESP32-S3-N16R8_User_Guide.pdf)、[ESP32-S3 Super Mini](https://www.espboards.dev/esp32/esp32-s3-super-mini/)。N16R8 的 GPIO35–37 留给 octal PSRAM，不要用于外接模块。

## 接线

Box0 使用板载屏幕、麦克风和扬声器，不需要外接音频模块。N16R8 和 Super Mini 共用以下接线：

| 模块 | 模块引脚 | 连接到 |
|---|---|---|
| INMP441 | VDD | ESP32 3V3 |
| INMP441 | GND | ESP32 GND |
| INMP441 | SCK / BCLK | GPIO4 |
| INMP441 | WS / LRCL | GPIO5 |
| INMP441 | SD | GPIO2 |
| INMP441 | L/R | GND（选择左声道） |
| MAX98357A | VIN | 5V |
| MAX98357A | GND | ESP32 GND（所有模块共地） |
| MAX98357A | BCLK | GPIO4 |
| MAX98357A | LRC / WS | GPIO5 |
| MAX98357A | DIN | GPIO1 |
| MAX98357A | SD / EN | 3V3（按模块引脚标识使能） |
| 喇叭 | 两根线 | MAX98357A 的扬声器输出端 |

注意事项：

- INMP441 使用 3.3V；不要把它接到 5V。
- ESP32、麦克风和功放必须共地。MAX98357A 的扬声器接功放输出，不能接 ESP32 GPIO。
- 功放的 5V 和扬声器规格按你手上的 MAX98357A 模块及喇叭确认。
- 两块无屏板的 BOOT 键就是 GPIO0：配对时按下确认；空闲时短按开始实时通话，按住录音、松开发送一条语音（不足 5 秒；达到 5 秒会进入配网）。

## 获取固件并烧录

### 使用 GitHub Actions 预编译固件

每次推送到 `main` 都会构建上述三款设备。打开 [Actions 构建记录](https://github.com/ysuolmai/muse-gadget-esp32/actions)，进入最新成功的 `main ESP32 firmware` 运行，在 **Artifacts** 下载对应的 `firmware-muse-...` 固件包。固件包保留 7 天。

也可以使用同一次构建生成的 `github-pages` 烧录页面包。此仓库是 fork，页面不会自动发布到 GitHub Pages；下载该 artifact 后在本机解包并启动本地 HTTP 服务：

```sh
gh run download RUN_ID -n github-pages -D /tmp/muse-pages
mkdir -p /tmp/muse-pages/site
tar -xf /tmp/muse-pages/artifact.tar -C /tmp/muse-pages/site
python3 -m http.server 8000 --bind 127.0.0.1 --directory /tmp/muse-pages/site
```

用 Chrome 或 Edge 打开 `http://127.0.0.1:8000`，选择准确的板型，连接 USB，按页面提示安装固件并填写自己的 SDK token。网页串口烧录需要电脑上的浏览器直接访问 USB 设备。

### 本地编译和 USB 烧录

固件使用 ESP-IDF **v6.0.1**。先按 [Espressif 安装说明](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/get-started/index.html)安装依赖，然后安装并启用工具链：

```sh
mkdir -p ~/esp
git clone -b v6.0.1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6.0.1
~/esp/esp-idf-v6.0.1/install.sh esp32s3
. ~/esp/esp-idf-v6.0.1/export.sh
```

从仓库的 `esp32` 目录运行对应命令：

```sh
tools/muse/board.sh build atk-box0
tools/muse/board.sh flash atk-box0

tools/muse/board.sh build n16r8
tools/muse/board.sh flash n16r8

tools/muse/board.sh build super-mini
tools/muse/board.sh flash super-mini
```

烧录脚本会按 USB 设备识别串口。N16R8 请将 CH343P COM 口接到电脑；Box0 和 Super Mini 使用芯片原生 USB。若连接了多块相同型号设备，可在 `flash` 命令末尾指定串口路径或 USB 序列号，例如 `tools/muse/board.sh flash n16r8 /dev/ttyUSB0`。如果板子未自动进入下载模式，按住 BOOT、点按 RESET，再松开 BOOT 后重试。

Super Mini 只有 4 MB Flash，固件使用单个应用分区，**不支持 OTA 更新**；更新固件时需要通过 USB 烧录。

## 配置与首次配网

可以直接用设备内置网页配置 Wi-Fi 和 SDK token，不需要先修改源码或重新编译。

1. 设备运行时长按配网键约 **5 秒**：Box0 按中间的 GPIO4 菜单键；N16R8 和 Super Mini 按 BOOT/GPIO0。设备会创建临时的 `Muse-Setup-XXXXXX` Wi-Fi 热点。
2. 三款设备的配网热点默认密码都是 **`12345678`**，无屏板也不需要先连接串口。可以在配置网页的“配网热点新密码”中修改（8–63 位英文、数字或符号）；留空保持当前密码，保存后下次进入配网时生效。Box0 屏幕和 USB 串口也会显示当前热点密码。
3. 手机连接这个热点后访问 `http://192.168.4.1/`。部分手机会提示网络无法访问互联网，选择保持连接即可。
4. 填入家中 2.4 GHz Wi-Fi 名称和密码；可同时填入 `mgst_` SDK token。token 留空会保留设备中已有的 token。
5. 点保存后设备会重启并连接路由器。手机也切回同一个路由器 Wi-Fi，再在 Muse App 中添加/配对设备。网页热点只在本次配置期间开放，重启后关闭。

该网页适合首次使用、换路由器或更新 token。Muse App 原有的 BLE 配网仍然可用。

### SDK token

每台设备都需要自己的 Muse SDK token。登录 [Muse SDK token 页面](https://gadgets.muse.ai/settings/sdk-tokens)创建以 `mgst_` 开头的 token。不要把 token 提交到 Git 或公开分享。

网页配置是最简单的方式。也可以在烧录后通过串口写入 token，这样 token 不会编进公开固件。进入 `esp32` 目录，按设备选择 build 目录并打开串口监视器：

```sh
# N16R8
idf.py -B build-muse-esp32-s3-n16r8 -p PORT monitor

# Super Mini
idf.py -B build-muse-esp32-s3-super-mini -p PORT monitor

# ATK-DNESP32S3-BOX0
idf.py -B build-muse-alientek-atk-dnesp32s3-box0 -p PORT monitor
```

将 `PORT` 替换为实际串口；也可先运行 `python3 tools/muse/ports.py n16r8`、`python3 tools/muse/ports.py super-mini` 或 `python3 tools/muse/ports.py atk-box0` 查询。监视器中输入下面一行并回车，看到 `@sdk_token ok` 后设备会重启：

```text
>sdk_token=mgst_你的token
```

也可以先构建一次，再从 `esp32` 目录运行 `idf.py -B build-muse-esp32-s3-n16r8 menuconfig`，在 **ESP32 Device SDK > Muse Gadgets SDK token** 中设置 `GADGET_SDK_TOKEN`，然后重新编译和烧录。Super Mini 和 Box0 分别把 build 目录改为 `build-muse-esp32-s3-super-mini` 和 `build-muse-alientek-atk-dnesp32s3-box0`。该方式会把 token 放进本地固件和 build 配置文件。

### Wi-Fi 与 Muse 配对

正常使用不需要把 Wi-Fi 名称或密码写入源码。设备通过手机上的 Muse App 接收 Wi-Fi 配置：

1. 确保手机已安装 Muse App，待配网路由器提供 **2.4 GHz Wi-Fi**。
2. 给设备上电。手机 Muse App 中进入 **Settings > Devices**，打开 **Developer mode**，选择 **Add Device**（右上角 `+`）。
3. 选择名称为 `MuseGadget-XXXXXX` 的设备，按 App 提示选择 Wi-Fi 并输入密码。
4. App 要求确认配对时，按设备的 GPIO0/BOOT 键。Box0 使用右侧 GPIO0 按钮；N16R8 和 Super Mini 使用板载 BOOT 键。
5. 等待设备连上 Wi-Fi 和 Muse 服务。无屏设备可通过 USB 串口日志查看状态；Box0 会在屏幕上显示状态。

开发调试时也可以在 menuconfig 中设置 `CONFIG_HOMEHUB_WIFI_SSID` 和 `CONFIG_HOMEHUB_WIFI_PASSWORD`，让设备启动时连接指定网络。这是本地调试覆盖项，不是正常配网方式；不要把含有私人 Wi-Fi 密码的 `sdkconfig` 提交到仓库。

## 实时语音对话

三款板子的固件默认启用实验性的 `CONFIG_MUSE_REALTIME_VOICE`：短按 GPIO0/BOOT 开始实时通话，之后可以直接轮流交谈，不必每句话都按键。Muse 会用语音回复；再次短按同一按键结束通话。通话外按住按键录音、松开发送语音消息；两块无屏板按住 BOOT 达到 5 秒会改为进入网页配网，Box0 的配网键是独立的 GPIO4 菜单键。

INMP441 和 MAX98357A 没有回声消除，因此通话采用半双工：Muse 播放回复时麦克风会暂时关闭，播放结束后自动恢复收音。对话中不能打断 Muse 插话。默认连续静音约 45 秒会结束通话，单次最长约 15 分钟；服务端可能设置更短的限制。实时通话还要求账号所连接的 Muse VM 接受该设备发起的语音通话。

## 日常使用与重置

- **开始/结束通话：**短按 GPIO0/BOOT 开始实时语音通话，再短按一次结束；Box0 的 GPIO4 打开菜单。
- **发送单条语音：**未进入实时通话时，按住 GPIO0/BOOT 说话，松开后发送。
- **通话中：**Muse 回复结束后设备会自动恢复收音，直接继续说即可；回复播放期间暂时不采集麦克风。
- **配对确认：**按下 GPIO0/BOOT 完成手机 App 中的配对确认。
- **重新配置 Wi-Fi/token：**按住配网键约 5 秒进入本地网页。此操作不会删除现有配对；若更换了 SDK token，需要在 Muse App 中解除旧设备并重新配对，设备才会使用新 token。
- **完全清空：**在 `esp32` 目录运行 `idf.py -B build-muse-esp32-s3-n16r8 -p PORT erase-flash` 会清除固件及 NVS 中的配对、Wi-Fi 和 token；按板型替换 build 目录后，清空完需要重新烧录固件。
- **查看日志：**使用上面的 `idf.py -B 对应build目录 -p PORT monitor`。退出串口监视器按 `Ctrl-]`。

本仓库的 GitHub Actions 会验证三种板型的固件构建和分区大小。编译通过不代表已在每个硬件批次上完成实体板验证；接线后请先低音量测试功放和喇叭。

## 同步 Muse SDK 更新

本项目基于 [facebookincubator/muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk)。在本仓库的 `main` 上可以直接合并上游更新：

```sh
git remote add upstream https://github.com/facebookincubator/muse-gadget-sdk.git
git fetch upstream
git merge upstream/main
git push origin main
```

如果本地已经有 `upstream` remote，跳过 `git remote add`。同步后 GitHub Actions 会重新构建三款目标设备；若上游与本项目的板级修改发生冲突，需要先处理冲突再推送。

## 许可

仓库保留上游及第三方组件各自的许可证声明，详见 [`LICENSE`](LICENSE) 和组件目录中的许可证文件。本项目是社区设备适配，不代表 Muse 服务或原 SDK 上游对这些板卡提供官方支持。
