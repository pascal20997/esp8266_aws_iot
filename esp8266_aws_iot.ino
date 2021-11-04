#include <DHT.h>
#include <DHT_U.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

using namespace std;

#define CONFIG_VERSION "1"

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
  String config_version;
  String wifi_ssid;
  String wifi_passphrase;
  uint8_t dht_type;
  uint8_t dht_pin;
  uint8_t reset_pin;

  char* aws_endpoint;
  BearSSL::X509List* iot_certificate_pem;
  BearSSL::PrivateKey* iot_private_key;

  String mqtt_publish_topic;
} esp_config;

// from https://docs.aws.amazon.com/iot/latest/developerguide/server-authentication.html#server-authentication-certs
const BearSSL::X509List amazon_root_certificate("-----BEGIN CERTIFICATE-----"
"MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF"
"ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6"
"b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL"
"MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv"
"b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj"
"ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM"
"9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw"
"IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6"
"VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L"
"93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm"
"jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC"
"AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA"
"A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI"
"U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs"
"N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv"
"o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU"
"5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy"
"rqXRfboQnoZsG4q5WTP468SQvvG5"
"-----END CERTIFICATE-----");

const String html_header = "<html dir=\"ltr\" lang=\"en-US\"><head><meta charset=\"utf-8\"><title>ESP8266 kronova.net</title><meta name=\"viewport\" content=\"width=device-width,initial-scale=1,maximum-scale=1.0,user-scalable=no,viewport-fit=cover\" /></head><body>";
const String html_footer = "</body></html>";

String html_page_additional_content = "";

const String configuration_home_html = html_header + "<h1>Welcome to ESP8266 WiFi Module</h1>" + html_page_additional_content + "<p>Press start to configure your ESP</p><p><a href=\"/configure\">Start</a></p>" + html_footer;

DHT_Unified *dht;

WiFiClientSecure *wifi_client;
PubSubClient *pub_sub_client;

