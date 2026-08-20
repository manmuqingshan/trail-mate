# Trail Mate Model Integrity Review Queue

This queue divides the content not displayed by Model Explorer into two categories, which cannot be confused:

1. **Existing model not found**: The code already has stable language, state owner, rules and use cases, but the author Registry or tool missed it.
2. **The design has not yet been formed**: Relevant capabilities and rules have emerged, but a model with closed responsibilities has not been formed; it can only stay in the Review Queue and cannot generate an empty shell Model.

This document was written by Codex after directly reading the source code and GitNexus relationship; no Praxis Agent was called.

## Model integrity overview

| Type | Current quantity | Processing method |
| --- | ---: | --- |
| Models with source code evidence in Model Explorer | 9 | Display and allow drill-down |
| Existing models are not found | 1 | Contact directory has been added to the Registry; finding is reserved for UI acceptance |
| The design has not yet been formed | 4 | Navigation, Configuration, Team Membership, IdentityLink remain in the queue |
| Undecided model candidate group | 1 | Call, Package, Firmware, Wi-Fi Lease determine Model or Projection item by item |
| Related boundary/rule defects | 2 | Team RxMeta coupling, contact visibility rule invalid |
| Fixed historical discovery defects | 1 | Fixed three template issues marked as resolved |

## Current issues

| Severity | Finding | Model integrity type | Status |
| --- | --- | --- | --- |
| P1 | [[Existing model not found] Contact person, peer directory and local trust are not entered into the Model Explorer](issues/contact-peer-directory-not-discovered.md) | discovery gap | acknowledged |
| P1 | [[Design is not formed] Route navigation rules are still held by UI Runtime](issues/route-navigation-domain-model-missing.md) | domain design gap | acknowledged |
| P1 | [[Design not formed] Configuration missing version, verification and atomic commit owner](issues/configuration-aggregate-missing.md) | domain design gap | acknowledged |
| P1 | [[Design is not formed] Team members and team lifecycle have no domain owner](issues/team-membership-lifecycle-model-missing.md) | domain design gap | acknowledged |
| |
| P2 | [[Candidates pending] System and media Runtime have not completed Model-or-Projection classification] (issues/runtime-model-candidates-unclassified.md) | classification gap | acknowledged |
| P2 | [[Border defect] Team domain events directly depend on Chat Receive metadata](issues/team-domain-imports-chat-rxmeta.md) | boundary gap | acknowledged |
| P2 | [[Rule invalid] Nearby node visibility annotation is inconsistent with the actual query behavior](issues/contact-visibility-policy-disabled.md) | rule gap | acknowledged |
| P1 | [[Fixed discovery defect] Fixed three templates having obscured the real model](issues/domain-models-existed-but-were-not-discovered.md) | historical discovery gap | resolved |

## Judgment boundary

