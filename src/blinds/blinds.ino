// esp32-rf-blinds-controller - ESP32 controller for 433 MHz motorised blinds.
//
// Replaces the original remote by sending the same RF frames through a cheap
// 433 MHz transmitter module (FS1000A or similar). Commands can come from:
//
//   * USB serial - same 1-byte binary protocol as the original Arduino Nano
//                  version, so existing host scripts keep working.
//   * Wi-Fi      - a small web page at http://rfblinds.local/ and a REST API
//                  (/api/blinds, /api/blind?id=1&cmd=open) for home automation.
//
// See ../../README.md for wiring, configuration and the API reference.

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "rf_protocol.h"
#include "rf_transmitter.h"

using rfblinds::Command;

static RfTransmitter rf;
static WebServer server(80);
static bool debugMode = false;
static bool wifiEnabled = false;

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

static void setLed(bool on) {
#if STATUS_LED_PIN >= 0
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
#endif
}

static bool sendCommand(uint8_t blindId, Command cmd) {
  const uint32_t code = rfblinds::makeCode(blindId, cmd);
  setLed(true);
  const bool ok = rf.send(code, RF_REPEATS);
  setLed(false);
  if (debugMode) {
    Serial.printf("Blind ID 0x%02X  %-5s  code 0x%06lX  %s\n", blindId,
                  rfblinds::commandName(cmd), (unsigned long)code, ok ? "sent" : "FAILED");
  }
  return ok;
}

static bool sendAll(Command cmd) {
  bool ok = true;
  for (int i = 0; i < NUM_BLINDS; ++i) {
    ok &= sendCommand(BLINDS[i].id, cmd);
    delay(ALL_BLINDS_DELAY_MS);
  }
  return ok;
}

// ---------------------------------------------------------------------------
// USB serial protocol (unchanged from the Arduino Nano version)
//
//   one byte per command:  bit 7..6 = command (0 open, 1 stop, 2 close)
//                          bit 5..0 = blind ID (e.g. 0x11 for blind 1)
//   0xFF                   enables debug output
//
//   e.g. 0x11 = open blind 1, 0x51 = stop blind 1, 0x91 = close blind 1
// ---------------------------------------------------------------------------

static void handleSerial() {
  while (Serial.available() > 0) {
    const uint8_t b = Serial.read();
    if (b == 0xFF) {
      debugMode = true;
      Serial.println("Debug mode enabled");
      continue;
    }
    const uint8_t id = b & 0x3F;
    const uint8_t cmd = (b & 0xC0) >> 6;
    if (cmd > rfblinds::CMD_CLOSE) {
      if (debugMode) Serial.printf("Ignoring 0x%02X: unknown command %u\n", b, cmd);
      continue;
    }
    sendCommand(id, Command(cmd));
  }
}

