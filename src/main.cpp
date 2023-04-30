// Including Default Libraries
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

// Including Library provided by mobizt
#include <FirebaseESP32.h>

#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>



/************************************** Pin Defination **************************************/
/* 74HC165 */
namespace PISO
{
    const uint8_t latchPin = 33;  // SH/!LD, PL
    const uint8_t clkInhPin = 27; // CLK INH
    const uint8_t clockPin = 25;  // CLK
    const uint8_t dataPin = 26;   // QH, Q7
}

/* 74HC595 */
namespace SIPO
{
    const uint8_t latchPin = 12; // RCLK, ST_CP
    const uint8_t clockPin = 14; // SRCLK, SH_CP
    const uint8_t dataPin = 13;  // SER, DS
}

/* WiFi Pins */
const uint8_t WiFiResetPin = 18;
const uint8_t onlineStatePin = 17;

/* Analog Input */
const uint8_t LDRPin = 36;

/************************************** Data Defination **************************************/

// Shift Register Data
const uint8_t shiftRegisters = 1;
const uint8_t gpioPins = 8 * shiftRegisters;

uint8_t switchState[gpioPins];
uint8_t ledState[gpioPins];
bool enableLDRState[gpioPins];

// WiFi Credentials Storage
Preferences prefs;

String wifi_ssid = "";     // Wifi SSID
String wifi_password = ""; // Wifi password

const char *user_email = "crrit2021.ee.majorproject@gmail.com"; // Firebase login email
const char *user_password = "CRRITee@202109";                   // Firebase login password

// Used to Access Firebase Data
const char *api_key = "AIzaSyB7pb1coAl_6zuNRcXw21NnWbUY_ZO27AU";
const char *database_url = "crrit2021-ee-majorproject-default-rtdb.firebaseio.com/";

// Firebase database object
FirebaseData stream;
FirebaseData fbdo;

// Firebase Authentication & Configuration
FirebaseAuth auth;
FirebaseConfig config;

// Store Firebase data
FirebaseJson json;

// LDR Data
bool LDRState;

uint32_t LDRCount = 0;
unsigned long LDRStartTime = 0;

bool networkDown = false;

/******************************** Read & Write Shift Registers ********************************/

void readShiftRegisters()
{
    // Reading data from 74HC165
    digitalWrite(PISO::latchPin, LOW);
    delayMicroseconds(5);
    digitalWrite(PISO::latchPin, HIGH);
    delayMicroseconds(5);

    digitalWrite(PISO::clkInhPin, LOW);
    for (uint8_t i = 0; i < shiftRegisters; i++)
    {
        for (uint8_t j = 0; j < 8; j++)
        {
            uint32_t index = (i + 1) * j;

            switchState[index] = digitalRead(PISO::dataPin);

            digitalWrite(PISO::clockPin, HIGH);
            digitalWrite(PISO::clockPin, LOW);
        }
    }
    digitalWrite(PISO::clkInhPin, HIGH);
}

void writeShiftRegisters()
{
    // Writing data to 74HC595
    digitalWrite(SIPO::latchPin, LOW);
    for (int16_t i = shiftRegisters - 1; i >= 0; i--)
    {
        for (uint8_t j = 0; j < 8; j++)
        {
            uint32_t index = (i + 1) * 8 - (j + 1);

            digitalWrite(SIPO::dataPin, ledState[index]);

            digitalWrite(SIPO::clockPin, HIGH);
            digitalWrite(SIPO::clockPin, LOW);
        }
    }
    digitalWrite(SIPO::latchPin, HIGH);
}


