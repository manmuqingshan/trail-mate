const releaseVersionEls = document.querySelectorAll("[data-release-version]");
const releaseLinkEls = document.querySelectorAll("[data-release-link]");
const boardCards = document.querySelectorAll("[data-board-id]");
const packGrid = document.querySelector("[data-pack-grid]");
const packCountEls = document.querySelectorAll("[data-pack-count]");
const localeCountEls = document.querySelectorAll("[data-locale-count]");
const languageButtons = document.querySelectorAll("[data-lang-choice]");
const i18nEls = document.querySelectorAll("[data-i18n]");
const deviceNavButtons = document.querySelectorAll("[data-device-nav]");
const deviceTabButtons = document.querySelectorAll("[data-device-target]");
const deviceThumbGrid = document.querySelector("[data-device-thumbs]");
const deviceMainImage = document.querySelector("[data-device-main-image]");
const deviceMainCaption = document.querySelector("[data-device-main-caption]");
const deviceTitle = document.querySelector("[data-device-title]");
const deviceKicker = document.querySelector("[data-device-kicker]");
const deviceSummary = document.querySelector("[data-device-summary]");
const deviceStatus = document.querySelector("[data-device-status]");
const deviceInteractions = document.querySelector("[data-device-interactions]");

const I18N = {
  en: {
    "top.github": "GitHub",
    "top.wiki": "Wiki",
    "top.flash": "Flash",
    "side.label": "Trail Guide",
    "side.overview": "Overview",
    "side.capabilities": "What It Does",
    "side.devices": "Devices",
    "side.features": "Field Use",
    "side.languages": "Language Packs",
    "side.flasher": "Web Flasher",
    "side.docs": "Docs",
    "hero.eyebrow": "Offline Maps + LoRa for the Field",
    "hero.tagline": "Carry maps, messages, and teammate positions on a small LoRa device.",
    "hero.lede":
      "Trail Mate helps hikers and field groups stay oriented when cell service is gone: browse offline maps from SD, send LoRa messages, check GPS health, and keep team movement visible on compact handhelds.",
    "hero.primaryAction": "Open Web Flasher",
    "hero.secondaryAction": "View Repository",
    "hero.releaseKicker": "Latest Release",
    "hero.proofKicker": "Device Photos",
    "hero.proofText":
      "Screenshots are grouped by device so you can tell which screens match the hardware you plan to carry.",
    "overview.copy":
      "Built for days when a phone is not enough: keep maps on SD, send messages without mobile networks, record tracks, check signal and GPS, and install extra language packs when you need them.",
    "tag.maps": "Offline Maps",
    "tag.gnss": "GNSS Diagnostics",
    "tag.chat": "Mesh Chat",
    "tag.team": "Team Mode",
    "tag.languages": "Language Packs",
    "tag.scan": "Sub-GHz Scan",
    "tag.usb": "PC Link",
    "capabilities.eyebrow": "What It Does",
    "capabilities.title": "Prepare the device before you leave signal behind.",
    "capabilities.note":
      "Pick the right hardware, flash the matching firmware, load maps and language packs, then use the device when the phone network disappears.",
    "capabilities.deviceKicker": "Devices",
    "capabilities.deviceTitle": "Choose your hardware",
    "capabilities.deviceText":
      "Each supported board has its own firmware, install notes, and hardware caveats.",
    "capabilities.navigationKicker": "Navigation",
    "capabilities.navigationTitle": "Maps that work offline",
    "capabilities.navigationText":
      "Load map tiles from SD, switch layers, check GPS fix quality, and review routes or tracks in the field.",
    "capabilities.protocolKicker": "LoRa",
    "capabilities.protocolTitle": "Messages without cell service",
    "capabilities.protocolText":
      "Send direct, channel, or team messages over supported LoRa mesh modes and keep clear feedback when a send fails.",
    "capabilities.installKicker": "Install",
    "capabilities.installTitle": "Flash the right firmware",
    "capabilities.installText":
      "Supported ESP32 devices can be flashed from the browser; other boards use downloaded firmware files.",
    "capabilities.localeKicker": "Localization",
    "capabilities.localeTitle": "Add languages when needed",
    "capabilities.localeText":
      "Install fonts, translations, and keyboard layouts from Extensions instead of filling the firmware with every language.",
    "capabilities.fieldKicker": "Field Tools",
    "capabilities.fieldTitle": "Tools for the trip",
    "capabilities.fieldText":
      "Use track recording, GPS views, signal checks, SSTV, and USB data exchange when the trip calls for more than chat.",
    "devices.eyebrow": "Devices",
    "devices.title": "Pick the device you are holding.",
    "devices.note":
      "Choose the exact hardware before flashing. Some boards look similar but need different firmware.",
    "devices.interactionKicker": "What To Know",
    "features.navEyebrow": "Navigation",
    "features.navTitle": "Carry maps without cell service.",
    "features.navLead":
      "Put map tiles on the SD card before the trip, then browse OSM, terrain, and satellite layers from the device. GPS screens help you understand whether the current fix is trustworthy.",
    "features.commEyebrow": "Communication",
    "features.commTitle": "Stay in touch when phones stop helping.",
    "features.commLead":
      "Write messages on the device, retry failed sends from the message bubble, and keep team conversations separate from ordinary channel traffic.",
    "features.toolsEyebrow": "Field Utilities",
    "features.toolsTitle": "Bring a few field tools with you.",
    "features.toolsLead":
      "Use signal scanning, SSTV receive, USB data exchange, and track views when you need to inspect the environment or recover data.",
    "languages.eyebrow": "Localization",
    "languages.title": "Keep the firmware small, then install the languages you need.",
    "languages.note":
      "English is always available. Other fonts, translations, and keyboard layouts can be installed from Extensions, including Cyrillic keyboard support.",
    "languages.defaultKicker": "Built-In Default",
    "languages.defaultTitle": "The device can always fall back to English.",
    "languages.defaultText":
      "The device can boot, recover, and stay usable even when no external language packs are installed.",
    "languages.catalogKicker": "Pack List",
    "languages.bundleSuffix": "bundles covering",
    "languages.localeSuffix": "available locales.",
    "languages.catalogText":
      "Language packs can include translations, fonts, and keyboard layouts. Install them from the device instead of rebuilding firmware.",
    "languages.gateKicker": "Pack Status",
    "languages.gateTitle": "Review labels stay visible.",
    "languages.gateText":
      "Some languages are ready for daily use; others still need review from native speakers. The list keeps that visible.",
    "languages.loadingKicker": "Catalog",
    "languages.loadingTitle": "Loading package list...",
    "languages.loadingText": "Published locale bundles from the current Pages build will appear here.",
    "install.eyebrow": "Install",
    "install.title": "Web Flasher",
    "install.note":
      "Use Google Chrome or Microsoft Edge over HTTPS with a USB data cable. Pick the card that matches the radio chip on your hardware.",
    "install.pagerCopy":
      "SX1262 radio build for the keyboard Pager. Do not use this card for LR1121 hardware.",
    "install.pagerLr1121Copy":
      "LR1121 radio build for the Sub-GHz + 2.4 GHz keyboard Pager. Do not flash this onto SX1262 hardware.",
    "install.tdeckCopy": "Keyboard + touch build tuned for the T-Deck layout.",
    "install.twatchCopy": "Touch-first watch build for compact field experiments.",
    "install.gatCopy":
      "Browser flashing is not available for the nRF52 board yet. Use the downloaded firmware file for manual flashing.",
    "install.gatHint": "Download the packaged firmware from the latest GitHub release.",
    "install.checking": "Checking firmware...",
    "install.openRelease": "Open latest release",
    "install.step1": "1. Connect the device with a USB data cable.",
    "install.step2": "2. Put the board into download mode if your hardware requires it.",
    "install.step3":
      "3. Choose the exact SX1262 or LR1121 Pager target, then let the browser flash the merged image.",
    "docs.eyebrow": "Read More",
    "docs.title": "Need deeper setup details?",
    "docs.copy":
      "The wiki has hardware notes, flashing steps, map preparation, configuration help, troubleshooting, and full release notes.",
    "docs.wiki": "Wiki Home",
    "docs.hardware": "Supported Hardware",
    "footer.eyebrow": "Trail Mate Field Notes",
    "footer.title": "For the places where maps and signal both matter.",
    "footer.copy":
      "Follow the repository for firmware releases, device notes, map and language-pack updates, and field-focused fixes.",
    "footer.repo": "Repository",
    "footer.releases": "Releases",
    "footer.wiki": "Wiki",
    "release.waiting": "Waiting for first tagged release",
    "release.ready": "{tag} ready for install",
    "release.unavailable": "Release info unavailable",
    "flasher.activate": "Flash In Browser",
    "flasher.unsupported": "Use Chrome or Edge on desktop with Web Serial enabled.",
    "flasher.notAllowed": "Open this page over HTTPS to use the web flasher.",
    "flasher.readyNote": "Firmware image from the latest release.",
    "flasher.missingAsset":
      "This board is missing browser-flash firmware in the latest release. Use the manual download for now.",
    "flasher.pending": "Browser-flash firmware will appear here after the first tagged release is published.",
    "flasher.error":
      "Could not load release info. Open the GitHub release page for manual downloads.",
    "pack.release": "Release Bundle",
    "pack.review": "Review Bundle",
    "pack.locale": "locale",
    "pack.locales": "locales",
    "pack.font": "font pack",
    "pack.fonts": "font packs",
    "pack.ime": "IME",
    "pack.imes": "IMEs",
    "pack.download": "Download package",
    "pack.emptyTitle": "No published language bundles yet.",
    "pack.emptyText": "Language packs will appear here after the package list is published.",
    "pack.errorTitle": "Language pack list unavailable",
    "pack.errorText": "The homepage could not load the published language-pack list right now.",
    "device.emptyTitle": "Device photos coming later.",
    "device.emptyText":
      "This device has firmware available, but its own screen photos have not been added yet.",
  },
  zh: {
    "top.github": "GitHub",
    "top.wiki": "维基",
    "top.flash": "刷机",
    "side.label": "使用指南",
    "side.overview": "概览",
    "side.capabilities": "能做什么",
    "side.devices": "设备",
    "side.features": "现场使用",
    "side.languages": "语言包",
    "side.flasher": "网页刷机",
    "side.docs": "文档",
    "hero.eyebrow": "离线地图 + LoRa 通信",
    "hero.tagline": "把地图、消息和队友位置装进一台小型 LoRa 设备。",
    "hero.lede":
      "Trail Mate 面向徒步、骑行、露营和野外活动：没有手机信号时，也能从 SD 卡看离线地图、用 LoRa 发消息、检查 GPS 状态，并查看队友的最后位置。",
    "hero.primaryAction": "打开网页刷机",
    "hero.secondaryAction": "查看仓库",
    "hero.releaseKicker": "最新发布",
    "hero.proofKicker": "设备截图",
    "hero.proofText":
      "截图按设备分组展示，方便你确认屏幕效果是否对应自己手上的硬件。",
    "overview.copy":
      "它适合手机不够可靠的场景：地图放在 SD 卡里，消息走 LoRa，不依赖移动网络；还能记录轨迹、检查信号和 GPS，并按需要安装语言包。",
    "tag.maps": "离线地图",
    "tag.gnss": "GNSS 诊断",
    "tag.chat": "网格聊天",
    "tag.team": "团队模式",
    "tag.languages": "语言包",
    "tag.scan": "Sub-GHz 扫描",
    "tag.usb": "PC 连接",
    "capabilities.eyebrow": "能做什么",
    "capabilities.title": "出发前，把地图、通信和设备准备好。",
    "capabilities.note":
      "选择正确设备，刷入对应固件，放入离线地图和语言包，然后在没有网络的地方继续使用。",
    "capabilities.deviceKicker": "设备",
    "capabilities.deviceTitle": "选择你的硬件",
    "capabilities.deviceText": "每种设备都有对应固件、安装说明和需要注意的硬件差异。",
    "capabilities.navigationKicker": "导航",
    "capabilities.navigationTitle": "没有网络也能看地图",
    "capabilities.navigationText": "从 SD 卡加载地图瓦片，切换图层，查看 GPS 定位质量，也能回看路线和轨迹。",
    "capabilities.protocolKicker": "LoRa",
    "capabilities.protocolTitle": "没有手机信号也能发消息",
    "capabilities.protocolText": "通过支持的 LoRa 网格模式发送私聊、频道和团队消息，并在发送失败时给出清楚反馈。",
    "capabilities.installKicker": "安装",
    "capabilities.installTitle": "刷入正确版本",
    "capabilities.installText": "支持的 ESP32 设备可以直接网页刷机；其他设备使用发布包手动安装。",
    "capabilities.localeKicker": "本地化",
    "capabilities.localeTitle": "按需要安装语言",
    "capabilities.localeText": "字体、翻译和键盘布局通过 Extensions 安装，不把所有语言都塞进固件里。",
    "capabilities.fieldKicker": "现场工具",
    "capabilities.fieldTitle": "路上用得到的小工具",
    "capabilities.fieldText": "需要时可以记录轨迹、看 GPS、扫频、接收 SSTV，或者通过 USB 交换数据。",
    "devices.eyebrow": "设备",
    "devices.title": "选择你手上的设备。",
    "devices.note":
      "刷机前一定要确认硬件型号。有些设备外观相似，但不能混刷固件。",
    "devices.interactionKicker": "注意事项",
    "features.navEyebrow": "导航",
    "features.navTitle": "没有手机信号，也能带着地图走。",
    "features.navLead":
      "出发前把地图瓦片放进 SD 卡，路上就能在设备上查看 OSM、地形和卫星图层。GPS 页面会告诉你当前定位是否可信。",
    "features.commEyebrow": "通信",
    "features.commTitle": "手机帮不上忙时，仍然能联系队友。",
    "features.commLead":
      "可以直接在设备上写消息，失败后点消息气泡重发，也能把团队消息和普通频道消息分开。",
    "features.toolsEyebrow": "现场工具",
    "features.toolsTitle": "把常用现场工具也带上。",
    "features.toolsLead":
      "扫频、SSTV 接收、USB 数据交换和轨迹查看，可以在需要检查环境或导出数据时派上用场。",
    "languages.eyebrow": "本地化",
    "languages.title": "固件保持精简，需要什么语言再安装。",
    "languages.note":
      "英文始终内置可用。其他字体、翻译和键盘布局可以从 Extensions 安装，包括西里尔键盘支持。",
    "languages.defaultKicker": "内置默认",
    "languages.defaultTitle": "设备始终可以回到英文。",
    "languages.defaultText": "即使没有安装外部语言包，设备也可以启动、恢复并保持可用。",
    "languages.catalogKicker": "语言包目录",
    "languages.bundleSuffix": "个包，覆盖",
    "languages.localeSuffix": "个可用语言。",
    "languages.catalogText": "语言包可以包含翻译、字体和键盘布局。设备端安装即可，不需要重新编译固件。",
    "languages.gateKicker": "包状态",
    "languages.gateTitle": "复核状态会明确显示。",
    "languages.gateText": "有些语言已经适合日常使用，有些仍需要母语用户继续校对，目录里会直接标出来。",
    "languages.loadingKicker": "目录",
    "languages.loadingTitle": "正在加载语言包目录...",
    "languages.loadingText": "当前已发布的语言包会显示在这里。",
    "install.eyebrow": "安装",
    "install.title": "网页刷机",
    "install.note": "请使用 Google Chrome 或 Microsoft Edge，通过 HTTPS 和 USB 数据线刷写。请选择与硬件 radio 芯片一致的卡片。",
    "install.pagerCopy": "面向键盘 Pager 的 SX1262 固件。LR1121 硬件不要使用这个卡片。",
    "install.pagerLr1121Copy": "面向 Sub-GHz + 2.4 GHz 键盘 Pager 的 LR1121 固件。不要刷到 SX1262 硬件上。",
    "install.tdeckCopy": "面向 T-Deck 键盘 + 触屏布局调校的固件。",
    "install.twatchCopy": "面向紧凑触屏手表实验的固件。",
    "install.gatCopy": "nRF52 目标暂未接入浏览器刷机，请使用发布包手动刷写。",
    "install.gatHint": "从最新 GitHub Release 下载打包固件。",
    "install.checking": "正在检查可用固件...",
    "install.openRelease": "打开最新发布",
    "install.step1": "1. 使用 USB 数据线连接设备。",
    "install.step2": "2. 如果硬件需要，请先进入下载模式。",
    "install.step3": "3. 选择精确的 SX1262 或 LR1121 Pager 目标，让浏览器刷写合并镜像。",
    "docs.eyebrow": "继续阅读",
    "docs.title": "需要更详细的设置说明？",
    "docs.copy":
      "维基里有硬件说明、刷机步骤、地图准备、配置方法、故障排查和完整发布说明。",
    "docs.wiki": "维基首页",
    "docs.hardware": "支持硬件",
    "footer.eyebrow": "Trail Mate 现场笔记",
    "footer.title": "为地图和信号都很重要的地方而做。",
    "footer.copy":
      "关注仓库即可获取固件发布、设备说明、地图和语言包更新，以及面向户外使用的修复。",
    "footer.repo": "仓库",
    "footer.releases": "发布",
    "footer.wiki": "维基",
    "release.waiting": "等待首个带标签发布",
    "release.ready": "{tag} 可安装",
    "release.unavailable": "无法加载发布信息",
    "flasher.activate": "在浏览器中刷机",
    "flasher.unsupported": "请使用支持 Web Serial 的桌面版 Chrome 或 Edge。",
    "flasher.notAllowed": "请通过 HTTPS 打开页面以使用网页刷机。",
    "flasher.readyNote": "来自最新发布的合并固件镜像。",
    "flasher.missingAsset": "最新发布中缺少此板卡的网页刷机产物，请暂时手动下载。",
    "flasher.pending": "首个带标签发布完成后，网页刷机产物会显示在这里。",
    "flasher.error": "无法加载发布信息。请打开 GitHub Release 页面手动下载。",
    "pack.release": "正式包",
    "pack.review": "复核包",
    "pack.locale": "个语言",
    "pack.locales": "个语言",
    "pack.font": "个字体包",
    "pack.fonts": "个字体包",
    "pack.ime": "个输入法",
    "pack.imes": "个输入法",
    "pack.download": "下载语言包",
    "pack.emptyTitle": "还没有发布语言包。",
    "pack.emptyText": "包目录生成后，Pages 会在这里展示语言包。",
    "pack.errorTitle": "语言包目录不可用",
    "pack.errorText": "官网当前无法加载已发布的语言包元数据。",
    "device.emptyTitle": "已预留专属截图位。",
    "device.emptyText": "这个设备已经有固件目标，但还没有加入自己的屏幕截图。",
  },
};

