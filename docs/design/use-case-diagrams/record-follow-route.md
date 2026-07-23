# Use Case：记录并可靠保存现场轨迹

状态：**confirmed**
业务边界：地图、定位与现场感知

## 用户目标

开始一次轨迹记录，在定位暂时丢失、存储短暂忙碌或设备资源受限时仍保持可解释行为，停止后得到完整关闭的本地轨迹文件。

## 主场景

1. 用户 Start；`TrackStateMachine` 校验当前为 Idle 并创建 track/session 与存储 writer。
2. LocationService 提供有效、通过跳变过滤的 fix revision。
3. 采样策略决定是否形成 `TrackPoint`；点进入固定容量 buffer，不在 GNSS 热路径直接阻塞写盘。
4. storage worker 按批次写入格式化轨迹并更新计数、距离和最后写入结果。
5. Stop 停止接受新点，排空 buffer，flush/close，最后提交 Idle/Stopped 结果。

## 失败与恢复

- 无 fix：形成采样间断，不伪造两点直线。
- buffer full：执行明确 drop/backpressure 策略并暴露计数。
- 写入失败：进入 Error/停止继续无界积累；已写数据保持可恢复。
- USB 接管 SD 前必须先停止/flush 轨迹 owner。

源码：`modules/core_gps/include/gps/domain/track_state_machine.h`、`modules/core_gps/src/usecase/track_recorder.cpp`、`modules/core_gps/src/usecase/track_storage_worker.cpp`。

## 下钻

- [Activity](record-follow-route/activity.md)
- [Sequence](record-follow-route/sequences/sequence-record-follow-route.md)
- [State Machine](record-follow-route/state-machines/track-recording-session.md)
