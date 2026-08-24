# Meshtastic Protobuf usage

This document describes the actual Meshtastic protobuf message types used in the trail-mate project, as well as unused types.

## Project Overview

trail-mate is a Meshtastic-compatible chat application based on LilyGo T-LoRa Pager, which implements basic text messaging, node discovery and routing functions.

## Used Protobuf types

### Core data structure

#### `meshtastic_Data`
- **Purpose**: Core data message carrier
- **Usage location**: `mt_codec_pb.cpp`, `mt_adapter.cpp`
- **Field**: portnum, payload, want_response, bitfield
- **Function**: carry all application layer data (Text messages, user information, routing messages, etc.)

#### `meshtastic_User`
- **Use**: User/node information
- **Usage location**: `mt_codec_pb.cpp`, `mt_adapter.cpp`
- **Fields**: id, short_name, long_name, macaddr, public_key, hw_model, role
- **Function**: Node discovery and user information broadcast

### Port number enumeration

#### Used ports
- `meshtastic_PortNum_TEXT_MESSAGE_APP` - Text message ([portnums.pb.h](../modules/core_chat/generated/meshtastic/portnums.pb.h))
- `meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP` - Compressed text message ([portnums.pb.h](../modules/core_chat/generated/meshtastic/portnums.pb.h))
- `meshtastic_PortNum_NODEINFO_APP` - Node information broadcast ([portnums.pb.h](../modules/core_chat/generated/meshtastic/portnums.pb.h))
- `meshtastic_PortNum_ROUTING_APP` - Routing confirmation message ([portnums.pb.h](../modules/core_chat/generated/meshtastic/portnums.pb.h))

### Data structure

#### `meshtastic_Data` - Core data message carrier
- **Purpose**: Carrying all application layer data (Text messages, user information, routing messages, etc.)
- **Usage location**: `mt_codec_pb.cpp`, `mt_adapter.cpp`
- **File**: [mesh.pb.h](../modules/core_chat/generated/meshtastic/mesh.pb.h)
- **Field**: portnum, payload, want_response, bitfield

#### `meshtastic_User` - User information
- **Purpose**: User/node information and public key
- **Usage location**: `mt_codec_pb.cpp`, `mt_adapter.cpp`
- **File**: [mesh.pb.h](../modules/core_chat/generated/meshtastic/mesh.pb.h)
- **Fields**: id, short_name, long_name, macaddr, public_key, hw_model, role

### Routing related

#### `meshtastic_Routing` - routing message
- **Purpose**: Routing message and error handling
- **Usage location**: `mt_adapter.cpp` (sendRoutingAck function)
- **File**: [mesh.pb.h](../modules/core_chat/generated/meshtastic/mesh.pb.h)
- **Field**: error_reason
- **Function**: Messaging acknowledgment and error reporting

#### `meshtastic_Routing_Error` - Routing error enumeration
- `meshtastic_Routing_Error_NONE` - No error acknowledgment
- **File**: [mesh.pb.h](../modules/core_chat/generated/meshtastic/mesh.pb.h)

### Hardware model

#### `meshtastic_HardwareModel` - Hardware model enumeration
- `meshtastic_HardwareModel_T_LORA_PAGER` - LilyGo T-LoRa Pager
- **File**: [mesh.pb.h](../modules/core_chat/generated/meshtastic/mesh.pb.h)

### Device configuration

#### `meshtastic_Config_DeviceConfig_Role` - Device role
- `meshtastic_Config_DeviceConfig_Role_CLIENT` - Client role
- **File**: [config.pb.h](../modules/core_chat/generated/meshtastic/config.pb.h)

## Detailed explanation of unused Protobuf types

### Position and navigation related

#### `meshtastic_PortNum_POSITION_APP` - GPS location information
- **Purpose**: Broadcast the GPS location coordinates of the device
- **File**: [portnums.pb.h](../modules/core_chat/generated/meshtastic/portnums.pb.h)
- **Usage scenarios**:
 - Real-time location sharing and tracking
 - Map app showing node locations
 - Location reporting in emergencies
 - Navigation and rendezvous point coordination

#### `meshtastic_PortNum_WAYPOINT_APP` - Waypoint information
- **Use**: Define and manage navigation waypoints
- **File**: [portnums.pb.h](../modules/core_chat/generated/meshtastic/portnums.pb.h)
- **Usage scenarios**:
 - Outdoor adventure route planning
 - Search and rescue operations
 - Shared points of interest (POIs)
 - Team coordination of navigation goals