const PACK_NAME_ZH = {
  ar: "阿拉伯语",
  "europe-cyrillic-ext": "欧洲西里尔扩展",
  "europe-latin-ext": "欧洲拉丁扩展",
  ja: "日语",
  ko: "韩语",
  "zh-Hans": "简体中文",
  "zh-Hant": "繁体中文（台湾）",
};

const DEVICES = [
  {
    id: "tlora-pager-sx1262",
    chip: "ESP32-S3 + SX1262",
    title: {
      en: "LilyGo T-LoRa Pager SX1262",
      zh: "LilyGo T-LoRa Pager SX1262",
    },
    status: {
      en: "Mature keyboard target",
      zh: "成熟键盘目标",
    },
    summary: {
      en: "Keyboard Pager with the most complete screenshots, field testing, and broadest Trail Mate UI coverage. Use this target for SX1262 hardware.",
      zh: "键盘版 Pager，当前截图、实机验证和 Trail Mate UI 覆盖最完整。SX1262 硬件使用这个目标。",
    },
    interactions: {
      en: [
        "Use this firmware only for the SX1262 Pager. LR1121 Pager hardware has its own build below.",
        "Best current choice if you want the most tested Trail Mate experience.",
        "Open Help on the Pager main menu to see keyboard shortcuts, including keyboard backlight control.",
        "Install extra language, font, and keyboard packs from Extensions when you need them.",
      ],
      zh: [
        "这个固件只用于 SX1262 Pager。LR1121 Pager 需要使用下方自己的固件。",
        "如果想体验当前最完整、测试最多的 Trail Mate，优先选择这个设备。",
        "在 Pager 主菜单打开 Help 可以查看快捷键，包括键盘背光调节。",
        "需要其他语言、字体或键盘布局时，从 Extensions 安装即可。",
      ],
    },
    screenshots: [
      {
        src: "./assets/showcase/home-main.png",
        title: { en: "Home launcher", zh: "主菜单" },
        alt: { en: "T-LoRa Pager home launcher", zh: "T-LoRa Pager 主菜单" },
      },
      {
        src: "./assets/showcase/nav-map-osm.png",
        title: { en: "Offline OSM map", zh: "离线 OSM 地图" },
        alt: { en: "T-LoRa Pager OSM map screen", zh: "T-LoRa Pager OSM 地图界面" },
      },
      {
        src: "./assets/showcase/nav-map-terrain.png",
        title: { en: "Terrain map", zh: "地形地图" },
        alt: { en: "T-LoRa Pager terrain map screen", zh: "T-LoRa Pager 地形地图界面" },
      },
      {
        src: "./assets/showcase/nav-skyplot.png",
        title: { en: "GNSS sky plot", zh: "GNSS 天空图" },
        alt: { en: "T-LoRa Pager GNSS sky plot screen", zh: "T-LoRa Pager GNSS 天空图界面" },
      },
      {
        src: "./assets/showcase/chat-compose.png",
        title: { en: "Message compose", zh: "消息编写" },
        alt: { en: "T-LoRa Pager message compose screen", zh: "T-LoRa Pager 消息编写界面" },
      },
      {
        src: "./assets/showcase/chat-messages.png",
        title: { en: "Message history", zh: "消息记录" },
        alt: { en: "T-LoRa Pager message history screen", zh: "T-LoRa Pager 消息记录界面" },
      },
      {
        src: "./assets/showcase/team-map.png",
        title: { en: "Team map", zh: "团队地图" },
        alt: { en: "T-LoRa Pager team map screen", zh: "T-LoRa Pager 团队地图界面" },
      },
      {
        src: "./assets/showcase/utility-spectrum.png",
        title: { en: "Energy Sweep", zh: "频谱扫描" },
        alt: { en: "T-LoRa Pager Energy Sweep screen", zh: "T-LoRa Pager 频谱扫描界面" },
      },
      {
        src: "./assets/showcase/utility-tracker.png",
        title: { en: "Tracker", zh: "轨迹" },
        alt: { en: "T-LoRa Pager tracker screen", zh: "T-LoRa Pager 轨迹界面" },
      },
    ],
  },
  {
    id: "tlora-pager-lr1121",
    chip: "ESP32-S3 + LR1121",
    title: {
      en: "LilyGo T-LoRa Pager LR1121",
      zh: "LilyGo T-LoRa Pager LR1121",
    },
    status: {
      en: "0.1.32 LR1121 fix",
      zh: "0.1.32 LR1121 修复",
    },
    summary: {
      en: "Use this target for the LR1121 Pager. The 0.1.32 release fixes MeshCore discovery and contact handling for Android app pairing.",
      zh: "这是 LR1121 Pager 专用目标。0.1.32 修复了 Android app 配对时的 MeshCore 发现和联系人处理。",
    },
    interactions: {
      en: [
        "Check the radio chip before flashing; SX1262 and LR1121 builds are different.",
        "0.1.32 aligns MeshCore path descriptors and Android contact/adverts projection for LR1121 Pager testing.",
        "Some screenshots are shared with the keyboard Pager until dedicated LR1121 photos are added.",
      ],
      zh: [
        "刷机前请确认 radio 芯片；SX1262 和 LR1121 不能混刷。",
        "0.1.32 为 LR1121 Pager 测试对齐 MeshCore path descriptor，以及 Android 联系人/advert 投影。",
        "在补充 LR1121 专属截图前，这里会暂用键盘 Pager 的部分截图。",
      ],
    },
    screenshots: [
      {
        src: "./assets/showcase/home-main.png",
        title: { en: "Home launcher", zh: "主菜单" },
        alt: { en: "T-LoRa Pager LR1121 home launcher", zh: "T-LoRa Pager LR1121 主菜单" },
      },
      {
        src: "./assets/showcase/chat-messages.png",
        title: { en: "Message history", zh: "消息记录" },
        alt: { en: "T-LoRa Pager LR1121 message history screen", zh: "T-LoRa Pager LR1121 消息记录界面" },
      },
      {
        src: "./assets/showcase/team-map.png",
        title: { en: "Team map", zh: "团队地图" },
        alt: { en: "T-LoRa Pager LR1121 team map screen", zh: "T-LoRa Pager LR1121 团队地图界面" },
      },
    ],
  },
  {
    id: "tdeck",
    chip: "ESP32-S3",
    title: {
      en: "LilyGo T-Deck",
      zh: "LilyGo T-Deck",
    },
    status: {
      en: "Keyboard + touch target",
      zh: "键盘 + 触屏设备",
    },
    summary: {
      en: "T-Deck build for map panning, on-device typing, touch gestures, and compact field controls.",
      zh: "面向 T-Deck 的固件，适合触屏拖动地图、设备端输入、触控操作和紧凑现场控制。",
    },
    interactions: {
      en: [
        "Use this target for T-Deck hardware, not for the keyboard Pager.",
        "Touch support matters here: map panning and page controls should feel natural on the screen.",
        "Dedicated screenshots will be added as this device gets more field captures.",
      ],
      zh: [
        "这个目标用于 T-Deck，不要刷到键盘 Pager 上。",
        "触屏是这个设备的重要交互：地图拖动和页面操作应该直接在屏幕上完成。",
        "后续会随着实机测试补充 T-Deck 自己的截图。",
      ],
    },
    screenshots: [],
  },
  {
    id: "lilygo-twatch-s3",
    chip: "ESP32-S3",
    title: {
      en: "LilyGo T-Watch-S3",
      zh: "LilyGo T-Watch-S3",
    },
    status: {
      en: "Compact touch target",
      zh: "小屏触控设备",
    },
    summary: {
      en: "Watch-sized ESP32 target for compact touch experiments and quick field checks.",
      zh: "手表尺寸的 ESP32 目标，适合紧凑触屏实验和快速现场查看。",
    },
    interactions: {
      en: [
        "Use this build only for the T-Watch-S3 hardware.",
        "Expect a touch-first experience with tighter screen space than Pager or T-Deck.",
        "Web flashing appears when matching firmware is available.",
      ],
      zh: [
        "这个固件只用于 T-Watch-S3 硬件。",
        "它是触屏优先的小屏体验，显示空间比 Pager 和 T-Deck 更紧。",
        "对应固件可用时，网页刷机会在下方安装卡片中出现。",
      ],
    },
    screenshots: [],
  },
  {
    id: "gat562-mesh-evb-pro",
    chip: "nRF52",
    title: {
      en: "GAT562 Mesh EVB Pro",
      zh: "GAT562 Mesh EVB Pro",
    },
    status: {
      en: "Manual flashing",
      zh: "手动刷写",
    },
    summary: {
      en: "nRF52 board for users who flash downloaded firmware files manually. Browser flashing is not available for this board yet.",
      zh: "面向 nRF52 的目标，目前需要手动刷写发布包，暂不支持网页刷机。",
    },
    interactions: {
      en: [
        "Use the GAT562 nRF52840 UF2 release file instead of the browser flasher.",
        "This board follows a different flashing model than the ESP32-S3 devices above.",
        "Screenshots will be added when this target has its own captured UI set.",
      ],
      zh: [
        "请优先使用 GitHub Release 中的 GAT562 nRF52840 UF2 发布文件，不要使用网页刷机。",
        "这个设备的刷写方式和上面的 ESP32-S3 设备不同。",
        "后续有独立截图后，会补充该目标自己的界面展示。",
      ],
    },
    screenshots: [],
  },
  {
    id: "t-echo-lite",
    chip: "nRF52",
    title: {
      en: "LILYGO T-Echo-Lite-KeyShield",
      zh: "LILYGO T-Echo-Lite-KeyShield",
    },
    status: {
      en: "Manual flashing",
      zh: "手动刷写",
    },
    summary: {
      en: "nRF52 e-paper keyboard target with 192x176 mono UI, physical keypad input, and local radio/device settings.",
      zh: "nRF52 电子纸键盘目标，带 192x176 mono UI、实体键盘输入，以及本地无线和设备设置。",
    },
    interactions: {
      en: [
        "Use the t-echo-lite nRF52840 UF2 release file from GitHub Releases.",
        "Browser flashing is not available for this nRF52 target yet.",
        "Choose this firmware only for the T-Echo-Lite-KeyShield hardware.",
      ],
      zh: [
        "请优先使用 GitHub Release 中的 t-echo-lite nRF52840 UF2 发布文件。",
        "这个 nRF52 目标暂不支持网页刷机。",
        "这个固件只用于 T-Echo-Lite-KeyShield 硬件。",
      ],
    },
    screenshots: [],
  },
];

