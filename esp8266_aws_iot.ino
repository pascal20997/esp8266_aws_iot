#include <DHT.h>
#include <DHT_U.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>

using namespace std;

const String access_point_ssid = "ESP8266 First Configuration";
const String access_point_passphrase = "kronova.net";

// which mode should the arduino run?
const int mode_unconfigured = 0;
const int mode_configured = 1;
int current_mode;

String dht_types[] = {"22", "21"};

ESP8266WebServer server(80);

int read_position = 0;
int write_position = 0;

struct ESP_CONFIG {
  // position of properties == position in eeprom!
  String wifi_ssid;
  String wifi_passphrase;
  uint8_t dht_type;
  uint8_t dht_pin;
  uint8_t reset_pin;
} esp_config;

const String html_header = "<html dir=\"ltr\" lang=\"en-US\"><head><meta charset=\"utf-8\"><title>ESP8266 kronova.net</title><meta name=\"viewport\" content=\"width=device-width,initial-scale=1,maximum-scale=1.0,user-scalable=no,viewport-fit=cover\" /></head><body>";
const String html_footer = "</body></html>";

const String configuration_home_html = html_header + "<h1>Welcome to ESP8266 WiFi Module</h1><p>Press start to configure your ESP</p><p><a href=\"/configure\">Start</a></p>" + html_footer;

DHT_Unified *dht;

String readStringFromEeprom() 
{
  Serial.print("Read EEPROM from " + String(read_position)+ "...");
  String value = "";
  int position = read_position;
  int end_position = read_position + 200; // prevents endless loop and limitates string to max 200 chars
  bool read_error = false;
  for (position; position < end_position; position++) {
    char current_character = EEPROM.read(position);
    if (current_character == '\0') {
      // end of current string
      Serial.println("Found end of string!");
      break;
    } else if (isWhitespace(current_character)) {
      Serial.println("Whitespace but no termination of string!");
      read_error = true;
      break;
    }
    value += current_character;
  }
  read_position = position + 2;
  Serial.println("ok");
  Serial.println("Fetched value: " + value);
  return read_error ? "" : value;
}

bool fetchConfiguration()
{
  esp_config.wifi_ssid = readStringFromEeprom();
  esp_config.wifi_passphrase = readStringFromEeprom();
  esp_config.dht_type = readStringFromEeprom().toInt();
  esp_config.dht_pin = readStringFromEeprom().toInt();
  esp_config.reset_pin = readStringFromEeprom().toInt();

  if (esp_config.wifi_ssid && esp_config.wifi_passphrase && esp_config.dht_type && esp_config.dht_pin && esp_config.reset_pin) {
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
    html += "<option value=\"" + dht_types[i] + "\"> DHT" + dht_types[i] + "</option>";
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

void writeConfiguration(String value) 
{
  Serial.println("Write \"" + value + "\" to address " + write_position + "...");
  Serial.print("Writing: ");
  int position = write_position;
  for (position; position < write_position + value.length(); position++) {
    EEPROM.write(position, value[position - write_position]);
    Serial.print(value[position - write_position]);
  }
  EEPROM.write(++position, '\0');
  write_position = position + 1;
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
    writeConfiguration(server.arg("wifi_ssid"));
    writeConfiguration(server.arg("wifi_passphrase"));
    writeConfiguration(server.arg("dht_type"));
    writeConfiguration(server.arg("dht_pin"));
    writeConfiguration(server.arg("reset_pin"));
  } else {
    response_code = 403;
    response_html = "<p>Your configuration is invalid! Submit your changes using the configuration form!</p>";
  }

  commitConfiguration();
  server.send(response_code, "text/html", response_html);

  Serial.println("Restart ESP...");
  ESP.restart();
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
    Serial.println("Mode configured...");
    dht = new DHT_Unified(esp_config.dht_pin, esp_config.dht_type);
    sensor_t sensor;

    dht->temperature().getSensor(&sensor);
    Serial.println(F("------------------------------------"));
    Serial.println(F("Temperature Sensor"));
    Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
    Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
    Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
    Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("°C"));
    Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("°C"));
    Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("°C"));
    Serial.println(F("------------------------------------"));

    dht->humidity().getSensor(&sensor);
    Serial.println(F("Humidity Sensor"));
    Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
    Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
    Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
    Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("%"));
    Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("%"));
    Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("%"));
    Serial.println(F("------------------------------------"));
  }
}

void loop() {
  if (current_mode == mode_unconfigured) {
    server.handleClient();
  } else {
    delay(5000);

    // Get temperature event and print its value.
    // TODO: Send measured data to AWS IoT
    sensors_event_t event;
    dht->temperature().getEvent(&event);
    if (isnan(event.temperature)) {
      Serial.println(F("Error reading temperature!"));
    }
    else {
      Serial.print(F("Temperature: "));
      Serial.print(event.temperature);
      Serial.println(F("°C"));
    }
    // Get humidity event and print its value.
    dht->humidity().getEvent(&event);
    if (isnan(event.relative_humidity)) {
      Serial.println(F("Error reading humidity!"));
    }
    else {
      Serial.print(F("Humidity: "));
      Serial.print(event.relative_humidity);
      Serial.println(F("%"));
    }
  }
}
