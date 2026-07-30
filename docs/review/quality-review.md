# Trail Mate 模型完整性评审队列

本队列把 Model Explorer 没有展示的内容分成两类，不能混为一谈：

1. **已有模型未发现**：代码已经有稳定语言、状态 owner、规则和用例，但作者 Registry 或工具漏掉了它。
2. **设计尚未形成**：相关能力和规则已经出现，但没有形成职责闭合的模型；它只能留在 Review Queue，不能生成空壳 Model。

本次文档由 Codex 直接阅读源码和 GitNexus 关系后编写；没有调用 Praxis Agent。

## 模型完整性总览

| 类型 | 当前数量 | 处理方式 |
| --- | ---: | --- |
| Model Explorer 中有源码证据的模型 | 9 | 展示并允许下钻 |
| 已有模型未发现 | 1 | 联系人目录已补入 Registry；finding 保留到 UI 验收 |
| 设计尚未形成 | 4 | Navigation、Configuration、Team Membership、IdentityLink 留在队列 |
| 尚未裁决的模型候选组 | 1 | Call、Package、Firmware、Wi-Fi Lease 逐项判定 Model 或 Projection |
| 相关边界/规则缺陷 | 2 | Team RxMeta 耦合、联系人可见性规则失效 |
| 已修复的历史发现缺陷 | 1 | 固定三模板问题标为 resolved |

## 当前问题

| Severity | Finding | 模型完整性类型 | 状态 |
| --- | --- | --- | --- |
| P1 | [【已有模型未发现】联系人、对端目录与本地信任未进入 Model Explorer](issues/contact-peer-directory-not-discovered.md) | discovery gap | acknowledged |
| P1 | [【设计未形成】路线导航规则仍由 UI Runtime 持有](issues/route-navigation-domain-model-missing.md) | domain design gap | acknowledged |
| P1 | [【设计未形成】配置缺少版本、验证与原子提交 owner](issues/configuration-aggregate-missing.md) | domain design gap | acknowledged |
| P1 | [【设计未形成】团队成员与团队生命周期没有领域 owner](issues/team-membership-lifecycle-model-missing.md) | domain design gap | acknowledged |
| P1 | [【设计未形成】协议身份到业务联系人的 IdentityLink 缺失](issues/peer-identity-ownership-split.md) | domain design gap | acknowledged |
| P2 | [【候选待裁决】系统与媒体 Runtime 尚未完成 Model-or-Projection 分类](issues/runtime-model-candidates-unclassified.md) | classification gap | acknowledged |
| P2 | [【边界缺陷】Team 领域事件直接依赖 Chat 接收元数据](issues/team-domain-imports-chat-rxmeta.md) | boundary gap | acknowledged |
| P2 | [【规则失效】附近节点可见性注释与实际查询行为不一致](issues/contact-visibility-policy-disabled.md) | rule gap | acknowledged |
| P1 | [【已修复的发现缺陷】固定三模板曾遮蔽真实模型](issues/domain-models-existed-but-were-not-discovered.md) | historical discovery gap | resolved |

## 判断边界

- “缺少文档”不自动等于“缺少模型”；先看代码中是否已有 owner 和不变量。
- `MeshPeerRecord` 已存在，因此联系人目录属于发现缺陷；`IdentityLink` 不存在，因此属于设计缺陷。
- `TeamService` 有 roster 操作，但没有 TeamMember 生命周期 owner，因此 Team 仍是 candidate。
- Call、Package、Firmware、Wi-Fi Lease 有稳定状态语言，但尚未判定为独立模型、现有模型元素、应用工作流或集成投影。
- 只有实现、测试和文档共同闭合后，设计缺口才能转成 Model Explorer 中的 confirmed model。