let currentLanguage = "en";
let releaseDataCache = null;
let packDataCache = null;
let activeDeviceId = "tlora-pager-sx1262";
let activeShotIndex = 0;

function t(key, params = {}) {
  const template = I18N[currentLanguage]?.[key] ?? I18N.en[key] ?? key;
  return Object.entries(params).reduce(
    (text, [name, value]) => text.replaceAll(`{${name}}`, String(value)),
    template,
  );
}

function localize(value) {
  if (typeof value === "string") {
    return value;
  }
  return value?.[currentLanguage] ?? value?.en ?? "";
}

function createElement(tagName, className, text) {
  const element = document.createElement(tagName);
  if (className) {
    element.className = className;
  }
  if (typeof text === "string") {
    element.textContent = text;
  }
  return element;
}

function createPill(text) {
  return createElement("span", "language-pill", text);
}

function setStat(elements, value) {
  elements.forEach((element) => {
    element.textContent = String(value);
  });
}

function setReleaseVersion(text) {
  releaseVersionEls.forEach((element) => {
    element.textContent = text;
  });
}

function setReleaseLinks(url) {
  releaseLinkEls.forEach((element) => {
    element.href = url;
  });
}

function applyStaticTranslations() {
  i18nEls.forEach((element) => {
    element.textContent = t(element.dataset.i18n);
  });

  document.documentElement.lang = currentLanguage === "zh" ? "zh-CN" : "en";
  document.title = currentLanguage === "zh" ? "Trail Mate - 离线现场通信固件" : "Trail Mate";
}

