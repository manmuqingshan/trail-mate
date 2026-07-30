# Use Case：安装、更新或卸载扩展包

状态：**confirmed behavior / model classification pending**
业务边界：设备维护与数据所有权

## 用户目标

浏览可用和已安装的语言、字体、IME 等扩展，确认与当前固件/内存 profile 兼容后安装、更新或卸载，并在离线时仍能看到本机 installed index。

## 主场景

1. Extensions 先加载本地 `InstalledPackageRecord`，再在有 Wi-Fi lease 时抓取 remote catalog 并合并状态。
2. 用户打开详情；不兼容 firmware/memory 的 package 禁用安装动作并解释原因。
3. 安装 worker 下载 archive，持续报告 Installing/progress。
4. 下载完成校验 SHA-256，解析并限制 ZIP 路径，解压到目标 storage。
5. payload 可见后原子更新 installed index；更新时保留 previous record，成功后再移除旧 payload。
6. 卸载删除受控 payload 并更新 index；失败不能先从 UI 消失。

## 失败与恢复

网络中断、hash mismatch、ZIP 越界、存储空间不足、index 保存失败分别进入 Failed；临时文件/半解压内容必须清理或保持不可见。后台 worker 只能有一个 active install。

源码：`modules/core_sys/include/platform/ui/pack_repository_runtime.h`、`platform/esp/arduino_common/src/ui/runtime/pack_repository.cpp`、`modules/ui_shared/src/ui/screens/extensions/extensions_page_runtime.cpp`。

## 下钻

- [Activity](manage-extension-packages/activity.md)
- [Sequence](manage-extension-packages/sequences/sequence-manage-extension-packages.md)
- [State Machine](manage-extension-packages/state-machines/package-install.md)
