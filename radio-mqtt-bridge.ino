//?? eliminare spazi e doppie righe bianche

#include <FS.h>  //this needs to be first, or it all crashes and burns...
#include <printf.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <RF24_config.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

// Builtin LED blink configs
#define RESET_LED_BLINKS 20
#define RESET_LED_DELAY 50
#define NRF24L01_CONFIG_LED_BLINKS 5
#define WIFI_LED_BLINKS            4
#define MQTT_LED_BLINKS            3
#define NRF24L01_MSG_LED_BLINKS    2
#define LED_ON_SHORT_DELAY         100
#define LED_ON_LONG_DELAY          1000
#define LED_OFF_DELAY              300

char   mDNSHostname[40]      = "";
char   mqttServerAddress[40] = "";
char   mqttPort[6]           = "1883";
char   mqttTopic[40]         = "";
int    wiFiConnectMaxRetries = 5;

char   datarateId[3]         = "2";
char   palevelId[3]          = "3";
char   radioChannel[6]       = "116";
char   payloadSize[3]        = "8";

#define RESET_TO_FACTORY_PIN D2
/* 
 * NodeMCU D3  <-> NRF24L01+ CSN
 * NodeMCU D8  <-> NRF24L01+ CE
 * NodeMCU D5  <-> NRF24L01+ SCK
 * NodeMCU D6  <-> NRF24L01+ MISO
 * NodeMCU D7  <-> NRF24L01+ MOSI
 * NodeMCU 3V3 <-> NRF24L01+ VCC
 */
RF24 nrf24l01(D3, D8);
#define LOG_LINES 35

bool   mqttEnabled           = false;
byte   radioAddresses[][6]   = {"0"};
String logDashedSegment      = "------------ ";
bool   mqttWasConnected      = false;
String devName;
WiFiManager wifiManager;
AsyncMqttClient mqttClient;

WiFiManagerParameter mDNSHostnameParam("hostname", "MDNS hostname", "", 40);
WiFiManagerParameter mqttText("<p style='text-align:center'>MQTT SETTINGS</p>");
WiFiManagerParameter mqttServerAddressParam("server", "MQTT server", "", 40);

WiFiManagerParameter mqttPortParam("mqttport", "MQTT port", "", 40);
WiFiManagerParameter mqttTopicParam("topic", "MQTT topic", "", 40);
WiFiManagerParameter nrf24l01Text("<p style='text-align:center'>NRF24L01 SETTINGS</p>");
WiFiManagerParameter radioChannelParam("radiochannel", "NRF24L01 radio channel", radioChannel, 40);
// Data rate combo
const char *datarateComboStr = R"(
<label for='datarate'>NRF24L01 data rate</label>
<select id="datarate" onchange="document.getElementById('dataratehidden').value = this.value">
  <option value="0">1 Mbps</option>
  <option value="1">2 Mbps</option>
  <option value="2">250 kbps</option>
</select>
<script>
  document.getElementById('datarate').value = "%d";
</script>
)";
char *datarateComboStrBuffer  = (char *)malloc(sizeof(char)*700);
WiFiManagerParameter datarateCombo(datarateComboStrBuffer);
WiFiManagerParameter datarateIdParam("dataratehidden", "", datarateId, 2);
WiFiManagerParameter payloadSizeParam("payloadsize", "NRF24L01 payload size", payloadSize, 40);
//Power amplifier level combo
const char *palevelComboStr = R"(
<label for='palevel'>NRF24L01 power amplifier level</label>
<select id="palevel" onchange="document.getElementById('palevelhidden').value = this.value">
  <option value="0">-18 dBm</option>
  <option value="1">-12 dBm</option>
  <option value="2">-6 dBm</option>
  <option value="3">0 dBm</option>
</select>
<script>
  document.getElementById('palevel').value = "%d";