function updateLanguageButtons() {
  languageButtons.forEach((button) => {
    const isActive = button.dataset.langChoice === currentLanguage;
    button.classList.toggle("is-active", isActive);
    button.setAttribute("aria-pressed", String(isActive));
  });
}

function createInstallButton(manifestPath) {
  const wrapper = document.createElement("div");
  wrapper.className = "install-button-wrap";

  const button = document.createElement("esp-web-install-button");
  button.setAttribute("manifest", `./${manifestPath}`);
  button.innerHTML = `
    <button slot="activate" class="flash-button">${t("flasher.activate")}</button>
    <span slot="unsupported" class="support-note">${t("flasher.unsupported")}</span>
    <span slot="not-allowed" class="support-note">${t("flasher.notAllowed")}</span>
  `;

  wrapper.append(button);
  return wrapper;
}

function renderBoardInstall(card, releaseData) {
  const installSlot = card.querySelector("[data-install-slot]");
  if (!installSlot) {
    return;
  }

  const boardId = card.dataset.boardId;
  const target = releaseData.targets?.[boardId];

  installSlot.replaceChildren();

  if (target?.available) {
    installSlot.append(createInstallButton(target.manifest_path));
    installSlot.append(createElement("p", "board-hint", t("flasher.readyNote")));
    return;
  }

  installSlot.append(
    createElement(
      "p",
      "board-hint",
      releaseData.available ? t("flasher.missingAsset") : t("flasher.pending"),
    ),
  );
}

