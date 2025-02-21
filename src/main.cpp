#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include "mbedtls/base64.h"  // Include mbedtls for base64 decoding

// Define the pins for the pumps
const int gardenPump = 27;        // Define pin 15 for garden pump
const int greenHousePump = 25;    // Define pin 18 for greenhouse pump

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
bool gardenScheduleEnabled = true;       // Variable to enable/disable schedule
bool greenHouseScheduleEnabled = true;   // Variable to enable/disable schedule

// Define pump activation times
struct PumpSchedule {
    int startHour;
    int startMinute;
    int endHour;
    int endMinute;
};

PumpSchedule gardenSchedule[] = {
    {6, 10, 6, 40},
    {12, 05, 12, 35},
    {17, 15, 17, 40}
};

PumpSchedule greenHouseSchedule[] = {
    {6, 0, 6, 10},
    {17, 11, 17, 22},
    {14, 06, 14, 9},
    {18, 5, 18, 15},
};

// Add helper function to format time as "HH:MM"
String formatTime(int hour, int minute) {
    String h = hour < 10 ? "0" + String(hour) : String(hour);
    String m = minute < 10 ? "0" + String(minute) : String(minute);
    return h + ":" + m;
}

// Username and password for authentication
const char* authUsername = "wagner"; // Set your username
const char* authPassword = "pumps48"; // Set your password

// Function declarations
void controlPump(PumpSchedule schedule[], int scheduleLength, int pumpPin, bool& pumpState, bool& pumpManualOverride, bool scheduleEnabled, int currentHour, int currentMinute);
void handleRoot();
void handlePumpControl();
void handleSetGardenSchedule();
void handleSetGreenhouseSchedule();
void handleResetOverride();
void setupServer();
bool isAuthenticated();

void setup() {
    Serial.begin(115200);

    // Set pump pins as outputs
    pinMode(gardenPump, OUTPUT);
    pinMode(greenHousePump, OUTPUT);
    digitalWrite(gardenPump, HIGH); // Ensure pump is off initially
    digitalWrite(greenHousePump, HIGH);

    //I TRIED TO GET THIS TO WORK AND IT DID NOT, THE INTENT WAS TO HAVE A STATIC IP ADDRESS TO CONNECT TO THE SERVER
    //     // Configure static IP address
    // IPAddress staticIP(192, 168, 1, 199);  // Set your desired static IP address
    // IPAddress gateway(192, 168, 0, 1);    // Set your gateway (usually your router's IP)
    // IPAddress subnet(255, 255, 255, 0);   // Set your subnet mask
    // IPAddress dns(8, 8, 8, 8);  // Google DNS THIS DOES NOT WORK AND IT'S NOT A PROBLEM BECAUSE I AM ONLY USING THE LOCAL NETWORK

    // // Apply the static IP configuration
    // if (!WiFi.config(staticIP, gateway, subnet, dns)) {
    //     Serial.println("Failed to configure static IP!");
    // }

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
        Serial.println("DNS IP:");
        Serial.println(WiFi.dnsIP());

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
    controlPump(gardenSchedule, sizeof(gardenSchedule) / sizeof(gardenSchedule[0]), gardenPump, gardenPumpState, gardenPumpManualOverride, gardenScheduleEnabled, currentHour, currentMinute);

    // Check greenhouse pump schedule
    Serial.print("Checking greenhouse pump at ");
    Serial.print(currentHour);
    Serial.print(":");
    Serial.println(currentMinute);
    controlPump(greenHouseSchedule, sizeof(greenHouseSchedule) / sizeof(greenHouseSchedule[0]), greenHousePump, greenHousePumpState, greenHousePumpManualOverride, greenHouseScheduleEnabled, currentHour, currentMinute);

    // Handle client requests
    server.handleClient();

    // Add a short delay before the next loop iteration
    delay(1000);
}

