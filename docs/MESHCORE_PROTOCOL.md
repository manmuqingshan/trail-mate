# Introduction to MeshCore communication protocol

## 📋 Overview

MeshCore is a lightweight C++ LoRa Mesh network protocol library focusing on multi-hop packet routing. Compared with Meshtastic, MeshCore pays more attention to **simplicity** and **scalability**, and is suitable for customized development of embedded projects.

## 🔧 Protocol architecture

### Core features
- **Multi-hop routing**: Supports Flood and Direct routing modes
- **Custom binary format**: Does not use Google Protocol Buffers
- **Ed25519 encryption**: Complete end-to-end security support
- **Lightweight design**: suitable for resource-constrained embedded devices

### Architectural advantages
- **Compact package structure**: Minimize air transmission time
- **Flexible routing policy**: Support multiple routing algorithms
- **Strong encryption guarantee**: Full node authentication
- **Easy to extend**: Supports custom payload types

## 📦 Packet structure

### Header format (1 byte)

```
Bits:  7 6 5 4 3 2 1 0
 [Version: 2] [Type: 4] [Route: 2]
```

**Field description**:
- **Route type** (Bits 0-1): Determine how the packet is routed
- **Payload type** (Bits 2-5): Specify the data type of the payload
- **Version number** (Bits 6-7): Protocol version control

### Complete packet structure

| Fields | Size | Description |
|------|------|------|
| `header` | 1 byte | route type + payload type + version |
| `transport_codes` | 4 bytes* | Transport layer routing optimization code |
| `path_len` | 1 byte | path field length |
| `path` | up to 64 bytes | routing path data |
| `payload` | Maximum 184 bytes | Actual transmission data |

* Only exists for specific route types

## 🛣️ Route type

### 1. Flood routing (`ROUTE_TYPE_FLOOD = 0x01`)
- **Dynamic path construction**: Establish routing paths during transmission
- **Network discovery**: Automatically explore network topology
- **Applicable scenarios**: Network initialization, broadcast messages

### 2. Direct routing (`ROUTE_TYPE_DIRECT = 0x02`)
- **Default path**: Use the specified routing path
- **Efficient transmission**: Reduce routing overhead
- **Applicable scenarios**: point-to-point communication, known paths

### 3. Transport routing (extended mode)
- **Flood+Transport**: Flood routing with transfer encoding
- **Direct+Transport**: Direct routing with transfer encoding
- **Optimized transmission**: Improved reliability through coding

## 📄 Payload Types (16)

### Base Communication Types
| Value | Name | Description |
|----|------|------|
| `0x00` | `PAYLOAD_TYPE_REQ` | Request message (with hash and MAC) |
| `0x01` | `PAYLOAD_TYPE_RESPONSE` | Response message |
| `0x02` | `PAYLOAD_TYPE_TXT_MSG` | Plain text message |
| `0x03` | `PAYLOAD_TYPE_ACK` | Confirmation message |

### Advanced feature type
| Value | Name | Description |
|----|------|------|
| `0x04` | `PAYLOAD_TYPE_ADVERT` | Node advertisement |
| `0x05` | `PAYLOAD_TYPE_GRP_TXT` | Group text message (not verified) |
| `0x06` | `PAYLOAD_TYPE_GRP_DATA` | Group datagram (not verified) |
| `0x08` | `PAYLOAD_TYPE_PATH` | Return path |
| `0x09` | `PAYLOAD_TYPE_TRACE` | Traceroute |

### Extended type
| Value | Name | Description |
|----|------|------|
| `0x0A` | `PAYLOAD_TYPE_MULTIPART` | Multipart package |
| `0x0B` | `PAYLOAD_TYPE_CONTROL` | Control package |
| `0x0F` | `PAYLOAD_TYPE_RAW_CUSTOM` | Custom package |

## 🔐 Security mechanism

### Encryption architecture
- **Ed25519 Public Key**: 32-byte public key used for node identity
- **Message Authentication**: 2-byte MAC ensures data integrity
- **End-to-end encryption**: Supports encrypted transmission of sensitive data
- **Node Fingerprint**: 1-byte public key hash used for routing decisions