function renderReleaseData() {
  if (!releaseDataCache) {
    setReleaseVersion(t("release.unavailable"));
    boardCards.forEach((card) => {
      const installSlot = card.querySelector("[data-install-slot]");
      if (installSlot) {
        installSlot.replaceChildren(createElement("p", "board-hint", t("flasher.error")));
      }
    });
    return;
  }

  const versionLabel = releaseDataCache.tag_name
    ? t("release.ready", { tag: releaseDataCache.tag_name })
    : t("release.waiting");

  setReleaseVersion(versionLabel);
  if (releaseDataCache.release_url) {
    setReleaseLinks(releaseDataCache.release_url);
  }

  boardCards.forEach((card) => renderBoardInstall(card, releaseDataCache));
}

function localizedPackName(pack) {
  if (currentLanguage === "zh") {
    return PACK_NAME_ZH[pack.id] ?? pack.display_name ?? pack.id ?? "语言包";
  }
  return pack.display_name || pack.id || "Unnamed package";
}

function localizedPackSummary(pack, locales, fontCount, imeCount) {
  if (currentLanguage === "zh") {
    return `${localizedPackName(pack)}语言包，包含 ${locales.length} ${t(locales.length === 1 ? "pack.locale" : "pack.locales")}、${fontCount} ${t(fontCount === 1 ? "pack.font" : "pack.fonts")}、${imeCount} ${t(imeCount === 1 ? "pack.ime" : "pack.imes")}。`;
  }
  return pack.summary || "No summary available.";
}

