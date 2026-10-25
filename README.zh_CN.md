[English](README.md) | **简体中文**

# Robo Dash

一款为 FoloToy AI Passport 开发的离线三跑道跑酷游戏。操控小机器人穿越连续障碍，通过触碰收集金币、选择路线，并击败最终守卫。

![Robo Dash 游戏画面](assets/images/robo-dash-cover.png)

## 游戏特色

- 三阶段关卡、连续障碍组合，以及用于调整节奏的恢复间隔。
- 金币必须触碰才能收集，支持连击奖励和可充能的超载能力。
- 护盾与冲刺道具，不包含磁铁或自动拾取金币功能。
- 奔跑途中可选择安全、普通或风险路线。
- 三血守卫战，红色攻击警告与三条跑道对齐，并提供反击窗口。
- 三点生命、持久化最高分、音效，以及原生桌面模拟器。
- 完全离线运行：无需 Wi-Fi、蓝牙、账号、语音录入或网络服务。

## 操作方式

设备有三个按键，其中 Up 和 Down 分别对应向左和向右移动。

| 输入 | 功能 |
| --- | --- |
| Up | 向左切换一条跑道 |
| Down | 向右切换一条跑道；在暂停或结算界面退出 |
| OK | 开始或重新开始；奔跑时跳跃；暂停时继续 |
| 双击 OK | 能量满时启动超载 |
| 长按 OK | 奔跑时暂停 |

桌面模拟器使用左右方向键换道、空格键跳跃、X 键启动超载、P 键暂停、Esc 键退出。

## 玩法

1. 按 OK 开始。换道躲避方块和栏杆，也可以根据障碍类型跳过障碍。
2. 触碰金币才会收集。金币为超载充能，连续拾取会提高连击奖励。
3. 护盾可以抵挡一次碰撞，冲刺提供短暂优势；游戏没有金币磁铁。
4. 路线提示出现时，在倒计时结束前用换道按键选择安全、普通或风险路线。
5. 能量达到 100% 后，双击 OK 启动超载。守卫战中躲开红色跑道警告，并在反击窗口使用超载。
6. 跑完全程并击败守卫即可获胜。存储可用时，最高分会保存到 NVS。

## 硬件与软件

- FoloToy AI Passport，ESP32-C3，8 MB Flash
- ESP-IDF 5.5.3
- 240 × 320 彩色屏幕
- 三个设备按键
- 可选音频输出，用于游戏音效

游戏采用纯 C 逻辑核心、单个 LVGL Canvas 游戏画布，以及独立的逻辑与渲染任务。模拟器与固件编译同一个 `main/runner_game.c` 核心。

## 构建与验证

激活 ESP-IDF 5.5.3，然后运行完整的仓库门禁：

```bash
source <path-to-esp-idf-v5.5.3>/export.sh
idf.py --version
./tools/validate.sh
```

门禁会执行仓库检查和主机测试，根据已跟踪的默认配置在独立目录构建，验证 Flash 布局，并生成：

```text
build/FoloToy-AI-Passport-full.bin
```

这是用于发布的合并固件，烧录偏移为 `0x0`。烧录合并固件可能会重置 NVS 数据，包括已保存的最高分。不要用仅包含应用程序的 `build/FoloToy-AI-Passport.bin` 替代它。

仅用于日常增量开发时：

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p <PORT> flash monitor
```

验证、烧录、数据保留和调试产物的详细说明见[构建与测试](docs/development/engineering/build-and-test.zh_CN.md)和[固件布局](docs/development/engineering/firmware-layout.zh_CN.md)。

## 桌面模拟器

要求：主机 C 编译器，以及带 Tk 8.6 或更新版本的 Python 3（推荐 Tk 9）。

```bash
# 编译 C 核心并执行检查，不打开窗口。
python3 simulator/run.py --check

# 运行交互式模拟器。
python3 simulator/run.py

# 明确重新编译 C 动态库。
python3 simulator/run.py --rebuild

# 打开窗口、执行自动化界面冒烟测试并关闭。
python3 simulator/run.py --ui-smoke
```

在 macOS 上也可以打开 `simulator/play.command`。启动器会查找常见的 Python 3 命令，并选择带有兼容 Tk 运行时的版本，不依赖某台电脑上的固定 Python 路径。

模拟器的最高分和构建产物写入已被 Git 忽略的 `build/simulator/` 目录。

## 源码结构

```text
main/main.c             设备输入、音频、HUD、NVS 和任务
main/runner_game.c/.h   纯游戏逻辑与碰撞几何
main/runner_world.c/.h  单 Canvas LVGL 游戏画面渲染
simulator/              原生 Tk 模拟器与启动器
tests/test_runner_game.c
tools/validate.sh       完整构建与主机测试门禁
```

## 说明

- 本分支使用专门设计的游戏界面替代模板演示菜单。
- 游戏无需联网、云服务、语音识别或麦克风。
- 构建或模拟器测试通过不能替代实机测试。
- 生成的固件、本地最高分、Python 缓存和调试包不会提交到仓库。

## 许可证

本项目根据仓库中的 [MIT 许可证](LICENSE)发布。