### Key management
- **Node public key**: exchanged during the first communication
- **Session key**: derived based on the node public key
- **Key cache**: local storage of the public key of a known node

## 💬 Communication process

### 1. Node discovery (Advertising)
```cpp
// Node advertising packet structure
struct NodeAdvert {
    uint8_t public_key[32];    // Ed25519 public key
    uint32_t timestamp;        // send timestamp
    uint8_t signature[64];     // digital signature
    uint8_t appdata[];         // optional application data
};
```

### 2. Text message communication
```cpp
// Text message payload structure
struct TextMessage {
    uint32_t timestamp;        // send timestamp
    uint8_t flags;            // message flags
    uint8_t text[];           // UTF-8-encoded text
};
```

### 3. Route tracing (Trace)
- **Collect SNR**: Collect signal quality data for each hop
- **Path record**: Record the complete routing path
- **Diagnostic information**: Provide network status analysis

## 🆚 Comparison with Meshtastic

| Features | MeshCore | Meshtastic |
|------|----------|------------|
| **Data format** | Custom binary | Google Protocol Buffers |
| **Packet size** | More compact | Relatively large |
| **Extensibility** | High (custom) | Medium (proto definition) |
| **Parsing Speed** | Fast | Medium |
| **Cross-platform** | C++ specific | Multi-language support|

## 🛠️ Key points of protocol implementation

### Packet encoding and decoding
```cpp
//Packet serialization
uint8_t Packet::writeTo(uint8_t dest[]) const {
    uint8_t i = 0;
    dest[i++] = header;
    if (hasTransportCodes()) {
        memcpy(&dest[i], &transport_codes[0], 2); i += 2;
        memcpy(&dest[i], &transport_codes[1], 2); i += 2;
    }
    dest[i++] = path_len;
    memcpy(&dest[i], path, path_len); i += path_len;
    memcpy(&dest[i], payload, payload_len); i += payload_len;
    return i;
}

//Packet deserialization
bool Packet::readFrom(const uint8_t src[], uint8_t len) {
 // Implement package parsing logic
}
```

### Routing decision
- **Flood mode**: Broadcast to all neighbor nodes
- **Direct mode**: Direct forwarding based on the path field
- **Transport mode**: Use encoding to optimize transmission

### Deduplication mechanism
- **Packet Hash**: SHA256 hash is used to uniquely identify the package
- **Time Window**: Timestamp-based deduplication check
- **Node filtering**: Avoid postback to the source node

## 🎯Usage scenarios

### 1. Offline communication network
- **Emergency communication**: Stay connected in disaster situations
- **Outdoor activities**: Hiking and camping team communication
- **Tactical applications**: Military and security scenarios

### 2. Sensor network
- **Environmental monitoring**: Remote sensor data collection
- **Industrial Internet of Things**: Equipment status monitoring
- **Agricultural applications**: Field equipment communication

### 3. Customized Mesh application
- **Specialized protocol**: Optimized for specific application scenarios
- **Lightweight implementation**: Resource-constrained equipment
- **Flexible expansion**: Supports custom load types

## 📊 Performance characteristics

### Network indicators
- **Maximum number of hops**: configurable, usually 3-5 hops
- **Packet size**: minimum 20 bytes, maximum 256 bytes
- **Transmission delay**: Depends on hop count and propagation time

### Reliability features
- **Auto-retry**: Automatic retransmission of failed packets
- **Path optimization**: Dynamically select the best route
- **Congestion control**: Avoid network overload

## 🔄 Protocol extensions

### Custom payload types
MeshCore supports expansion to more than 16 payload types. Developers can:
1. Define new payload formats
2. Implement corresponding encoding and decoding logic
3. Add processing function

### Routing algorithm extension
- **Customized routing strategy**: Implement new routing algorithm
- **QoS support**: Service quality assurance
- **Multipath routing**: Parallel transmission optimization

## 📚 Reference resources

- **Protocol specification**: `docs/packet_structure.md`
- **Payload format**: `docs/payloads.md`
- **Example code**: `examples/` Directory
- **API Documentation**: `include/MeshCore.h`

This protocol design is very suitable for embedded projects that require customized LoRa Mesh networks, maintaining simplicity while providing powerful function expansion capabilities.