function createPackCard(pack) {
  const locales = pack.provides?.locales ?? [];
  const runtime = pack.runtime ?? {};
  const fontCount = runtime.font_count ?? (pack.provides?.fonts?.length ?? 0);
  const imeCount = runtime.ime_count ?? (pack.provides?.ime?.length ?? 0);
  const statuses = Array.from(
    new Set(locales.map((locale) => locale.translation_status || "release").filter(Boolean)),
  );
  const hasRelease = statuses.includes("release");

  const card = createElement("article", "language-pack-card");
  card.append(createElement("p", "feature-kicker", t(hasRelease ? "pack.release" : "pack.review")));
  card.append(createElement("h3", "", localizedPackName(pack)));
  card.append(
    createElement(
      "p",
      "language-pack-summary",
      localizedPackSummary(pack, locales, fontCount, imeCount),
    ),
  );

  const meta = createElement("div", "language-pack-meta");
  meta.append(createPill(`${locales.length} ${t(locales.length === 1 ? "pack.locale" : "pack.locales")}`));
  meta.append(createPill(`${fontCount} ${t(fontCount === 1 ? "pack.font" : "pack.fonts")}`));
  meta.append(createPill(`${imeCount} ${t(imeCount === 1 ? "pack.ime" : "pack.imes")}`));
  statuses.forEach((status) => {
    meta.append(createPill(status));
  });
  card.append(meta);

  if (locales.length > 0) {
    const localeRow = createElement("div", "language-pack-locales");
    locales.slice(0, 7).forEach((locale) => {
      const status = locale.translation_status || "release";
      const label = currentLanguage === "zh" ? PACK_NAME_ZH[pack.id] ?? locale.display_name : locale.display_name;
      localeRow.append(createPill(status === "release" ? label : `${label} (${status})`));
    });
    if (locales.length > 7) {
      localeRow.append(createPill(`+${locales.length - 7} more`));
    }
    card.append(localeRow);
  }

  if (Array.isArray(pack.supported_memory_profiles) && pack.supported_memory_profiles.length > 0) {
    const memoryRow = createElement("div", "language-pack-profiles");
    pack.supported_memory_profiles.forEach((profile) => {
      memoryRow.append(createPill(profile));
    });
    card.append(memoryRow);
  }

  if (pack.archive?.path) {
    const link = createElement("a", "release-link", t("pack.download"));
    link.href = `./${pack.archive.path}`;
    card.append(link);
  }

  return card;
}

