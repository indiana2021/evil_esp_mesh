#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <ArduinoJson.h> // For JSON serialization/deserialization
#include <Preferences.h> // For potential future non-volatile storage (not used in this version)

// =============================================================================
// CONFIGURATION AND CONSTANTS
// =============================================================================
#define MASTER_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF} // Broadcast address for initial contact
                                                       // Master's actual MAC will be learned on first contact
#define MESH_CHANNEL 1 // Must match the master's channel
#define JSON_BUFFER_SIZE 2048 // Must match or be larger than master's JSON_BUFFER_SIZE
#define HEARTBEAT_INTERVAL 5000 // How often the slave sends a heartbeat to the master
#define BUTTON_PIN 0 // Default button pin for many ESP32 boards (e.g., ESP32-CAM, some dev boards)
                     // Adjust this if your board has a different button connected.

// Define a placeholder for device type and firmware version.
// You can customize these strings for each slave device.
#define DEVICE_TYPE "Generic ESP32 Slave"
#define FIRMWARE_VERSION "1.0.0"

// Message types (must match master)
enum MessageType {
  MSG_SLAVE_REGISTER,
  MSG_SCAN_REQUEST,
  MSG_SCAN_RESPONSE,
  MSG_DEAUTH_REQUEST,
  MSG_DEAUTH_RESPONSE,
  MSG_STATUS_REQUEST,
  MSG_STATUS_RESPONSE,
  MSG_HEARTBEAT,
  MSG_STATISTICS,
  MSG_ERROR
};

// =============================================================================
// DATA STRUCTURES (must match master for common fields)
// =============================================================================
// Structure for ESP-NOW messages (must be identical to master's)
struct ESPNowMessage {
  MessageType type;
  uint8_t slaveMac[6]; // MAC of the sender (this slave)
  uint32_t messageId;
  uint32_t timestamp;
  char payload[180]; // Payload for JSON or other data
};

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================
uint8_t masterMac[6] = MASTER_MAC; // Master's MAC address (initially broadcast)
bool masterPeerAdded = false; // Flag to track if master peer has been added

unsigned long lastHeartbeatSent = 0;
uint32_t messageCounter = 0; // Counter for outgoing messages

// Internal state for ongoing tasks
bool scanInProgress = false;
unsigned long scanStartTime = 0;
bool deauthInProgress = false;
unsigned long deauthStartTime = 0;
String currentDeauthBssid = "";
uint32_t deauthPacketsSent = 0;
uint32_t deauthDuration = 0;
uint32_t deauthPacketsPerSecond = 0;

// Statistics for the slave itself
uint32_t totalPacketsReceived = 0;
uint32_t totalPacketsSent = 0;
uint32_t packetsProcessed = 0; // Messages processed by this slave

// =============================================================================
// FUNCTION PROTOTYPES
// =============================================================================
// Utility
String macToString(const uint8_t* mac);
void stringToMac(const String& macStr, uint8_t* mac);
float getBatteryVoltage(); // Placeholder for battery voltage reading
float getTemperature(); // Placeholder for temperature reading
uint8_t getBatteryLevel(); // Convert voltage to percentage (placeholder)

// ESP-NOW Callbacks
void onESPNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void onESPNowDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len);

// Message Sending
void sendMessageToMaster(MessageType type, const String& payload = "");
void sendRegistration();
void sendHeartbeat();
void sendScanResponse(const JsonArray& networks);
void sendDeauthResponse(const String& bssid, bool success, uint32_t packets);
void sendError(const String& errorMessage);

// Message Handlers (from Master)
void handleScanRequest(const ESPNowMessage& message);
void handleDeauthRequest(const ESPNowMessage& message);
void handleMasterHeartbeat(const ESPNowMessage& message); // Master's heartbeat to slave

// Task Processors
void processScanTask();
void processDeauthTask();

// Button handler
void handleButtonPress();

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
  uint32_t mac_part[6];
  sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x",
         &mac_part[0], &mac_part[1], &mac_part[2], &mac_part[3], &mac_part[4], &mac_part[5]);
  for(int i=0; i<6; ++i) {
    mac[i] = (uint8_t)mac_part[i];
  }
}

