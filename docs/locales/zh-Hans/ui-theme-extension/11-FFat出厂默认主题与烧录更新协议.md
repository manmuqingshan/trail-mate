# 11：FFat 出厂默认主题与烧录/更新协议

## 结论

完整的 `builtin.default` 主题资源不应编译到应用固件镜像，而应作为与固件版本配套的**出厂 FFat 主题镜像**写入内部 Flash。应用固件只包含业务能力、路由、动作、渲染/加载代码和独立的 `rescue` 恢复呈现。

这意味着“内置默认主题”需要严格区分两层含义：

| 层 | 存储位置 | 内容 | 是否完整 UI |
| --- | --- | --- | --- |
| `builtin.default` 出厂主题 | FFat 分区 | 图标、颜色/令牌、主题布局二进制、资源索引、主题允许的字体资源 | 是，FFat 正常时的完整默认界面。 |
| `rescue` 恢复呈现 | 应用固件镜像 | 固定文本/几何呈现、FFat 诊断、恢复/刷写引导、重启和受控维护入口 | 否；不能承担日常完整界面。 |

`rescue` 是独立的恢复责任：如果 FFat 损坏且固件不保留显示能力，设备会变成无法诊断的黑屏。它不得复制当前主题的资源、菜单或布局，只提供“FFat 未挂载/资源校验失败/版本不匹配”、目标/profile 摘要、恢复指引、重启和已授权维护入口。

## FFat 固定目录与只读逻辑边界

```text
FFat 根目录
└── /trailmate/factory/
    ├── factory-theme.header
    ├── factory-theme.signature
    └── themes/
        └── builtin.default/
            ├── package.ini
            ├── theme.ini
            ├── theme.tmb
            ├── assets.index
            ├── assets/
            │   └── <asset-id>.bin
            └── fonts/
                └── <font-id>.bin        # 仅主题 API 明确允许时
```

规则如下：

- FFat `/trailmate/factory/` 是**逻辑只读**区域。运行时、Extensions、用户主题安装器和 SD 侧载均不得向其中写入文件。
- 用户下载主题只允许写入 SD `/trailmate/packs/themes/`；即使用户主题与默认主题 ID 相同也必须在安装前拒绝。
- `builtin.default` 不使用 SD 的 `theme-index.a/b`，也不使用 SD 的 `theme-state.a/b` 作为完整资源来源。
- 固件通过固定路径加载 FactoryThemeHeader，而不是扫描 FFat 找主题；这避免了大目录扫描和任意文件误识别。
- FFat 中的 `package.ini` / `theme.ini` 遵守文档 10 的有界行式读取上限；`theme.tmb` / `assets.index` 遵守相同的有界二进制读取规则。

## `FactoryThemeHeader`：应用与 FFat 的配对契约

`factory-theme.header` 必须是固定大小的二进制记录，而不是 JSON。建议命名 magic 为 `TMFH`（Trail Mate Factory Header），并至少包含：

```text
magic                         4 B  = TMFH
header format major/minor     u16/u16
factory generation            u64
firmware UI ABI major/minor   u16/u16
capability_contract_sha256    32 B
builtin theme version         定长 SemVer 字节字段
package.ini SHA-256           32 B
theme.ini SHA-256             32 B
theme.tmb SHA-256             32 B
assets.index SHA-256          32 B
factory payload root hash     32 B
build identity / key id       定长字段
header CRC32                  u32
signature                     固定长度签名字段
```

应用固件编译时固化自己的：

- 可接受的 `firmware UI ABI` 主版本；
- 可接受的 `capability_contract_sha256`；
- 官方 FFat 发布根公钥或受信 key ID；
- FactoryThemeHeader 支持的格式版本范围。

启动时先读取这个固定头，再决定是否继续读取 FFat 主题内容。验证顺序为：

