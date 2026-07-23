# P2 · 【规则失效】附近节点可见性注释与实际查询行为不一致

状态：**acknowledged**  
类别：**代码质量 / 领域规则**

## 结论

`ContactService::getNearby` 的接口注释声明只返回六天内可见的非联系人节点，`formatTimeStatus` 也能在六天后显示 Offline；但实际过滤函数 `isNodeVisible(uint32_t)` 忽略参数并无条件返回 `true`。

因此过期节点不会按注释退出 nearby/ignored 查询。这不是文档可以自行补成的规则，必须由产品设计确认保留期限和联系人例外后再修改代码。

## 证据

- `modules/core_chat/include/chat/usecase/contact_service.h`：附近节点六天可见期的接口说明。
- `modules/core_chat/src/usecase/contact_service.cpp`：`isNodeVisible` 当前直接返回 true。
- 同文件 `formatTimeStatus`：六天后返回 Offline，但该函数没有成为查询过滤规则。

## 需要确认的规则

1. 非联系人节点的 retention/visibility 是否确实为六天？
2. ignored 节点是否同样过期？
3. 已保存联系人是否永久保留，即使无线观察过期？
4. 无有效 epoch 的设备如何计算新鲜度？
5. 清理是查询时隐藏，还是应从持久化目录删除？

## 验收

规则确认后，应由一个可测试的 visibility policy 同时驱动 nearby 查询、ignored 查询、状态文案和清理策略；不能继续让注释、显示文本与存储行为各自定义新鲜度。