// Placeholder for battery voltage reading
// You might need to adjust this based on your specific battery and voltage divider
float getBatteryVoltage() {
  // Replace with actual ADC reading and voltage conversion
  // Example: return analogRead(34) / 4095.0 * 3.3 * 2; // Assuming 2:1 voltage divider
  return 3.7; // Dummy value for demonstration
}

// Placeholder for temperature reading
// You might need to add a sensor like DHT11/BMP280 or use internal ESP32 temperature sensor (less accurate)
float getTemperature() {
  // Example for internal ESP32 temperature (note: this is often not calibrated/accurate)
  // return temperatureRead();
  return 25.5; // Dummy value for demonstration
}

// Placeholder for battery level percentage conversion
uint8_t getBatteryLevel() {
  float voltage = getBatteryVoltage();
  // Simple linear mapping for demonstration (adjust as per your battery characteristics)
  if (voltage >= 4.2) return 100;
  if (voltage <= 3.2) return 0;
  return (uint8_t)((voltage - 3.2) / (4.2 - 3.2) * 100);
}

// =============================================================================
// ESP-NOW CALLBACKS
// =============================================================================
// Callback when ESP-NOW data is sent
void onESPNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.printf("Last Packet Send Status: %s to %s\n", status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail", macToString(mac_addr).c_str());
  if (status == ESP_NOW_SEND_SUCCESS) {
    totalPacketsSent++;
  }
}

// Callback when ESP-NOW data is received
void onESPNowDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  totalPacketsReceived++;
  if (len != sizeof(ESPNowMessage)) {
    Serial.println("Received malformed ESP-NOW message (wrong size).");
    return;
  }

  ESPNowMessage message;
  memcpy(&message, incomingData, sizeof(message));
  packetsProcessed++; // Increment count of messages this slave has processed

  // If this is the first message from the master (broadcast or direct), add it as a peer
  if (!masterPeerAdded && message.type == MSG_SLAVE_REGISTER) {
    // Slave registration is sent from master to slave to confirm registration
    // If master responded to our initial broadcast registration, we can learn its MAC.
    Serial.printf("Master responded, learning MAC: %s\n", macToString(mac).c_str());
    memcpy(masterMac, mac, 6);
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, masterMac, 6);
    peerInfo.channel = MESH_CHANNEL;
    peerInfo.encrypt = false; // Assuming no encryption for simplicity now
    if (!esp_now_is_peer_exist(masterMac)) {
      if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        masterPeerAdded = true;
        Serial.println("Master peer added.");
      } else {
        Serial.println("Failed to add master peer.");
      }
    } else {
      masterPeerAdded = true; // Peer already exists (e.g., re-running)
    }
  }


  // Handle message based on its type
  switch (message.type) {
    case MSG_SCAN_REQUEST:
      handleScanRequest(message);
      break;
    case MSG_DEAUTH_REQUEST:
      handleDeauthRequest(message);
      break;
    case MSG_HEARTBEAT:
      handleMasterHeartbeat(message); // Master's heartbeat to the slave
      break;
    case MSG_SLAVE_REGISTER: // Master responding to our registration
      // No specific action needed here beyond learning master's MAC if it's the first time
      Serial.println("Received master's registration acknowledgment.");
      break;
    case MSG_STATUS_REQUEST:
      // Master is requesting status, respond with a heartbeat-like message
      sendHeartbeat();
      Serial.println("Received status request from master, sent heartbeat.");
      break;
    case MSG_ERROR:
      sendError(String("Received error from master: ") + message.payload);
      break;
    default:
      Serial.printf("Received unknown message type: %d\n", message.type);
      sendError(String("Unknown message type received: ") + String(message.type));
      break;
  }
}