void controlPump(PumpSchedule schedule[], int scheduleLength, int pumpPin, bool& pumpState, bool& pumpManualOverride, bool scheduleEnabled, int currentHour, int currentMinute) {
    if (pumpManualOverride) {
        digitalWrite(pumpPin, pumpState ? LOW : HIGH);
        Serial.print("Pump on pin ");
        Serial.print(pumpPin);
        Serial.println(pumpState ? " is ON (Manual Override)." : " is OFF (Manual Override).");
        return;  // Exit early if in manual mode
    }

    // Only proceed if scheduling is enabled
    bool pumpOn = false;
    if (scheduleEnabled) {
        for (int i = 0; i < scheduleLength; i++) {
            if ((currentHour > schedule[i].startHour || (currentHour == schedule[i].startHour && currentMinute >= schedule[i].startMinute)) &&
                (currentHour < schedule[i].endHour || (currentHour == schedule[i].endHour && currentMinute <= schedule[i].endMinute))) {
                pumpOn = true;
                break;
            }
        }
    }

    // Update the pump state based on the schedule
    pumpState = pumpOn;

    digitalWrite(pumpPin, pumpOn ? LOW : HIGH);
    Serial.print("Pump on pin ");
    Serial.print(pumpPin);
    Serial.println(pumpOn ? " is ON due to schedule." : " is OFF due to schedule.");
}


void setupServer() {
    server.on("/", handleRoot);
    server.on("/control", handlePumpControl);
    server.on("/setGardenSchedule", handleSetGardenSchedule);
    server.on("/setGreenhouseSchedule", handleSetGreenhouseSchedule);
    server.on("/resetOverride", handleResetOverride);  // <-- New route added
    server.begin();
    Serial.println("HTTP server started");
}

