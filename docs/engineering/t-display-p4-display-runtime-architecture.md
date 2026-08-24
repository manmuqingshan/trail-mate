# T-Display-P4 显示运行时最终架构

## 状态与适用范围

本文是 Trail Mate 在 ESP32-P4 T-Display-P4 TFT（HI8561）和 AMOLED（RM69A10）
目标上的显示运行时架构约束。它取代了历史上的应用层三 framebuffer
presenter，不允许再引入独立的 front/back swapchain、软件整帧旋转或 DPI
panel-owned LVGL buffer。

本文只定义显示数据路径。面板时序、MIPI-DSI lane/bit-rate、面板驱动、触控
I2C、SD、NVS、LoRa 和业务 UI 不属于本次架构替换的修改范围。

## 目标

显示在 UI 动画、SD I/O 和 NVS/Flash 提交同时发生时必须维持稳定。实现依赖
ESP32-P4 的 PPA，而不是在 CPU 与 PSRAM 上执行整帧旋转 copy。显示运行时必须：

- 让 LVGL 和 DPI driver 拥有清晰、互不重叠的 buffer ownership；
- 对 dirty area 使用 LVGL `PARTIAL` rendering；
- 使用 PPA 完成 90 度旋转；
- 在颜色传输完成时释放 LVGL flush；
- 让 DPI driver 负责传输，不把它当作应用层 swapchain；
- 在 TFT 与 AMOLED 两个 target 使用同一条显示内存模型。

## 最终数据流

```text
LVGL logical dirty area（横屏 UI）
        |
        v
独立 LVGL draw buffer（PSRAM | DMA）
        |
        v
esp_lvgl_port PPA rotation（blocking PPA operation）
        |
        v
PPA cache-line-aligned output buffer
        |
        v
esp_lcd_panel_draw_bitmap(rotated dirty area)
        |
        v
DPI / DMA2D / MIPI-DSI
        |
        v
on_color_trans_done -> lv_display_flush_ready()
```

`buffer_size` 可以覆盖完整物理屏幕，以容纳最大 dirty area；这不表示 full-frame
rendering。`full_refresh` 和 `direct_mode` 必须关闭，因此 LVGL 的 render mode 是
`PARTIAL`。

## 所有权与同步契约

| 资源 | 所有者 | 使用规则 |
| --- | --- | --- |
| LVGL draw buffer | `esp_lvgl_port` | 从 PSRAM + DMA capability 分配，作为 LVGL 的独立渲染输入。 |
| PPA output buffer | `esp_lvgl_port` PPA helper | 按 L2 cache line 对齐，仅保存当前旋转的 dirty area。 |
| DPI driver 内部资源 | ESP-IDF DPI driver | `num_fbs = 0`；应用层不得获取、保留或交换 panel framebuffer。 |
| LVGL flush 生命周期 | `on_color_trans_done` | 只有颜色传输完成才调用 `lv_display_flush_ready()`。 |

这意味着运行时没有 `front_buffer`、`pending_front_buffer`、`awaiting_refresh`、
refresh-boundary semaphore 或应用层 back-buffer 选择状态。

## 实现约束

### DPI panel

- `dpi_cfg.num_fbs` 必须为 `0`。
- `dpi_cfg.flags.use_dma2d` 必须保持 `true`。
- `lvgl_port_display_dsi_cfg_t.flags.avoid_tearing` 必须为 `false`。
- 不得调用 `esp_lcd_dpi_panel_get_frame_buffer()`。
- 不得以 `on_refresh_done` 驱动 LVGL flush completion。

### LVGL 与 PPA

- 两个 T-Display-P4 target 必须设置 `CONFIG_LVGL_PORT_ENABLE_PPA=y`。
- 显示必须由 `lvgl_port_add_disp_dsi()` 创建。
- 显示配置必须使用 `buff_spiram=true`、`buff_dma=true` 和 `sw_rotate=true`。
  在 ESP32-P4 且 PPA Kconfig 已启用时，`esp_lvgl_port` 的这个历史字段会调用
  PPA，不会调用 `lv_draw_sw_rotate()`。
- `double_buffer=false`、`full_refresh=false`、`direct_mode=false`。
- 逻辑方向通过 `lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90)` 建立。
- 运行时不得直接 include `driver/ppa.h` 或自行注册 PPA client；PPA client、output
  buffer、对齐、dirty-area coordinate transform 和 `draw_bitmap()` 调用由已锁定的
  `esp_lvgl_port` 实现统一管理。

## 明确禁止的旧架构

以下模式不得恢复为 fallback、feature flag 或条件编译分支：

- `P4DsiRotatingPresenter`；
- `dpi_cfg.num_fbs = 3`；
- panel-owned LVGL logical buffer；
- 自建 front/back/logical 三 framebuffer swapchain；
- `LV_DISPLAY_RENDER_MODE_FULL`；
- `lv_draw_sw_rotate()`；
- 每次 flush 的全屏 `esp_lcd_panel_draw_bitmap()`；
- `on_refresh_done -> lv_display_flush_ready()`。

## 触控与业务边界

触控继续将物理坐标绑定到同一个 `lv_display_t`。LVGL display rotation 负责从物理
portrait 面板到 logical landscape UI 的映射。因此本架构不需要改写触控控制器读数、
I2C 锁、键盘输入、SD 或 NVS 代码。

## 验收要求

每次修改 P4 显示运行时时，至少满足：

1. TFT 与 AMOLED target 都能配置和编译。
2. 架构 contract test 断言 PPA enabled、`num_fbs=0`、`avoid_tearing=false` 和
   `lvgl_port_add_disp_dsi()`，并断言旧 presenter 不存在。
3. 真机检查启动画面、四角触控、边缘触控、滑动、键盘焦点和 UI animation。
4. 真机在持续 UI animation、每 500 ms NVS commit 与持续 SD write 并发时无闪屏、
   无 PPA error、无 LVGL flush stall。
5. 所有自动化验证完成后，执行影响审计，确认改动只影响本架构及其验证文件。