#### `meshtastic_Position` - Position data structure
- **Purpose**: Store location information such as GPS coordinates, precision, timestamps
- **File**: [mesh.pb.h](../modules/core_chat/generated/meshtastic/mesh.pb.h)
- **Fields**: latitude, longitude, altitude, precision, timestamp, etc.

#### `meshtastic_Waypoint` - Waypoint data structure
- **Purpose**: Define navigation point information
- **File**: [mesh.pb.h](../modules/core_chat/generated/meshtastic/mesh.pb.h)
- **Fields**: name, description, position, icon, etc.

### Telemetry and sensor data

#### `meshtastic_PortNum_TELEMETRY_APP` - Telemetry data
- **Purpose**: Collect and broadcast device sensor data
- **File**: [portnums.pb.h](../modules/core_chat/generated/meshtastic/portnums.pb.h)
- **Usage scenarios**:
 - Environmental monitoring (temperature, humidity, atmospheric pressure)
 - Equipment status monitoring (battery power, signal strength)
 - Meteorological data collection
 - Agricultural and industrial IoT applications

#### `meshtastic_Telemetry` - Sensor data structure
- **Purpose**: Encapsulate various sensor measurement values
- **File**: [telemetry.pb.h](../modules/core_chat/generated/meshtastic/telemetry.pb.h)
- **Supported sensor types**:
 - Environmental sensor (Temperature, humidity, air pressure)
 - Motion sensor (accelerometer, gyroscope)
 - Power management (battery voltage, current)
 - Radio Performance (SNR, RSSI)

### Remote Hardware Control

#### `meshtastic_PortNum_REMOTE_HARDWARE_APP` - Remote Hardware Control
- **Purpose**: Remotely control hardware devices connected to Meshtastic nodes
- **File**: [portnums.pb.h](../modules/core_chat/generated/meshtastic/portnums.pb.h)
- **Usage scenarios**:
 - Remote switch control (relay, LED)
 - Sensor data reading
 - Actuator control (motor, valve)
 - IoT device integration

#### `meshtastic_RemoteHardware` - Hardware control message
- **Use**: Define hardware control commands and responses
- **File**: [remote_hardware.pb.h](../modules/core_chat/generated/meshtastic/remote_hardware.pb.h)
- **Supported operations**: GPIO reading and writing, ADC reading, PWM control, etc.

### Network diagnosis

#### `meshtastic_PortNum_TRACEROUTE_APP` - Traceroute
- **Purpose**: Diagnose network paths and performance
- **File**: [portnums.pb.h](../modules/core_chat/generated/meshtastic/portnums.pb.h)
- **Usage scenarios**:
 - Network troubleshooting
 - Route optimization analysis
 - Network topology discovery
 - Connection quality assessment

#### `meshtastic_TraceRoute` - Route tracing data
- **Purpose**: Record the node path that the message passes through
- **File**: [mesh.pb.h](../modules/core_chat/generated/meshtastic/mesh.pb.h)
- **Field**: hop list, delay time, signal quality, etc.

### Advanced message type

#### `meshtastic_PortNum_STORE_FORWARD_APP` - Store and forward
- **Purpose**: Store messages when the node is offline and forward them after they come online
- **File**: [portnums.pb.h](../modules/core_chat/generated/meshtastic/portnums.pb.h)
- **Protobuf**: [storeforward.pb.h](../modules/core_chat/generated/meshtastic/storeforward.pb.h)
- **Usage scenarios**:
 - Networks with intermittent connections
 - Offline communication with mobile nodes
 - Delay-tolerant network applications

#### `meshtastic_PortNum_RANGE_TEST_APP` - Distance test
- **Purpose**: Test communication distance and signal quality between nodes
- **File**: [portnums.pb.h](../modules/core_chat/generated/meshtastic/portnums.pb.h)
- **Encoding format**: Simple text message (no dedicated protobuf required)
- **Usage scenarios**:
 - Network coverage evaluation
 - Antenna performance test
 - Communication distance optimization

#### `meshtastic_PortNum_PAXCOUNTER_APP` - Crowd counter
- **Purpose**: Use WiFi sniffing to count the number of nearby devices
- **File**: [portnums.pb.h](../modules/core_chat/generated/meshtastic/portnums.pb.h)
- **Protobuf**: [paxcount.pb.h](../modules/core_chat/generated/meshtastic/paxcount.pb.h)
- **Usage scenarios**:
 - Crowd density monitoring
 - Traffic flow analysis
 - Commercial place passenger flow statistics

