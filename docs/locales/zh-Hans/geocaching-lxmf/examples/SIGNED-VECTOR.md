# 公开测试身份的签名记录与GPX向量

文件：[signed-record-vector.json](./signed-record-vector.json)、[signed-geocache.gpx](./signed-geocache.gpx)。仅用于设计核对，不是真实宝藏，不可使用这些公开测试私钥作为真实身份。

固定Ed25519种子为00..1f，固定X25519测试私钥字节为20..3f，nonce为40..4f。记录16项按01的CMP1规则手工构造为121字节，字段逻辑值、完整字节、公钥、ID/hash和签名均保存在JSON中。不是运行协议实现生成的产品输出。

cache_id为：

```text
8a77c881525c0de861415bd5b965ff9229eb912ef8b1d08cabc53351a75a2ed5
```

验证使用cryptography 44.0.2执行Ed25519签名与验证，lxml 6.0.2执行GPX1.1整体和Groundspeak1.0.1子树schema验证；另以独立的固定标签字节遍历逐项核对16项记录，并从GPX Base64读回原文和签名验证一致。

实际结果：

```text
record bytes 121
GPX and Groundspeak schema PASS; Ed25519 verify PASS
16 fields and exact fixture bytes PASS; GPX Base64/signature round-trip PASS
```

边界：没有运行通用CMP1解析器、LXMF路由或设备固件；没有第二种密码库的独立验证；没有证明任何第三方寻宝软件已识别此文件。该向量覆盖一个有效基础签名对象和GPX映射，不覆盖所有错误/版本/请求信封。后续开发者可按JSON复算；协议名称或签名域改变必须重生成并评审。

## 应用信封与负例补充

[envelope-and-negative-vectors.json](./envelope-and-negative-vectors.json)提供同一对象的publish请求216字节、精确get请求95字节和SignedCache190字节。请求ID均为00..0f，两个请求仅是独立场景，不能向同一来源实际同时复用该ID。它们是CUSTOM_DATA原字节，不包含LXMF外层认证和地址封装。

负例包含：修改原文但保留签名、非最短整数编码、尾随字节。第一例已执行密码学验证并确认InvalidSignature；后两例给出按CMP1应拒绝的精确字节和预期原因，尚未运行通用协议解析器。签名错误例更改公钥加密部分而保留签名公钥，明确验证签名覆盖整个记录。

实际检查：Mutated record rejected PASS；三个文件片段长度216/95/190字节匹配。此补充仍不是全操作与完整外层LXMF向量集。