```text
挂载 FFat
  → 读取固定大小 FactoryThemeHeader
  → 验证 magic / 长度 / CRC / 头部格式
  → 验证签名和 key ID
  → 比对 UI ABI、能力契约哈希与当前固件
  → 流式哈希 package.ini / theme.ini / theme.tmb / assets.index
  → 以有界 reader 验证 manifest、布局和资产索引
  → 允许 FactoryThemeProvider 提供 builtin.default
```

任一失败都不允许将 SD 主题提升为默认主题，因为 SD 是用户可变存储。失败处理固定为进入 `rescue`。

大体积 `assets/*.bin` 不要求每次启动完整哈希整个资源集合；它们由 `assets.index` 中的每资源 SHA-256 绑定，并在首次按需读取时流式验证。对默认主题而言，任何关键资源校验失败都应使相应路由不可用并进入 rescue；不允许回退到“固件里还有另一份完整图标”。

## 出厂构建产物

默认主题的源工程可以与第三方主题使用同一编译工具，但产物角色不同。

```text
themes/builtin.default-source/
  ├── theme.yaml
  ├── package.ini
  ├── assets-src/
  └── ...
          ↓ theme_pack.py build --kind factory
build/factory-theme/
  └── trailmate/factory/themes/builtin.default/
      ├── package.ini
      ├── theme.ini
      ├── theme.tmb
      ├── assets.index
      └── assets/*.bin
          ↓ factory image builder
build/ffat/factory-theme-image.bin
          ↓ production flash / paired firmware release
FFat partition
```

建议新增独立的主机命令语义：

```powershell
# 仅构建出厂默认主题负载，不生成可安装第三方 .tmt
python tools/theme_pack.py build --kind factory --source themes/builtin.default-source --out build/factory-theme

# 从已验证负载构造 FFat 镜像并写入 FactoryThemeHeader
python tools/theme_pack.py build-ffat-image --factory-root build/factory-theme --firmware-contract build/theme-sdk/contract.sha256 --out build/ffat/factory-theme-image.bin

# 在主机侧用同一有界格式验证器检查镜像内容与应用 ABI 配对
python tools/theme_pack.py verify-ffat-image --image build/ffat/factory-theme-image.bin --firmware-contract build/theme-sdk/contract.sha256
```

这些是拟议工具接口；它们必须由同一个规范化构建过程生成确定性结果、哈希和签名，不能允许手工复制文件绕过 FactoryThemeHeader。

## 启动状态机

```text
Boot
  → 初始化 UI 能力壳、UX Pack、输入和 rescue
  → mount FFat
      └─失败 → rescue: FFAT_MOUNT_FAILED
  → verify FactoryThemeHeader
      └─失败 → rescue: FACTORY_HEADER_INVALID
  → verify ABI / contract / factory core files
      └─失败 → rescue: FACTORY_THEME_INCOMPATIBLE
  → activate FactoryThemeProvider(builtin.default)
      └─失败 → rescue: FACTORY_THEME_LOAD_FAILED
  → 显示完整默认界面
  → mount SD 并尝试用户主题覆盖
      └─失败 → 保持 FFat builtin.default
```

`rescue` 中至少应提供：错误码、FFat 版本/期望 ABI 的非敏感摘要、重新启动、进入 USB/维护恢复模式（如该目标已有安全能力）以及“请刷写配套资源镜像”的说明。它不能自动格式化 FFat、自动删除用户 SD 包或执行不可逆操作。

## 工厂烧录与版本配对

一个正式固件发布物至少包含两个不可分割的逻辑组件：

```text
Release R
├── app firmware image
└── FFat factory-theme image
```

它们通过 FactoryThemeHeader 中的 UI ABI 和能力契约哈希配对。发布工程必须保证：

```text
app(R) 只接受 factory-theme(R) 或明确声明兼容的 factory-theme generation
factory-theme(R) 只服务于 app(R) 支持的 UI ABI/contract
```

制造/全量刷机流程需要先擦写或准备 FFat 分区，再写入应用与 FFat 配套镜像，并在首次启动验证 FactoryThemeHeader。SD 用户主题完全不属于这个出厂刷写事务。

