#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

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

// Define pump activation times
struct PumpSchedule {
    int startHour;
    int startMinute;
    int endHour;
    int endMinute;
};

// Garden pump schedule (3 times a day)
PumpSchedule gardenSchedule[] = {
    {6, 10, 6, 40},
    {12, 10, 12, 45},
    {17, 15, 17, 40}
};

// Greenhouse pump schedule (4 times a day)
PumpSchedule greenHouseSchedule[] = {
    {6, 0, 6, 10},
    {10, 20, 10, 30},
    {14, 10, 14, 20},
   {18, 05, 18, 15},
};

// Function declaration
void controlPump(PumpSchedule schedule[], int scheduleLength, int pumpPin, int currentHour, int currentMinute);

void setup() {
    Serial.begin(115200);

    // Set pump pins as outputs
    pinMode(gardenPump, OUTPUT);
    pinMode(greenHousePump, OUTPUT);

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
    } else {
        Serial.println("Failed to connect to WiFi");
    }

    // Start NTP client with time zone offset
    timeClient.begin();
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
    controlPump(gardenSchedule, sizeof(gardenSchedule) / sizeof(gardenSchedule[0]), gardenPump, currentHour, currentMinute);

    // Check greenhouse pump schedule
    Serial.print("Checking greenhouse pump at ");
    Serial.print(currentHour);
    Serial.print(":");
    Serial.println(currentMinute);
    controlPump(greenHouseSchedule, sizeof(greenHouseSchedule) / sizeof(greenHouseSchedule[0]), greenHousePump, currentHour, currentMinute);

    // Add a short delay before the next loop iteration
    delay(20000);
}

void controlPump(PumpSchedule schedule[], int scheduleLength, int pumpPin, int currentHour, int currentMinute) {
    bool pumpOn = false;

    for (int i = 0; i < scheduleLength; i++) {
        Serial.print("Schedule ");
        Serial.print(i + 1);
        Serial.print(" - Start: ");
        Serial.print(schedule[i].startHour);
        Serial.print(":");
        Serial.print(schedule[i].startMinute);
        Serial.print(", End: ");
        Serial.print(schedule[i].endHour);
        Serial.print(":");
        Serial.println(schedule[i].endMinute);

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
        Serial.println(" is ON.");
    } else {
        digitalWrite(pumpPin, HIGH); // Turn pump OFF
        Serial.print("Pump on pin ");
        Serial.print(pumpPin);
        Serial.println(" is OFF.");
    }
}
