#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>


using namespace std;

const String access_point_ssid = "ESP8266 First Configuration";
const String access_point_passphrase = "kronova.net";

// which mode should the arduino run?
const int mode_unconfigured = 0;
const int mode_configured = 1;
int current_mode;

String dht_types[] = {"DHT22", "DHT21"};

ESP8266WebServer server(80);

// initialize with fetchConfiguration()
String wifi_ssid;
String wifi_passphrase;
String dht_type;
int dht_pin;
int reset_pin;

// addresses in eeprom
int wifi_ssid_address = 0;
int wifi_passphrase_address = 60;
int dht_type_address = 180;
int dht_pin_address = 182;
int reset_pin_address = 184; // length: 2

const String html_header = "<html dir=\"ltr\" lang=\"en-US\"><head><meta charset=\"utf-8\"><title>ESP8266 kronova.net</title><meta name=\"viewport\" content=\"width=device-width,initial-scale=1,maximum-scale=1.0,user-scalable=no,viewport-fit=cover\" /></head><body>";
const String html_footer = "</body></html>";

const String configuration_home_html = html_header + "<h1>Welcome to ESP8266 WiFi Module</h1><p>Press start to configure your ESP</p><p><a href=\"/configure\">Start</a></p>" + html_footer;

// TODO: Use special char at the end of each string and check that in here
// Check like: if (current_character == "@") { end_char_position = position }
// Then at the and check the last position of that char and remove the rest of the string!
// Important: Need to calculate each address +1 because of that end char!
String readConfigurationItem(int start_address, int end_address) 
{
  Serial.print("Read EEPROM from " + String(start_address) + " to " + String(end_address) + "...");
  String value = "";
  for (int position = start_address; position < end_address; position++) {
    char current_character = EEPROM.read(position);
    if (isWhitespace(current_character)) {
      // too much?
      break;
    }
    value += current_character;
  }
  Serial.println("ok");
  Serial.println("Fetched value: " + value);
  return value;
}

bool fetchConfiguration()
{
  wifi_ssid = readConfigurationItem(wifi_ssid_address, wifi_passphrase_address - wifi_ssid_address);
  wifi_passphrase = readConfigurationItem(wifi_passphrase_address, dht_type_address - wifi_passphrase_address);
  dht_type = readConfigurationItem(dht_type_address, dht_pin_address - dht_type_address);
  dht_pin = readConfigurationItem(dht_pin_address, reset_pin_address - dht_pin_address).toInt();
  reset_pin = readConfigurationItem(reset_pin_address, reset_pin_address + 2).toInt();

  if (wifi_ssid && wifi_passphrase && dht_type && dht_pin && reset_pin) {
    return true;
  }
  return false;
}

String get_wifi_stations_html()
{
  String ssid;
  int32_t rssi;
  uint8_t encryptionType;
  uint8_t* bssid;
  int32_t channel;
  bool hidden;
  int scanResult;

  String html = "";

  Serial.println("Scan networks...");
  scanResult = WiFi.scanNetworks(false, true);
  if (scanResult > 0) {
    for (int8_t i = 0; i < scanResult; i++) {
      WiFi.getNetworkInfo(i, ssid, encryptionType, rssi, bssid, channel, hidden);
      Serial.printf(PSTR("  %02d: [CH %02d] [%02X:%02X:%02X:%02X:%02X:%02X] %ddBm %c %c %s\n"),
                    i,
                    channel,
                    bssid[0], bssid[1], bssid[2],
                    bssid[3], bssid[4], bssid[5],
                    rssi,
                    (encryptionType == ENC_TYPE_NONE) ? ' ' : '*',
                    hidden ? 'H' : 'V',
                    ssid.c_str());
      //yield();
      html += "<option value=\"" + ssid + "\">" + ssid + " (Channel: " + channel + ")</option>";
    }
  }

  return html;
}

String get_dht_types_html()
{
  int amount_of_types = sizeof(dht_types) / sizeof(dht_types[0]);
  String html = "";

  Serial.print("Amount of types: ");
  Serial.println(amount_of_types);

  for (int i = 0; i < amount_of_types; i++) {
    html += "<option value=\"" + dht_types[i] + "\">" + dht_types[i] + "</option>";
  }

  return html;
}