void setup()
{
    Serial.begin(115200);

    /*************************************** Setup Pins **************************************/

    // Initialize Pins
    pinMode(PISO::latchPin, OUTPUT);
    pinMode(PISO::clkInhPin, OUTPUT);
    pinMode(PISO::clockPin, OUTPUT);
    pinMode(PISO::dataPin, INPUT);

    pinMode(SIPO::latchPin, OUTPUT);
    pinMode(SIPO::clockPin, OUTPUT);
    pinMode(SIPO::dataPin, OUTPUT);

    pinMode(onlineStatePin, OUTPUT);
    pinMode(WiFiResetPin, INPUT);

    // Default state of Pins
    digitalWrite(PISO::latchPin, HIGH);
    digitalWrite(PISO::clkInhPin, HIGH);

    digitalWrite(SIPO::latchPin, HIGH);

    // Default switchState & enableLDRState
    readShiftRegisters();
    for (uint8_t i = 0; i < gpioPins; i++)
    {
        ledState[i] = switchState[i];
        enableLDRState[i] = false;
    }
    writeShiftRegisters();

    // Initial LDRState
    LDRState = analogRead(LDRPin) < 300;

    /************************************** Setup WiFi ****************************************/
  
    // Get Password From Storage
    prefs.begin("WiFi", false);

    wifi_ssid = prefs.getString("ssid", "");
    wifi_password = prefs.getString("password", "");

    bool networkFound = false;

    uint8_t networks = WiFi.scanNetworks();

    if (networks == 0)
    {
        Serial.println("No Networks Found");
        networkDown = true;
        return;
    }

    for (uint8_t i = 0; i < networks; i++)
    {
        if (WiFi.SSID(i) == wifi_ssid)
        {
            Serial.printf("%s found\n", wifi_ssid);
            networkFound = true;
        }
    }

    if (networkFound)
    {
        Serial.println();
        Serial.printf("WiFi network: %s\n", wifi_ssid);
        Serial.println("[WiFi-event] Connecting...");

        WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

        unsigned long wifi_startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - wifi_startTime < 4000)
        {
            delay(100);
        }
    }
    else
    {
        WiFi.mode(WIFI_AP_STA);
        WiFi.beginSmartConfig();

        Serial.println("Waiting for SmartConfig...");
        while (!WiFi.smartConfigDone())
        {
            digitalWrite(onlineStatePin, HIGH);
            delay(200);
            digitalWrite(onlineStatePin, LOW);
            delay(200);
        }
        Serial.println("SmartConfig received.");

        // Connect to SmartConfig WiFi
        Serial.println();
        Serial.printf("WiFi network: %s\n", WiFi.SSID());
        Serial.println("[WiFi-event] Connecting...");

        unsigned long wifi_startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - wifi_startTime < 4000)
        {
            delay(100);
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            prefs.putString("ssid", WiFi.SSID());
            prefs.putString("password", WiFi.psk());
        }
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[WiFi-event] Connection failed");
        networkDown = true;
        return;
    }

    Serial.println("[WiFi-event] Connected");

    /******************************* Connect to Firebase ************************************/

    config.api_key = api_key;
    config.database_url = database_url;

    config.token_status_callback = tokenStatusCallback;

    auth.user.email = user_email;
    auth.user.password = user_password;

    Serial.println();
    Serial.printf("Firebase email: %s\n", user_email);
    Serial.println("[Firebase-event] Connecting...");

    Firebase.begin(&config, &auth);

    if (!Firebase.ready() || !Firebase.RTDB.getJSON(&fbdo, "/"))
    {
        Serial.println("[Firebase-event] Connection failed");
        networkDown = true;
        return;
    }

    Serial.println("[Firebase-event] Connected");
    digitalWrite(onlineStatePin, HIGH);

    // Set Default Pin Status to Firebase
    json = fbdo.jsonObject();

    for (uint8_t i = 0; i < gpioPins; i++)
    {
        String path = "/Switch" + String(gpioPins - i, DEC);

        json.set(path + "/value", (bool)switchState[i]);
        json.set(path + "/enableLDR", enableLDRState[i]);
    }

    if (!Firebase.RTDB.setJSON(&fbdo, "/", &json))
    {
        Serial.println("[Firebase-event] Connection failed");
        networkDown = true;
    }

    Firebase.beginStream(stream, "/");
}

