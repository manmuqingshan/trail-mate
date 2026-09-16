# GPX静态样例与兼容验证记录

状态：基础格式验证材料；不代表实际设备或第三方软件已实测兼容。

## 1. 官方schema来源

通过只读HTTPS获取官方 `https://www.topografix.com/GPX/1/1/gpx.xsd`，保存为[references/gpx-1.1.xsd](./references/gpx-1.1.xsd)，保留原文，不修改schema。

获取日期：2026-09-15。长度26665字节，SHA-256：

```text
9e4d1988b862edbe556305b130f8f6f1b29864fefd0dc02d5dab04ccdd1f34d6
```

网页工具曾因text/xml类型无法展示该URL；实际通过标准HTTP读取获得文件并可以进行本地XMLSchema验证。不能把网页工具错误解释为schema不存在。

## 2. 样例用途

| 样例 | 预期分类 | 覆盖 |
| --- | --- | --- |
| [plain-waypoint.gpx](./examples/plain-waypoint.gpx) | PlainWaypoint | 单点、中文、标准描述、cmt提示和地理坐标 |
| [multiple-waypoints.gpx](./examples/multiple-waypoints.gpx) | PlainWaypoint，各wpt独立 | 批量、负坐标、日期线附近、XML实体转义 |

这些样例故意没有伪造原作者签名；完整签名GPX及密码学静态向量仍需补齐。基础样例不含实际身份，因此不套用正式cache_id文件名或说明块中的虚假ID。

## 3. 验证方法与边界

使用lxml 6.0.2加载本地schema，解析器设置resolve_entities=false、load_dtd=false、no_network=true，再执行XMLSchema.assertValid。另读取每个wpt的lat/lon/name，核对点数量和中文/转义后的值。

这是对静态文档附件的校验，不是协议实现、GPX导入器或功能原型。没有运行固件、连接公共目录或测试真实寻宝软件。

## 4. 软件兼容矩阵仍需逐项验证

| 目标能力 | 必须验证的行为 | 当前状态 |
| --- | --- | --- |
| GPX1.1基础schema | 根、wpt、字段顺序、类型合法 | 本轮执行，结果写入下节 |
| 普通地图软件 | 导入两个点、坐标准确、中文和描述完整 | 未执行，不宣称支持具体产品 |
| 寻宝专用软件 | 原生识别难度/地形/容器/状态 | 未执行，扩展方案仍待选择 |
| 外部软件再保存 | 忽略/保留扩展、修改与编码变化可辨认 | 未执行 |
| 本协议签名GPX往返 | 原文/hash/签名与标准字段一致 | SIGNED-VECTOR已核对一个有效静态样本；非真实软件往返或完整负例集 |

后续必须写明软件名称、版本、导入入口、结果与字段损失，不能只用“支持GPX的软件”推断原生geocaching兼容。

### 4.1 明确的外部验收目标

| 目标 | 官方支持依据 | 执行步骤与通过条件 | 当前证据 |
| --- | --- | --- | --- |
| QGIS 3.44系列，记录实际安装patch版本 | [官方GPX数据源说明](https://doc.qgis.org/3.44/en/docs/pyqgis_developer_cookbook/loadlayer.html)明确可读取waypoints/routes/tracks | 从文件导入waypoints；单点=1、多点=2，WGS84坐标与源文件精度一致，中文name和desc可读；不要求读取签名扩展 | 仅官方能力依据，未运行软件 |
| c:geo，执行时记录实际版本 | [官方用户指南](https://manual.cgeo.org/en/start)列出GPX geocache导入功能 | 导入signed-geocache.gpx，检查能否在不伪造GC编号和账户ID的情况下识别名称、坐标、1.5难度、2.0地形、Small容器、Root提示及状态；缺字段逐项记录 | 仅官方能力依据，未证明独立Reticulum记录已被识别 |
| 任一支持本协议的独立实现 | 01/04/17/21契约 | 读取GPX→验签→重新保存→再次校验ID/hash/原文一致；修改坐标后必须标外部编辑 | 静态样例已验证，真实实现尚未开发 |

QGIS承担基础GPX可交换性，c:geo承担寻宝专用语义识别，不能用其中一个成功代替另一个。测试不从平台下载别人的缓存，也不要求用户购买账号；如软件仅接受平台编码而拒绝独立文件，就记录不兼容，不篡改ID蒙混。

还应分别另存副本再导回：记录扩展是否被保留。扩展被丢弃后只保证普通标点可用，不保证作者签名状态。真实软件测试是明确的兼容性验收任务；当前文档不将它们记为已通过。

## 5. 本轮验证结果

另已新增[完整签名GPX与静态记录向量](./examples/SIGNED-VECTOR.md)，实际通过基础GPX、Groundspeak子树、16字段字节核对与Ed25519往返验证。尚非全协议向量集或真实软件互操作结果。

结果须以实际校验输出为准；若校验失败先修样例，不用说明文字豁免格式错误。schema通过只证明17中的兼容等级A，不证明B/C/D。

本轮实际输出：

```text
plain-waypoint.gpx SCHEMA PASS; WAYPOINTS 1
multiple-waypoints.gpx SCHEMA PASS; WAYPOINTS 2
Coordinate precision and XML entity round-trip PASS
```

核对第二个点的负纬度与经度原精度，以及第一个点cmt解码后等于“树根 & 石块 <示意>”。该断言覆盖XML转义往返，没有执行真实软件导入。

另做XML字符边界检查：lxml拒绝U+FFFE/U+FFFF，而它们原先没有被协议控制字符规则明确排除；01/17已补约束。&<>以及含LF/TAB文本的序列化再读取保持原值。此检查修正了网络文本与GPX表示能力之间的缺口，未运行设备解析器。
