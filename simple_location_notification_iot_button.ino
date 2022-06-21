#include <Arduino.h>
#include <Adafruit_TinyUSB.h> // for Serial
#include "nrfx.h"
#include "nrfx_power.h"

// Select your modem:
#define TINY_GSM_MODEM_SARAR4

// Set serial for debug console (to the Serial Monitor, default speed 115200)
// #define SerialMon Serial
#ifdef SerialMon
#define DBG_begin SerialMon.begin
#define DBG_print SerialMon.print
#define DBG_println SerialMon.println
#define DBG_printf SerialMon.printf
#else
#define DBG_begin
#define DBG_print
#define DBG_println
#define DBG_printf
#endif

// Set serial for AT commands (to the module)
#define SerialAT Serial1

// Increase RX buffer to capture the entire response
// Chips without internal buffering (A6/A7, ESP8266, M590)
// need enough space in the buffer for the entire response
// else data will be lost (and the http library will fail).
#if !defined(TINY_GSM_RX_BUFFER)
#define TINY_GSM_RX_BUFFER 650
#endif

// See all AT commands, if wanted
// #define DUMP_AT_COMMANDS

// Define the serial console for debug prints, if needed
#ifdef SerialMon
#define TINY_GSM_DEBUG SerialMon
// #define LOGGING  // <- Logging is for the HTTP library
#endif

// Range to attempt to autobaud
// NOTE:  DO NOT AUTOBAUD in production code.  Once you've established
// communication, set a fixed baud rate using modem.setBaud(#).
#define GSM_AUTOBAUD_MIN 9600
#define GSM_AUTOBAUD_MAX 115200

// Add a reception delay, if needed.
// This may be needed for a fast processor at a slow baud rate.
// #define TINY_GSM_YIELD() { delay(2); }

// set GSM PIN, if any
#define GSM_PIN ""

// flag to force SSL client authentication, if needed
// #define TINY_GSM_SSL_CLIENT_AUTHENTICATION

// Your GPRS credentials, if any
const char apn[] = "soracom.io";
const char gprsUser[] = "sora";
const char gprsPass[] = "sora";

// Server details
const char server[] = "notify-api.line.me";
const int port = 443;

// Your LINE Notify Token
const char line_notify_token[] = "";

// Sever2 details
const char server2[] = "opencellid.org";
const int port2 = 80;

// Your OpenCellid API Token
const char opencellid_token[] = "";

#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

extern "C" void tusb_hal_nrf_power_event(uint32_t event);

TinyGsmClientSecure client(modem);
HttpClient http(client, server, port);

TinyGsmClient client2(modem);
HttpClient http2(client2, server2, port2);

const int SARA_V_INT = A0;
const int SARA_PWR_ON = 10;
const int SARA_REG_EN = 19; // A1
const int TRIGGER_PIN = 9;
const int STATUS_LED = LED_BUILTIN;

#define V_INT_ENABLE_LEVEL (409)  // 1.8V * 0.8 / 3.6V * 1024
#define V_INT_DISABLE_LEVEL (102) // 1.8V * 0.2 / 3.6V * 1024

long MCC = -1;
long MNC = -1;
long TAC = -1;
long CID = -1;
String Latitude = "";  // 緯度
String Longitude = ""; // 経度

void setup()
{
    pinMode(TRIGGER_PIN, INPUT_PULLUP);
    pinMode(SARA_REG_EN, OUTPUT);
    digitalWrite(SARA_REG_EN, LOW);
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, HIGH);

#ifndef SerialMon
    nrfx_power_usbevt_disable();
    tusb_hal_nrf_power_event(1);
#endif

    // Set console baud rate
    DBG_begin(115200);
    delay(100);
}