<!-- praxis:quality-review:model:start -->
{
  "schemaVersion": "praxis.qualityReviewDocuments.v1",
  "root": "C:\\Users\\vicliu\\Projects\\trail-mate",
  "generatedAt": "2026-07-22T23:40:00+08:00",
  "run": {
    "schemaVersion": "praxis.reviewRun.v1",
    "id": "review:trail-mate-model-completeness:2026-07-22",
    "root": "C:\\Users\\vicliu\\Projects\\trail-mate",
    "generatedAt": "2026-07-22T23:40:00+08:00",
    "source": "hybrid",
    "status": "completed",
    "categories": ["documentation_knowledge", "architecture_boundaries", "configuration_environment", "dependencies_coupling", "code_quality_maintainability"],
    "findingIds": [
      "finding:contact-peer-directory-not-discovered",
      "finding:route-navigation-domain-model-missing",
      "finding:configuration-aggregate-missing",
      "finding:team-membership-lifecycle-model-missing",
      "finding:peer-identity-ownership-split",
      "finding:runtime-model-candidates-unclassified",
      "finding:team-domain-imports-chat-rxmeta",
      "finding:contact-visibility-policy-disabled",
      "finding:domain-models-existed-but-were-not-discovered"
    ],
    "summary": {
      "total": 9,
      "bySeverity": { "P0": 0, "P1": 6, "P2": 3, "P3": 0 },
      "byCategory": { "documentation_knowledge": 2, "architecture_boundaries": 4, "configuration_environment": 1, "dependencies_coupling": 1, "code_quality_maintainability": 1 }
    },
    "traceIds": ["trace:identity-to-directory", "trace:directory-to-team"]
  },
  "categoryOrder": ["documentation_knowledge", "architecture_boundaries", "configuration_environment", "dependencies_coupling", "code_quality_maintainability"],
  "categories": [
    {
      "category": "documentation_knowledge",
      "title": "模型发现与作者文档",
      "status": "has_unresolved",
      "summary": "固定三模板问题已修复；联系人目录作为具体漏识别模型仍需完成 UI 验收。",
      "evaluatorSummary": "九个模型已有源码证据；每个发现缺口必须单独报告。",
      "findingIds": ["finding:contact-peer-directory-not-discovered", "finding:domain-models-existed-but-were-not-discovered"],
      "unresolvedFindingIds": ["finding:contact-peer-directory-not-discovered"],
      "docPath": "docs/review/categories/documentation_knowledge.md",
      "htmlPath": "docs/review/categories/documentation_knowledge.html"
    },
    {
      "category": "architecture_boundaries",
      "title": "尚未形成的领域模型",
      "status": "has_unresolved",
      "summary": "Navigation、Team Membership 与 IdentityLink 没有形成明确 owner；四组系统/媒体 Runtime 尚未完成模型分类。",
      "evaluatorSummary": "三项是已确认的设计缺口；Call、Package、Firmware、Wi-Fi Lease 是必须逐项裁决的候选。",
      "findingIds": ["finding:route-navigation-domain-model-missing", "finding:team-membership-lifecycle-model-missing", "finding:peer-identity-ownership-split", "finding:runtime-model-candidates-unclassified"],
      "unresolvedFindingIds": ["finding:route-navigation-domain-model-missing", "finding:team-membership-lifecycle-model-missing", "finding:peer-identity-ownership-split", "finding:runtime-model-candidates-unclassified"],
      "docPath": "docs/review/categories/architecture_boundaries.md",
      "htmlPath": "docs/review/categories/architecture_boundaries.html"
    },
    {
      "category": "configuration_environment",
      "title": "配置模型",
      "status": "has_unresolved",
      "summary": "AppConfig、领域 settings、默认值、迁移和平台 store 没有统一提交边界。",
      "evaluatorSummary": "配置数据存在，但版本、验证与原子提交模型尚未形成。",
      "findingIds": ["finding:configuration-aggregate-missing"],
      "unresolvedFindingIds": ["finding:configuration-aggregate-missing"],
      "docPath": "docs/review/categories/configuration_environment.md",
      "htmlPath": "docs/review/categories/configuration_environment.html"
    },
    {
      "category": "dependencies_coupling",
      "title": "跨模型依赖",
      "status": "has_unresolved",
      "summary": "Team 领域事件直接携带 chat::RxMeta。",
      "evaluatorSummary": "Team 需要自己的最小接收上下文。",
      "findingIds": ["finding:team-domain-imports-chat-rxmeta"],
      "unresolvedFindingIds": ["finding:team-domain-imports-chat-rxmeta"],
      "docPath": "docs/review/categories/dependencies_coupling.md",
      "htmlPath": "docs/review/categories/dependencies_coupling.html"
    },
    {
      "category": "code_quality_maintainability",
      "title": "规则一致性",
      "status": "has_unresolved",
      "summary": "联系人附近节点的新鲜度说明、状态文本与查询行为不一致。",
      "evaluatorSummary": "isNodeVisible 当前无条件返回 true。",
      "findingIds": ["finding:contact-visibility-policy-disabled"],
      "unresolvedFindingIds": ["finding:contact-visibility-policy-disabled"],
      "docPath": "docs/review/categories/code_quality_maintainability.md",
      "htmlPath": "docs/review/categories/code_quality_maintainability.html"
    }
  ],
  "findings": [
    {
      "schemaVersion": "praxis.reviewFinding.v1",
      "id": "finding:contact-peer-directory-not-discovered",
      "runId": "review:trail-mate-model-completeness:2026-07-22",
      "category": "documentation_knowledge",
      "severity": "P1",
      "status": "acknowledged",
      "title": "【已有模型未发现】联系人、对端目录与本地信任未进入 Model Explorer",
      "summary": "MeshPeerRecord、IMeshPeerDirectory、ContactService 和独立 stores 已构成可运行的目录/联系人边界，但上一版 Registry 只列出八个模型。",
      "whyItMatters": "漏掉该模型会把联系人、协议节点、Mesh 密钥身份和会话参与者混为一谈，也会隐藏本机 nickname、ignored 和 trust 的状态所有权。",
      "suggestedAction": "将 contact-peer-directory 作为第九个源码支持模型加入 Registry，补齐元素、生命周期图和跨模型关系，并在 UI 验收后关闭 finding。",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
        { "source": "file", "path": "modules/core_chat/include/chat/domain/mesh_peer_directory.h", "summary": "定义目录身份、记录、协议 facts、观察和用户 flags。" },
        { "source": "file", "path": "modules/core_chat/include/chat/ports/i_mesh_peer_directory.h", "summary": "定义目录 record/find/search/flags/remove/flush 契约。" },
        { "source": "file", "path": "modules/core_chat/include/chat/usecase/contact_service.h", "summary": "定义联系人、附近节点、ignored 和人工验证用例。" },
        { "source": "file", "path": "docs/models/contact-peer-directory/model.md", "summary": "作者模型文档已经补入。" }
      ],
      "affectedAnchors": [{ "kind": "file", "id": "docs/models/contact-peer-directory/model.md", "path": "docs/models/contact-peer-directory/model.md" }],
      "traceIds": ["trace:identity-to-directory"],
      "createdAt": "2026-07-22T23:40:00+08:00",
      "updatedAt": "2026-07-22T23:40:00+08:00"
    },
    {
      "schemaVersion": "praxis.reviewFinding.v1",
      "id": "finding:route-navigation-domain-model-missing",
      "runId": "review:trail-mate-model-completeness:2026-07-22",
      "category": "architecture_boundaries",
      "severity": "P1",
      "status": "acknowledged",
      "title": "【设计未形成】路线导航规则仍由 UI Runtime 持有",
      "summary": "项目能计算点到 route segment 的最近距离并用双阈值更新偏航状态，但 Route、NavigationSession、RouteProgress 与 DeviationPolicy 没有核心 owner。",
      "whyItMatters": "偏航和恢复是导航业务决策；留在 UI 会让目标、界面和测试产生不同语义，也无法表达路线进度和重入规则。",
      "suggestedAction": "先形成 Route、NavigationSession、RouteProgress 和 DeviationPolicy 的设计与测试，再决定放入 core_gps navigation package 或独立 core_navigation。",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
        { "source": "file", "path": "modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp", "summary": "distance_to_segment_m、nearest_route_distance_m 与 update_route_deviation_state 位于 UI runtime。" },
        { "source": "file", "path": "modules/core_sys/include/platform/ui/route_storage.h", "summary": "路线存储仍位于 platform/ui 契约。" }
      ],
      "affectedAnchors": [{ "kind": "file", "id": "modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp", "path": "modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp" }],
      "traceIds": [],
      "createdAt": "2026-07-22T00:00:00+08:00",
      "updatedAt": "2026-07-22T23:40:00+08:00"
    },
    {
      "schemaVersion": "praxis.reviewFinding.v1",
      "id": "finding:configuration-aggregate-missing",
      "runId": "review:trail-mate-model-completeness:2026-07-22",
      "category": "configuration_environment",
      "severity": "P1",
      "status": "acknowledged",
      "title": "【设计未形成】配置缺少版本、验证与原子提交 owner",
      "summary": "AppConfig 同时承载通信、设备、GPS、地图、网络、隐私、路线和 APRS 设置；默认值、兼容转换与平台持久化没有统一模型。",
      "whyItMatters": "跨字段和 capability 约束无法在单一边界验证，迁移和部分写入行为也难以证明一致。",
      "suggestedAction": "设计有 schema version 的 ConfigurationSnapshot、按领域划分的 typed settings、ConfigurationPolicy 与原子 ConfigurationService。",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
        { "source": "file", "path": "modules/core_sys/include/app/app_config.h", "summary": "AppConfig 聚集多个领域设置和默认值。" },
        { "source": "file", "path": "modules/core_chat/include/chat/domain/chat_types.h", "summary": "MeshConfig 与通信大类型共置。" },
        { "source": "file", "path": "platform/esp/arduino_common/src/app_config_store.cpp", "summary": "持久化与兼容逻辑位于平台实现。" }
      ],
      "affectedAnchors": [{ "kind": "file", "id": "modules/core_sys/include/app/app_config.h", "path": "modules/core_sys/include/app/app_config.h" }],
      "traceIds": [],
      "createdAt": "2026-07-22T00:00:00+08:00",
      "updatedAt": "2026-07-22T23:40:00+08:00"
    },
    {
      "schemaVersion": "praxis.reviewFinding.v1",
      "id": "finding:team-membership-lifecycle-model-missing",
      "runId": "review:trail-mate-model-completeness:2026-07-22",
      "category": "architecture_boundaries",
      "severity": "P1",
      "status": "acknowledged",
      "title": "【设计未形成】团队成员与团队生命周期没有领域 owner",
      "summary": "TeamService 已执行 roster、kick、leader transfer、key distribution 与 PKI verification，但 team/domain 只有 TeamId、TeamKeys 和 pairing 状态。",
      "whyItMatters": "vector<NodeId> 无法表达成员资格来源、角色、revision、撤销和跨协议稳定身份，分散动作也没有共同不变量。",
      "suggestedAction": "先定义团队生命周期、成员资格、leader 唯一性、roster revision 与凭据撤销规则，再建立实现和测试；完成前 Team 保持 candidate。",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
        { "source": "file", "path": "modules/core_team/include/team/domain/team_types.h", "summary": "只有 TeamId、TeamKeys、配对角色和配对状态。" },
        { "source": "file", "path": "modules/core_team/include/team/usecase/team_service.h", "summary": "公开 roster、kick、leader transfer、key distribution 与协同发送行为。" },
        { "source": "file", "path": "modules/core_team/src/usecase/team_service.cpp", "summary": "rememberTeamMember 与 updateTeamMemberRoster 只维护 NodeId vector。" }
      ],
      "affectedAnchors": [{ "kind": "file", "id": "modules/core_team/include/team/domain/team_types.h", "path": "modules/core_team/include/team/domain/team_types.h" }],
      "traceIds": ["trace:directory-to-team"],
      "createdAt": "2026-07-22T23:40:00+08:00",
      "updatedAt": "2026-07-22T23:40:00+08:00"
    },
    {
      "schemaVersion": "praxis.reviewFinding.v1",
      "id": "finding:peer-identity-ownership-split",
      "runId": "review:trail-mate-model-completeness:2026-07-22",
      "category": "architecture_boundaries",
      "severity": "P1",
      "status": "acknowledged",
      "title": "【设计未形成】协议身份到业务联系人的 IdentityLink 缺失",
      "summary": "PeerPublicKey、MeshPeerRecord、Reticulum identity、联系人和未来 TeamMember 已分别出现，但没有显式、可撤销的映射模型。",
      "whyItMatters": "改名、密钥轮换、跨协议关联和撤销时可能产生重复联系人、错误覆盖或错误成员归属。",
      "suggestedAction": "设计记录 protocol namespace、源身份、业务身份、证明来源、建立时间和撤销状态的 IdentityLink；不要再增加一个含糊 Peer 类型。",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
        { "source": "file", "path": "modules/core_mesh/include/mesh/domain/peer_identity.h", "summary": "core_mesh 实际定义 PeerPublicKey。" },
        { "source": "file", "path": "modules/core_chat/include/chat/domain/mesh_peer_directory.h", "summary": "core_chat 定义目录级 MeshPeerIdentity 和 MeshPeerRecord。" },
        { "source": "file", "path": "modules/core_chat/include/chat/domain/contact_types.h", "summary": "联系人/节点投影仍以 NodeId 和协议字段关联。" }
      ],
      "affectedAnchors": [{ "kind": "file", "id": "modules/core_chat/include/chat/domain/mesh_peer_directory.h", "path": "modules/core_chat/include/chat/domain/mesh_peer_directory.h" }],
      "traceIds": ["trace:identity-to-directory", "trace:directory-to-team"],
      "createdAt": "2026-07-22T00:00:00+08:00",
      "updatedAt": "2026-07-22T23:40:00+08:00"
    },
    {
      "schemaVersion": "praxis.reviewFinding.v1",
      "id": "finding:runtime-model-candidates-unclassified",
      "runId": "review:trail-mate-model-completeness:2026-07-22",
      "category": "architecture_boundaries",
      "severity": "P2",
      "status": "acknowledged",
      "title": "【候选待裁决】系统与媒体 Runtime 尚未完成 Model-or-Projection 分类",
      "summary": "Reticulum Call、Package Install、Firmware Update 与 Wi-Fi Lease 都有稳定状态语言，但 owner 主要位于 platform/ui runtime，尚未完成模型边界分类。",
      "whyItMatters": "直接忽略会漏掉潜在模型；直接登记又会把应用工作流、集成投影和资源调度误报成领域聚合。",
      "suggestedAction": "逐项裁决为 independent model、element of existing model、application workflow 或 integration projection，并记录 owner、不变量、端口与测试证据。",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
        { "source": "file", "path": "modules/core_sys/include/platform/ui/reticulum_call_runtime.h", "summary": "定义 Call State、RealtimePhase、Peer、Snapshot 和通话命令。" },
        { "source": "file", "path": "modules/core_sys/include/platform/ui/pack_repository_runtime.h", "summary": "定义 PackageRecord、InstalledPackageRecord 和 PackageInstallPhase。" },
        { "source": "file", "path": "modules/core_sys/include/platform/ui/firmware_update_runtime.h", "summary": "定义固件检查到重启的 Phase 与 Status。" },
        { "source": "file", "path": "modules/core_sys/include/platform/ui/wifi_access_runtime.h", "summary": "定义 Request、Lease、Decision、ExclusiveOwner 与抢占阶段。" }
      ],
      "affectedAnchors": [
        { "kind": "file", "id": "modules/core_sys/include/platform/ui/reticulum_call_runtime.h", "path": "modules/core_sys/include/platform/ui/reticulum_call_runtime.h" },
        { "kind": "file", "id": "modules/core_sys/include/platform/ui/wifi_access_runtime.h", "path": "modules/core_sys/include/platform/ui/wifi_access_runtime.h" }
      ],
      "traceIds": [],
      "createdAt": "2026-07-22T23:55:00+08:00",
      "updatedAt": "2026-07-22T23:55:00+08:00"
    },
    {
      "schemaVersion": "praxis.reviewFinding.v1",
      "id": "finding:team-domain-imports-chat-rxmeta",
      "runId": "review:trail-mate-model-completeness:2026-07-22",
      "category": "dependencies_coupling",
      "severity": "P2",
      "status": "acknowledged",
      "title": "【边界缺陷】Team 领域事件直接依赖 Chat 接收元数据",
      "summary": "TeamEventContext 直接包含 chat::RxMeta，使通信模块的数据结构成为 Team 领域事件契约。",
      "whyItMatters": "Chat 元数据演进会穿透 Team，也模糊团队授权、去重和审计真正需要哪些字段。",
      "suggestedAction": "在 core_team 定义最小 TeamReceiveContext，并由 adapter 从 chat::RxMeta 显式映射。",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
        { "source": "file", "path": "modules/core_team/include/team/domain/team_events.h", "summary": "TeamEventContext 直接持有 chat::RxMeta。" },
        { "source": "file", "path": "modules/core_chat/include/chat/domain/chat_types.h", "summary": "RxMeta 属于 chat 类型。" }
      ],
      "affectedAnchors": [{ "kind": "file", "id": "modules/core_team/include/team/domain/team_events.h", "path": "modules/core_team/include/team/domain/team_events.h" }],
      "traceIds": ["trace:conversation-to-team"],
      "createdAt": "2026-07-22T00:00:00+08:00",
      "updatedAt": "2026-07-22T23:40:00+08:00"
    },
    {
      "schemaVersion": "praxis.reviewFinding.v1",
      "id": "finding:contact-visibility-policy-disabled",
      "runId": "review:trail-mate-model-completeness:2026-07-22",
      "category": "code_quality_maintainability",
      "severity": "P2",
      "status": "acknowledged",
      "title": "【规则失效】附近节点可见性注释与实际查询行为不一致",
      "summary": "接口说明宣称 nearby 只包含六天内可见节点，但 isNodeVisible 忽略 last_seen 并无条件返回 true。",
      "whyItMatters": "过期节点不会按文档退出附近/忽略列表，状态文本、查询和未来清理策略也可能使用不同新鲜度语义。",
      "suggestedAction": "先确认 retention、联系人例外和无有效 epoch 时的行为，再用一个可测试 policy 统一查询、状态文本和清理。",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
        { "source": "file", "path": "modules/core_chat/include/chat/usecase/contact_service.h", "summary": "getNearby 注释声明六天可见期。" },
        { "source": "file", "path": "modules/core_chat/src/usecase/contact_service.cpp", "summary": "isNodeVisible 当前无条件返回 true；formatTimeStatus 另行计算六天 Offline。" }
      ],
      "affectedAnchors": [{ "kind": "file", "id": "modules/core_chat/src/usecase/contact_service.cpp", "path": "modules/core_chat/src/usecase/contact_service.cpp" }],
      "traceIds": [],
      "createdAt": "2026-07-22T23:40:00+08:00",
      "updatedAt": "2026-07-22T23:40:00+08:00"
    },
    {
      "schemaVersion": "praxis.reviewFinding.v1",
      "id": "finding:domain-models-existed-but-were-not-discovered",
      "runId": "review:trail-mate-model-completeness:2026-07-22",
      "category": "documentation_knowledge",
      "severity": "P1",
      "status": "resolved",
      "title": "【已修复的发现缺陷】固定三模板曾遮蔽真实模型",
      "summary": "旧 Registry 固定展示组织、软件和部署三个顶层容器；当前 Praxis 已支持作者声明的任意模型数量，Trail Mate Registry 已列出九个模型。",
      "whyItMatters": "历史问题用于解释为什么旧页面误导评审，但不能继续代替逐模型完整性审计。",
      "suggestedAction": "保持作者 Registry 优先和只读 inspection；以后为每个具体漏识别模型建立独立 finding。",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
        { "source": "file", "path": "docs/models/model-registry.json", "summary": "当前 Registry 包含九个模型。" },
        { "source": "file", "path": "docs/models/models-map.md", "summary": "作者文档解释模型与 projection 的区别。" }
      ],
      "affectedAnchors": [{ "kind": "file", "id": "docs/models/model-registry.json", "path": "docs/models/model-registry.json" }],
      "traceIds": [],
      "createdAt": "2026-07-22T00:00:00+08:00",
      "updatedAt": "2026-07-22T23:40:00+08:00"
    }
  ],
  "unresolvedFindingIds": [
    "finding:contact-peer-directory-not-discovered",
    "finding:route-navigation-domain-model-missing",
    "finding:configuration-aggregate-missing",
    "finding:team-membership-lifecycle-model-missing",
    "finding:peer-identity-ownership-split",
    "finding:runtime-model-candidates-unclassified",
    "finding:team-domain-imports-chat-rxmeta",
    "finding:contact-visibility-policy-disabled"
  ],
  "documents": {
    "rootDocPath": "docs/review/quality-review.md",
    "rootHtmlPath": "docs/review/quality-review.html",
    "categories": [],
    "issues": [
      { "findingId": "finding:contact-peer-directory-not-discovered", "category": "documentation_knowledge", "title": "【已有模型未发现】联系人、对端目录与本地信任未进入 Model Explorer", "severity": "P1", "status": "acknowledged", "docPath": "docs/review/issues/contact-peer-directory-not-discovered.md", "htmlPath": "docs/review/issues/contact-peer-directory-not-discovered.html" },
      { "findingId": "finding:route-navigation-domain-model-missing", "category": "architecture_boundaries", "title": "【设计未形成】路线导航规则仍由 UI Runtime 持有", "severity": "P1", "status": "acknowledged", "docPath": "docs/review/issues/route-navigation-domain-model-missing.md", "htmlPath": "docs/review/issues/route-navigation-domain-model-missing.html" },
      { "findingId": "finding:configuration-aggregate-missing", "category": "configuration_environment", "title": "【设计未形成】配置缺少版本、验证与原子提交 owner", "severity": "P1", "status": "acknowledged", "docPath": "docs/review/issues/configuration-aggregate-missing.md", "htmlPath": "docs/review/issues/configuration-aggregate-missing.html" },
      { "findingId": "finding:team-membership-lifecycle-model-missing", "category": "architecture_boundaries", "title": "【设计未形成】团队成员与团队生命周期没有领域 owner", "severity": "P1", "status": "acknowledged", "docPath": "docs/review/issues/team-membership-lifecycle-model-missing.md", "htmlPath": "docs/review/issues/team-membership-lifecycle-model-missing.html" },
      { "findingId": "finding:peer-identity-ownership-split", "category": "architecture_boundaries", "title": "【设计未形成】协议身份到业务联系人的 IdentityLink 缺失", "severity": "P1", "status": "acknowledged", "docPath": "docs/review/issues/peer-identity-ownership-split.md", "htmlPath": "docs/review/issues/peer-identity-ownership-split.html" },
      { "findingId": "finding:runtime-model-candidates-unclassified", "category": "architecture_boundaries", "title": "【候选待裁决】系统与媒体 Runtime 尚未完成 Model-or-Projection 分类", "severity": "P2", "status": "acknowledged", "docPath": "docs/review/issues/runtime-model-candidates-unclassified.md", "htmlPath": "docs/review/issues/runtime-model-candidates-unclassified.html" },
      { "findingId": "finding:team-domain-imports-chat-rxmeta", "category": "dependencies_coupling", "title": "【边界缺陷】Team 领域事件直接依赖 Chat 接收元数据", "severity": "P2", "status": "acknowledged", "docPath": "docs/review/issues/team-domain-imports-chat-rxmeta.md", "htmlPath": "docs/review/issues/team-domain-imports-chat-rxmeta.html" },
      { "findingId": "finding:contact-visibility-policy-disabled", "category": "code_quality_maintainability", "title": "【规则失效】附近节点可见性注释与实际查询行为不一致", "severity": "P2", "status": "acknowledged", "docPath": "docs/review/issues/contact-visibility-policy-disabled.md", "htmlPath": "docs/review/issues/contact-visibility-policy-disabled.html" },
      { "findingId": "finding:domain-models-existed-but-were-not-discovered", "category": "documentation_knowledge", "title": "【已修复的发现缺陷】固定三模板曾遮蔽真实模型", "severity": "P1", "status": "resolved", "docPath": "docs/review/issues/domain-models-existed-but-were-not-discovered.md", "htmlPath": "docs/review/issues/domain-models-existed-but-were-not-discovered.html" }
    ]
  }
}
<!-- praxis:quality-review:model:end -->