void handleRoot() {
    if (!isAuthenticated()) {
        server.sendHeader("WWW-Authenticate", "Basic realm=\"Pump Control\"");
        server.send(401, "text/plain", "Authentication required.");
        return;
    }

    // HTML content with embedded CSS
    String html = "<html><head><style>";
    
    // Add CSS for styling
    html += "body { font-family: Arial, sans-serif; background-color: #f4f4f9; margin: 0; padding: 20px; color: #333; }";
    html += "h1 { color: #333366; }";
    html += "h2 { color: #5a5a8f; }";
    html += "p { font-size: 16px; margin: 10px 0; }";
    html += "form { margin-top: 20px; }";
    html += "input[type=submit] { padding: 10px 20px; margin: 5px; font-size: 16px; color: #fff; background-color: #4CAF50; border: none; border-radius: 5px; cursor: pointer; }";
    html += "input[type=submit]:hover { background-color: #45a049; }";
    html += "</style></head><body><h1>Pump Control</h1>";

    html += "</style></head><body><h1>Pump Control</h1>";
    
    // Display the current state of each pump
    html += "<h2>Current States</h2>";
    html += "<p>Bomba da Horta esta " + String(gardenPumpState ? "<Strong>Ligada</strong>" : "<Strong>Desligada</strong>") + "</p>";
    html += "<p>Bomba da Estufa esta " + String(greenHousePumpState ? "<Strong>Ligada</strong>" : "<Strong>Desligada</strong>") + "</p>";
    
    // Display the current schedule status for each pump
    html += "<h2>Schedule Status</h2>";
    html += "<p>Programacao da bomba da horta esta " + String(gardenScheduleEnabled ? "<Strong>Ativa</Strong>" : "<Strong>Inativa</Strong>") + "</p>";
    html += "<p>Programacao da bomba da estufa esta " + String(greenHouseScheduleEnabled ? "<Strong>Ativa</Strong>" : "<Strong>Inativa</Strong>") + "</p>";

    // Reset override
    html += "<h2>Reset Override</h2>";
    html += "<button onclick=\"resetOverride()\">Reset to Schedule</button>";

    html += "<script>";
    html += "function resetOverride() {";
    html += "  fetch('/resetOverride', { method: 'POST' })";
    html += "    .then(response => {";
    html += "      if (response.ok) {";
    html += "        window.location.reload();";  // Force the page to reload
    html += "      } else {";
    html += "        alert('Failed to reset override.');";
    html += "      }";
    html += "    })";
    html += "    .catch(error => console.error('Error:', error));";
    html += "}";
    html += "</script>";

    // Manual control form
    html += "<h2>Controle Manual</h2>";
    html += "<form action=\"/control\" method=\"POST\">";
    html += "<input type=\"submit\" name=\"garden\" value=\"Alternar Bomba da Horta\">";
    html += "<input type=\"submit\" name=\"greenhouse\" value=\"Alternar Bomba da Estufa\"><br><br>";
    
    // Schedule control form
    html += "<h2>Irrigacao Programada</h2>";
    html += "<input type=\"submit\" name=\"enableGardenSchedule\" value=\"" + String(gardenScheduleEnabled ? "Desabilitar" : "Abilitar") + " Programacao da Horta\">";
    html += "<input type=\"submit\" name=\"enableGreenhouseSchedule\" value=\"" + String(greenHouseScheduleEnabled ? "Desabilitar" : "Abilitar") + " Programacao da Estufa\">";
    
    html += "</form>";
    html += "</body></html>";
    
    // Garden Pump Schedule Form
    html += "<h2>Garden Pump Schedule</h2>";
    html += "<form action=\"/setGardenSchedule\" method=\"POST\">";
    for (int i = 0; i < sizeof(gardenSchedule)/sizeof(gardenSchedule[0]); i++) {
        html += "Schedule " + String(i+1) + ": ";
        html += "<input type=\"time\" name=\"start" + String(i+1) + "\" value=\"" + formatTime(gardenSchedule[i].startHour, gardenSchedule[i].startMinute) + "\" required> to ";
        html += "<input type=\"time\" name=\"end" + String(i+1) + "\" value=\"" + formatTime(gardenSchedule[i].endHour, gardenSchedule[i].endMinute) + "\" required><br>";
    }
    html += "<input type=\"submit\" value=\"Update Garden Schedule\">";
    html += "</form>";

    // Greenhouse Pump Schedule Form
    html += "<h2>Greenhouse Pump Schedule</h2>";
    html += "<form action=\"/setGreenhouseSchedule\" method=\"POST\">";
    for (int i = 0; i < sizeof(greenHouseSchedule)/sizeof(greenHouseSchedule[0]); i++) {
        html += "Schedule " + String(i+1) + ": ";
        html += "<input type=\"time\" name=\"start" + String(i+1) + "\" value=\"" + formatTime(greenHouseSchedule[i].startHour, greenHouseSchedule[i].startMinute) + "\" required> to ";
        html += "<input type=\"time\" name=\"end" + String(i+1) + "\" value=\"" + formatTime(greenHouseSchedule[i].endHour, greenHouseSchedule[i].endMinute) + "\" required><br>";
    }
    html += "<input type=\"submit\" value=\"Update Greenhouse Schedule\">";
    html += "</form>";

    html += "</body></html>";
    server.send(200, "text/html", html);
}

// Add handlers for setting schedules
void handleSetGardenSchedule() {
    if (!isAuthenticated()) {
        server.sendHeader("WWW-Authenticate", "Basic realm=\"Pump Control\"");
        server.send(401, "text/plain", "Authentication required.");
        return;
    }

    int numSchedules = sizeof(gardenSchedule)/sizeof(gardenSchedule[0]);
    for (int i = 0; i < numSchedules; i++) {
        String startArg = "start" + String(i+1);
        String endArg = "end" + String(i+1);
        
        if (!server.hasArg(startArg) || !server.hasArg(endArg)) {
            server.send(400, "text/plain", "Missing parameters.");
            return;
        }
        
        String startTime = server.arg(startArg);
        String endTime = server.arg(endArg);
        
        int startHour = startTime.substring(0, 2).toInt();
        int startMinute = startTime.substring(3).toInt();
        int endHour = endTime.substring(0, 2).toInt();
        int endMinute = endTime.substring(3).toInt();
        
        // Validate times
        if (startHour < 0 || startHour > 23 || startMinute < 0 || startMinute > 59 ||
            endHour < 0 || endHour > 23 || endMinute < 0 || endMinute > 59) {
            server.send(400, "text/plain", "Invalid time values.");
            return;
        }
        
        if (startHour > endHour || (startHour == endHour && startMinute >= endMinute)) {
            server.send(400, "text/plain", "Start time must be before end time.");
            return;
        }
        
        // Update schedule
        gardenSchedule[i].startHour = startHour;
        gardenSchedule[i].startMinute = startMinute;
        gardenSchedule[i].endHour = endHour;
        gardenSchedule[i].endMinute = endMinute;
    }
    
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Redirecting...");
}