void loop()
{
    if (trigger_is_active())
    {
        digitalWrite(STATUS_LED, LOW);
        if (modem_start())
        {
            if (get_area_cellid())
            {
                if (get_cell_position(MCC, MNC, TAC, CID))
                {
                    // https://www.google.com/maps/search/?api=1&query=35.6812362,139.7649361
                    // https://www.google.co.jp/maps/@xxx,yyy,18z?hl=ja
                    // String msg = "https://www.google.co.jp/maps/@";
                    // msg += Latitude;
                    // msg += ",";
                    // msg += Longitude;
                    // msg += ",18z?hl=ja";
                    // https://maps.google.com/maps?q=36.879676,-111.512351
                    // String msg = "https://maps.google.com/maps?q=35.6585805,139.7454329";
                    String msg = "https://maps.google.com/maps?q=";
                    msg += Latitude;
                    msg += ",";
                    msg += Longitude;
                    DBG_println(msg);
                    line_notify(msg);
                }
            }
        }
        modem_stop();
        digitalWrite(STATUS_LED, HIGH);
    }
    delay(1000);
}

bool trigger_is_active(void)
{
    int rd = digitalRead(TRIGGER_PIN);
    delay(20);
    return rd == LOW && digitalRead(TRIGGER_PIN) == LOW;
}

bool modem_start(void)
{
    bool ret;

    if (sara_is_power_on())
    {
        DBG_println("Modem Force Power Off...");
        sara_power_off(10000);
    }

    DBG_print("Modem Power On...");
    if (sara_power_on(10000))
    {
        DBG_println(" success");
    }
    else
    {
        DBG_println(" fail");
        return false;
    }

    // Set GSM module baud rate
    // TinyGsmAutoBaud(SerialAT, GSM_AUTOBAUD_MIN, GSM_AUTOBAUD_MAX);
    SerialAT.begin(115200);
    // delay(6000);

    // Restart takes quite some time
    // To skip it, call init() instead of restart()
    DBG_println("Initializing modem...");
    // modem.restart();
    modem.init();

    // String modemInfo = modem.getModemInfo();
    // DBG_print("Modem Info: ");
    // DBG_println(modemInfo);

    // Unlock your SIM card with a PIN if needed
    if (strlen(GSM_PIN) && modem.getSimStatus() != 3)
    {
        DBG_println("Unlock SIM");
        modem.simUnlock(GSM_PIN);
    }

    DBG_print("Waiting for network...");
    ret = modem.waitForNetwork();
    if (ret)
    {
        DBG_println(" success");
    }
    else
    {
        DBG_println(" fail");
    }

    if (ret)
    {
        // GPRS connection parameters are usually set after network registration
        DBG_print("Connecting to ");
        DBG_print(apn);
        ret = modem.gprsConnect(apn, gprsUser, gprsPass);
        if (ret)
        {
            DBG_println(" success");
        }
        else
        {
            DBG_println(" fail");
        }
    }

    return ret;
}

void modem_stop(void)
{
    DBG_println("GPRS disconnect");
    modem.gprsDisconnect();

    DBG_println("Modem Power Off");
    sara_power_off(10000);

    // Disable UART
    SerialAT.end();
}

bool get_area_cellid(void)
{
    MCC = MNC = TAC = CID = -1;

    modem.sendAT("+CEREG=2");
    modem.waitResponse();
    modem.sendAT("+CEREG?");
    int resp = modem.waitResponse("+CEREG: 1", "+CEREG: 2,1"); // wait stat 1 registered
    if (resp == 1 || resp == 2)
    {
        String tac, ci;
        if (modem.stream.find('"'))
        {
            tac = modem.stream.readStringUntil('"');
        }
        if (modem.stream.find('"'))
        {
            ci = modem.stream.readStringUntil('"');
        }
        modem.waitResponse();
        TAC = strtol(tac.c_str(), NULL, 16);
        CID = strtol(ci.c_str(), NULL, 16);
        DBG_printf("tac:%d\n", TAC);
        DBG_printf("ci:%d\n", CID);
    }

    modem.sendAT("+UCGED=2");
    modem.waitResponse();

    modem.sendAT("+UCGED?");
    resp = modem.waitResponse("+UCGED:");
    if (resp == 1)
    {
        String mcc, mnc;
        modem.stream.find(','); // skip 1st value
        mcc = modem.stream.readStringUntil(',');
        mnc = modem.stream.readStringUntil(',');
        modem.waitResponse();
        MCC = strtol(mcc.c_str(), NULL, 10);
        MNC = strtol(mnc.c_str(), NULL, 10);
        DBG_printf("MCC:%d\n", MCC);
        DBG_printf("MNC:%d\n", MNC);
    }

    return MCC >= 0 && MNC >= 0 && TAC >= 0 && CID >= 0;
}

