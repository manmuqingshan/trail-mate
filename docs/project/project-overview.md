# Trail Mate 项目概要

<!-- praxis:project-overview:start -->

- 项目版本：0.1.30-alpha
- Git：34aad0bffa2f / main / dirty
- 更新于：2026-06-25T09:10:47.794Z
- 知识状态：CANDIDATE；当前概要来自项目文档与仓库证据，尚未提升为 CONFIRMED 项目记忆。

## 项目定位

低功耗离线优先的户外手持导航与通信设备，基于 ESP32 硬件，支持 LoRa 文本聊天、离线地图、SSTV 接收等功能，兼容 Meshtastic 及 MeshCore 网络。

Trail Mate 专为无蜂窝网络覆盖的户外场景设计，提供独立于智能手机的简单自定位与直接 LoRa 文本通信能力，强调稳定、高效与互操作性。

项目聚焦于两个核心需求：固定北向上的离线 GPS 地图显示，以及无需手机即可在 Meshtastic 或 MeshCore 网络中发送自由格式消息的 LoRa 聊天。

设计哲学追求不确定性诚实呈现、确定性系统行为和长期可靠性，适合资源受限的真实户外环境，而非替代智能手机。

## 当前状态

**Alpha 开发中**（high）

项目处于活跃开发阶段，最新发布版本为 0.1.30-alpha（2026-06-24），功能持续增加且 API 可能变动，尚未达到 1.0 稳定版。

### 状态证据

- CHANGELOG.md#[0.1.30-alpha]
- README.md#概述与设计哲学

## 关键能力

### 离线 GPS 地图

固定北向的离线地图渲染，支持 OSM、地形、卫星三种底图及等高线叠加，具备实时位置标记与面包屑轨迹。

### 证据

- README.md#GPS Map

### LoRa 文本聊天

基于 LoRa 的短消息通信，兼容 Meshtastic 和 MeshCore 协议，支持中文文本，无需中央基础设施。

### 证据

- README.md#LoRa Chat

### SSTV 图像接收

接收慢扫描电视信号并在设备上解码为图像，适合低功耗嵌入式环境。

### 证据

- README.md#SSTV Receiver

### 团队模式与位置共享

通过 ESP-NOW 近距离配对，交换团队密钥后在 LoRa 上实现成员列表、位置共享和团队聊天。

### 证据

- README.md#Team Mode

### 轨迹录制与路线跟随

支持轨迹录制、存储、浏览，以及 KML 路线叠加和 GPX 导出。

### 证据

- README.md#Track Recording & Route Following

### Walkie Talkie 语音对讲

基于 FSK 和 Codec2 的半双工语音通信，按压说话释放接听。

### 证据

- README.md#Walkie Talkie

### 协议空口参数探测

发现承载 MeshCore、Meshtastic 或 Reticulum 真实协议流量的完整 LoRa
air profile，并在协议允许时取得正向确认；不以 RSSI 或低噪声推荐替代
协议证据。

### 证据

- README.md#Protocol Probe

### GNSS 天空图

实时绘制可见卫星方位角与仰角，显示 SNR 及定位状态。

### 证据

- README.md#GNSS Sky Plot

## 工程入口

- **PlatformIO 构建配置**：platformio.ini（推测）。管理多个 ESP32 设备构建目标，如 tlora_pager_sx1262、tdeck、techo_lite 等。
- **固件源码**：src/。包含 Trail Mate 主逻辑、协议适配、UI 等核心代码。
- **项目文档与资源**：docs/。存放项目图片、图表等说明材料。
## 设计与架构入口

- 暂无明确入口。

## 当前进度

- **T-LoRa-Pager 固件**（done）：当前主要验证设备，支持全部核心功能。
  - 证据：README.md#设备支持表
- **T-Deck 固件**（done）：键盘和聊天功能已适配，是第二个主要验证平台。
  - 证据：README.md#设备支持表
- **LoRa 聊天（Meshtastic/MeshCore）**（done）：文本消息、联系人列表、位置分享等功能已实现，兼容两个主流协议。
  - 证据：README.md#LoRa Chat
  - 证据：CHANGELOG.md#0.1.30-alpha
- **离线地图基础功能**（done）：OSM、地形、卫星底图及等高线叠加可用，缩放和层切换正常。
  - 证据：README.md#GPS Map
- **nRF52 类设备初步支持**（in_progress）：已添加 MeshCore 发现入口和基本 UI 适配，但完整功能（如聊天、地图）尚在开发。
  - 证据：CHANGELOG.md#0.1.30-alpha
  - 证据：README.md#Planned Supported Devices
- **多语言与本地化**（in_progress）：中文、西里尔文等扩展语言包已引入，但覆盖面和输入法支持仍需完善。
  - 证据：CHANGELOG.md#0.1.30-alpha
## 风险与缺口

### 缺乏高层面架构文档

尚未发现独立的系统架构或设计文档，可能增加新贡献者的理解成本。

### 证据

- 文档目录中缺少 architecture/design 类文档

### CHANGELOG 可读性问题

当前 CHANGELOG 文件存在编码损坏，部分条目无法正常解读，影响项目历史追溯。

### 证据

- CHANGELOG.md 中的乱码片段

### 预发布阶段稳定性风险

项目尚未达到 1.0，协议和 API 可能在版本间发生破坏性变更。

### 证据

- CHANGELOG.md 声明“breaking changes may occur between minor versions”

### 低功耗设备兼容性不确定

nRF52 等目标设备的完整功能支持仍在早期阶段，性能和集成落地存在挑战。

### 证据

- README.md#Planned Supported Devices
- CHANGELOG.md#0.1.30-alpha

## 待确认问题

- 稳定版 1.0 的计划发布时间是什么？
- nRF52 及超低功耗设备的完整功能路线图是什么？
- 是否会提供 SD 卡离线地图切片的预装工具或指南？
- 目前缺少性能测试与功耗基准数据，何时会补充？

## 下一步

- 修复 CHANGELOG 编码问题，确保版本历史清晰可读。
- 编写项目架构概览文档，帮助贡献者快速了解系统模块与数据流。
- 推进 nRF52 设备上的 LoRa 聊天、位置共享等核心功能开发。
- 优化离线地图的用户体验，考虑提供地图切片一键下载或制作工具。
- 扩大设备兼容性测试，逐步收敛 API，向 1.0 稳定版迈进。

## 来源文档

- README.md
- CHANGELOG.md
- AGENTS.md

<!-- praxis:project-overview:end -->