#### `meshtastic_PortNum_ATAK_PLUGIN` - ATAK plug-in
- **Purpose**: Integrate with Android Team Awareness Kit (ATAK)
- **File**: [portnums.pb.h](../modules/core_chat/generated/meshtastic/portnums.pb.h)
- **Protobuf**: [atak.pb.h](../modules/core_chat/generated/meshtastic/atak.pb.h)
- **Usage scenarios**:
 - Military and Emergency Response Communications
 - Professional Team Coordination
 - GIS Data Integration

#### `meshtastic_PortNum_AUDIO_APP` - Audio Messaging
- **Purpose**: Transmit codec2 encoded audio data
- **File**: [portnums.pb.h](../modules/core_chat/generated/meshtastic/portnums.pb.h)
- **Encoding format**: codec2 audio frame (non protobuf, direct binary)
- **Usage scenarios**:
 - Voice communication (2.4GHz bandwidth only)
 - Audio data transmission
- **Related configuration**: `meshtastic_ModuleConfig_AudioConfig` ([module_config.pb.h](../modules/core_chat/generated/meshtastic/module_config.pb.h))

### Configuration and Settings

#### `meshtastic_Config` - Complete configuration structure
- **Purpose**: Complete configuration management of the device
- **File**: [config.pb.h](../modules/core_chat/generated/meshtastic/config.pb.h)
- **Configuration types included**: LoRa, WiFi, Bluetooth, display and other module configurations

#### `meshtastic_Config_LoRaConfig` - LoRa configuration
- **Purpose**: LoRa radio parameter configuration
- **File**: [config.pb.h](../modules/core_chat/generated/meshtastic/config.pb.h)
- **Configuration items**: Frequency, modulation parameters, transmit power, regional settings, etc.

#### `meshtastic_ModuleConfig` - Module configuration
- **Purpose**: Configuration of each functional module (MQTT, telemetry, location, etc.)
- **File**: [module_config.pb.h](../modules/core_chat/generated/meshtastic/module_config.pb.h)
- **Usage scenario**: Remotely configure the device through the network

#### `meshtastic_Channel` - Channel settings
- **Purpose**: Define communication channel and encryption settings
- **File**: [channel.pb.h](../modules/core_chat/generated/meshtastic/channel.pb.h)
- **Fields**: Channel name, PSK key, settings, etc.

### Advanced network functions

#### `meshtastic_NodeInfo` - Extended node information
- **Purpose**: More detailed node information than User
- **File**: [mesh.pb.h](../modules/core_chat/generated/meshtastic/mesh.pb.h)
- **Extra fields**: device status, neighbor nodes, routing tables, etc.

#### `meshtastic_DeviceState` - Device status
- **Purpose**: Report device operating status
- **File**: [mesh.pb.h](../modules/core_chat/generated/meshtastic/mesh.pb.h)
- **Fields**: battery status, memory usage, temperature, etc.

#### `meshtastic_MqttClientProxyMessage` - MQTT proxy message
- **Purpose**: Connect to Internet services through MQTT gateway
- **File**: [mqtt.pb.h](../modules/core_chat/generated/meshtastic/mqtt.pb.h)
- **Usage scenarios**: Cloud service integration, remote monitoring, data forwarding

#### `meshtastic_AdminMessage` - Management message
- **Purpose**: Remote device management and configuration
- **File**: [admin.pb.h](../modules/core_chat/generated/meshtastic/admin.pb.h)
- **Supported operations**: Restart, configuration update, firmware upgrade, etc.

## Architecture description

### Application layer abstraction
The project uses domain types (`ChatMessage`, `NodeInfo`, `MeshConfig`, etc.) instead of directly using Meshtastic protobuf, which provides better abstraction and maintainability.

### Protocol Compatibility
Conversion between domain types and Meshtastic protobuf is implemented through `mt_codec_pb.cpp` to ensure compatibility with Meshtastic networks.

### Limitation of functional scope
The current implementation focuses on basic chat functions and deliberately does not implement the following advanced functions:
-GPS location sharing
-Sensor data collection
- Remote hardware control
- Complex network management functions

This makes the code more concise, takes up less memory, and is suitable for embedded devices with limited resources.

## Extension suggestions

If you need to add more features in the future, you can consider implementing:

1. **Location service**: use `meshtastic_Position` and related ports
2. **Telemetry**: Use `meshtastic_Telemetry` to collect sensor data
3. **Remote control**: Use `meshtastic_RemoteHardware` to implement hardware control
4. **Network Diagnosis**: Use `meshtastic_TraceRoute` for network analysis

## Summary

The trail-mate project uses only a core subset of Meshtastic protobuf and focuses on providing reliable text communication capabilities. This design choice ensures compatibility with Meshtastic networks while maintaining code simplicity and device lightweight.