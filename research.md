Building a Mesh-Based Wi-Fi Deauth Tool with M5Stack Cardputer and ESP32-C5 Nodes
Adhering to M5Stack Coding Standards and Best Practices
When coding for the M5Stack Cardputer (ESP32-S3 based device), it’s important to follow official M5Stack coding standards and use their provided libraries. The Cardputer is supported by the M5Unified framework and a specific M5Cardputer library, which handle the device’s display, keyboard, etc. In practice, the Cardputer library is a thin layer over M5Unified (it “just remaps definitions” from M5Unified
reddit.com
). Therefore:
Use M5Unified/M5Cardputer for initialization and hardware control: For example, initialize the device with M5Cardputer.begin() and regularly call M5Cardputer.update() in the loop to process system events (keyboard input, etc.)
docs.m5stack.com
docs.m5stack.com
. This adheres to M5Stack’s standard practice of using their API rather than low-level hacks.
Follow M5Stack’s code structure: Typically, include the necessary headers (M5Cardputer.h, M5GFX.h, etc.), then in setup() call M5.begin()/M5Cardputer.begin() with appropriate config, and in loop() call the update function
docs.m5stack.com
. Using the official start-up sequence ensures all peripherals (display, keyboard, speaker, etc.) are properly initialized according to M5Stack guidelines.
Keep code modular and organized: Even if the project is “all in one script,” organize functionality into functions (e.g., scanNetworks(), deauthAttack(), handleCommand()) and possibly separate sections for master vs slave logic. This modularity follows best practices and makes future expansion easier (a stated goal). Maintain a clear separation of concerns (UI handling vs network scanning vs attack logic) to improve readability and maintainability.
Coding style and readability: Use clear naming and add comments for complex sections (like constructing raw packets). Keep loops tight and avoid long blocking delays in the main loop, so the UI stays responsive (especially important for reading keyboard input without lag). Utilize non-blocking techniques or short scheduling delays when possible (for example, using TaskScheduler with PainlessMesh, see below).
M5Stack UI standards: Utilize M5GFX (graphic driver in M5Unified) for drawing on the TFT. The Cardputer’s 1.14″ screen is small (240×135), so design text UI carefully (small font sizes, short messages). The M5Unified library supports drawing text, rectangles, etc., and even scrollable text areas using M5Canvas. The official Cardputer examples demonstrate how to draw a bordered text area and scroll text
docs.m5stack.com
docs.m5stack.com
 – you can use a similar approach for the on-screen terminal interface.