- "Missing document" does not automatically equal "missing model"; first check whether there is an owner and invariant in the code.
- `MeshPeerRecord` already exists, so the contact directory is a discovery flaw; `IdentityLink` does not exist, so it is a design flaw.
- `TeamService` has roster operations but no TeamMember lifecycle owner, so Team is still a candidate.
- Call, Package, Firmware, Wi-Fi Lease have stable state languages, but have not yet been determined as independent models, existing model elements, application workflows, or integrated projections.
- Design gaps can be converted into confirmed models in Model Explorer only after implementation, testing and documentation have been closed together.

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
 "title": "Model discovery and author documentation",
      "status": "has_unresolved",
 "summary": "The fixed three-template issue has been fixed; the contact directory still needs to complete UI acceptance as a specific missing identification model.",
 "evaluatorSummary": "Nine models have source code evidence; each discovered gap must be reported separately.",
      "findingIds": ["finding:contact-peer-directory-not-discovered", "finding:domain-models-existed-but-were-not-discovered"],
      "unresolvedFindingIds": ["finding:contact-peer-directory-not-discovered"],
      "docPath": "docs/review/categories/documentation_knowledge.md",
      "htmlPath": "docs/review/categories/documentation_knowledge.html"
    },
    {
      "category": "architecture_boundaries",
 "title": "Domain model that has not yet been formed",
      "status": "has_unresolved",
 "summary": "Navigation, Team Membership and IdentityLink have not formed a clear owner; the four groups of system/media runtime have not yet completed model classification.",
 "evaluatorSummary": "Three items are confirmed design gaps; Call, Package, Firmware, Wi-Fi Lease are candidates that must be adjudicated on a case-by-case basis.",
      "findingIds": ["finding:route-navigation-domain-model-missing", "finding:team-membership-lifecycle-model-missing", "finding:peer-identity-ownership-split", "finding:runtime-model-candidates-unclassified"],
      "unresolvedFindingIds": ["finding:route-navigation-domain-model-missing", "finding:team-membership-lifecycle-model-missing", "finding:peer-identity-ownership-split", "finding:runtime-model-candidates-unclassified"],
      "docPath": "docs/review/categories/architecture_boundaries.md",
      "htmlPath": "docs/review/categories/architecture_boundaries.html"
    },
    {
      "category": "configuration_environment",
 "title": "Configuration model",
      "status": "has_unresolved",
 "summary": "AppConfig, domain settings, default values, migrations and platform stores do not have unified submission boundaries.",
 "evaluatorSummary": "Configuration data exists, but version, verification and atomic submission models have not yet been formed.",
      "findingIds": ["finding:configuration-aggregate-missing"],
      "unresolvedFindingIds": ["finding:configuration-aggregate-missing"],
      "docPath": "docs/review/categories/configuration_environment.md",
      "htmlPath": "docs/review/categories/configuration_environment.html"
    },
    {
      "category": "dependencies_coupling",
 "title": "Cross-model dependency",
      "status": "has_unresolved",
 "summary": "Team domain events directly carry chat::RxMeta.",
 "evaluatorSummary": "Team requires its own minimum receiving context.",
      "findingIds": ["finding:team-domain-imports-chat-rxmeta"],
      "unresolvedFindingIds": ["finding:team-domain-imports-chat-rxmeta"],
      "docPath": "docs/review/categories/dependencies_coupling.md",
      "htmlPath": "docs/review/categories/dependencies_coupling.html"
    },
    {
      "category": "code_quality_maintainability",
 "title": "Rule consistency",
      "status": "has_unresolved",
 "summary": "The freshness description, status text and query behavior of nodes near the contact are inconsistent.",
 "evaluatorSummary": "isNodeVisible currently returns true unconditionally.",
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
 "title": "[Existing model not found] Contact, peer directory and local trust have not been entered into the Model Explorer",
 "summary": "MeshPeerRecord, IMeshPeerDirectory, ContactService, and independent stores already form a working directory/contact boundary, but the previous version of the Registry only listed eight models.",
 "whyItMatters": "Omitting this model would conflate contacts, protocol nodes, Mesh key identities, and session participants, and also hide the machine. State ownership of nickname, ignored and trust ",
 "suggestedAction": "Add contact-peer-directory to the Registry as the ninth source code support model, complete the elements, life cycle diagram and cross-model relationships, and close finding after UI acceptance.",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
 { "source": "file", "path": "modules/core_chat/include/chat/domain/mesh_peer_directory.h", "summary": "Defines directory identities, records, protocol facts, observations, and user flags." },
 { "source": "file", "path": "modules/core_chat/include/chat/ports/i_mesh_peer_directory.h", "summary": "Defines directory record/find/search/flags/remove/flush contracts." },
 { "source": "file", "path": "modules/core_chat/include/chat/usecase/contact_service.h", "summary": "Define contact, nearby nodes, ignored and manual verification use cases." },
 { "source": "file", "path": "docs/models/contact-peer-directory/model.md", "summary": "The author's model document has been added." }
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
 "title": "[Design has not yet been formed] Route navigation rules are still held by UI Runtime",
 "summary": "The project can calculate the nearest distance from the point to the route segment and update the yaw status with dual thresholds, but Route, NavigationSession, RouteProgress and DeviationPolicy have no core owner.",
 "whyItMatters": "Yaw and recovery are navigation business decisions; stay in the UI It will produce different semantics for goals, interfaces and tests, and it will not be possible to express route progress and reentry rules. ",
 "suggestedAction": "First form the design and testing of Route, NavigationSession, RouteProgress and DeviationPolicy, and then decide to put it into the core_gps navigation package or independent core_navigation. ",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
 { "source": "file", "path": "modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp", "summary": "distance_to_segment_m, nearest_route_distance_m and update_route_deviation_state are located in the UI runtime." },
 { "source": "file", "path": "modules/core_sys/include/platform/ui/route_storage.h", "summary": "Route storage is still located in the platform/ui contract." }
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
 "title": "[Design not formed] Configuration lacks versioning, verification and atomic commit owner",
 "summary": "AppConfig simultaneously carries communication, device, GPS, map, network, privacy, routing and APRS settings; there is no unified model for default values, compatibility conversions and platform persistence.",
 "whyItMatters": "Cross-field and capability constraints cannot be verified at a single boundary, and migration and partial write behavior are difficult to prove consistent.",
 "suggestedAction": "Designed with ConfigurationSnapshot of schema version, typed settings by realm, ConfigurationPolicy and atomic ConfigurationService. ",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
 { "source": "file", "path": "modules/core_sys/include/app/app_config.h", "summary": "AppConfig aggregates multiple realm settings and defaults." },
 { "source": "file", "path": "modules/core_chat/include/chat/domain/chat_types.h", "summary": "MeshConfig is co-located with the communication large type." },
 { "source": "file", "path": "platform/esp/arduino_common/src/app_config_store.cpp", "summary": "Persistence and compatibility logic lies in the platform implementation." }
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
 "title": "[Design is not formed] Team members and team life cycle have no domain owner",
 "summary": "TeamService has performed roster, kick, leader transfer, key distribution and PKI verification, but team/domain only has TeamId, TeamKeys and pairing status.",
 "whyItMatters": "vector<NodeId> Membership sources, roles, revisions, revocations, and cross-protocol stable identities cannot be expressed, and decentralized actions have no common invariants. ",
 "suggestedAction": "First define team lifecycle, membership, leader uniqueness, roster revision, and credential revocation rules, then build implementation and testing; Team remains candidate before completion. ",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
 { "source": "file", "path": "modules/core_team/include/team/domain/team_types.h", "summary": "Only TeamId, TeamKeys, matchmaking role and matchmaking status." },
 { "source": "file", "path": "modules/core_team/include/team/usecase/team_service.h", "summary": "Public roster, kick, leader transfer, key distribution and collaborative sending behavior. " },
 { "source": "file", "path": "modules/core_team/src/usecase/team_service.cpp", "summary": "rememberTeamMember and updateTeamMemberRoster only maintain NodeId vector." }
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
 "title": "[Design not formed] The IdentityLink from the protocol identity to the business contact is missing",
 "summary": "PeerPublicKey, MeshPeerRecord, Reticulum identity, Contact, and future TeamMember have emerged separately, but there is no explicit, revocable mapping model.",
 "whyItMatters": "Renaming, key rotation, cross-protocol association, and revocation may result in duplicate contacts, incorrect overrides, or incorrect member attribution.",
 "suggestedAction": "Design an IdentityLink that records the protocol namespace, source identity, business identity, certification source, establishment time, and revocation status; don't add another ambiguous Peer type.",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
 { "source": "file", "path": "modules/core_mesh/include/mesh/domain/peer_identity.h", "summary": "core_mesh actually defines PeerPublicKey." },
 { "source": "file", "path": "modules/core_chat/include/chat/domain/mesh_peer_directory.h", "summary": "core_chat defines directory-level MeshPeerIdentity and MeshPeerRecord." },
 { "source": "file", "path": "modules/core_chat/include/chat/domain/contact_types.h", "summary": "Contact/node projection is still associated with NodeId and protocol fields." }
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
 "title": "[Candidate pending decision] System and media runtime has not completed Model-or-Projection classification",
 "summary": "Reticulum Call, Package Install, Firmware Update and Wi-Fi Lease all have stable state languages, but the owner is mainly located in the platform/ui runtime, and the model boundary classification has not yet been completed. ",
 "whyItMatters": "Direct ignoring will miss potential models; direct registration will misreport application workflow, integration projection and resource scheduling into domain aggregations.",
 "suggestedAction": "Item-by-item decisions are independent model and element. of existing model, application workflow or integration projection, and record owner, invariants, ports and test evidence ",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
 { "source": "file", "path": "modules/core_sys/include/platform/ui/reticulum_call_runtime.h", "summary": "Define Call State, RealtimePhase, Peer, Snapshot and call commands." },
 { "source": "file", "path": "modules/core_sys/include/platform/ui/pack_repository_runtime.h", "summary": "Define PackageRecord, InstalledPackageRecord and PackageInstallPhase." },
 { "source": "file", "path": "modules/core_sys/include/platform/ui/firmware_update_runtime.h", "summary": "Define the Phase and Status when the firmware checks for restart." },
 { "source": "file", "path": "modules/core_sys/include/platform/ui/wifi_access_runtime.h", "summary": "Define the Request, Lease, Decision, ExclusiveOwner and preemption phases." }
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
 "title": "[Boundary defect] Team domain events directly rely on Chat to receive metadata",
 "summary": "TeamEventContext directly contains chat::RxMeta, making the data structure of the communication module become a Team domain event contract.",
 "whyItMatters": "Chat metadata evolution penetrates Team and obscures which fields are actually needed for team authorization, deduplication and auditing.",
 "suggestedAction": "Defining a minimal TeamReceiveContext in core_team and explicitly mapping it from chat::RxMeta by the adapter.",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
 { "source": "file", "path": "modules/core_team/include/team/domain/team_events.h", "summary": "TeamEventContext directly holds chat::RxMeta." },
 { "source": "file", "path": "modules/core_chat/include/chat/domain/chat_types.h", "summary": "RxMeta belongs to chat Type. " }
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
 "title": "[Rule invalidation] Nearby node visibility annotation is inconsistent with the actual query behavior",
 "summary": "The interface description declares that nearby only contains nodes visible within six days, but isNodeVisible ignores last_seen and returns true unconditionally.",
 "whyItMatters": "Expired nodes will not exit the nearby/ignored list by document, and status text, query, and future cleanup policies may also use different freshness semantics.",
 "suggestedAction": "First confirm retention, contact exceptions, and behavior without valid epochs, and then use a testable policy to unify query, status text, and cleanup.",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
 { "source": "file", "path": "modules/core_chat/include/chat/usecase/contact_service.h", "summary": "getNearby annotation declares a six-day visibility period." },
 { "source": "file", "path": "modules/core_chat/src/usecase/contact_service.cpp", "summary": "isNodeVisible Currently, it returns true unconditionally; formatTimeStatus calculates another six days Offline. " }
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
 "title": "[Fixed discovered defect] Fixed three templates once obscured the real model",
 "summary": "The old Registry fixedly displays three top-level containers for organization, software, and deployment; currently Praxis supports any number of models stated by the author, Trail Mate Registry. Nine models are listed.",
 "whyItMatters": "Historical questions are used to explain why old pages mislead reviewers, but cannot continue to replace model-by-model integrity audits.",
 "suggestedAction": "Keep author registry priority and read-only inspection; build independent findings for each specific missed identification model in the future.",
      "confidence": "high",
      "source": "hybrid",
      "knowledgeKind": "CANDIDATE",
      "evidence": [
 { "source": "file", "path": "docs/models/model-registry.json", "summary": "The current Registry contains nine models." },
 { "source": "file", "path": "docs/models/models-map.md", "summary": "The author's document explains the difference between model and projection." }
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
 { "findingId": "finding:contact-peer-directory-not-discovered", "category": "documentation_knowledge", "title": "[Existing model not found] Contacts, peer directories and local trusts are not entered into Model Explorer", "severity": "P1", "status": "acknowledged", "docPath": "docs/review/issues/contact-peer-directory-not-discovered.md", "htmlPath": "docs/review/issues/contact-peer-directory-not-discovered.html" },
 { "findingId": "finding:route-navigation-domain-model-missing", "category": "architecture_boundaries", "title": "[Design not yet formed] Route navigation rules are still held by UI Runtime", "severity": "P1", "status": "acknowledged", "docPath": "docs/review/issues/route-navigation-domain-model-missing.md", "htmlPath": "docs/review/issues/route-navigation-domain-model-missing.html" },
 { "findingId": "finding:configuration-aggregate-missing", "category": "configuration_environment", "title": "[Design is not formed] Configuration is missing version, verification and atomic commit owner", "severity": "P1", "status": "acknowledged", "docPath": "docs/review/issues/configuration-aggregate-missing.md", "htmlPath": "docs/review/issues/configuration-aggregate-missing.html" },
 { "findingId": "finding:team-membership-lifecycle-model-missing", "category": "architecture_boundaries", "title": "[Design is not formed] Team members and team lifecycle have no field owner", "severity": "P1", "status": "acknowledged", "docPath": "docs/review/issues/team-membership-lifecycle-model-missing.md", "htmlPath": "docs/review/issues/team-membership-lifecycle-model-missing.html" },
 { "findingId": "finding:peer-identity-ownership-split", "category": "architecture_boundaries", "title": "[Design is not formed] The IdentityLink from the agreement identity to the business contact is missing", "severity": "P1", "status": "acknowledged", "docPath": "docs/review/issues/peer-identity-ownership-split.md", "htmlPath": "docs/review/issues/peer-identity-ownership-split.html" },
 { "findingId": "finding:runtime-model-candidates-unclassified", "category": "architecture_boundaries", "title": "[Candidates to be determined] System and Media Runtime has not completed Model-or-Projection classification", "severity": "P2", "status": "acknowledged", "docPath": "docs/review/issues/runtime-model-candidates-unclassified.md", "htmlPath": "docs/review/issues/runtime-model-candidates-unclassified.html" },
 { "findingId": "finding:team-domain-imports-chat-rxmeta", "category": "dependencies_coupling", "title": "[Boundary defect] Team domain events directly rely on Chat to receive metadata", "severity": "P2", "status": "acknowledged", "docPath": "docs/review/issues/team-domain-imports-chat-rxmeta.md", "htmlPath": "docs/review/issues/team-domain-imports-chat-rxmeta.html" },
 { "findingId": "finding:contact-visibility-policy-disabled", "category": "code_quality_maintainability", "title": "[Rule invalidation] Nearby node visibility comments are inconsistent with actual query behavior", "severity": "P2", "status": "acknowledged", "docPath": "docs/review/issues/contact-visibility-policy-disabled.md", "htmlPath": "docs/review/issues/contact-visibility-policy-disabled.html" },

    ]
  }
}
<!-- praxis:quality-review:model:end -->