</script>
)";
char *palevelComboStrBuffer = (char *)malloc(sizeof(char)*700);
WiFiManagerParameter palevelCombo(palevelComboStrBuffer);
WiFiManagerParameter palevelIdParam("palevelhidden", "", palevelId, 2);
const char *scriptStr = R"(
<script>
  function isPositiveInteger(str) {
    if (typeof str !== 'string') {
      return false;
    }
    const num = Number(str);
    if (Number.isInteger(num) && num > 0) {
      return true;
    }
    return false;
  }
  function checkAndCorrectVal(obj) {
    id = obj.getAttribute('id');
    currValTrim = obj.value.trim()
    obj.value = currValTrim
    if ((currValTrim == '') || !isPositiveInteger(currValTrim)) {
        correctedVal = '';
        if (id == 'radiochannel')
            correctedVal = 116;
        else if (id == 'payloadsize')
            correctedVal = 8;
        else if ((id == 'mqttport') && (currValTrim == ''))
            correctedVal = currValTrim;
        else if ((id == 'mqttport') && !isPositiveInteger(currValTrim))
            correctedVal = 1883;
        obj.value = correctedVal;
    }
    if ((id == 'payloadsize') && (obj.value > 32))
      obj.value = 32;
  }
  document.getElementById("mqttport").addEventListener('blur', (event) => { checkAndCorrectVal(event.target) });
  document.getElementById("radiochannel").addEventListener('blur', (event) => { checkAndCorrectVal(event.target) });
  document.getElementById("payloadsize").addEventListener('blur', (event) => { checkAndCorrectVal(event.target) });
  document.querySelector("[for='dataratehidden']").style = "display: none; height: 0px;";
  document.getElementById('dataratehidden').style = "display: none; height: 0px;";
  document.querySelector("[for='palevelhidden']").style = "display: none; height: 0px;";
  document.getElementById('palevelhidden').style = "display: none; height: 0px;";
</script>
)";
WiFiManagerParameter scriptParam(scriptStr);

char msgLogCircularBuf[LOG_LINES][100] = {0};
String msgLog = "";
const char *logHTMLTags = R"(
  <html>
    <body>
      <div id="loggerdiv" style="overflow-y: scroll; font-size: 1em; font-family: monospace; border: 1px solid black;"></div>
    </body>
    <script>
      var updateTime = 1000
      var getUrl = window.location;
      var baseUrl = getUrl.protocol + '//' + getUrl.host + '/';
      function updateLog() {
        fetch(baseUrl + "loggercontent").then(x => x.text()).
          then((resp) => { document.getElementById("loggerdiv").innerHTML = resp; });
        setTimeout(updateLog, updateTime);
      }
      updateLog();
    </script>
  </html>
)";

void log(const char *str) {
  static unsigned currLogLine = 0;
  static int circularCounter;
  strcpy(msgLogCircularBuf[currLogLine++], str);
  msgLog = "";
  for (unsigned k = 0; k < LOG_LINES; k++) {
    circularCounter = (circularCounter + 1) % LOG_LINES;
    msgLog += String(msgLogCircularBuf[circularCounter]) + "<br>";
  }
  circularCounter = (circularCounter + 1) % LOG_LINES;
  if (currLogLine == LOG_LINES)
    currLogLine = 0;
  Serial.println(str);
}

void bindServerCallback(){
  wifiManager.server->on("/logger", handleLoggerPageRoute);
  wifiManager.server->on("/loggercontent", handleLoggerContentRoute);
}

void handleLoggerContentRoute(){
  wifiManager.server->send(200, "text", msgLog.c_str());
}

void handleLoggerPageRoute(){
  wifiManager.server->send(200, "text/html", logHTMLTags);
}

void loadNrf24l01AndMqttConfig() {

  String logStr = logDashedSegment + "RESTORE SAVED CONFIG:";
  log(logStr.c_str());
  if (SPIFFS.begin()) {
    if (SPIFFS.exists("/config.json")) {
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        if (!deserializeError) {
#else
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        if (json.success()) {
#endif
          strcpy(mqttServerAddress, json["mqtt_server"]);
          strcpy(mqttPort, json["mqtt_port"]);
          strcpy(mqttTopic, json["mqtt_topic"]);
          strcpy(mDNSHostname, json["mdns_hostname"]);
          strcpy(radioChannel, json["radio_channel"]);
          strcpy(datarateId, json["datarate_id"]);
          strcpy(payloadSize, json["payload_size"]);
          strcpy(palevelId, json["palevel_id"]);

          logStr = "MQTT server address: " + String(mqttServerAddress);
          log(logStr.c_str());
          logStr = "MQTT port: " + String(mqttPort);
          log(logStr.c_str());
          logStr = "MQTT topic: " + String(mqttTopic);
          log(logStr.c_str());
          logStr = "MDNS hostname: " + String(mDNSHostname);
          log(logStr.c_str());
          logStr = "Radio channel: " + String(radioChannel);
          log(logStr.c_str());
          //TODO: convert ID to value
          logStr = "Data rate ID: " + String(datarateId);
          log(logStr.c_str());
          logStr = "Payload size: " + String(payloadSize);
          log(logStr.c_str());
          //TODO: convert ID to value
          logStr = "Power amplifier level ID: " + String(palevelId);
          log(logStr.c_str());
        } else {
          log("Could not load json config");
        }
      }
      else {
        log("Could not open config file");
      }
      configFile.close();
    } else {
      log("Nothing to restore");
    }
  } else {
    Serial.println("Failed to mount FS");
  }
  log("");

}