void handleConfigurationHome()
{
  server.send(200, "text/html", configuration_home_html);
}

void handleConfigurationForm()
{
  server.send(200, "text/html", html_header
  + "<h1>Configure your ESP</h1>"
  "<form method=\"post\" action=\"/save\">"
  "<fieldset><label for=\"wifi-ssid\">Select your network<label><select name=\"wifi_ssid\" id=\"wifi-ssid\">" + get_wifi_stations_html() + "</select></fieldset>"
  "<fieldset><label for=\"wifi-passphrase\">Network passphrase (leave empty if network is unsecured)</label><input type=\"password\" name=\"wifi_passphrase\" id=\"wifi-passphrase\" /></fieldset>"
  "<fieldset><label for=\"dht-type\">DHT type</label><select name=\"dht_type\" id=\"dht-type\">" + get_dht_types_html() + "</select></fieldset>"
  "<fieldset><label for=\"dht-pin\">DHT pin</label><input name=\"dht_pin\" type=\"number\" min=\"0\" max=\"99\" value=\"5\" /></fieldset>"
  "<fieldset><label for=\"reset-pin\">Reset (wake) pin</label><input name=\"reset_pin\" type=\"number\" min=\"0\" max=\"99\" value=\"16\" /></fieldset>"
  "<fieldset><label for=\"submit\">Save your changes to continue</label><button type=\"submit\">Save changes</submit>"
  "</form>"
  + html_footer);
}

void writeConfiguration(int address, String value) 
{
  Serial.println("Write \"" + value + "\" to address " + address + "...");
  Serial.print("Writing: ");
  for (int position = address; position < address + value.length(); position++) {
    EEPROM.write(position, value[position - address]);
    Serial.print(value[position - address]);
  }
  Serial.println("...ok");
}

void commitConfiguration()
{
  if (!EEPROM.commit()) {
    Serial.println("ERROR! Could not commit EEPROM changes!");
  }
}

void handleConfigurationSave()
{
  int response_code = 200;
  String response_html = "<p>Changes saved successfully! Reset the ESP to boot into configured mode!</p>";

  if (server.hasArg("wifi_ssid") && server.hasArg("wifi_passphrase") && server.hasArg("dht_type") && server.hasArg("dht_pin") && server.hasArg("reset_pin")) {
    writeConfiguration(wifi_ssid_address, server.arg("wifi_ssid"));
    writeConfiguration(wifi_passphrase_address, server.arg("wifi_passphrase"));
    writeConfiguration(dht_type_address, server.arg("dht_type"));
    writeConfiguration(dht_pin_address, server.arg("dht_pin"));
    writeConfiguration(reset_pin_address, server.arg("reset_pin"));
  } else {
    response_code = 403;
    response_html = "<p>Your configuration is invalid! Submit your changes using the configuration form!</p>";
  }

  commitConfiguration();
  server.send(response_code, "text/html", response_html);

  // try a reset using the reset pin
  int reset_pin = server.arg("reset_pin").toInt();
  Serial.println("Use reset pin to reset esp...");
  pinMode(reset_pin, OUTPUT);
  digitalWrite(reset_pin, HIGH);
}


void setup() {
  delay(1000);
  Serial.begin(115200);
  EEPROM.begin(512);

  if (!fetchConfiguration()) {
    current_mode = mode_unconfigured;
    Serial.print("ESP is unconfigured. Start with configuring the ESP using the WiFi Network: ");
    Serial.println(access_point_ssid);
    WiFi.softAP(access_point_ssid, access_point_passphrase);
    Serial.print("Access point IP: ");
    Serial.println(WiFi.softAPIP());
    
    server.on("/", handleConfigurationHome);
    server.on("/configure", handleConfigurationForm);
    server.on("/save", handleConfigurationSave);

    server.begin();
    Serial.println("HTTP server started");
  } else {
    current_mode = mode_configured;
  }
}

void loop() {
  if (current_mode == mode_unconfigured) {
    server.handleClient();
  } else {

  }
}