String readStringFromEeprom() 
{
  Serial.print("Read EEPROM from " + String(read_position)+ "...");
  String value = "";
  int position = read_position;
  bool read_error = false;
  for (position; position < 4096; position++) {
    char current_character = EEPROM.read(position);
    if (current_character == '\0') {
      // end of current string
      Serial.println("Found end of string!");
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
  esp_config.config_version = readStringFromEeprom();
  if (esp_config.config_version != CONFIG_VERSION) {
    Serial.print("Application config version is ");
    Serial.print(CONFIG_VERSION);
    Serial.print(" but config has ");
    Serial.println(esp_config.config_version);
    html_page_additional_content = "<p alert=\"alert alert-info\">Configuration options have been updated! Please configure your ESP again!</p>";
    return false;
  }

  esp_config.wifi_ssid = readStringFromEeprom();
  esp_config.wifi_passphrase = readStringFromEeprom();
  esp_config.dht_type = readStringFromEeprom().toInt();
  esp_config.dht_pin = readStringFromEeprom().toInt();
  esp_config.reset_pin = readStringFromEeprom().toInt();

  String aws_endpoint = readStringFromEeprom();
  char* aws_endpoint_char;
  aws_endpoint.toCharArray(aws_endpoint_char, aws_endpoint.length() + 1);
  esp_config.aws_endpoint = aws_endpoint_char;
  
  const char* iot_certificate_pem(readStringFromEeprom().c_str());
  esp_config.iot_certificate_pem = new BearSSL::X509List(iot_certificate_pem);
  const char* iot_private_key(readStringFromEeprom().c_str());
  esp_config.iot_private_key = new BearSSL::PrivateKey(iot_private_key);

  esp_config.mqtt_publish_topic = readStringFromEeprom();

  if (
    esp_config.wifi_ssid != ""
    && esp_config.wifi_passphrase != ""
    && esp_config.dht_type
    && esp_config.dht_pin
    && esp_config.reset_pin
    && esp_config.aws_endpoint != ""
    && iot_certificate_pem != ""
    && iot_private_key != ""
    && esp_config.mqtt_publish_topic
  ) {
    return true;
  }
  Serial.println("Configuration is not complete!");
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
  + "<h1>Configure your ESP</h1>" + html_page_additional_content +
  "<form method=\"post\" action=\"/save\">"
  "<fieldset><label for=\"wifi-ssid\">Select your Network<label><select name=\"wifi_ssid\" id=\"wifi-ssid\" required>" + get_wifi_stations_html() + "</select></fieldset>"
  "<fieldset><label for=\"wifi-passphrase\">Network Passphrase (leave empty if network is unsecured)</label><input type=\"password\" name=\"wifi_passphrase\" id=\"wifi-passphrase\" required /></fieldset>"
  "<fieldset><label for=\"dht-type\">DHT Type</label><select name=\"dht_type\" id=\"dht-type\" required>" + get_dht_types_html() + "</select></fieldset>"
  "<fieldset><label for=\"dht-pin\">DHT Pin</label><input name=\"dht_pin\" type=\"number\" min=\"0\" max=\"99\" value=\"5\" required/></fieldset>"
  "<fieldset><label for=\"reset-pin\">Reset (Wake) Pin</label><input name=\"reset_pin\" type=\"number\" min=\"0\" max=\"99\" value=\"16\" required/></fieldset>"
  "<fieldset><label for=\"aws-endpoint\">AWS Endpoint</label><input name=\"aws_endpoint\" placeholder=\"<random-stuff>.iot.eu-central-1.amazonaws.com\" required/></fieldset>"
  "<fieldset><label for=\"iot-certificate-pem\">AWS IoT Thing Device Certificate</label><textarea name=\"iot_certificate_pem\" rows=\"10\" cols=\"50\" required></textarea></fieldset>"
  "<fieldset><label for=\"iot-private-key\">AWS IoT Thing Private Key File</label><textarea name=\"iot_private_key\" rows=\"10\" cols=\"50\" required></textarea></fieldset>"
  "<fieldset><label for=\"mqtt-publish-topic\">AWS Endpoint</label><input name=\"mqtt_publish_topic\" placeholder=\"$aws/things/myname/shadow\" required/></fieldset>"
  "<fieldset><label for=\"submit\">Save your Changes to Continue</label><button type=\"submit\">Save changes</submit>"
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
  Serial.print("...ok (Write position: "); Serial.print(write_position); Serial.println(")");
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

  if (
    server.hasArg("wifi_ssid")
    && server.hasArg("wifi_passphrase")
    && server.hasArg("dht_type")
    && server.hasArg("dht_pin")
    && server.hasArg("reset_pin")
    && server.hasArg("aws_endpoint")
    && server.hasArg("iot_certificate_pem")
    && server.hasArg("iot_private_key")
    && server.hasArg("mqtt_publish_topic")
  ) {
    
    // reset eeprom to prevent bad segements
    for (int i = 0; i < 4096; i++) {
      EEPROM.write(i, 0);
    }

    writeConfiguration(CONFIG_VERSION);
    writeConfiguration(server.arg("wifi_ssid"));
    writeConfiguration(server.arg("wifi_passphrase"));
    writeConfiguration(server.arg("dht_type"));
    writeConfiguration(server.arg("dht_pin"));
    writeConfiguration(server.arg("reset_pin"));
    writeConfiguration(server.arg("aws_endpoint"));
    writeConfiguration(server.arg("iot_certificate_pem"));
    writeConfiguration(server.arg("iot_private_key"));
    writeConfiguration(server.arg("mqtt_publish_topic"));
  } else {
    response_code = 403;
    response_html = "<p>Your configuration is invalid! Submit your changes using the configuration form!</p>";
  }

  commitConfiguration();
  server.send(response_code, "text/html", response_html);

  Serial.println("Restart ESP...");
  ESP.restart();
}

bool setup_wifi()
{
  Serial.print("Connect to wireless network '" + esp_config.wifi_ssid + "'");
  WiFi.begin(esp_config.wifi_ssid, esp_config.wifi_passphrase);
  int checks = 0;
  while (checks < 10 && WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    html_page_additional_content = "<p class=\"alert alert-danger\">ERROR: Could not connect to wireless network '" + esp_config.wifi_ssid + "'!</p>";
    return false;
  }
  Serial.println(" CONNECTED");
  return true;
}

void setup_dht()
{
    Serial.print("DHT Pin: ");
    Serial.println(esp_config.dht_pin);
    Serial.print("DHT Type: ");
    Serial.println(esp_config.dht_type);
    dht = new DHT_Unified(esp_config.dht_pin, esp_config.dht_type);
    dht->begin();
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

// adapted from github.com/HarringayMakerSpace
void setCurrentTime()
{
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: "); Serial.print(asctime(&timeinfo));
}

void messageReceived(char* topic, unsigned char* payload, unsigned int length)
{
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void setup_iot()
{
  wifi_client = new WiFiClientSecure();
  pub_sub_client = new PubSubClient(esp_config.aws_endpoint, 8883, *wifi_client);
  pub_sub_client->setCallback(messageReceived);
  wifi_client->setClientRSACert(esp_config.iot_certificate_pem, esp_config.iot_private_key);
  wifi_client->setTrustAnchors(&amazon_root_certificate);
  setCurrentTime();
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  EEPROM.begin(4096);

  if (!fetchConfiguration() || !setup_wifi()) {
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
    setup_dht();
    setup_iot();
  }
}

void loop_connect()
{
    if (!pub_sub_client->connected()) {
      const char* thing_name = "basement_laundry"; // todo: config!!
      Serial.print("Connect to device");
      Serial.print(thing_name);
      Serial.print(" ");
      while (!pub_sub_client->connected()) {
        Serial.print(".");
        if (!pub_sub_client->connect(thing_name)) {
          Serial.println("!");
          Serial.print("Can not connect to MQTT. ERROR: ");
          Serial.println(pub_sub_client->state());
        }
        
      }
      Serial.println(" CONNECTED");
    }
}

void loop_publishMeasurement()
{
    sensors_event_t event;
    dht->temperature().getEvent(&event);
    
    if (isnan(event.temperature) || isnan(event.relative_humidity)) {
      Serial.println("Could not read values from DHT!");
      return;
    }

    String message = "{\"temperature\":" + String(event.temperature) + ",\"humidity\":" + String(event.relative_humidity) + "}";
    Serial.print("Publish latest data to topic '"); Serial.print(esp_config.mqtt_publish_topic); Serial.println("'...");
    pub_sub_client->publish(esp_config.mqtt_publish_topic.c_str(), message.c_str());
}

void loop()
{
  if (current_mode == mode_unconfigured) {
    server.handleClient();
  } else {
    delay(5000);
    loop_connect();
    loop_publishMeasurement();
  }
}
