# ESP-NOW WiFi Mesh Network Tool

A distributed WiFi scanning and deauthentication tool using ESP-NOW protocol for communication between a master M5Cardputer device and multiple ESP32 slave devices.

## Overview

This project implements a master-slave architecture where:
- **Master (M5Cardputer)**: Provides user interface, coordinates operations, and displays results
- **Slaves (ESP32 devices)**: Perform WiFi scanning and deauthentication attacks on command

The system uses ESP-NOW for fast, reliable communication between devices without requiring a traditional WiFi network.

## Features

### Core Functionality
- **Distributed WiFi Scanning**: Coordinate scans across multiple ESP32 devices
- **Real-time Network Discovery**: Aggregate scan results from all slaves
- **Deauthentication Attacks**: Execute targeted or mass deauth operations
- **Live Device Management**: Monitor slave status, battery, and performance
- **Interactive Display**: Multi-mode interface showing APs, slaves, and statistics

### Advanced Features
- **Band-specific Scanning**: Target 2.4GHz or 5GHz networks
- **Hidden Network Detection**: Discover networks with hidden SSIDs
- **Mass Operations**: Scan or deauth multiple targets simultaneously
- **Real-time Statistics**: Track packets, uptime, and operation success rates
- **Automatic Slave Discovery**: Slaves automatically register with master

## Hardware Requirements

### Master Device
- **M5Cardputer** with ESP32-S3
- Built-in keyboard and display
- WiFi capability

### Slave Devices
- **ESP32-C5** or compatible ESP32 variants
- WiFi capability with monitor mode support
- Optional: Battery for portable operation

## Software Dependencies

### Master (M5Cardputer)
```cpp
#include <M5Cardputer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <ArduinoJson.h>
```

### Slave (ESP32)
```cpp
#include <esp_wifi.h>
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>
```

## Installation

### 1. Arduino IDE Setup
1. Install ESP32 board package in Arduino IDE
2. Install required libraries:
   - M5Cardputer (for master)
   - ArduinoJson
   - ESP32 WiFi libraries (included)

### 2. Flash Master Device
1. Open `MASTER/master_cardputer/master_cardputer.ino`
2. Select M5Cardputer board
3. Upload to M5Cardputer device

### 3. Flash Slave Devices
1. Open `SLAVE/slave32c5/slave32c5.ino`
2. Select appropriate ESP32 board
3. Upload to each ESP32 slave device

## Configuration

### Network Settings
```cpp
#define MESH_CHANNEL 1          // ESP-NOW communication channel
#define HEARTBEAT_INTERVAL 5000 // Slave heartbeat interval (ms)
#define SLAVE_TIMEOUT 15000     // Slave disconnect timeout (ms)
```

### Scan Settings
```cpp
#define SCAN_TIMEOUT_MS 15000   // Maximum scan duration
#define DEAUTH_DURATION_MS 30000 // Deauth attack duration
```

## Usage

### Basic Commands

| Command | Description |
|---------|-------------|
| `scan` | Full WiFi scan (all bands, all slaves) |
| `scan 2g` | Scan 2.4GHz networks only |
| `scan 5g` | Scan 5GHz networks only |
| `scan hidden` | Include hidden networks |
| `[number]` | Select AP by number |
| `deauth` | Deauth selected AP |
| `confirm deauth` | Confirm deauth operation |
| `stop` | Stop all active operations |
| `clear` | Clear scan results |
| `view` | Cycle display modes |
| `help` | Show command help |

### Navigation
- **`;`** - Scroll up in lists
- **`.`** - Scroll down in lists
- **`,`** or **`/`** - Switch display modes
- **Enter** - Execute command
- **Backspace** - Delete characters

### Display Modes
1. **APs Mode**: Shows discovered access points
2. **Slaves Mode**: Shows connected slave devices
3. **Stats Mode**: Shows system statistics

## Protocol Specification

### ESP-NOW Message Structure
```cpp
struct ESPNowMessage {
  MessageType type;        // Message type identifier
  uint8_t slaveMac[6];     // Sender MAC address
  uint32_t messageId;      // Unique message ID
  uint32_t timestamp;      // Message timestamp
  char payload[180];       // JSON payload data
};
```

### Message Types
- `MSG_SLAVE_REGISTER` - Slave registration with capabilities
- `MSG_SCAN_REQUEST` - Request WiFi scan
- `MSG_SCAN_RESPONSE` - Scan results from slave
- `MSG_DEAUTH_REQUEST` - Request deauth attack
- `MSG_DEAUTH_RESPONSE` - Deauth operation result
- `MSG_HEARTBEAT` - Periodic status update
- `MSG_ERROR` - Error reporting

### Communication Flow
1. **Slave Discovery**: Slaves automatically register with master
2. **Heartbeat**: Periodic status updates maintain connection
3. **Command Distribution**: Master broadcasts commands to slaves
4. **Result Aggregation**: Master collects and displays results

## Security Considerations

⚠️ **IMPORTANT LEGAL NOTICE** ⚠️

This tool is designed for:
- **Educational purposes**
- **Authorized penetration testing**
- **Network security research**
- **Testing your own networks**

### Legal Requirements
- Only use on networks you own or have explicit permission to test
- Comply with local laws and regulations
- Obtain proper authorization before testing
- Use responsibly and ethically

### Technical Security
- ESP-NOW communication is unencrypted by default
- Consider implementing encryption for sensitive deployments
- Monitor for unauthorized usage
- Implement access controls as needed

## Troubleshooting

### Common Issues

**Slaves not connecting:**
- Check ESP-NOW channel configuration
- Verify WiFi mode settings
- Ensure devices are within range

**Scan results missing:**
- Confirm slaves are responding to heartbeat
- Check scan timeout settings
- Verify JSON payload size limits

**Deauth not working:**
- Ensure target AP was discovered by a slave
- Check channel switching functionality
- Verify 802.11 frame injection capability

### Debug Information
- Monitor serial output for error messages
- Check statistics display for packet counts
- Verify slave status in slaves view

## Development

### Adding New Features
1. Define new message types in both master and slave
2. Implement message handlers
3. Update command processing
4. Test communication protocol

### Customization
- Modify display layouts in master code
- Adjust timing parameters for your environment
- Add new slave capabilities
- Implement additional security features

## Performance Metrics

### Typical Performance
- **Slave Discovery**: < 5 seconds
- **WiFi Scan**: 5-15 seconds (depending on environment)
- **Deauth Execution**: Near real-time
- **Communication Latency**: < 100ms
- **Range**: 50-200 meters (depending on environment)

### Scalability
- **Maximum Slaves**: 15 (configurable)
- **Concurrent Operations**: Limited by ESP-NOW bandwidth
- **Network Capacity**: 100+ APs per scan

## Contributing

1. Fork the repository
2. Create feature branch
3. Implement changes with proper testing
4. Submit pull request with detailed description

## License

This project is provided for educational and research purposes. Users are responsible for compliance with applicable laws and regulations.

## Changelog

### Version 1.0.0
- Initial ESP-NOW implementation
- Master-slave architecture
- Basic WiFi scanning and deauth
- Interactive M5Cardputer interface
- Multi-mode display system
- Automatic slave discovery
- Real-time statistics tracking

## Support

For issues, questions, or contributions:
1. Check troubleshooting section
2. Review existing issues
3. Create detailed bug reports
4. Provide system information and logs

---

**Remember**: Use this tool responsibly and only on networks you own or have explicit permission to test.