void copyValFromParam(char *dst, unsigned paramIndex) {

  String buf = wifiManager.getParameters()[paramIndex]->getValue();
  buf.trim();
  strcpy(dst, buf.c_str());

}

void saveNrf24l01AndMqttConfig() {

  log("Saving config");
  copyValFromParam(mDNSHostname, 0);
  copyValFromParam(mqttServerAddress, 2);
  copyValFromParam(mqttPort, 3);
  copyValFromParam(mqttTopic, 4);
  copyValFromParam(radioChannel, 6);
  copyValFromParam(datarateId, 8);
  copyValFromParam(payloadSize, 9);
  copyValFromParam(palevelId, 11);

#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
  DynamicJsonDocument json(1024);
#else
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
#endif

  json["mdns_hostname"] = mDNSHostname;
  json["mqtt_server"]   = mqttServerAddress;
  json["mqtt_port"]     = mqttPort;
  json["mqtt_topic"]    = mqttTopic;
  json["radio_channel"] = radioChannel;
  json["datarate_id"]   = datarateId;
  json["payload_size"]  = payloadSize;
  json["palevel_id"]    = palevelId;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    log("Failed to open config file for writing");
    return;
  }

#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
  //serializeJson(json, Serial);
  serializeJson(json, configFile);
#else
  //json.printTo(Serial);
  json.printTo(configFile);
#endif*/

  configFile.close();

  initNrf24l01();
  initMqtt();

}

void saveWiFiCallback() {

  Serial.println("WiFi params saved");
  wifiManager.reboot();

}

void blink(unsigned repeatNum, unsigned millisecsOn, unsigned millisecsOff, unsigned millisecsDelayAfter) {

  for (unsigned i = 0; i < repeatNum; i++) {
    digitalWrite(BUILTIN_LED, LOW);
    delay(millisecsOn);
    digitalWrite(BUILTIN_LED, HIGH);
    delay(millisecsOff);
  }
  delay(millisecsDelayAfter);

}