By following these practices and using the M5Unified-based libraries, you ensure compliance with M5Stack’s coding standards. (For instance, M5Stack’s own PlatformIO config shows that M5Unified is a required dependency for the Cardputer project
docs.m5stack.com
, underlining that we should program via that framework.)
Required Libraries and Dependencies
This project spans multiple areas (device UI, Wi-Fi scanning, mesh networking, raw packet injection), so several libraries are needed. Make sure to install these libraries (and their dependencies) in Arduino before coding:
M5Stack Cardputer Library (M5Cardputer) and M5Unified: Provides drivers for the Cardputer’s hardware (display, keyboard, etc.). Install M5Unified (the unified driver for all M5Stack devices) and the Cardputer add-on if available
docs.m5stack.com
. This gives you M5Cardputer.begin(), M5Cardputer.Display, M5Cardputer.Keyboard objects, etc. The Cardputer library’s examples (e.g. “Cardputer Keyboard Example”) show how to use these APIs
docs.m5stack.com
docs.m5stack.com
, which you can emulate for your interface.
ESP32 Wi-Fi Library (WiFi.h): This comes with the ESP32 Arduino core. It will be used for scanning networks and possibly for setting Wi-Fi modes (Station/AP). Functions like WiFi.scanNetworks() (for scanning APs) and WiFi.mode(), WiFi.disconnect() will be utilized. Ensure you have an up-to-date ESP32 core, especially since we want ESP32-C5 support (the ESP32-C5 is a new dual-band Wi-Fi 6 chip
espressif.com
, supported from recent ESP-IDF/Arduino core versions).
Mesh Networking Library (PainlessMesh): We need a way for the master Cardputer and slave nodes to communicate in a mesh topology (no single AP dependency). The painlessMesh library is a good choice – it allows ESP32 nodes to form a true ad-hoc mesh (self-organizing, no central router needed)
randomnerdtutorials.com
. PainlessMesh is Arduino-compatible and works on ESP32/ESP8266. Install painlessMesh (version 1.4.5 or latest) via the library manager. Note: PainlessMesh has dependencies that must be installed as well; if the Arduino IDE doesn’t auto-prompt, manually install ArduinoJson, TaskScheduler, and AsyncTCP (ESP32 version)
randomnerdtutorials.com
. These are needed for the mesh library to function.
ESP-NOW or alternative (optional): If not using full mesh, an alternative is Espressif’s ESP-NOW protocol or a simpler star network. However, given the requirement of “slaves create a mesh,” painlessMesh is the intended solution. (ESP-NOW could be considered for efficiency as it’s connectionless, but it would complicate multi-hop communication. So we’ll proceed with PainlessMesh as the primary library.)
Raw Packet Injection (esp_wifi_80211_tx): There isn’t a high-level Arduino library solely for sending deauth frames, but we will use the Espressif Wi-Fi API provided by the ESP32 Arduino core. By including <esp_wifi.h>, we gain access to esp_wifi_80211_tx(), the function that sends raw IEEE 802.11 frames. This function was introduced by Espressif to allow low-level frame injection (it supersedes older hacks)
github.com
. We will use it to transmit deauthentication packets. No separate install is needed since it’s part of the ESP32 WiFi libraries, but you should be aware of how to declare and use it (the function signature can be found in the ESP-IDF docs and Jeija’s sample code
github.com
).
Other supportive libraries: If needed, you might use additional ones such as ESP32NetBIOS or WiFiProv – but for our scope, the above are the main ones. Also, ensure you use the official M5Stack coding style, which means using these official libs instead of low-level direct code unless necessary (for instance, prefer M5Cardputer.Keyboard API for key input rather than manually scanning matrix, and use esp_wifi_80211_tx only because it’s necessary for deauth frames – an exception to high-level coding, as allowed “when necessary”).
Installation: Use the Arduino Library Manager or PlatformIO to get these libraries. For example, install “M5Unified” and “M5Cardputer”, then “painlessMesh” (which should also fetch ArduinoJson, TaskScheduler, AsyncTCP). If installing manually, note the versions; e.g., painlessMesh 1.4.5 is known to work with ESP32 core 2.0.x (if using core v3.x and above, check the library’s documentation for compatibility or updates). After installing, include the relevant headers in your sketch (#include <M5Cardputer.h>, #include <WiFi.h>, #include <painlessMesh.h>, etc.).
Cardputer UI: Terminal Menu and Command Input
The master device (M5Stack Cardputer) will provide a text-based user interface on its built-in screen and keyboard. The goal is to have it boot into a terminal-like menu showing status and accepting commands (“scan”, “select”, “deauth”, etc.). Here are the technical details and recommendations for implementing this:
Display Layout: Use the small TFT to show a list of options or status info at the top, and a command prompt at the bottom. A common approach (demonstrated in M5Stack’s Cardputer examples) is to divide the screen: e.g. draw a rectangle as a border for the text area, leaving a reserved line at the bottom for input
docs.m5stack.com
docs.m5stack.com
. You can create an M5Canvas (off-screen buffer) for the scrolling text region above, and use canvas.println() to print output lines (so older messages scroll up)
docs.m5stack.com
. Then use canvas.pushSprite() to draw that buffer to the display. Meanwhile, draw the prompt and user-typed text directly to the bottom line of the display using M5Cardputer.Display.drawString()
docs.m5stack.com
 (as in the Cardputer keyboard example). This approach yields a basic terminal: the top area scrolls with output, the bottom shows the current input being typed.
Showing Connected Slaves Count: One of the status lines on the UI should display the number of connected slave devices. Since the master and slaves form a mesh, the master can query the mesh library for connected nodes. For instance, PainlessMesh provides mesh.getNodeList() which returns a list of node IDs
painlessmesh.gitlab.io
 – the length of this list (excluding the master itself) is the count of slaves currently in the mesh. You could update this count periodically or whenever connections change (using mesh callbacks, see below). Display it at a prominent place (e.g., top of the screen: “Connected nodes: X”). This gives immediate feedback when slaves join/leave.
Menu Options and Usage Instructions: Print out a brief menu or usage guide on startup (for example: “Commands: scan – scan for APs, select <ID> – choose target AP, deauth – attack selected AP. Type a command and press Enter.”). Since the screen is small, keep instructions concise. You might only show available commands when no operation is running. After initial boot, the output area can be repurposed to show scan results, logs, etc.
Keyboard Input Handling: The Cardputer’s 56-key keyboard is accessed via M5Cardputer.Keyboard. Poll it in each loop iteration. The official example uses M5Cardputer.update(); if (M5Cardputer.Keyboard.isChange()) { … } to detect any key state changes
docs.m5stack.com
. When keys are pressed, you can retrieve them as characters. The example’s Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState(); yields a structure where status.word contains the typed characters, and boolean flags like status.enter or status.del indicate special keys
docs.m5stack.com
. Use this logic to build the input string: append normal characters to a buffer (e.g., a String commandBuffer), handle backspace (status.del) by removing the last char, and handle status.enter by finalizing the command. When Enter is pressed, you take the commandBuffer (after the prompt symbol) as the user’s command. Then reset commandBuffer to empty (or to a prompt prefix like "> ") for the next input.
Parsing and Executing Commands: Implement a simple parser in the master’s code. Likely commands:
"scan" – initiate Wi-Fi scan via slaves.
"select <num>" – choose one of the discovered APs as the target (by index or ID from the scan list).
"deauth" – begin deauthentication attack on the selected target.
Possibly "exit" or "stop" could be implemented to halt an ongoing attack or scanning, if needed. (Not explicitly requested, but could be useful for completeness.)
After the user hits enter, compare the command string. You might do if(command == "scan") { ... } else if(command.startsWith("select")) { ... } else if(command == "deauth") { ... }. For “select”, parse the number (e.g., using sscanf or String.toInt() after splitting the string). Provide feedback in the terminal: e.g., after “scan” is entered, print “Scanning for access points...” and after completion, list the APs (with an index for each). When “select X” is entered, confirm selection like “Target AP set to: <SSID> (MAC)”. When “deauth” starts, print something like “Sending deauth frames to [target SSID]...” so the user knows the action is in progress. All these messages should be added to the canvas (scrollable area) via println() and then pushed to display.
Responsiveness: The UI should remain responsive to key presses even while operations run. This may require using non-blocking techniques or at least updating the display during longer tasks. For instance, a Wi-Fi scan can take a few seconds if scanning many channels. You might want to perform scanning in a separate task or at least call M5Cardputer.update() periodically during the scan loop to not miss key presses. Alternatively, you can indicate a busy state (e.g., “Scanning... please wait”) and ignore new commands until done. Given the complexity, it’s acceptable to make “scan” and “deauth” blocking operations as long as you inform the user (since these are short-lived actions in a test scenario). Just be mindful to not freeze the device for too long without feeding the watch-dog or processing events.
By carefully designing the UI layout and input handling as above, the Cardputer will “boot to a terminal of options” and allow the user to input commands at a prompt, exactly as requested.
Mesh Network Design for Master–Slave Communication
To coordinate multiple nodes (master Cardputer and slave ESP32 devices), we set up a mesh network among them. The mesh ensures all nodes can communicate “openly” and cooperate without a central Wi-Fi AP. Key points for implementing the mesh:
Mesh Initialization: Using the PainlessMesh library, initialize the mesh in both master and slaves in their setup(). All nodes should use the same mesh network ID (SSID), password, and port. For example, you might call mesh.init("MeshNetwork", "meshPassword", 5555, WIFI_AP_STA); on each node
painlessmesh.gitlab.io
painlessmesh.gitlab.io
. This causes each device to start in AP+Station mode on a chosen channel (you can specify a channel, or let it default). They will automatically find each other and form a network – no extra user code needed to connect, as “any system of 1 or more nodes will self-organize into [a] fully functional mesh”
randomnerdtutorials.com
.
Master–Slave Roles: Though the mesh is peer-to-peer, we logically designate the Cardputer as the master (it takes user commands and issues orders), and all other nodes as slaves (they perform scanning/deauth and report back). You can implement this by programming different behavior depending on device: e.g., have a compile-time flag or simply separate sketches (“Cardputer_Master.ino” vs “SlaveNode.ino”). The master will listen for messages from slaves and send control messages, while slaves mostly listen for commands from master and send results back.
Communication Protocol: Decide on a simple messaging format for mesh communication. PainlessMesh allows sending strings or binary to either a specific node or broadcast to all. A straightforward approach: use broadcast for commands (master broadcasts “scan” or “deauth <targetMAC>”), and have each slave respond directly to the master with their data. Because PainlessMesh can deliver messages to specific nodes by ID, you could have slaves send results to master’s nodeID. If you know the master’s nodeId (e.g., you can set master’s nodeId to a known value or obtain it via mesh.getNodeId() and perhaps share it), you might prefer directed messages to reduce chatter. Otherwise, broadcasting results and filtering them at the master is another approach (less efficient but simpler). For example, master could send a JSON string like {"cmd":"scan"} to all. Slaves receive it (via mesh.onReceive() callback) and trigger their scanning. After scanning, each slave could send back a JSON like {"result":"scan","APs":[ {...}, {...} ]} containing the list of APs it found (with fields like SSID, BSSID, channel, RSSI). The master collects these, merges them into one comprehensive list of APs. If using JSON, note that PainlessMesh uses ArduinoJson, so you can construct/parse JSON easily. Keep messages concise to avoid exceeding buffer sizes.
Connection Callbacks: Utilize PainlessMesh callbacks to manage node count and possibly to identify the master. For instance, in the master code you can do:
cpp
Copy
Edit
mesh.onNewConnection([](size_t nodeId){ 
    // A new node joined – update count
    size_t n = mesh.getNodeList().size();
    Serial.printf("Node %u joined, total=%d\n", nodeId, n);
    // (Also update display for node count)
});
mesh.onDroppedConnection([](size_t nodeId){
    // A node left – update count
});
These callbacks (or onChangedConnections) let you keep the “# of connected slaves” display updated in real time
randomnerdtutorials.com
. They also could be used to, say, auto-start a scan when a new node joins (if you desire). In slaves, you might not need special connection handling except perhaps to detect when the master is present. If needed, you could program the master to broadcast a “heartbeat” or an ID message so slaves know who is master. However, since our master will always initiate commands, this may be unnecessary.
Efficiency and Coordination: The mesh allows parallel operations, which is great for efficiency. All slaves can act simultaneously on a command. For scanning, this is very useful – you can divide the work to speed it up. For example, if you have N slaves, assign each a subset of Wi-Fi channels to scan (one scans channels 1-5, another 6-11, etc., and for ESP32-C5 that supports 5 GHz, maybe assign it the 5 GHz band). That way, the overall scan completes faster than if one device scanned everything sequentially. The master can coordinate this by instructing, e.g., “slave 1: scan ch1-6; slave 2: ch7-13; slave3 (C5): ch36-165” etc. Or simpler: let all slaves perform a full scan concurrently and merge results (with duplicates filtered). Concurrent scanning may congest the mesh (since nodes leave the channel briefly), so a more structured split can be better.
Mesh vs Wi-Fi Scanning Conflict: Be mindful that when a node performs an active Wi-Fi scan, it will cycle through channels, potentially disconnecting from the mesh (which operates on a fixed channel) momentarily. With PainlessMesh, if a node temporarily drops out to scan, it should rejoin automatically once the scan is done (the mesh is self-healing
randomnerdtutorials.com
randomnerdtutorials.com
). But during the scan, that node can’t communicate. A strategy: when master broadcasts a “scan” command, all slaves might pause mesh communications, do their scan, then come back and send results. The mesh will reform quickly. Ensure you allow some buffer time for all to finish scanning and re-establish connectivity before master expects to receive results. You can implement a small delay or handshake: e.g., slaves could wait for a second after scanning before sending data (giving them time to rejoin the mesh). Master could also wait and collect responses for a few seconds before concluding the scan. This way, “all nodes work together for efficiency” without permanent disconnections. It’s a tricky part of logic – essentially orchestrating a brief mesh suspension for scanning – but it will result in a much faster and thorough scan across 2.4/5 GHz.
Hidden SSIDs: Ensure that the scanning method on slaves is set to include hidden networks. By default, some scan functions ignore hidden SSIDs unless specified. In the ESP-IDF, the wifi_scan_config_t has a show_hidden parameter (0 or 1)
docs.espressif.com
. The Arduino WiFi.scanNetworks() typically will include hidden networks (they appear with empty SSID strings). If you find hidden APs aren’t being reported, you might need to use the lower-level scan API with show_hidden=true. It’s important for your use-case because you explicitly want to detect hidden SSIDs. The slaves should report hidden APs as well, perhaps denoting them as “<hidden>” or simply by their BSSID if no name is available.
ESP32-C5 Dual-Band Utilization: One of your requirements is to leverage 5 GHz on capable slaves (ESP32-C5). The code should detect or know which slaves support 5 GHz. You might hard-code that certain node IDs are ESP32-C5 devices and assign them the 5 GHz scan task. The ESP32-C5 can scan channels in the 5 GHz band (36,40,... up to 165)
espressif.com
, unlike ESP32-S3 or others which are 2.4 GHz only
reddit.com
. Use the appropriate API – if using Arduino WiFi.scanNetworks(), ensure you have an Arduino core that supports dual-band scanning (this may have recent updates; historically Arduino core didn’t scan 5 GHz by default
github.com
, but newer versions or IDF calls can). If needed, you can use esp_wifi_set_channel() to manually set the radio to a 5 GHz channel and then do a scan on that channel. However, an easier method (if supported) is specifying the scan to cover all bands. Keep in mind scanning the full 5 GHz range (which has many channels) will take longer (~9-10 seconds to scan all channels as noted by users
esp32.com
). You can limit to certain channels if you know your local AP uses only lower 5 GHz channels, for example.
Data Aggregation: After a scan, the master needs to aggregate results from all slaves. Use unique identifiers for each AP (BSSID MAC address is ideal) to merge the lists without duplicates. You might store a structure with fields: SSID, BSSID, channel, RSSI. For hidden SSIDs, SSID will be empty – you can list them as “[Hidden]” with their MAC to differentiate. Once merged, present the list to the user in the terminal UI, enumerating them (1,2,3,...). Keep the list in memory because the user will issue a “select” next. Map the selection number to the target AP’s details (MAC, channel).
Implementing Wi-Fi Scanning on Slaves (2.4 GHz and 5 GHz)
Each slave device will handle the actual scanning of nearby Wi-Fi access points. Technical considerations for coding this:
Wi-Fi Mode for Scanning: Ensure the slave’s Wi-Fi is in station mode and not connected to any network when scanning. Typically, you’d call WiFi.mode(WIFI_STA); WiFi.disconnect(); before scanning (as shown in official examples)
randomnerdtutorials.com
randomnerdtutorials.com
. This readies the radio to scan. In a mesh scenario, the node might already be in AP_STA mode for mesh; you might temporarily drop the mesh connection (e.g., call mesh.stop() or even WiFi.disconnect() which could halt mesh activity) just for the scan duration. After scanning, you can restart/join the mesh if needed. PainlessMesh doesn’t provide a built-in scan utility, so using WiFi.h is standard.
Performing the Scan: Use WiFi.scanNetworks() for a full scan on 2.4 GHz. This returns the number of networks and then you can loop for (int i=0; i<n; ++i) and get details: WiFi.SSID(i), WiFi.BSSID(i), WiFi.RSSI(i), WiFi.channel(i) etc. Include logic to capture hidden networks: those will have WiFi.SSID(i) as empty string. You can label them specially. If using the ESP-IDF scan function directly, set show_hidden=true
docs.espressif.com
 to include them.
Scanning 5 GHz on ESP32-C5: The Arduino WiFi.scanNetworks might or might not automatically cover 5 GHz on a C5. If it doesn’t, one workaround is to perform two scans: one on 2.4 GHz (default), and one restricted to 5 GHz channels. Espressif’s Wi-Fi driver allows specifying a list of channels to scan via esp_wifi_scan_start(). For instance, you could populate a wifi_scan_config_t with channel and scan_type fields. Another simpler method is to call esp_wifi_set_bandwidth or a not-yet-high-level-exposed API. Because the C5 is relatively new, you may need to dig into IDF documentation for scanning dual-band. A known issue on earlier Arduino core was lack of 5 GHz scanning support
github.com
, but presumably by 2025 there are fixes. In any case, utilize the ESP32-C5’s capability by scanning the additional channels – you don’t want to miss 5 GHz APs on your network.
Time and Synchronization: Scanning is one of the slower operations. A full 2.4 GHz scan (channels 1–13) plus 5 GHz (36–165) can take several seconds. If multiple slaves scan concurrently, that’s fine since they operate independently (faster overall completion). Just ensure the master waits long enough to receive all results. A strategy is to have slaves send a “scan_complete” message after sending results, so the master knows it heard from everyone. Alternatively, master can wait a fixed window (like 5–8 seconds) then proceed. To be safe, and to handle varying numbers of slaves, a handshake is better. For instance: master issues “scan”; each slave replies “scan_result” and then a short “done” message. Master counts “done” messages – when it matches the number of slaves it initially knew were connected, it concludes the scan phase. This guarantees all slaves responded.
Hidden SSIDs details: As noted, scanning can detect hidden networks but not their names. If your goal is purely deauthentication, knowing the BSSID (MAC address) and channel of a hidden AP is sufficient to target it. However, if you wanted to reveal a hidden SSID’s name, you’d need a different approach (like sending deauth and capturing the probe request from the client – beyond our scope). We assume listing it as “Hidden AP (MAC xx:xx:xx)” is enough, and then you can still deauth it by MAC.
In summary, the slaves will carry out Wi-Fi scanning on both bands (as hardware allows) and return a comprehensive list of APs (with duplicates merged by master). This fulfills the requirement of finding “all access points including hidden SSIDs on both 2.4GHz and 5GHz.”
Deauthentication Attack Implementation
Once an access point is selected, the system should perform a deauthentication attack to knock clients off that network. This is done by sending crafted deauthentication frames (802.11 management packets) to the target AP/clients. Here are the technical specifics and considerations:
Understanding Deauth Frames: A deauthentication frame is a type of management frame that, when received by a Wi-Fi client or AP, instructs it to disconnect. Typically, the AP sends deauth frames to clients (or vice versa) to terminate a connection. In an attack, we forge these frames. For maximum effect, attackers often send them with the AP’s identity to all clients (broadcast)
ambientnode.uk
ambientnode.uk
. In practice, that means using the target AP’s BSSID as the source address and the broadcast MAC (FF:FF:FF:FF:FF:FF) or a specific client’s MAC as the destination, with the frame subtype set to deauth (0xC0) and a reason code (e.g., 0x07). Our slaves will generate these frames.
Preparing to Send Raw Frames: The ESP32 (and C5) can send raw Wi-Fi packets using the esp_wifi_80211_tx() function
github.com
. To use it, ensure Wi-Fi is started (esp_wifi_init() and esp_wifi_start() are done under the hood by WiFi.mode and beginning operations). The function signature is: esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq)
github.com
. We will call this from Arduino by including <esp_wifi.h>. We need to craft the buffer containing the deauth frame bytes. This means manually constructing the 802.11 frame header and payload:
Frame Control bytes: set type/subtype to deauthentication (0xC0), set appropriate flags (likely 0x00 to indicate no WEP, not a retry, etc.).
Duration: can be 0x0000 for management frames.
Addresses: we need three MAC addresses in the header. For a deauth frame, typically: Addr1 (DA) = target client’s MAC (or broadcast), Addr2 (SA) = AP’s MAC, Addr3 (BSSID) = AP’s MAC. If using broadcast deauth (to kick everyone), Addr1 = FF:FF:FF:FF:FF:FF.
Sequence/control: set sequence number (if we use en_sys_seq=true in the API, the system will insert a sequence for us, which is easier
docs.espressif.com
).
Reason code: a 2-byte reason code in the payload (common is 0x0007 or 0x0001 – reason codes like “unspecified” or “class 3 frame received from nonassociated station”). For our purpose, the exact reason isn’t critical; 0x0001 can be used.
The total frame length will be 24 bytes header + 2 bytes reason = 26 bytes. We can define a uint8_t deauthPacket[26] and fill it accordingly. (There are code examples in projects like Spacehuhn’s deauther or others that show how to fill this array; you can refer to those for the exact byte layout.)
Channel and Interface: It’s crucial to transmit the deauth frames on the correct Wi-Fi channel of the target AP. The slave that performs the attack must tune its radio to the AP’s channel or else the target won’t hear it. Since the slaves were scanning, they know the channel (from scan results). To ensure this, call WiFi.setChannel(targetChannel) or use esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE) on the attacking node before sending packets. Also, choose the appropriate interface for esp_wifi_80211_tx: if the node is not connected to the target AP (it won’t be, we are just injecting), you can use the SoftAP interface (WIFI_IF_AP) for transmission. In fact, Espressif’s docs note that if the device is in station+AP mode, you can send from either interface
docs.espressif.com
. Some implementations use WIFI_IF_AP to send as though from AP.
Coordinating the Attack: The master, upon “deauth” command, should instruct one or more slaves to start deauthing the selected AP. The simplest method: master broadcasts a message like {"cmd":"deauth","bssid":"<AP_BSSID>","channel":<CH>}. All slaves could listen and perform the attack. However, having all slaves do it might be overkill or could increase effectiveness (multiple devices sending deauth frames from different locations could cover more area or redundancy). If your slaves are spread out or some closer to the target AP, you might choose a specific one to attack (e.g., the one that reported the strongest signal for that AP). For simplicity, you could have all slaves send deauth frames for a short period. They will each tune to the target channel (temporarily leaving the mesh channel), send a burst of deauth packets, then return to mesh. This maximizes chance to disconnect clients.
Sending Packets in a Loop: How many deauth frames to send? In many implementations, the device continuously sends them (a form of DoS) for a certain duration or until stopped. Since the request is to “deauth clients quickly,” you might not need a prolonged attack – a quick burst might suffice to knock off most clients. For instance, each slave could send, say, 10 deauth frames in a row, with slight delays, then stop. Or continue until the user stops it. To keep it simple, perhaps send a fixed number (like a few hundred packets spread over a few seconds) and then automatically stop. You can always extend this logic.
Master UI during attack: The master can display status like “Deauth attack in progress on [SSID]...” and maybe a prompt to press a key or command to stop (if you implement that). If the attack is short and stops by itself, you can print “Deauth complete.” Once done, the slaves should retune back to the mesh channel and resume normal operation (the mesh will heal and they reconnect). If the user wants to run it again, they can.
Legal and Ethical Note: Even though the user says this is for a closed home network test, it’s worth reiterating (perhaps in documentation or comments) that deauth attacks should only be done on networks you own or have permission to test. It’s illegal to carry out indiscriminate jamming or deauth on others’ networks
github.com
. The code we write should include such warnings. For instance, in the source or readme: “Use this tool only on networks you are authorized to test.” This aligns with best practices and the caution noted by projects utilizing these techniques
github.com
.
Testing on ESP32-C5: Since one specific aim is 5 GHz deauth, ensure that the code runs properly on an ESP32-C5. The ESP32-C5 is RISC-V based and supports both bands
espressif.com
. The esp_wifi_80211_tx API should work similarly on it (the ESP-IDF underlying supports C5 with dual-band, though note that some 5 GHz channels might be subject to regulatory restrictions/DFS where the device may not allow sending arbitrary frames). In practice, channels 36-48 should be fine for testing. Test the deauth function on a 5 GHz network of your own to confirm clients indeed disconnect.
In summary, the deauth functionality will be implemented by crafting raw 802.11 packets on the slave nodes and using Espressif’s WiFi driver to transmit them on the target channel
github.com
. This achieves the goal of “deauthenticate clients quickly” on the chosen Wi-Fi network. Each node will report back to the master or the master will observe that the command was executed (perhaps with a simple acknowledgment message).
Modular Code Structure and Future Expansion
Bringing it all together, your project should be structured for clarity and extensibility:
Master (Cardputer) Code Modules: It will handle UI/keyboard, maintain the list of APs and selected target, and coordinate the mesh. Key components:
Mesh Communication: Initialization and callbacks (for connections and message receipt).
UI & Command Handler: As discussed, manage display and parse user input into actions.
Command Execution: Functions like sendScanCommand() (broadcasts “scan” to slaves and then waits/collects results), sendDeauthCommand() (sends “deauth” order to slaves with target info). Also functions to process incoming data from slaves (e.g., parse scan results messages and store them).
Slave Code Modules: The slave sketch will be simpler:
Mesh Communication: Join mesh and setup onReceive to handle commands from master.
Command Handlers: On receiving a “scan” message, run the scanning routine and send results. On “deauth” message, extract target info, run the deauth routine.
(The slaves don’t need any display or human interface; they run headlessly, so loop can mostly mesh.update() and wait for commands.)
Use of Tasks/Multitasking: The ESP32 has dual cores and can handle multiple tasks. You may not need to explicitly create tasks for this project, but if you find, for example, that updating the UI in master and handling mesh messages can be parallelized, you could leverage FreeRTOS tasks. PainlessMesh internally uses TaskScheduler (cooperative multitasking) for handling its networking events
randomnerdtutorials.com
, so avoid blocking those for too long. Likewise, sending many deauth packets could be done in a separate task on a slave to avoid freezing the mesh loop. Keep in mind memory and CPU constraints, but overall an ESP32-S3 or C5 is quite capable for these networking tasks.
Following M5Stack Standards: When expanding or integrating with other M5Stack components later, continue to use the M5Unified API for consistency. For example, if later adding sound output on the Cardputer’s speaker for alerts, use M5.Unified’s sound classes. Adhering to their patterns ensures compatibility with future updates from M5Stack.
Scalability: The design is such that you can add more slave nodes (including different ESP32 variants) and they will mesh and participate. The master can always query how many nodes and adapt. The code can also be extended with more commands (for example, a “beacon spam” command or a “client scan” to sniff connected devices, etc.) using the same framework. Keeping the parsing logic modular (perhaps using if/else or a dispatch table for commands) will make it easier to insert new functionality.
Testing and Debugging: It’s wise to test components individually. For instance, first get the Cardputer showing the prompt and echoing typed commands (you can test on USB Serial as well). Then test mesh connectivity (maybe have slaves send a periodic ping and see if master receives). Then test scanning in isolation on one node, etc. Build up to the integrated system. Because this is a complex project, taking it step by step will help. Use serial prints generously for debug (on Cardputer, you have USB serial via the M5StampS3, which you can use alongside the on-screen output for debugging). Once stable, you can remove or reduce debug logs.
By structuring the code in this modular way and following best practices, the project will be robust and maintainable. It will also align with the style expected in M5Stack official projects and allow for future growth (for example, adding an on-device menu UI instead of text commands, or integrating an SD card to log events, etc.). You’re effectively creating a foundation that can be “built on later”, so clarity and proper use of libraries is key.
Summary and Key Takeaways
In this deep-dive, we explored how to build a Wi-Fi testing tool using an M5Stack Cardputer as a master controller and ESP32-based slaves (including the ESP32-C5 for dual-band capability). To recap the critical points and rules for coding this project:
Follow Official Libraries & Standards: Use M5Stack’s M5Unified and Cardputer libraries for all hardware interactions on the master (screen, keyboard)
docs.m5stack.com
. Keep code modular and clear, respecting M5Stack coding style and general best practices (non-blocking UI, sufficient comments, etc.).
Use the Right Libraries: Install and include painlessMesh for networking
randomnerdtutorials.com
 (plus its dependencies
randomnerdtutorials.com
), utilize the Arduino WiFi library for scanning, and use Espressif’s raw packet API for deauth frames
github.com
. Ensure the ESP32-C5’s dual-band features are utilized by scanning 5 GHz channels and targeting them for deauth as needed
espressif.com
.
Cardputer Terminal UI: Implement a text-based interface with an input prompt and scrolling output. Display the number of connected slave nodes in real time, and accept commands like “scan”, “select <id>”, “deauth” from the built-in keyboard
docs.m5stack.com
. Use the Cardputer’s display APIs to render this cleanly.
Mesh Network Coordination: Create a self-organizing mesh so master and slaves communicate without a dedicated router
randomnerdtutorials.com
. Use mesh callbacks and messaging to coordinate actions. All nodes should report data back to the master so it can compile a full view of the Wi-Fi environment. Nodes should work in unison – e.g., splitting scanning tasks for efficiency and all reporting findings to master.
Comprehensive Scanning: When “scan” is initiated, slaves scan all accessible Wi-Fi channels (2.4 GHz on all, plus 5 GHz on ESP32-C5 nodes) for all access points. Include hidden SSIDs in results (the scan should not ignore them
docs.espressif.com
). The master aggregates these results, removes duplicates, and displays all discovered APs (with indices, SSIDs or “[hidden]”, MAC, channel, etc.).
Deauthentication Logic: Once an AP is selected, the master sends a command to deauthenticate clients. Slaves will then craft and inject deauth frames targeting that AP’s clients, using the esp_wifi_80211_tx function. They switch to the target AP’s channel to do so, then return to the mesh. The attack should be effective yet controlled (e.g., a burst of packets). This will “quickly deauthenticate clients” on the chosen network as intended
ambientnode.uk
.
Node Cooperation and Feedback: Throughout, all nodes remain communicative – slaves inform the master when tasks are done or if they encounter any issue. The master provides feedback to the user via the terminal (e.g., “3 nodes scanning...”, “Scan complete: 8 APs found”, “Deauth sent on Channel 36”). This transparency helps in a testing scenario to understand what’s happening.
By diligently implementing these details and rules, you will create a functional system that boots into a user-friendly terminal on the Cardputer, displays connected node count, allows scanning the Wi-Fi environment across multiple bands, and lets you select a target to deauth – all in a well-structured, efficient manner. This design leverages the strengths of the M5Stack Cardputer for control and Espressif’s mesh and Wi-Fi capabilities for distributed sensing and attack execution, fulfilling the project’s goals. Always remember to use this tool responsibly and only on authorized networks, as deauth attacks can be disruptive
github.com