// ---------------------------------------------------------------------------
// HTTP API and web page
// ---------------------------------------------------------------------------

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RF Blinds</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mut:#6b7280;--acc:#2f6fdb;--bd:#d9dde3}
@media (prefers-color-scheme:dark){:root{--bg:#14171c;--card:#1e232b;--fg:#e8ebf0;--mut:#9aa3af;--acc:#5b93f0;--bd:#323946}}
body{margin:0;font:16px system-ui,sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:520px;margin:0 auto;padding:16px}
h1{font-size:1.4em;margin:8px 0 16px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:12px 14px;margin-bottom:10px;
display:flex;align-items:center;gap:8px}
.card span{flex:1;font-weight:600}
button{font:inherit;border:1px solid var(--bd);background:var(--bg);color:var(--fg);border-radius:8px;
padding:8px 12px;cursor:pointer;min-width:64px}
button:active{background:var(--acc);color:#fff}
#st{color:var(--mut);font-size:.9em;min-height:1.2em}
</style></head><body><main>
<h1>RF Blinds</h1><div id="list"></div><p id="st"></p>
<script>
const st=document.getElementById('st');
function send(id,cmd){st.textContent='Sending...';
fetch('/api/blind?id='+id+'&cmd='+cmd,{method:'POST'}).then(r=>r.json())
.then(j=>st.textContent=j.ok?('Sent '+cmd+' to '+j.blind):('Error: '+j.error))
.catch(e=>st.textContent='Error: '+e)}
function row(id,name){const d=document.createElement('div');d.className='card';
const s=document.createElement('span');s.textContent=name;d.appendChild(s);
for(const [c,l] of [['open','Open'],['stop','Stop'],['close','Close']]){const b=document.createElement('button');
b.textContent=l;b.onclick=()=>send(id,c);d.appendChild(b)}return d}
fetch('/api/blinds').then(r=>r.json()).then(j=>{const L=document.getElementById('list');
j.blinds.forEach(b=>L.appendChild(row(b.index,b.name)));L.appendChild(row('all','All blinds'))});
</script></main></body></html>)HTML";

static bool authorised() {
  if (strlen(API_TOKEN) == 0) return true;
  if (server.arg("token") == API_TOKEN) return true;
  if (server.header("X-Token") == API_TOKEN) return true;
  server.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorised\"}");
  return false;
}

static void replyError(int status, const String& msg) {
  server.send(status, "application/json", "{\"ok\":false,\"error\":\"" + msg + "\"}");
}

static void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

static void handleList() {
  String json = "{\"blinds\":[";
  for (int i = 0; i < NUM_BLINDS; ++i) {
    if (i) json += ',';
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"index\":%d,\"name\":\"%s\",\"id\":\"0x%02X\"}", i + 1,
             BLINDS[i].name, BLINDS[i].id);
    json += buf;
  }
  json += "],\"commands\":[\"open\",\"stop\",\"close\"]}";
  server.send(200, "application/json", json);
}

// /api/blind?id=<1..N|all>&cmd=<open|stop|close>
// Alternatively ?code=0x11 addresses a raw blind ID that is not in the table.
static void handleBlind() {
  if (!authorised()) return;

  const Command cmd = rfblinds::parseCommand(server.arg("cmd").c_str());
  if (cmd == rfblinds::CMD_INVALID) return replyError(400, "cmd must be open, stop or close");

  const String id = server.arg("id");
  String target;
  bool ok;
  if (id.equalsIgnoreCase("all")) {
    ok = sendAll(cmd);
    target = "all";
  } else if (server.hasArg("code")) {
    const long raw = strtol(server.arg("code").c_str(), nullptr, 0);
    if (raw < 0 || raw > 0x3F) return replyError(400, "code must be 0x00..0x3F");
    ok = sendCommand(uint8_t(raw), cmd);
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%02lX", raw);
    target = buf;
  } else {
    const long idx = id.toInt();
    if (idx < 1 || idx > NUM_BLINDS) return replyError(400, String("id must be 1..") + NUM_BLINDS + " or all");
    ok = sendCommand(BLINDS[idx - 1].id, cmd);
    target = BLINDS[idx - 1].name;
  }

  server.send(ok ? 200 : 500, "application/json",
              String("{\"ok\":") + (ok ? "true" : "false") + ",\"blind\":\"" + target +
                  "\",\"cmd\":\"" + rfblinds::commandName(cmd) + "\"}");
}

static void setupNetwork() {
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("Wi-Fi not configured (see secrets.example.h) - USB serial only");
    return;
  }
  wifiEnabled = true;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to %s", WIFI_SSID);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected, IP %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("Not connected yet - will keep retrying in the background");
  }

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  }

  const char* headers[] = {"X-Token"};
  server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/blinds", HTTP_GET, handleList);
  server.on("/api/blind", HTTP_ANY, handleBlind);
  server.onNotFound([] { replyError(404, "not found"); });
  server.begin();

  if (strlen(OTA_PASSWORD) > 0) {
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
  }
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(SERIAL_BAUD);
#if STATUS_LED_PIN >= 0
  pinMode(STATUS_LED_PIN, OUTPUT);
  setLed(false);
#endif
  delay(200);
  Serial.println("\nesp32-rf-blinds-controller");

  if (!rf.begin(RF_TX_PIN, RF_UNIT_US, RF_GAP_US)) {
    Serial.println("ERROR: could not initialise the RF transmitter");
  }
  setupNetwork();
}

void loop() {
  handleSerial();
  if (wifiEnabled) {
    server.handleClient();
    if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.handle();
  }
  delay(1);
}
