# Use Case：检查 GNSS 卫星、定位与时间权威

状态：**confirmed**
业务边界：地图、定位与现场感知

## 用户目标

判断 GNSS 是未启用、启动中、无卫星、无 fix、定位质量不足还是正常；查看天空图和卫星表，并理解系统时间是否来自有效 GNSS 输入。

## 行为与规则

1. 页面申请 GNSS power lease，读取 `GnssStatus`、卫星快照和 `GpsDiagnosticsSnapshot`。
2. 天空图按 constellation、azimuth、elevation、SNR 与 used-in-fix 显示卫星；表格与顶部摘要来自同一快照。
3. empty state 区分 receiver starting、disabled、no data 和 no fix。
4. LocationService 只接受有效 revision/fix；时间更新由 time authority port 执行。
5. RMC 时间必须通过日期、epoch 合理性和同步策略校验；无效 NMEA 不改系统时钟。

源码：`modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp`、`modules/core_gps/src/usecase/location_service.cpp`、`platform/esp/idf_common/src/gps_runtime.cpp`。

## 下钻

- [Activity](inspect-gnss-health/activity.md)
- [Sequence](inspect-gnss-health/sequences/sequence-inspect-gnss-health.md)