## OTA 更新：必须在实现前作出的选择

把默认主题放进 FFat 后，OTA 不能再只更新应用镜像。否则出现以下危险状态：新固件已启动，但 FFat 仍是旧主题契约；或 FFat 已写一半而旧固件还在运行。

因此在实现前必须在以下两种策略中做出明确选择，不能临时处理：

### 策略 A：单 FFat 分区的兼容性 OTA 与配套维护刷写

- 应用 OTA 在下载前读取/比较当前 FactoryThemeHeader；只有 ABI/契约明确兼容时才允许单独更新应用。
- 任何需要默认主题资源变化的发布，要求使用“应用 + FFat 镜像”的配套全量刷机/维护升级流程。
- 优点：不需要双 FFat 分区，最容易证明断电安全。
- 代价：默认主题随正式版本更新时，用户需要执行配套资源更新，不能只走最简单 OTA。

### 策略 B：双 FFat 分区的成对 OTA

```text
factory_ffat_a + factory_ffat_b
app_ota_a + app_ota_b
受 CRC 保护的 boot-pair state
```

更新写入非活动 app/FFat 对 → 验证两个镜像 → 写入 `pending` 配对状态 → 重启到新对 → 应用自检 → 提交 `committed`。任一失败恢复原 app/FFat 对。

- 优点：可安全 OTA 更新默认主题。
- 代价：需要两份 FFat 分区容量、boot 配对选择逻辑、分区表变更、掉电状态机和更多验证工作。

当前分区尚未证明具备双 FFat 容量与 boot-pair 状态机时，应冻结为策略 A：应用 OTA 仅接受与当前 `TMFH` 明确兼容的组合，默认主题变更通过配套维护刷写完成。只有在策略 B 所需的分区预算、bootloader/启动顺序、状态机和全掉电试验已实际具备后，发布策略才能切换为策略 B；切换必须伴随新的 ADR、分区迁移和完整回滚证据。

## FFat 损坏与救援边界

| 情形 | 完整 UI | 行为 |
| --- | --- | --- |
| SD 不存在，FFat 正常 | FFat `builtin.default` | 正常运行，无外部主题。 |
| SD 外部主题损坏，FFat 正常 | FFat `builtin.default` | 拒绝外部主题并回退。 |
| FFat 头部/ABI/核心文件错误 | 否 | 进入 rescue；要求恢复配套 FFat 镜像。 |
| FFat 关键资产按需校验失败 | 否 | 进入 rescue；不能借 SD 或旧主题伪造默认。 |
| FFat 挂载失败 | 否 | 进入 rescue；提供非破坏性维护入口。 |

这条边界必须被产品接受：既然完整默认资源不再在应用固件中冗余保存，FFat 损坏时不可能继续保证完整 UI。正确做法是提供可操作的恢复模式和可靠的 FFat 刷写流程，而不是在代码里暗藏第二份完整资源。

## 实施准入补充条件

在开始把现有 C/C++ 图标和布局迁移到 FFat 前，以下全部必须冻结：

- [ ] FFat 分区名称、大小、挂载时机、格式化策略和与现有数据的隔离方式；
- [ ] FactoryThemeHeader 的精确字节格式、签名算法、key ID 和根公钥轮换策略；
- [ ] `builtin.default` 源工程、FFat 镜像构建、签名与生产烧录流程；
- [ ] 应用与 FFat 的 ABI/契约配对规则，以及策略 A 或策略 B 的 OTA 决定；
- [ ] rescue 的显示能力、输入路径、维护入口与不可逆操作限制；
- [ ] FFat 头部坏、目录缺、单资源坏、分区写满、挂载失败和掉电的实机测试矩阵；
- [ ] 现有资源从 C/C++ 编译产物迁出后，固件镜像大小、FFat 空间、启动时间、堆/栈和渲染性能基线。

没有这些结论时，“把内置资源放进 FFat”只是存储意图，尚不足以开始安全实现。
