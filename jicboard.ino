#include <ArduinoJson.h>

#include <IotWebConf.h>
#include <ESP8266HTTPClient.h>
#include <TaskScheduler.h>

// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "JICBoard";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "password";

// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "jic1"

#define STRING_LEN 128


// -- When CONFIG_PIN is pulled to ground on startup, the Thing will use the initial
//      password to buld an AP. (E.g. in case of lost password)
#define CONFIG_PIN D2

// -- Status indicator pin.
//      First it will light up (kept LOW), on Wifi connection it will blink,
//      when connected to the Wifi it will turn off (kept HIGH).
#define STATUS_PIN LED_BUILTIN

// -- Callback method declarations.
void configSaved();
void onWifiConnected();
boolean formValidator();

/** Fetches the trigger schedule from JIC remote API */
void fetchSchedule();

DNSServer dnsServer;
WebServer server(80);

char jicId[STRING_LEN];
// The fingerprint of the current play-link domain certifiacte. This will change, but I hope it does not matter as long as we do not verify it.
const char fingerprint[] = "991a881018a21c72b4ecc8aeb9b7b69facda4ea2";

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);
IotWebConfParameter jicIdParam = IotWebConfParameter("JIC ID", "jicId", jicId, STRING_LEN);


const int s1Pin = 5;
const int r1Pin = 14;
const int r2Pin = 12;
const int r3Pin = 13;
const int r4Pin = 15;


Scheduler runner;

Task tFetchSchedule(5000, TASK_FOREVER, &fetchSchedule);



void setup()
{

  pinMode(s1Pin, INPUT_PULLUP);
  pinMode(r1Pin, OUTPUT);
  pinMode(r2Pin, OUTPUT);
  pinMode(r3Pin, OUTPUT);
  pinMode(r4Pin, OUTPUT);
  digitalWrite(r1Pin, HIGH);
  digitalWrite(r2Pin, HIGH);
  digitalWrite(r3Pin, HIGH);
  digitalWrite(r4Pin, HIGH);

  runner.init();
  runner.addTask(tFetchSchedule);
  
  Serial.println(F("Initialized scheduler"));
  
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("Starting up..."));

#if LWIP_IPV6
  Serial.println(F("IPV6 is enabled\n"));
#else
  Serial.println(F("IPV6 is not enabled\n"));
#endif

  iotWebConf.setStatusPin(STATUS_PIN);
  iotWebConf.setConfigPin(CONFIG_PIN);
  iotWebConf.addParameter(&jicIdParam);
  iotWebConf.setConfigSavedCallback(&configSaved);
  iotWebConf.setFormValidator(&formValidator);
  iotWebConf.getApTimeoutParameter()->visible = true;
  //iotWebConf.setApTimeoutMs(1);
  iotWebConf.setWifiConnectionCallback(&onWifiConnected);
  // -- Initializing the configuration.
  iotWebConf.init();

  // -- Set up required URL handlers on the web server.
  server.on("/", handleRoot);
  server.on("/config", [] { iotWebConf.handleConfig(); });
  server.onNotFound([]() {
    iotWebConf.handleNotFound();
  });
  Serial.println("Ready.");
}


void loop()
{
  runner.execute();
  // -- doLoop should be called as frequently as possible.
  iotWebConf.doLoop();
}

/**
   Handle web requests to "/" path.
*/
void handleRoot()
{
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    return;
  }
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>JIC Board</title></head><body>JIC Board is successfully configured!";
  s += "<ul>";
  s += "<li>JIC ID: ";
  s += jicId;
  s += "</ul>";
  s += "Go to <a href='config'>configure page</a> to change values.";
  s += "</body></html>\n";

  server.send(200, "text/html", s);
}

void reportSuccess() {
  
}

void fetchSchedule() {
  Serial.println("Pulling Schedule.");
  const String host = "play-link.com";
  WiFiClientSecure client;
  client.setFingerprint(fingerprint);
  if (!client.connect(host, 443)) {
    Serial.println("connection failed");
    return;
  }
  String url = String("/jen/remote.php?id=") + jicId;

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: JICBoardArduino\r\n" +
               "Connection: close\r\n\r\n");

  while (client.connected()) {
    String line = client.readStringUntil('\n');
    Serial.println(line);
    if (line == "\r") {
      Serial.println(F("headers received"));
      break;
    }
  }
  
  // enough size for roughly 10 triggers according to https://arduinojson.org/v6/assistant/
  // If there are more scheduled triggers, the parsing fails 
  const size_t capacity = JSON_ARRAY_SIZE(10) + JSON_OBJECT_SIZE(2) + 10*JSON_OBJECT_SIZE(4) + (10 * 48) + 22;
  DynamicJsonDocument doc(capacity);
  
  DeserializationError error = deserializeJson(doc, client);
  if (error != DeserializationError::Ok) {
    Serial.println(F("failed to read JSON-Document"));
    return;
  }

  const unsigned long servertime = doc[F("servertime")].as<long>();
  const JsonArray triggers = doc[F("triggers")].as<JsonArray>();
  // Example: { "servertime": 1551782776, "triggers": [ { "start": 1551782865, "level": 50, "device": "lock", "duration": 2 } ]}
  for (unsigned short i = 0; i <= triggers.size(); ++i) {
    const JsonObject trigger = triggers[i].as<JsonObject>();
    const unsigned long start = trigger[F("start")].as<unsigned long>();
    const unsigned int duration = trigger[F("duration")].as<unsigned int>();
    const char* device = trigger[F("device")];
  }
  
  
  Serial.println(F("reply was:"));
  Serial.println(F("=========="));
  Serial.print(F("servertime: "));
  Serial.println(servertime);
  Serial.print(F("triggers: "));
  Serial.println(triggers.size());
  Serial.println(F("=========="));
  Serial.println(F("closing connection"));
  client.stop();
  
}

void onWifiConnected()
{
  Serial.println(F("Connected to WIFI."));
  tFetchSchedule.enable();
  Serial.println(F("Enabled scheduled fetch"));
}

void configSaved()
{
  Serial.println(F("Configuration was updated."));
}

boolean formValidator()
{
  Serial.println(F("Validating form."));
  boolean valid = true;

  int l = server.arg(jicIdParam.getId()).length();
  if (l != 9)
  {
    jicIdParam.errorMessage = "Please provide exactly nine characters for the JIC id!";
    valid = false;
  }

  return valid;
}