bool get_cell_position(long mcc, long mnc, long lac, long cellid)
{
    // http://opencellid.org/cell/get?key=xxx&mcc=260&mnc=2&lac=10250&cellid=26511&format=AT

    DBG_println("Cell Position Get request... ");
    http2.connectionKeepAlive(); // Currently, this is needed for HTTPS

    String url = "/cell/get";
    url += "?key=";
    url += opencellid_token;
    url += "&mcc=";
    url += mcc;
    url += "&mnc=";
    url += mnc;
    url += "&lac=";
    url += lac;
    url += "&cellid=";
    url += cellid;
    url += "&format=AT";
    DBG_println(url);

    http2.get(url);

    int status = http2.responseStatusCode();
    DBG_print("Response status code: ");
    DBG_println(status);

    Latitude = Longitude = "";

    if (status >= 0)
    {
        if (http2.skipResponseHeaders() >= 0)
        {
            // +Location:xxx,yyy,zzz
            if (http2.find("+Location:"))
            {
                Latitude = http2.readStringUntil(',');
                Longitude = http2.readStringUntil(',');
            }
            DBG_printf("lat %s lon %s\n", Latitude.c_str(), Longitude.c_str());
        }
    }

    // Shutdown
    http2.stop();

    return Latitude.length() >= 0 && Longitude.length() >= 0;
}

void line_notify(String message)
{
    DBG_println("LINE Notify request... ");
    http.connectionKeepAlive(); // Currently, this is needed for HTTPS

    String postData = "message=";
    postData += URLEncoder.encode(message);

    String authToken = "Bearer ";
    authToken += line_notify_token;

    http.beginRequest();
    http.post("/api/notify");
    http.sendHeader("Content-Type", "application/x-www-form-urlencoded");
    http.sendHeader("Content-Length", postData.length());
    http.sendHeader("Authorization", authToken);
    http.beginBody();
    http.print(postData);
    http.endRequest();

    int status = http.responseStatusCode();
    DBG_print("Response status code: ");
    DBG_println(status);

    // Shutdown
    http.stop();
}

bool sara_is_power_on()
{
    int rd = analogRead(SARA_V_INT);
    return rd > V_INT_ENABLE_LEVEL ? true : false;
}

bool sara_power_off(unsigned long timeout)
{
    pinMode(SARA_PWR_ON, OUTPUT);
    digitalWrite(SARA_PWR_ON, LOW);
    delay(2250);
    pinMode(SARA_PWR_ON, INPUT);
    unsigned long tstart = millis();
    do
    {
        int rd = analogRead(SARA_V_INT);
        if (rd < V_INT_DISABLE_LEVEL)
        {
            break;
        }
        delay(100);
        unsigned long curr = millis();
        if ((curr - tstart) > timeout)
        {
            break;
        }
    } while (1);
    digitalWrite(SARA_REG_EN, LOW);
    return true;
}

bool sara_power_on(unsigned long timeout)
{
    digitalWrite(SARA_REG_EN, HIGH);
    delay(10);
    pinMode(SARA_PWR_ON, OUTPUT);
    digitalWrite(SARA_PWR_ON, LOW);
    delay(225);
    pinMode(SARA_PWR_ON, INPUT);
    unsigned long tstart = millis();
    do
    {
        int rd = analogRead(SARA_V_INT);
        if (rd > V_INT_ENABLE_LEVEL)
        {
            break;
        }
        delay(100);
        unsigned long curr = millis();
        if ((curr - tstart) > timeout)
        {
            digitalWrite(SARA_REG_EN, LOW);
            return false;
        }
    } while (1);
    return true;
}