function renderPackCatalog() {
  if (!packGrid) {
    return;
  }

  if (!packDataCache) {
    setStat(packCountEls, 0);
    setStat(localeCountEls, 0);
    packGrid.replaceChildren();

    const fallback = createElement("article", "language-pack-card language-pack-empty");
    fallback.append(createElement("p", "feature-kicker", t("languages.loadingKicker")));
    fallback.append(createElement("h3", "", t("pack.errorTitle")));
    fallback.append(createElement("p", "language-pack-summary", t("pack.errorText")));
    packGrid.append(fallback);
    return;
  }

  const packs = (packDataCache.packages ?? []).filter((pack) => pack.package_type === "locale-bundle");
  const totalLocales = packs.reduce((sum, pack) => {
    const runtimeLocales = pack.runtime?.locale_count;
    if (typeof runtimeLocales === "number") {
      return sum + runtimeLocales;
    }
    return sum + (pack.provides?.locales?.length ?? 0);
  }, 0);

  setStat(packCountEls, packs.length);
  setStat(localeCountEls, totalLocales);
  packGrid.replaceChildren();

  if (packs.length === 0) {
    const emptyCard = createElement("article", "language-pack-card language-pack-empty");
    emptyCard.append(createElement("p", "feature-kicker", t("languages.loadingKicker")));
    emptyCard.append(createElement("h3", "", t("pack.emptyTitle")));
    emptyCard.append(createElement("p", "language-pack-summary", t("pack.emptyText")));
    packGrid.append(emptyCard);
    return;
  }

  packs.forEach((pack) => {
    packGrid.append(createPackCard(pack));
  });
}