void setup() {

  //Uncomment and execute once in case of troubles with SPIFFS
  //SPIFFS.format();

  String logStr = "";

  pinMode(RESET_TO_FACTORY_PIN, INPUT_PULLUP);
  pinMode(BUILTIN_LED, OUTPUT);
  char id[7];
  snprintf (id, 7, "%06x", ESP.getChipId()) & 0xFFFFFF;
  devName = "rmb-" + String(id);

  Serial.begin(115200);
  log("");
  strcpy(mDNSHostname, devName.c_str());
  strcpy(mqttTopic, devName.c_str());
  loadNrf24l01AndMqttConfig();

  /*
     INIT WIFI-MANAGER
  */
  wifiManager.setSaveConfigCallback(saveWiFiCallback);
  wifiManager.setConfigPortalBlocking(false);
  // It will redirect to the log page
  wifiManager.setWebServerCallback(bindServerCallback);
  wifiManager.setDebugOutput(false);

  // set custom html menu content , inside menu item "custom"
  const char* loggerHtml = "<form action='/logger' method='get'><button>Log</button></form><br/>\n";
  wifiManager.setCustomMenuHTML(loggerHtml);

  std::vector<const char *> menu = {"wifi","info","param","custom", "sep","restart","exit"};
  wifiManager.setSaveParamsCallback(saveNrf24l01AndMqttConfig);
  wifiManager.setMenu(menu);
  wifiManager.setDarkMode(true);
  wifiManager.setTitle("RADIO MQTT BRIDGE");

  sprintf(datarateComboStrBuffer, datarateComboStr, atoi(datarateId));
  sprintf(palevelComboStrBuffer, palevelComboStr, atoi(palevelId));
  wifiManager.addParameter(&mDNSHostnameParam);
  wifiManager.getParameters()[0]->setValue(mDNSHostname, 40);
  wifiManager.addParameter(&mqttText);
  wifiManager.addParameter(&mqttServerAddressParam);
  wifiManager.getParameters()[2]->setValue(mqttServerAddress, 40);
  wifiManager.addParameter(&mqttPortParam);
  wifiManager.getParameters()[3]->setValue(mqttPort, 40);
  wifiManager.addParameter(&mqttTopicParam);
  wifiManager.getParameters()[4]->setValue(mqttTopic, 40);
  wifiManager.addParameter(&nrf24l01Text);
  wifiManager.addParameter(&radioChannelParam);
  wifiManager.getParameters()[6]->setValue(radioChannel, 40);
  wifiManager.addParameter(&datarateCombo);
  wifiManager.addParameter(&datarateIdParam);
  wifiManager.getParameters()[8]->setValue(datarateId, 40);
  wifiManager.addParameter(&payloadSizeParam);
  wifiManager.getParameters()[9]->setValue(payloadSize, 40);
  wifiManager.addParameter(&palevelCombo);
  wifiManager.addParameter(&palevelIdParam);
  wifiManager.getParameters()[11]->setValue(palevelId, 40);
  wifiManager.addParameter(&scriptParam);

  /*
   * CONNECT WIFI
   */
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  logStr = logDashedSegment + "WIFI INFO:";
  log(logStr.c_str());
  bool wiFiConnected = false;
  String savedSSID = WiFi.SSID();
  if (savedSSID != "") {
    while (wiFiConnectMaxRetries-- > 0) {
      if (WiFi.status() == WL_CONNECTED) {
        wiFiConnected = true;
        wifiManager.startWebPortal();
        break;
      }
      delay(3000);
    }
  }

  if (!wiFiConnected) {
    WiFi.mode(WIFI_AP);
    blink(1, 5000, 0, 2000);
    wifiManager.startConfigPortal(devName.c_str());
    log("Access point mode ON");
    logStr = "IP: " + WiFi.softAPIP().toString();
    log(logStr.c_str());
    logStr = "SSID: " + devName;
  } else {
    logStr = "Connected to: " + savedSSID;
    log(logStr.c_str());
    logStr = "IP address: " + WiFi.localIP().toString();
  }
  log(logStr.c_str());
  log("");

  /*
     INIT NRF24L01
  */
  initNrf24l01();

  /*
   *  START MDNS
   */
  String mDNSHostnameStr = String(mDNSHostname);
  mDNSHostnameStr.trim();
  if (String(mDNSHostname) != "")
    MDNS.begin(mDNSHostname);

  /*
   * INIT MQTT
   */
  initMqtt();

  log("******** START LOOP ********");
  log("");
}

void initNrf24l01() {

  byte addresses[][6] = {"0"};
  nrf24l01.begin();
  String logStr;

  rf24_datarate_e datarate;
  switch (atoi(datarateId))
  {
    case 0:
      datarate = RF24_1MBPS;
      break;
    case 1:
      datarate = RF24_2MBPS;
      break;
    case 2:
      datarate = RF24_250KBPS;
      break;
    default:
      break;
  }

  logStr = logDashedSegment + "SETUP NRF24L01:";
  log(logStr.c_str());
  nrf24l01.setChannel(atoi(radioChannel));
  nrf24l01.setDataRate(datarate);
  nrf24l01.setPayloadSize(atoi(payloadSize));
  nrf24l01.setPALevel(atoi(palevelId));
  // in case it's already listening
  nrf24l01.stopListening();

  nrf24l01.openReadingPipe(1, addresses[0]);
  nrf24l01.startListening();
  log("NRF24L01 configured");
  String res[4];
  unsigned ledOnDelay = LED_ON_SHORT_DELAY;
  res[0] = nrf24l01.getChannel() == atoi(radioChannel) ? "OK" : "KO";
  logStr = "Channel config check: " + res[0];
  log(logStr.c_str());
  res[1] = nrf24l01.getDataRate() == datarate ? "OK" : "KO";
  logStr = "DataRate config check: " + res[1];
  log(logStr.c_str());
  res[2] = nrf24l01.getPayloadSize() == atoi(payloadSize) ? "OK" : "KO";
  logStr = "Payload size config check: " + res[2];
  log(logStr.c_str());
  res[3] = nrf24l01.getPALevel() == atoi(palevelId) ? "OK" : "KO";
  logStr = "Power amplifier level config check: " + res[3];
  log(logStr.c_str());
  log("");

  for (unsigned i = 0; i < 4; i++)
    if (res[i] == "KO")
      ledOnDelay = LED_ON_LONG_DELAY;
  blink(NRF24L01_CONFIG_LED_BLINKS, ledOnDelay, LED_OFF_DELAY, 2000);

}