void handleSetGreenhouseSchedule() {
    if (!isAuthenticated()) {
        server.sendHeader("WWW-Authenticate", "Basic realm=\"Pump Control\"");
        server.send(401, "text/plain", "Authentication required.");
        return;
    }

    int numSchedules = sizeof(greenHouseSchedule)/sizeof(greenHouseSchedule[0]);
    for (int i = 0; i < numSchedules; i++) {
        String startArg = "start" + String(i+1);
        String endArg = "end" + String(i+1);
        
        if (!server.hasArg(startArg) || !server.hasArg(endArg)) {
            server.send(400, "text/plain", "Missing parameters.");
            return;
        }
        
        String startTime = server.arg(startArg);
        String endTime = server.arg(endArg);
        
        int startHour = startTime.substring(0, 2).toInt();
        int startMinute = startTime.substring(3).toInt();
        int endHour = endTime.substring(0, 2).toInt();
        int endMinute = endTime.substring(3).toInt();
        
        // Validation (same as garden)
        if (startHour < 0 || startHour > 23 || startMinute < 0 || startMinute > 59 ||
            endHour < 0 || endHour > 23 || endMinute < 0 || endMinute > 59) {
            server.send(400, "text/plain", "Invalid time values.");
            return;
        }
        
        if (startHour > endHour || (startHour == endHour && startMinute >= endMinute)) {
            server.send(400, "text/plain", "Start time must be before end time.");
            return;
        }
        
        greenHouseSchedule[i].startHour = startHour;
        greenHouseSchedule[i].startMinute = startMinute;
        greenHouseSchedule[i].endHour = endHour;
        greenHouseSchedule[i].endMinute = endMinute;
    }
    
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Redirecting...");
}

void handleResetOverride() {
    if (!isAuthenticated()) {
        server.sendHeader("WWW-Authenticate", "Basic realm=\"Pump Control\"");
        server.send(401, "text/plain", "Authentication required.");
        return;
    }

    gardenPumpManualOverride = false;
    greenHousePumpManualOverride = false;

    server.send(200, "text/plain", "Manual override reset. Schedule control restored.");
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
        digitalWrite(gardenPump, gardenPumpState ? LOW : HIGH); // Immediately update the pump state
    } 
    if (server.hasArg("greenhouse")) {
        greenHousePumpState = !greenHousePumpState; // Toggle the state
        greenHousePumpManualOverride = true;        // Enable manual override
        digitalWrite(greenHousePump, greenHousePumpState ? LOW : HIGH); // Immediately update the pump state
    }
    if (server.hasArg("enableGardenSchedule")) {
        gardenScheduleEnabled = !gardenScheduleEnabled; // Toggle schedule enable
        if (gardenScheduleEnabled) {
            gardenPumpManualOverride = false; // Reset manual override when enabling schedule
        }
    }
    if (server.hasArg("enableGreenhouseSchedule")) {
        greenHouseScheduleEnabled = !greenHouseScheduleEnabled; // Toggle schedule enable
        if (greenHouseScheduleEnabled) {
            greenHousePumpManualOverride = false; // Reset manual override when enabling schedule
        }
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
        
        int ret = mbedtls_base64_decode(decoded, sizeof(decoded), &decodedLength, (const unsigned char*)encoded.c_str(), encoded.length());
        if (ret != 0) return false;
        
        String decodedString = String((char*)decoded, decodedLength);
        
        // Check username and password
        if (decodedString == String(authUsername) + ":" + String(authPassword)) {
            return true;
        }
    }
    
    return false;
}