function findDevice(deviceId) {
  return DEVICES.find((device) => device.id === deviceId) ?? DEVICES[0];
}

function updateDeviceActiveStates() {
  deviceNavButtons.forEach((button) => {
    button.classList.toggle("is-active", button.dataset.deviceNav === activeDeviceId);
  });
  deviceTabButtons.forEach((button) => {
    button.classList.toggle("is-active", button.dataset.deviceTarget === activeDeviceId);
  });
}

function renderDeviceShot(device, index) {
  const shot = device.screenshots[index];
  if (!shot) {
    deviceMainImage.src = "./assets/showcase/brand-logo.png";
    deviceMainImage.alt = localize(device.title);
    deviceMainCaption.textContent = t("device.emptyTitle");
    return;
  }

  deviceMainImage.src = shot.src;
  deviceMainImage.alt = localize(shot.alt);
  deviceMainCaption.textContent = localize(shot.title);
}

function renderDevice() {
  const device = findDevice(activeDeviceId);
  activeShotIndex = Math.min(activeShotIndex, Math.max(device.screenshots.length - 1, 0));

  deviceKicker.textContent = device.chip;
  deviceTitle.textContent = localize(device.title);
  deviceSummary.textContent = localize(device.summary);
  deviceStatus.textContent = localize(device.status);

  deviceInteractions.replaceChildren();
  localize(device.interactions).forEach((item) => {
    deviceInteractions.append(createElement("li", "", item));
  });

  renderDeviceShot(device, activeShotIndex);
  deviceThumbGrid.replaceChildren();

  if (device.screenshots.length === 0) {
    const empty = createElement("div", "device-empty");
    empty.append(createElement("h4", "", t("device.emptyTitle")));
    empty.append(createElement("p", "", t("device.emptyText")));
    deviceThumbGrid.append(empty);
    return;
  }

  device.screenshots.forEach((shot, index) => {
    const button = document.createElement("button");
    button.className = `device-thumb${index === activeShotIndex ? " is-active" : ""}`;
    button.type = "button";
    button.dataset.shotIndex = String(index);

    const image = document.createElement("img");
    image.src = shot.src;
    image.alt = localize(shot.alt);
    image.loading = "lazy";

    button.append(image, createElement("span", "", localize(shot.title)));
    deviceThumbGrid.append(button);
  });
}

function selectDevice(deviceId, shouldScroll = false) {
  activeDeviceId = deviceId;
  activeShotIndex = 0;
  updateDeviceActiveStates();
  renderDevice();

  if (shouldScroll) {
    document.querySelector("#devices")?.scrollIntoView({ behavior: "smooth", block: "start" });
  }
}

function setLanguage(language) {
  currentLanguage = language === "zh" ? "zh" : "en";
  localStorage.setItem("trail-mate-language", currentLanguage);
  updateLanguageButtons();
  applyStaticTranslations();
  renderReleaseData();
  renderPackCatalog();
  renderDevice();
}

async function loadReleaseData() {
  try {
    const response = await fetch("./data/latest-release.json", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`Unexpected status ${response.status}`);
    }

    releaseDataCache = await response.json();
    renderReleaseData();
  } catch (error) {
    releaseDataCache = null;
    renderReleaseData();
  }
}

async function loadPackCatalog() {
  if (!packGrid) {
    return;
  }

  try {
    const response = await fetch("./data/packs.json", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`Unexpected status ${response.status}`);
    }

    packDataCache = await response.json();
    renderPackCatalog();
  } catch (error) {
    packDataCache = null;
    renderPackCatalog();
  }
}

languageButtons.forEach((button) => {
  button.addEventListener("click", () => {
    setLanguage(button.dataset.langChoice);
  });
});

deviceNavButtons.forEach((button) => {
  button.addEventListener("click", () => {
    selectDevice(button.dataset.deviceNav, true);
  });
});

deviceTabButtons.forEach((button) => {
  button.addEventListener("click", () => {
    selectDevice(button.dataset.deviceTarget, false);
  });
});

deviceThumbGrid?.addEventListener("click", (event) => {
  const thumb = event.target.closest("[data-shot-index]");
  if (!thumb) {
    return;
  }

  const device = findDevice(activeDeviceId);
  activeShotIndex = Number(thumb.dataset.shotIndex);
  renderDeviceShot(device, activeShotIndex);
  deviceThumbGrid.querySelectorAll(".device-thumb").forEach((button) => {
    button.classList.toggle("is-active", button === thumb);
  });
});

const urlParams = new URLSearchParams(window.location.search);
const urlLanguage = urlParams.get("lang");
const urlDevice = urlParams.get("device");
const savedLanguage = localStorage.getItem("trail-mate-language");
if (DEVICES.some((device) => device.id === urlDevice)) {
  activeDeviceId = urlDevice;
}
setLanguage(urlLanguage === "zh" || savedLanguage === "zh" ? "zh" : "en");
selectDevice(activeDeviceId, false);
loadReleaseData();
loadPackCatalog();
