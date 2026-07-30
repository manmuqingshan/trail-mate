# Use Case：接收并保存 SSTV 图像

状态：**confirmed receive-only behavior**
业务边界：通信、媒体与投递

## 用户目标

启动 SSTV 接收，观察音频电平、模式和解码进度，在完整图像到达后查看图像及其保存路径。

## 行为与规则

1. 设备 capability 和存储条件允许时显示 SSTV。
2. 用户启动 RX；runtime 从 Listening 进入 Receiving，持续报告 mode、audio level 和 progress。
3. frame ready 后投影图像；保存成功显示 `last_saved_path`。
4. 取消、解码失败或存储失败分别结束，不把不完整 frame 标成已保存。
5. 当前 UI 只有 RX，不应在 Design Explorer 中声称支持 SSTV 发送。

源码：`modules/ui_shared/src/ui/screens/sstv/sstv_page_runtime.cpp`、`modules/core_sys/include/platform/ui/sstv_runtime.h`。

## 下钻

- [Activity](receive-sstv-image/activity.md)
- [Sequence](receive-sstv-image/sequences/sequence-receive-sstv-image.md)