void initMqtt() {

  mqttWasConnected = false;
  if (mqttClient.connected()) {
    mqttClient.disconnect();
  }
  if (mqttServerAddress[0] != '\0') {
    mqttClient.setServer(mqttServerAddress, atoi(mqttPort));
    mqttEnabled = true;
  } else {
    mqttEnabled = false;
  }
  String logStr = logDashedSegment + "SETUP MQTT:";
  log(logStr.c_str());
  logStr = mqttEnabled ? "MQTT ENABLED" : "MQTT DISABLED";
  log(logStr.c_str());
  if (mqttEnabled) {
    logStr = "MQTT Server address: " + String(mqttServerAddress) + ":" + mqttPort;
    log(logStr.c_str());
    //will connect automatically in loop()
  }
  log("");

}

void loop() {

  static unsigned long timer = 0;
  static bool wifiConnected = false;
  String logStr = "";
  static char radioMsg[32];

  wifiManager.process();

  if (wifiConnected && (WiFi.status() != WL_CONNECTED)) {
    blink(WIFI_LED_BLINKS, LED_ON_LONG_DELAY, LED_OFF_DELAY, 2000);
    wifiConnected = false;
    logStr = "NOT connected to " + WiFi.SSID();
    log(logStr.c_str());
  } else if (!wifiConnected && (WiFi.status() == WL_CONNECTED)) {
    blink(WIFI_LED_BLINKS, LED_ON_SHORT_DELAY, LED_OFF_DELAY, 2000);
    wifiConnected = true;
    logStr = "Connected to " + WiFi.SSID();
    log(logStr.c_str());
  }

  if (mqttEnabled) {
    if (mqttClient.connected()) {
      if (!mqttWasConnected) {
        log("Connected to MQTT server");
        blink(MQTT_LED_BLINKS, LED_ON_SHORT_DELAY, LED_OFF_DELAY, 2000);
        mqttWasConnected = true;
      }
    } else {
      if (mqttWasConnected) {
        log("Not connected to MQTT server");
        mqttWasConnected = false;
        blink(MQTT_LED_BLINKS, LED_ON_LONG_DELAY, LED_OFF_DELAY, 2000);
      }
      //attempting to connect
      mqttClient.connect();
    }
  }

  if (!digitalRead(RESET_TO_FACTORY_PIN)) {
    if (!timer) {
      timer = millis();
    }
    if ((millis() - timer) > 3000) {
      log("Reset settings");
      blink(RESET_LED_BLINKS, RESET_LED_DELAY, RESET_LED_DELAY, 2000);
      SPIFFS.format();
      wifiManager.resetSettings();
      ESP.restart();
    }
  } else {
    timer = 0;
  }

  uint8_t pipe;
  if (nrf24l01.available(&pipe)) {
    uint8_t bytes = nrf24l01.getPayloadSize();
    while (nrf24l01.available()) {
      nrf24l01.read( &radioMsg, bytes );
    }
    blink(NRF24L01_MSG_LED_BLINKS, LED_ON_SHORT_DELAY, LED_OFF_DELAY, 0);
    logStr = "Received " + String(bytes) + " bytes on pipe " + String(pipe) + ": ";
    for (unsigned i = 0; i < atoi(payloadSize); i++)
      logStr += radioMsg[i];
    log(logStr.c_str());
    if (mqttClient.connected())
      mqttClient.publish(mqttTopic, 0, true, radioMsg);
  }

}