void loop()
{
    /*********************** Stream Data from firebase and update Led ***********************/
    if (!networkDown)
    {
        if (!Firebase.readStream(stream))
        {
            Serial.println("[Firebase-event] Connection failed");
            networkDown = true;
        }
        else if (stream.streamAvailable())
        {
            for (uint8_t i = 0; i < gpioPins; i++)
            {
                String path = "/Switch" + String(gpioPins - i, DEC);

                if (stream.dataPath() == path + "/value")
                {
                    ledState[i] = (uint8_t)stream.boolData();
                }

                if (stream.dataPath() == path + "/enableLDR")
                {
                    enableLDRState[i] = stream.boolData();
                }
            }
        }
    }

    bool updateFirebase = false;

    /******************************* Reading data from 74HC165 ******************************/
    digitalWrite(PISO::latchPin, LOW);
    delayMicroseconds(5);
    digitalWrite(PISO::latchPin, HIGH);
    delayMicroseconds(5);

    digitalWrite(PISO::clkInhPin, LOW);
    for (uint8_t i = 0; i < shiftRegisters; i++)
    {
        for (uint8_t j = 0; j < 8; j++)
        {
            uint32_t index = (i + 1) * j;
            uint8_t previous = switchState[index];

            switchState[index] = digitalRead(PISO::dataPin);

            if (previous != switchState[index])
            {
                updateFirebase = true;
                ledState[index] = switchState[index];
            }

            digitalWrite(PISO::clockPin, HIGH);
            digitalWrite(PISO::clockPin, LOW);
        }
    }
    digitalWrite(PISO::clkInhPin, HIGH);

    /***************************** Check LDR State & Update LED *****************************/
    bool prevLDRState = LDRState;
    LDRState = analogRead(LDRPin) < 300;

    if (prevLDRState == LDRState)
    {
        LDRCount++;
    }
    else
    {
        LDRStartTime = millis();
    }

    if (millis() - LDRStartTime > 3000)
    {
        for (uint8_t i = 0; i < gpioPins; i++)
        {
            if (enableLDRState[i] && ledState[i] != (uint8_t)LDRState)
            {
                ledState[i] = (uint8_t)LDRState;
                updateFirebase = true;
            }
        }
    }

    /******************************* Update LED State on Firebase ***************************/
    if (!networkDown)
    {
        if (!Firebase.ready())
        {
            Serial.println("[Firebase-event] Connection failed");
            networkDown = true;
        }

        if (updateFirebase)
        {
            for (uint8_t i = 0; i < gpioPins; i++)
            {
                String path = "/Switch" + String(gpioPins - i, DEC);
                json.set(path + "/value", (bool)ledState[i]);
                json.set(path + "/enableLDR", (bool)enableLDRState[i]);
            }

            if (!Firebase.RTDB.setJSON(&fbdo, "/", &json))
            {
                Serial.println("[Firebase-event] Connection failed");
                networkDown = true;
            }
        }
    }

    writeShiftRegisters();

    /********************************* Handle Network status *********************************/
    if (networkDown)
    {
        digitalWrite(onlineStatePin, LOW);

        if (digitalRead(WiFiResetPin) == HIGH)
        {
            Serial.println("Network Running Again");
            networkDown = false;
        }
    } else {
        digitalWrite(onlineStatePin, HIGH);
    }

    /************************************ Handle Wifi reset **********************************/
    unsigned long resetStartTime = millis();
    while (digitalRead(WiFiResetPin) == HIGH);

    if (millis() - resetStartTime >= 2000)
    {
        Serial.println("Reseting the WiFi credentials");
        prefs.putString("ssid", "");
        prefs.putString("password", "");
        Serial.println("Wifi credentials erased");
        Serial.println("Restarting the ESP");
        ESP.restart();
    }
}
