# BambuHelper 简体中文版

BambuHelper 是运行在 ESP32 显示设备上的拓竹打印机监控器。通过局域网或拓竹云连接打印机，显示打印进度、温度、风扇、AMS、剩余时间和错误信息，并提供时钟屏保、完成提示音和可选智能插座控制。

完整硬件列表、接线图及功能说明见 [英文文档](README.md)。

## 中文固件适用设备

新增 `cyd_zh_cn` 编译环境，适用于经典 **ESP32-2432S028、2.8 英寸 CYD、ILI9341 240×320、XPT2046 电阻触摸屏、4MB 闪存**。

外观相似的设备不一定兼容：

- TZT L1435-2.4 使用 ST7789 面板，背光引脚也不同。
- ESP32-2432S024 是 2.4 英寸机型，触摸总线和背光引脚不同。
- ESP32-C5 或 ESP32-S3 型号不能使用经典 ESP32 的固件。

请核对背面型号。此改动为经典 CYD 提供中文构建；其他原有编译环境仍使用英文，暂不声称其他面板的中文显示已通过硬件验证。

## 编译与烧录

安装 Python 和 [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)，在仓库目录执行：

```sh
pio run -e cyd_zh_cn
```

首次构建会下载 ESP32 工具链和依赖。生成文件：

```text
.pio/build/cyd_zh_cn/firmware.bin
.pio/build/cyd_zh_cn/bootloader.bin
.pio/build/cyd_zh_cn/partitions.bin
```

连接设备后查看端口并烧录：

```sh
pio device list
pio run -e cyd_zh_cn -t upload --upload-port COM4
pio device monitor --port COM4 --baud 115200
```

`COM4` 仅为示例，请替换为自己的设备端口。若无法连接，可将烧录波特率降为 115200，并按设备需要使用 BOOT/RESET 按键。

`firmware.bin` 是应用固件，不是包含引导程序和分区表的合并镜像。首次烧录建议使用上述 PlatformIO 命令，不要把它当作完整镜像写入地址 0。

中文环境沿用 CYD 原有 `min_spiffs.csv` 分区表，保留双应用分区及手动 OTA。为容纳字体，使用项目已有的不启用 C++ 异常配置；英文 CYD 环境不变。

## 首次配网

1. 未配置网络时，设备显示 WiFi 配网提示。
2. 根据屏幕显示的热点名称和密码连接设备热点。
3. 在浏览器打开 `http://192.168.4.1`，输入家庭 WiFi 名称和密码。经典 ESP32 仅支持 **2.4 GHz** WiFi。
4. 保存后设备重启，连接家庭网络。通过屏幕显示的 IP 地址访问设置页面。

已有 BambuHelper 设置会继续使用，不会因为选择中文环境而主动恢复出厂设置。不同固件之间迁移前，建议备份设置或完整闪存。

## 添加打印机

### 局域网模式

在“打印机设置”选择局域网模式，填写打印机 IP、序列号和 8 位局域网访问码。序列号应与打印机显示完全一致，注意大写。也可使用“扫描局域网”自动填写 IP 和序列号。

监控器和打印机需要位于能互相通信的网络中。打印机的局域网连接数有限，过多其他监控程序连接时可能无法接收数据。

### 拓竹云

选择拓竹云模式，并按账号所属区域选择服务器，例如中国大陆账号使用“中国大陆（CN）”。可直接登录或粘贴访问令牌，随后选择账号绑定的打印机。需要验证码时按页面提示填写。

设置页面使用 HTTP，直接登录时请仅在可信网络操作。令牌和密码不应写进代码、提交到 GitHub，或放进公开测试记录。

## 显示设置

- “屏幕”：方向、亮度、夜间模式以及按键或触摸操作。
- “打印页面”：仪表或卡片样式、仪表布局、温度量程、圆弧动画。
- “打印结束后”：完成页面、边缘灯效、屏保和提示音。
- “时钟”：时区、时间格式和日期样式；中国标准时间为 UTC+8。
- “颜色”：各类文字和仪表颜色、自定义标签。
- “WiFi 与系统”：网络、设置备份、固件更新和恢复出厂设置。

CYD 默认只连接一台打印机。双打印机模式是低内存设备上的实验功能，同时运行两路 TLS/MQTT 可能导致内存不足。

若背景异常发白，可尝试“反转显示颜色”。经典 CYD 面板兼容选项仅供对应旧款面板使用，不要将它当作其他硬件型号的驱动。

## 更新中文固件

**上游在线发布目前使用原有英文环境。网页中的在线更新可能安装英文固件。** 如需保留中文，请重新编译 `cyd_zh_cn`，在“手动更新”中上传对应的 `firmware.bin`，或使用 USB 烧录。

## 翻译与字体维护

- 屏幕文案：`BH_TEXT("English", "中文")`，英文在默认环境中保留。
- 网页翻译表：`locales/zh_CN.json`。
- `tools/localize_web.py` 在构建时翻译静态网页、可访问性提示和前端提示文案，生成文件保存在当前环境的构建目录中。表单值、DOM ID、请求路径和协议状态不翻译。
- 字体：保留 Inter 的 ASCII 字形，加入 Noto Sans SC 中界面所需的汉字，并为字体表按 Unicode 排序。无需运行时下载字体。

4MB CYD 没有空间内置完整中文字库。当前字库覆盖已翻译的屏幕文案；任意中文文件名、打印机名称和自定义标签可能包含未收录的汉字。网页使用浏览器字体，不受此限制。拓竹提供的错误描述及服务端原始错误可能仍为英文，错误码和协议字段保持原样。

修改屏幕中文文案后，需要重新生成字库。安装 `freetype-py`，从 [Google Fonts 的 Noto Sans SC 目录](https://github.com/google/fonts/tree/main/ofl/notosanssc) 下载 `NotoSansSC[wght].ttf`，然后执行：

```sh
python -m pip install freetype-py==2.5.1
python scripts/generate_zh_fonts.py /path/to/NotoSansSC.ttf
python -m unittest discover -s tests -v
pio run -e cyd_zh_cn
```

生成的 `include/fonts/zh_*.h` 已提交，正常编译不需要 FreeType、原始字体文件或字体下载。字体许可证见 `fonts/OFL-NotoSansSC.txt` 和 `fonts/OFL-Inter.txt`。原始字体只用于重新生成，不必提交其约 18MB 的文件。

测试涵盖表单和 API 兼容、模板占位符、中文提示与字库排序和覆盖。经典 CYD 已完成烧录、联网及中文卡片截图测试，详细结果和未覆盖项目见[验证记录](docs/zh-CN-validation.md)。其他硬件与触摸操作仍需分别验证。
