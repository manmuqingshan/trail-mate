# 静态GPX格式样例

完整应用交互字节见[五种操作请求/响应](./FIVE-OPERATIONS.md)，与GPX签名向量共用同一公开测试对象。

新增[签名向量说明](./SIGNED-VECTOR.md)：公开测试身份的完整记录与GPX，包含Groundspeak字段和本应用原文签名扩展。下文“没有作者签名”仅指两个plain/multiple基础样例。

这些是设计样例，不是设备实现输出，不代表真实宝藏。没有作者签名扩展，因此按17分类为PlainWaypoint，不能显示为已验证公共对象，也不能直接作为发布事务测试向量。

- [单点GPX](./plain-waypoint.gpx)：普通软件忽略全部应用扩展也应能读取的坐标、中文名称、提示和描述。
- [多点GPX](./multiple-waypoints.gpx)：两个独立wpt，含中文、XML转义、负坐标和日期线附近小数。

两文件均按[保存的官方GPX1.1 schema](../references/gpx-1.1.xsd)验证。具体检查与证据见[兼容性验证记录](../18-GPX样例与兼容验证记录.md)。没有伪造cache_id或无效Base64签名来假装完整协议对象。
