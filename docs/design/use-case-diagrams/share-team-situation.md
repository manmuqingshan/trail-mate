# Use Case：共享团队位置、航点、轨迹与聊天

状态：**confirmed behavior**
业务边界：团队协作

## 用户目标

在已经持有有效 Team keys 的前提下，把位置、航点、轨迹片段或聊天作为 Team 业务消息发送给成员，并在接收端更新相应地图/聊天投影。

## 主场景

1. 用户选择 share position/waypoint/track/chat，或 TeamTrackSampler 到达采样时机。
2. TeamService 校验 keys、payload 类型、目标/频道、是否请求业务响应。
3. Team codec 编码独立 payload，Team crypto 认证加密后交给活动 mesh transport。
4. 接收端先做 Team envelope/key 验证，再按 Position、Waypoint、Track、Chat、Status 分派事件。
5. EventBus/UI reducer 更新地图或聊天；delivery ACK 与 Team `want_response` 保持分离。

失败：无 keys、加密失败、transport unavailable、payload invalid 不产生“已共享”；接收验证失败不更新地图/聊天。

源码：`modules/core_team/src/usecase/team_service.cpp`、`modules/core_team/src/usecase/team_track_sampler.cpp`、`apps/esp32_lvgl/src/esp32_lvgl_idf_app_facade_runtime.cpp`。

## 下钻

- [Activity](share-team-situation/activity.md)
- [Sequence](share-team-situation/sequences/sequence-share-team-situation.md)
