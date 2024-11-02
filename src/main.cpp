#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include "mbedtls/base64.h"  // Include mbedtls for base64 decoding

// Define the pins for the pumps
const int gardenPump = 15;        // Define pin 15 for garden pump
const int greenHousePump = 18;    // Define pin 18 for greenhouse pump

// WiFi credentials
const char* ssid = "CHNET - Wagner";
const char* password = "think987";

// NTPClient setup
WiFiUDP udp;
int timeOffset = -3 * 3600; // Offset for your time zone (-3 hours)
NTPClient timeClient(udp, "pool.ntp.org", timeOffset, 60000); // Adjust the timezone as needed

// Web server setup
WebServer server(80);

// Pump state variables
bool gardenPumpState = false;
bool greenHousePumpState = false;
bool gardenPumpManualOverride = false;
bool greenHousePumpManualOverride = false;

// Define pump activation times
struct PumpSchedule {
    int startHour;
    int startMinute;
    int endHour;
    int endMinute;
};

PumpSchedule gardenSchedule[] = {
    {6, 10, 6, 40},
    {13, 59, 14, 02},
    {17, 15, 17, 40}
};

PumpSchedule greenHouseSchedule[] = {
    {6, 0, 6, 10},
    {10, 20, 10, 30},
    {14, 06, 14, 9},
    {18, 5, 18, 15},
};

// Username and password for authentication
const char* authUsername = "wagner"; // Set your username
const char* authPassword = "pumps48"; // Set your password

// Function declarations
void controlPump(PumpSchedule schedule[], int scheduleLength, int pumpPin, bool& pumpState, bool& pumpManualOverride, int currentHour, int currentMinute);
void handleRoot();
void handlePumpControl();
void setupServer();
bool isAuthenticated();

void setup() {
    Serial.begin(115200);

    // Set pump pins as outputs
    pinMode(gardenPump, OUTPUT);
    pinMode(greenHousePump, OUTPUT);
    digitalWrite(gardenPump, HIGH); // Ensure pump is off initially
    digitalWrite(greenHousePump, HIGH);

    // Connect to Wi-Fi
    WiFi.begin(ssid, password);
    int timeout = 30;
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
        timeout--;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to WiFi");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("Failed to connect to WiFi");
    }

    // Start NTP client
    timeClient.begin();

    // Set up web server
    setupServer();
}

void loop() {
    // Update the NTP client
    timeClient.update();

    // Get the current time
    int currentHour = timeClient.getHours();
    int currentMinute = timeClient.getMinutes();

    // Check garden pump schedule
    Serial.print("Checking garden pump at ");
    Serial.print(currentHour);
    Serial.print(":");
    Serial.println(currentMinute);
    controlPump(gardenSchedule, sizeof(gardenSchedule) / sizeof(gardenSchedule[0]), gardenPump, gardenPumpState, gardenPumpManualOverride, currentHour, currentMinute);

    // Check greenhouse pump schedule
    Serial.print("Checking greenhouse pump at ");
    Serial.print(currentHour);
    Serial.print(":");
    Serial.println(currentMinute);
    controlPump(greenHouseSchedule, sizeof(greenHouseSchedule) / sizeof(greenHouseSchedule[0]), greenHousePump, greenHousePumpState, greenHousePumpManualOverride, currentHour, currentMinute);

    // Handle client requests
    server.handleClient();

    // Add a short delay before the next loop iteration
    delay(1000);
}

void controlPump(PumpSchedule schedule[], int scheduleLength, int pumpPin, bool& pumpState, bool& pumpManualOverride, int currentHour, int currentMinute) {
    bool pumpOn = false;

    // If manual override is active, control the pump based on the manual state
    if (pumpManualOverride) {
        digitalWrite(pumpPin, pumpState ? LOW : HIGH); // Use the manual state directly
        Serial.print("Pump on pin ");
        Serial.print(pumpPin);
        Serial.println(pumpState ? " is ON (Manual Override)." : " is OFF (Manual Override).");
        return;
    }

    // Proceed with schedule check if no manual override is active
    for (int i = 0; i < scheduleLength; i++) {
        if ((currentHour > schedule[i].startHour || (currentHour == schedule[i].startHour && currentMinute >= schedule[i].startMinute)) &&
            (currentHour < schedule[i].endHour || (currentHour == schedule[i].endHour && currentMinute <= schedule[i].endMinute))) {
            pumpOn = true;
            break;
        }
    }

    if (pumpOn) {
        digitalWrite(pumpPin, LOW); // Turn pump ON
        Serial.print("Pump on pin ");
        Serial.print(pumpPin);
        Serial.println(" is ON due to schedule.");
    } else {
        digitalWrite(pumpPin, HIGH); // Turn pump OFF
        Serial.print("Pump on pin ");
        Serial.print(pumpPin);
        Serial.println(" is OFF due to schedule.");
    }
}

void setupServer() {
    server.on("/", handleRoot);
    server.on("/control", handlePumpControl);
    server.begin();
    Serial.println("HTTP server started");
}

void handleRoot() {
    if (!isAuthenticated()) {
        server.sendHeader("WWW-Authenticate", "Basic realm=\"Pump Control\"");
        server.send(401, "text/plain", "Authentication required.");
        return;
    }

    String html = "<html><body><h1>Pump Control</h1>";
    html += "<h2>Manual Control</h2>";
    html += "<form action=\"/control\" method=\"POST\">";
    html += "<input type=\"submit\" name=\"garden\" value=\"Toggle Garden Pump\">";
    html += "<input type=\"submit\" name=\"greenhouse\" value=\"Toggle Greenhouse Pump\">";
    html += "</form>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handlePumpControl() {
    if (!isAuthenticated()) {
        server.sendHeader("WWW-Authenticate", "Basic realm=\"Pump Control\"");
        server.send(401, "text/plain", "Authentication required.");
        return;
    }

    if (server.hasArg("garden")) {
        gardenPumpState = !gardenPumpState; // Toggle the state
        gardenPumpManualOverride = true;    // Enable manual override
    } 
    if (server.hasArg("greenhouse")) {
        greenHousePumpState = !greenHousePumpState; // Toggle the state
        greenHousePumpManualOverride = true;        // Enable manual override
    }

    // Redirect back to the main page
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Redirecting...");
}

bool isAuthenticated() {
    if (!server.hasHeader("Authorization")) {
        return false;
    }
    
    String authHeader = server.header("Authorization");
    if (authHeader.startsWith("Basic ")) {
        String encoded = authHeader.substring(6); // Get the encoded part
        
        // Decode Base64
        size_t decodedLength = 0;
        uint8_t decoded[64];  // Buffer for decoded data, adjust size if necessary
        
        int ret = mbedtls_base64_decode(decoded, sizeof(decoded), &decodedLength, 
                                        (const unsigned char*)encoded.c_str(), encoded.length());
        
        if (ret != 0) {
            Serial.println("Base64 decoding failed");
            return false;
        }

        String decodedStr = String((char*)decoded).substring(0, decodedLength);
        
        // Check if the decoded string matches "username:password"
        return decodedStr == String(authUsername) + ":" + String(authPassword);
    }

    return false;
}