// =============================================================================
// MESSAGE SENDING FUNCTIONS (to Master)
// =============================================================================
void sendMessageToMaster(MessageType type, const String& payload) {
  if (!masterPeerAdded && type != MSG_SLAVE_REGISTER) {
    // If master peer not added, we can only send registration messages via broadcast
    Serial.println("Master peer not added. Can only send registration via broadcast.");
    // If sending other messages before master is known, they'll fail.
    return;
  }

  // Set the target MAC. If masterPeerAdded is false, this should be a broadcast MAC.
  // Otherwise, use the stored master's MAC.
  uint8_t targetMac[6];
  if (type == MSG_SLAVE_REGISTER && !masterPeerAdded) {
    memcpy(targetMac, (uint8_t[]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, 6);
  } else {
    memcpy(targetMac, masterMac, 6);
  }

  ESPNowMessage message;
  message.type = type;
  memcpy(message.slaveMac, WiFi.macAddress().c_str(), 6); // Set sender MAC (this slave)
  message.messageId = messageCounter++;
  message.timestamp = millis();
  strncpy(message.payload, payload.c_str(), sizeof(message.payload) - 1);
  message.payload[sizeof(message.payload) - 1] = '\0';

  esp_err_t result = esp_now_send(targetMac, (uint8_t*)&message, sizeof(message));
  if (result == ESP_OK) {
    Serial.printf("Sent message type %d to master. Payload: %s\n", type, payload.c_str());
  } else {
    Serial.printf("Error sending message type %d to master: %s\n", type, esp_err_to_name(result));
    // If we failed to send to the master, and it's not a broadcast, it might be disconnected
    if (result == ESP_ERR_ESPNOW_NOT_INIT) {
        Serial.println("ESP-NOW not initialized. Retrying init.");
        esp_now_init(); // Try to re-initialize
    }
  }
}

// Send initial registration message to the master (broadcast)
void sendRegistration() {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  doc["mac"] = WiFi.macAddress();
  doc["deviceType"] = DEVICE_TYPE;
  doc["firmwareVersion"] = FIRMWARE_VERSION;
  doc["supports5GHz"] = (bool)esp_wifi_get_max_tx_power() > 0; // Simple heuristic for 5GHz support
  doc["supportsBLE"] = true; // Assuming ESP32 generally supports BLE
  doc["batteryLevel"] = getBatteryLevel();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["temperature"] = getTemperature();

  String payload;
  serializeJson(doc, payload);
  Serial.println("Sending registration to master...");
  sendMessageToMaster(MSG_SLAVE_REGISTER, payload);
}

// Send periodic heartbeat message to the master
void sendHeartbeat() {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  doc["rssi"] = WiFi.RSSI(); // Master can get RSSI of slave's signal
  doc["batteryLevel"] = getBatteryLevel();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis();
  doc["packetsProcessed"] = packetsProcessed;
  doc["temperature"] = getTemperature();

  String payload;
  serializeJson(doc, payload);
  // Serial.println("Sending heartbeat to master...");
  sendMessageToMaster(MSG_HEARTBEAT, payload);
}

// Send WiFi scan results to the master
void sendScanResponse(const JsonArray& networks) {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  doc["mac"] = WiFi.macAddress();
  doc["networks"] = networks; // Attach the array of networks

  String payload;
  serializeJson(doc, payload);
  Serial.println("Sending scan response to master...");
  sendMessageToMaster(MSG_SCAN_RESPONSE, payload);
}

// Send deauthentication response to the master
void sendDeauthResponse(const String& bssid, bool success, uint32_t packets) {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  doc["bssid"] = bssid;
  doc["success"] = success;
  doc["packets"] = packets;

  String payload;
  serializeJson(doc, payload);
  Serial.printf("Sending deauth response to master (BSSID: %s, Success: %d, Packets: %d)\n", bssid.c_str(), success, packets);
  sendMessageToMaster(MSG_DEAUTH_RESPONSE, payload);
}

// Send an error message to the master
void sendError(const String& errorMessage) {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  doc["error"] = errorMessage;

  String payload;
  serializeJson(doc, payload);
  Serial.printf("Sending error to master: %s\n", errorMessage.c_str());
  sendMessageToMaster(MSG_ERROR, payload);
}

// =============================================================================
// MESSAGE HANDLERS (from Master)
// =============================================================================
void handleScanRequest(const ESPNowMessage& message) {
  if (scanInProgress) {
    Serial.println("Scan already in progress, ignoring new request.");
    sendError("Scan already in progress.");
    return;
  }

  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  DeserializationError error = deserializeJson(doc, message.payload);
  if (error) {
    Serial.printf("JSON deserialize error for scan request: %s\n", error.c_str());
    sendError(String("JSON error in scan request: ") + error.c_str());
    return;
  }

  bool both_bands = doc["both_bands"] | true; // Default to true if not specified
  bool include_hidden = doc["include_hidden"] | true; // Default to true
  // bool passive_scan = doc["passive_scan"] | false; // Not directly supported by WiFi.scanNetworks in this simple way
  uint32_t scan_time_ms = doc["scan_time"] | 3000; // Default scan time

  Serial.printf("Received scan request. Bands: %d, Hidden: %d, Scan Time: %dms\n",
                both_bands, include_hidden, scan_time_ms);

  // Clear previous scan results (if storing, not needed for direct response)
  // accessPoints.clear(); // If we were storing them locally

  scanInProgress = true;
  scanStartTime = millis();

  // Perform WiFi scan. Note: ESP32's WiFi.scanNetworks usually scans both 2.4 and 5GHz
  // if hardware supports it. 'passive' is not a direct argument for Arduino WiFi.
  WiFi.scanNetworks(true, include_hidden, false, (uint8_t)scan_time_ms / 1000); // async, hidden, passive (false), scan_time_s

  // In a real scenario, the scan might take time. We'll send results once scanComplete() is ready.
}

void handleDeauthRequest(const ESPNowMessage& message) {
  if (deauthInProgress) {
    Serial.println("Deauth already in progress, ignoring new request.");
    sendError("Deauth already in progress.");
    return;
  }

  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  DeserializationError error = deserializeJson(doc, message.payload);
  if (error) {
    Serial.printf("JSON deserialize error for deauth request: %s\n", error.c_str());
    sendError(String("JSON error in deauth request: ") + error.c_str());
    return;
  }

  currentDeauthBssid = doc["bssid"].as<String>();
  uint32_t channel = doc["channel"];
  deauthDuration = doc["duration"];
  deauthPacketsPerSecond = doc["packets_per_second"];

  Serial.printf("Received deauth request for BSSID: %s on channel %d for %dms at %dpps\n",
                currentDeauthBssid.c_str(), channel, deauthDuration, deauthPacketsPerSecond);

  // --- IMPORTANT NOTE ON DEAUTHENTICATION ---
  // The following deauthentication implementation is a SIMULATION.
  // Directly performing a deauthentication *attack* (sending forged deauth frames
  // to disconnect arbitrary clients from an AP) requires advanced, low-level
  // WiFi packet injection capabilities, typically involving ESP-IDF's promiscuous
  // mode and raw 802.11 frame construction (e.g., esp_wifi_80211_inject).
  // This is a complex topic and is beyond the scope of simple Arduino WiFi library
  // calls within this straightforward ESP-NOW framework.
  //
  // This simulation will:
  // 1. Acknowledge the request.
  // 2. Simulate sending packets over the specified duration.
  // 3. Send a MSG_DEAUTH_RESPONSE with a success status and simulated packet count.
  //
  // If actual deauthentication packet injection is required, the slave firmware
  // would need significant additional complexity, including custom WiFi driver hooks
  // and 802.11 frame manipulation.

  deauthInProgress = true;
  deauthStartTime = millis();
  deauthPacketsSent = 0;
  // It's good practice to switch the slave's WiFi channel to the target channel for deauth
  // However, constantly switching channels can disrupt ESP-NOW communication.
  // For this simplified version, we'll assume the master already ensures channel compatibility.
  // esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE); // Only if deauth requires it on this channel
}

void handleMasterHeartbeat(const ESPNowMessage& message) {
  // Master sends heartbeats to the slave to check slave's activity
  // In this slave firmware, simply receiving it means master is active.
  Serial.printf("Received heartbeat from master. Uptime: %s\n", message.payload);
}


// =============================================================================
// TASK PROCESSORS (Run in loop)
// =============================================================================
void processScanTask() {
  if (!scanInProgress) return;

  int scanResult = WiFi.scanComplete();
  if (scanResult >= 0) { // Scan is complete
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    JsonArray networksArray = doc.to<JsonArray>();

    for (int i = 0; i < scanResult; i++) {
      JsonObject network = networksArray.add<JsonObject>();
      network["ssid"] = WiFi.SSID(i);
      network["bssid"] = WiFi.BSSIDstr(i);
      network["rssi"] = WiFi.RSSI(i);
      network["channel"] = WiFi.channel(i);
      network["hidden"] = (WiFi.SSID(i).length() == 0); // SSID length 0 means hidden
      network["is5GHz"] = (WiFi.channel(i) > 14); // Channels > 14 typically 5GHz
      network["encryption"] = WiFi.encryptionType(i);
      network["beaconInterval"] = 100; // Placeholder, not directly available from WiFi.scanNetworks
      network["vendor"] = "Unknown"; // Placeholder
    }

    sendScanResponse(networksArray); // Send results to master
    WiFi.scanDelete(); // Clear scan results from WiFi library
    scanInProgress = false;
    Serial.println("Scan task completed.");
  } else if (scanResult == -1) {
    // Scan still in progress, do nothing
  } else if (scanResult == -2) {
    // Scan not started or internal error. Restart or report.
    Serial.println("WiFi scan error (-2), restarting scan...");
    // Not explicitly restarting, rely on master to re-request if needed.
    scanInProgress = false;
    sendError("WiFi scan internal error (-2).");
  }

  // Check for timeout if scan takes too long (optional, as master has its own timeout)
  if (millis() - scanStartTime > (scan_time_ms + 1000) && scanInProgress) { // Add a buffer
    Serial.println("Scan task timed out.");
    scanInProgress = false;
    sendError("Scan task timed out on slave.");
  }
}

void processDeauthTask() {
  if (!deauthInProgress) return;

  unsigned long currentTime = millis();
  if (currentTime - deauthStartTime < deauthDuration) {
    // Simulate sending packets
    uint32_t expectedPackets = (uint32_t)((currentTime - deauthStartTime) / 1000.0 * deauthPacketsPerSecond);
    if (expectedPackets > deauthPacketsSent) {
      // In a real scenario, you'd inject a deauth frame here
      // For simulation, just increment the counter.
      deauthPacketsSent = expectedPackets;
      // Serial.printf("Simulating deauth for %s. Sent %d packets.\n", currentDeauthBssid.c_str(), deauthPacketsSent);
    }
  } else {
    // Deauth duration expired
    Serial.printf("Deauth simulation for %s completed. Sent %d packets.\n", currentDeauthBssid.c_str(), deauthPacketsSent);
    sendDeauthResponse(currentDeauthBssid, true, deauthPacketsSent); // Report success
    deauthInProgress = false;
    currentDeauthBssid = "";
  }
}

// =============================================================================
// BUTTON HANDLER
// =============================================================================
void handleButtonPress() {
  static unsigned long lastButtonPressTime = 0;
  // Simple debounce
  if (digitalRead(BUTTON_PIN) == LOW && (millis() - lastButtonPressTime > 200)) { // Assuming pull-up, button connects to GND
    Serial.println("Button pressed! Sending manual heartbeat.");
    sendHeartbeat();
    lastButtonPressTime = millis();
  }
}

// =============================================================================
// ARDUINO SETUP AND LOOP FUNCTIONS
// =============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 ESP-NOW Slave initializing...");

  // Initialize button pin
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Set WiFi mode to station (required for ESP-NOW)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true); // Disconnect from any previous AP and clear credentials
  esp_wifi_set_channel(MESH_CHANNEL, WIFI_SECOND_CHAN_NONE); // Set fixed channel for ESP-NOW mesh

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  Serial.println("ESP-NOW initialized.");

  // Register ESP-NOW send and receive callbacks
  esp_now_register_send_cb(onESPNowDataSent);
  esp_now_register_recv_cb(onESPNowDataRecv);

  // Add the master as a peer (using broadcast initially)
  // This allows the slave to send its initial registration message.
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, masterMac, 6); // Use broadcast MAC initially
  peerInfo.channel = MESH_CHANNEL;
  peerInfo.encrypt = false; // No encryption
  if (!esp_now_is_peer_exist(masterMac)) {
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      Serial.println("Broadcast peer added for initial registration.");
      // masterPeerAdded will be true once master replies
    } else {
      Serial.println("Failed to add broadcast peer.");
    }
  }

  // Send initial registration message
  sendRegistration();
  lastHeartbeatSent = millis(); // Initialize heartbeat timer
}

void loop() {
  // Process tasks that might be ongoing (scan, deauth)
  processScanTask();
  processDeauthTask();

  // Periodically send heartbeats to the master
  if (millis() - lastHeartbeatSent > HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    lastHeartbeatSent = millis();
  }

  // Handle button input
  handleButtonPress();

  