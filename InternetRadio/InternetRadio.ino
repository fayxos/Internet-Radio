/**
 * 
 * Internet Radio
 * Seminarkurs IoT
 * Felix Haag & Henrik Bruder
 * 
 * PAP: 
 * 
 * Funktionen:
 * - Verbinden mit WLAN aus gespeicherten Netzwerken (Station Mode)
 * - Falls keine Verbindung möglich, verbinden mit neuen Netzwerk über SAP und Website (neue Netzwerke werden gespeichert)
 * - Auswahl von Sendern per Dreh-Encoder (56 Sender)
 * - Starten / Stoppen des Streams
 * - Einstellung der Lautstärke per Dreh-Encoder
 * - Anzeige des aktuellen Senders, sowie der Beschreibung des Senders (z.B. Titel und Interpret)
 * - Speichern des letzten Senders und Lautstärke
 * 
 * Website:
 * - Liste aller Sender
 * - Auswahl eines Senders
 * - Start/Stop des Streams
 * - Anzeige des aktuellen Senders, sowie der Beschreibung des Senders (z.B. Titel und Interpret)
 * - Verbinden mit neuem WLAN
 * 
 * (App)
 * 
 */


// Bibliotheken
// ============

#include "Arduino.h"

// Bibliotheken für WLAN Verbindungen / Webserver
#include "WiFi.h"
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ESPmDNS.h>
#include <AsyncElegantOTA.h>
#include <HTTPClient.h>

#include "AiEsp32RotaryEncoder.h" // Auslesen des Dreh-Encoders

#include <Arduino_JSON.h> // Speicherung von Daten in JSON Format (Erklärung weiter unten)
#include <SPIFFS.h> // Schreiben/Lesen auf/vom Speichern des ESP über SPIFFS
#include "FS.h" // Verwendung des Datentyps "File"

#include <SPI.h> // SPI Datenbus für LCD
#include <U8g2lib.h> // Ansteuerung des LCD
#include "Audio.h" // Ansteuerung des Lautsprechers / Streamen der Radiosender

#include "index.h" // Code der Website
#include "images.h" // Code des StartScreen Bildes


// Pin-Belegungen
// ==============

// Pins für Lautsprecher
#define I2S_DOUT 33
#define I2S_BCLK 26
#define I2S_LRC 25

// Pins für Dreh-Encoder (Lautstärke)
#define VOLUME_ENCODER_A_PIN 34 //DT
#define VOLUME_ENCODER_B_PIN 32 //CLK
#define VOLUME_ENCODER_BUTTON_PIN 35 //SW
#define VOLUME_ENCODER_VCC_PIN -1

// Pins für Dreh-Encoder (Senderwahl)
#define SENDER_ENCODER_A_PIN 14 //DT
#define SENDER_ENCODER_B_PIN 27 //CLK
#define SENDER_ENCODER_BUTTON_PIN 13 //SW
#define SENDER_ENCODER_VCC_PIN -1

// Einstellung für Dreh-Encoder
#define ROTARY_ENCODER_STEPS 4


// Globale Varibalen
// =================

AiEsp32RotaryEncoder volumeEncoder = AiEsp32RotaryEncoder(VOLUME_ENCODER_A_PIN, VOLUME_ENCODER_B_PIN, VOLUME_ENCODER_BUTTON_PIN, VOLUME_ENCODER_VCC_PIN, ROTARY_ENCODER_STEPS); // Initialisierung des Dreh-Encoders für Lautstärke
AiEsp32RotaryEncoder senderEncoder = AiEsp32RotaryEncoder(SENDER_ENCODER_A_PIN, SENDER_ENCODER_B_PIN, SENDER_ENCODER_BUTTON_PIN, SENDER_ENCODER_VCC_PIN, ROTARY_ENCODER_STEPS); // Initialisierung des Dreh-Encoders für Senderwahl

U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, /* clock=*/ 0 /* Digital */ , /* data=*/ 23 /* SPI */, /* CS=*/ 15 /* SPI */, /* reset=*/ U8X8_PIN_NONE); // Deklaration und Pin-Zuweisung des LCD

Audio audio; // Deklaration des Audios
AsyncWebServer server(80); // Standardport ist 80


const char *ort = "hfradio"; // Name des ESP im Netzwerk / URL
String ssid = ""; // SSID des Netzwerks (zu Beginn leer)
String password = ""; // Passwort des Netzwerks (zu Beginn leer)


String currentSender; // Name des aktuellen Senders
String currentInfo; // Info des aktuellen Senders (Z.B. Titel und Interpret)
String splitInfo[4]; // Info in 4 einzelne Strings aufgeteilt für Anzeige auf dem Display in 4 Zeilen (falls zu lang für 1 Zeile / Titel in eine Zeile, Interpret in die nächste)


// Die folgenden Standardwerte werden im Setup an die zuletzt ausgewählten Einstellung angepasst und bleiben nur falls Laden der letzten Einstellung nicht funktioniert
int volume = 10; // Aktuelle Lautstärke (Standardmäßig 10)
int startVolume; // Lautstärke zu Beginn, wichtig für Einstellung des Dreh-Encoders

String currentChosenSender = "SWR3"; // Name des Senders, der aktuell bei Senderwahl angezeigt wird (Standardmäßig SWR3)
const int senderCount = 55; // Anzahl der Sender (56 - 1 weil von 0)
String senderTitles[senderCount+1]; // Array der größe 56, das alle Sendernamen enthält 

int currentSenderCount = 51; // Index des aktuell ausgewählten Senders im senderTitles Array, wichtig auswählen der Sender per Dreh-Encoders
int lastSenderCount = 51; // Index des zuletzt ausgewählten Senders im senderTitles Array, wichtig für zurücksetzten des Dreh-Encoders, falls kein neuer Sender ausgewählt wurde
int startCount; // Index des zu Beginn ausgewählten Senders, wichtig für Einstellung des Dreh-Encoders


unsigned long lastTime = 0; // Millisekunden, an denen der Dreh-Encoder das letzte mal gedreht wurde

boolean newConnection = false; // Gibt an, ob gerade versucht wird eine neue Verbindung zu einem Netzwerk herzustellen

boolean isSenderWahl = false; // Gibt an, ob gerade ein Sender per Dreh-Encoder ausgewählt wird
boolean isVolumeWahl = false; // Gibt an , ob gerade die Lautstärke per Dreh-Encoder eingestellt wird


// Speicherung von Daten in JSON-Format
// Erklärung: https://www.w3schools.com/js/js_json_intro.asp
JSONVar sender; // Aller Sendername und Links zum Stream im Format: { "SenderName1": "SenderLink1", "SenderName2": "SenderLink2", ... } (wird geladen aus SPIFFS)
JSONVar state; // Aktueller Sender, SenderCount und Lautstärke im Format: { "sender": "SWR3", "senderCount": "51", "volume": 10 } (wird geladen aus und gespeichert in SPIFFS)
JSONVar network; // Alle früheren verbundenen Netzwerke mit jeweiligem Passwort im Format: { "NetzwerkName1": "Passwort1", "NetzwerkName2": "Passwort2", ... }


// Helper-Metoden
// ==============

// NotFound page, falls url aufgerufen wird, die nicht existiert
void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}


// Funktion der Dreh-Encoder Bibliothek zum Auslesen des Dreh-Encoders (Lautstärke)
void IRAM_ATTR readVolumeEncoderISR()
{
  volumeEncoder.readEncoder_ISR();
}

// Funktion der Dreh-Encoder Bibliothek zum Auslesen des Dreh-Encoders (Senderwahl)
void IRAM_ATTR readSenderEncoderISR()
{
  senderEncoder.readEncoder_ISR();
}


// SETUP / LOOP
// ============

/**
 * SETUP 
 * 
 * - Start-Screen
 * - Starten aller Komponenten 
 * - Einstellung aller Komponenten
 * - Netzwerkkonfigurationen
 * - Verbinden zu alten Netzwerken 
 * - Starten der Homepage / Verbindungspage
 * - Laden der SPIFFS
 * - Erstellung der JSON Objekte
 */ 
void setup() {  
  Serial.begin(115200);

  u8g2.begin(); // LCD wird gestartet
  u8g2.setFontMode(1); 
  u8g2.setFont(u8g2_font_cu12_tr); // Standard Schriftgröße 

  showStartScreen();

  SPIFFS.begin(); // Starten der SPIFFS

  connectToSavedNetwork(); // Laden aller gespeicherten Netzwerke aus den SPIFFS und Versuch sich mit einem gespeicherten Netzwerk zu verbinden

  // Falls WLAN nicht verbunden wurde
  if(WiFi.status() != WL_CONNECTED) {
    WiFi.softAP("hfradio"); // Starten des Soft Access Point
    
    showInstructionsForConnection(false); // Anzeigen der Anleitung zum Verbinden mit neuem WLAN auf dem LCD
  }

  // Starten der Homepage
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    // Falls WLAN verbunden
    if(WiFi.status() == WL_CONNECTED) {
      request->send(200, "text/html", homepage); // Anzeigen der Homepage
    } 
    // Falls WLAN nicht verbunden
    else {
      request->send(200, "text/html", connectionPage); // Anzeigen der Verbindungspage
    }
  });

  // Wenn bei der Verbindungspage auf Verbinden gedrückt wird, wird diese URL aufgerufen mit Parameter ssid und password
  // z.B.: 
  // <IP>/volume?ssid=felix&password=12345678
  server.on("/connect", HTTP_GET, [](AsyncWebServerRequest *request){
    // Falls Parameter für Ssid und Passwort vorhanden sind
    if(request->hasParam("ssid") && request->hasParam("password")) {
      request->send(200, "text/html", "Connecting..."); // Connecting... wird angezeigt
      ssid = request->getParam("ssid")->value(); // Parameter wird in ssid Variable gespeichert
      password = request->getParam("password")->value(); // Parameter wird in password Variable gespeichert
    } 
    else {
      request->send(200, "text/html", "Error!"); // Error! wird angezeigt
    }
  });

  configureNetwork(); // Netzwerkkonfigurationen (MDNS, OTA, ...)

  // Solange nicht mit WLAN verbunden
  while(WiFi.status() != WL_CONNECTED) {
    connectToNewWifi(); // Mit neuem WLAN verbinden  
  }
  
  Serial.printf("IP-Adresse: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("URL: http://%s.local\n", WiFi.getHostname());
  
  spiffs(); // Laden aller SPIFFS (Bilder, die auf der Homepage angezeigt werden)

  loadSender(); // Laden der Sender aus den SPIFFS und speichern in JSON varibale
  loodRadioState(); // Laden der Einstellungen aus den SPIFFS und speichern in globalen Varibalen
  
  getter(); // Starten aller Links um aktuelle Informationen (Lautstärke, Sender, Info) zu erhalten (für Website und App)
  setter(); // Starten aller Links um Zustände (Lautstärke, Sender) zu ändern (für Website und App)
  
  configureEncoders(); // Einstellung der Dreh-Encoder

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT); // Zuweisung der Pins für den Lautsprecher
  audio.setVolume(volume); // Einstellung der anfänglichen Lautstärke
  audio.connecttohost(sender[currentSender]); // Starten des anfänglichen Radiostreams

  showInfo(); // Anzeigen der Informationen auf LCD
}


/**
 * LOOP
 * 
 * - Beenden der Lautstärkeänderung / Senderwahl nach bestimmter Zeit
 * - Auslesen der Dreh-Encoder
 * - Audio loop
 * - Verbinden zu neuem Netzwerk falls vorherige Verbindung abbricht
 * 
 */ 
void loop()
{
  rotary_loop(); // Auslesen der Dreh-Encoder
  audio.loop(); // Audio loop

  // Falls verbindung abbricht
  if(WiFi.status() != WL_CONNECTED && !newConnection) {
    newConnection = true; // Es wird versucht zu neuem Netzwerk zu verbinden
    
    WiFi.softAP("hfradio"); // Soft Access Point wird gestartet

    showInstructionsForConnection(true); // Anleitung für verbinden zu neuem Netzwerk
  }

  // Falls versucht wird mit neuem Netzwerk zu verbinden
  if(newConnection) {
    connectToNewWifi(); // Verbinden zu neuem Netzwerk
    audio.connecttohost(sender[currentSender]); // Starten des Radiostreams wenn wieder eine Verbindung zum WLAN besteht
  }
}


// Methoden
// ========

/*
 * Netzwerkkonfigurationen
 * 
 * - Hostname
 * - MDNS
 * - OTA
 * - Starten des Servers
 */
void configureNetwork() {
  WiFi.setHostname(ort); // Name des Gerätes im Netz

  // Starten des MDNS Service 
   if(!MDNS.begin(ort)) {
    Serial.println("Error setting up MDNS responder!");
    while(true) {
      delay(1000); 
    }  
  }

  server.onNotFound(notFound); // Setzten der notFound Page des Servers

  AsyncElegantOTA.begin(&server, "Nutzer", "Basisfach"); // Starten des OTA

  server.begin(); // Starten des Servers

  MDNS.addService("http", "tcp", 80); // Hinzufügen des MDNS Services  
}


/*
 * Einstellung der Dreh-Encoder
 * 
 * Wird im Setup aufgerufen
 */
void configureEncoders() {
  volumeEncoder.begin(); // Starten des Dreh-Encoders für Lautstärke
  volumeEncoder.setup(readVolumeEncoderISR); // Einstellung der Read Methode
  volumeEncoder.setBoundaries(-volume, 20-volume, false); // Einstellung des minimalen und maximalen Werts des Dreh-Encoders abhängig von anfänglicher Lautstärke
  volumeEncoder.setAcceleration(0); // keine Acceleration

  senderEncoder.begin(); // Starten des Dreh-Encoders für Senderwahl
  senderEncoder.setup(readSenderEncoderISR); // Einstellung der Read Methode
  senderEncoder.setBoundaries(-currentSenderCount, senderCount-currentSenderCount+1, true); // Einstellung des minimalen und maximalen Werts des Dreh-Encoders abhängig von anfänglichem Sender, mit loop
  senderEncoder.setAcceleration(0); // keine Acceleration  
}


/**
 * Anzeige des Start-Screens auf dem LCD
 * 
 * Wird zu Beginn im Setup aufgerufen
 */ 
void showStartScreen() {
  u8g2.drawBitmap(24, 0, G0F87L_BMPWIDTH/8, 64, bitmap_g0f87l); // StartScreen Bitmap
  
  u8g2.setDisplayRotation(U8G2_R3);
  u8g2.setCursor(17, 18);
  u8g2.print("I o T");
  
  u8g2.setDisplayRotation(U8G2_R1);
  u8g2.setCursor(10, 18);
  u8g2.print("H & F");

  u8g2.setDisplayRotation(U8G2_R0);
  u8g2.sendBuffer();
}

/**
 * Anzeige der Anleitung zum Verbinden eines neuen Netzwerks auf dem LCD
 * 
 * Wird zu Beginn im Setup aufgerufen, falls keine Verbindung zu gespeicherten Netzwerken hergestellt wurden,
 * oder wenn im Laufe des Programms die Verbindung abbricht
 * 
 * Parameter "lost" gibt an ob schon eine Verbindung zu einem Netzwerk bestand
 */ 
void showInstructionsForConnection(bool lost) {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_7x13B_tf);
  u8g2.clearBuffer();
  u8g2.setCursor(0,10);
  if(lost) u8g2.print("Lost Connection");
  else u8g2.print("Connection");
  
  u8g2.setFont(u8g2_font_simple1_te);
  u8g2.setCursor(0,25);
  u8g2.print("1. Connect to hfradio WiFi"); 
  u8g2.setCursor(0,35);
  u8g2.print("2. Go to http://hfradio.local"); 
  u8g2.setCursor(0,45);
  u8g2.print("3. Enter SSID and Password");
  u8g2.sendBuffer();
}


/**
 * Anzeige der Informarmationen auf dem LCD
 * 
 * Wird bei jeder Änderung des Senders, der Lautstärke oder der Informationen aufgerufen
 * 
 * - Standardmäßig Name des Senders und Senderinformationen
 * - Änderung der Lautstärke
 * - Auswahl der Sender
 */ 
void showInfo() {
    // Falls gerade ein Sender ausgewählt wird
    if(isSenderWahl) {

      u8g2.setFont(u8g2_font_7x13B_tf); // Einstellung der Schriftart
      u8g2.clearBuffer(); // Löschen des Display Inhalts
      u8g2.setCursor(0,15);
      u8g2.print("Senderwahl: ");
      
      // Falls Name des Senders kürzer als 19 Zeichen ist (aufs Display passt)
      if(currentChosenSender.length() < 19) {
        u8g2.setCursor(0,45);
        u8g2.print(currentChosenSender); // Name des aktuell ausgewählten Senders
      } 
      // Falls Name des Senders länger als 19 Zeichen ist (nicht aufs Display passt)
      else {
        // Zeilenumbruch am Leerzeichen
        String s = "";
        boolean newRow = true;
        for(int i=currentChosenSender.length()-1; i>=0; i--) {
          if(newRow && currentChosenSender[i] == ' ') {
            if(currentChosenSender.length()-s.length() < 19) {
                newRow = false;
                u8g2.setCursor(0,50);
                u8g2.print(s); // 2. Hälfte des Sendernamens
                s="";
            }  
          }
          s = currentChosenSender[i]+s;   
        }  

        u8g2.setCursor(0,39);
        u8g2.print(s); // 1. Hälfte des Sendernamens
      }
      
      u8g2.sendBuffer();
      
    } 
    // Falls gerade die Lautstärke geändert wird
    else if(isVolumeWahl) {
      u8g2.setFont(u8g2_font_7x13B_tf);
      u8g2.clearBuffer();
      u8g2.setCursor(0,15);
      u8g2.print("Lautstaerke: ");
      u8g2.setCursor(0,45);
      u8g2.print(volume);
      u8g2.sendBuffer();
    }
    // Falls Stream nicht läuft  
    else if(!audio.isRunning()) {
      u8g2.clearBuffer();
      u8g2.drawBitmap(44, 7, A58OF_BMPWIDTH/8, 48, bitmap_a58of); // Pause Bitmap
      u8g2.sendBuffer();
    }
    // Sonst normale Anzeige von Sendername und Info
    else {
      u8g2.setFont(u8g2_font_7x13B_tf);
      u8g2.clearBuffer();

      // Falls Name des Senders kürzer als 19 Zeichen ist (aufs Display passt)
      if(senderTitles[currentSenderCount].length() < 19) {
        u8g2.setCursor(0,9);
        u8g2.print(senderTitles[currentSenderCount]); // Sendername aus Array senderTitles an Stelle currentSenderCount
      }
      // Falls Name des Senders länger als 19 Zeichen ist (nicht aufs Display passt)
      else {
        // Zeilenumbruch am Leerzeichen
        String s = "";
        boolean newRow = true;
        for(int i=senderTitles[currentSenderCount].length()-1; i>=0; i--) {
          if(newRow && senderTitles[currentSenderCount][i] == ' ') {
            if(senderTitles[currentSenderCount].length()-s.length() < 19) {
                newRow = false;
                u8g2.setCursor(0,20); 
                u8g2.print(s); // 2. Hälfte des Sendernamens
                s="";
            }  
          }
          s = senderTitles[currentSenderCount][i]+s;   
        }  

        u8g2.setCursor(0,9);
        u8g2.print(s); // 1. Hälfte des Sendernamens
      }
      
      u8g2.setFont(u8g2_font_simple1_te);
      // Jeder String aus splitInfo wird in einer Zeile ausgegeben
      for(int i=0; i<4; i++) {
          u8g2.setCursor(0,32+10*i);
          u8g2.print(splitInfo[i]); 
      }

      u8g2.sendBuffer();
    }
}


/*
 * Laden der gespeicherten Netzwerke aus den SPIFFS und Versuch der Verbindung zu einem der gespeicherten Netzwerke
 * 
 * Wird zu Beginn im Setup aufgerufen
 */
void connectToSavedNetwork() {
  File n = SPIFFS.open("/networks.json", "r"); // Öffnen des network.json Dokuments aus den SPIFFS (enthält alle schonaml verbundenen Netzwerke mit Passwort)
  String str = n.readString(); // Auslesen des Dokuments
  n.close(); // Schließen des Dokuments
  network = JSON.parse(str); // Umwandeln des Dokumentinhalts und speichern in JSON Varibale

  /*
   * Ausgabe aller gespeicherten Nextwerke (nur zum Test)
  for(int saves = 0; saves<network.keys().length(); saves++ ) {
    String nN = JSON.stringify(network.keys()[saves]);
    Serial.println(nN);  
    String networkName = "";
    for(int i=0; i<nN.length(); i++) {
      if(nN[i] != '"') networkName += nN[i];
    }
    Serial.println(JSON.stringify(network[network.keys()[saves]]));
  }
  */
  
  int numSsid = WiFi.scanNetworks(); // Anzahl aller verfügbaren Netzwerke

  for(int thisNet = 0; thisNet < numSsid; thisNet++) { // Durchlaufen aller verfügbaren Netzwerke
    Serial.println(WiFi.SSID(thisNet)); // Name des Netzwerks an Stelle thisNet
    for(int saves = 0; saves<network.keys().length(); saves++ ) { // Durchlaufen aller gespeicherten Netzwerke
    
      // entfernen der " vom Netzwerk Name (Die Keys der JSON Varibale werden mit " ausgegeben, also z.B. "Wlan1" anstatt Wlan1)
      String nN = JSON.stringify(network.keys()[saves]);
      String networkName = "";
      for(int i=0; i<nN.length(); i++) {
        if(nN[i] != '"') networkName += nN[i];
      }
      
      // Überprügfen ob der Name des aktuelles Netzwerk gespeichert ist
      if(networkName == WiFi.SSID(thisNet))  {
         
        // entfernen der " vom Passwort (Wird mit " ausgegeben, also z.B. "Passwort1" anstatt Passwort1)
        String pwd = JSON.stringify(network[network.keys()[saves]]);
        String password = "";
        for(int i=0; i<pwd.length(); i++) {
          if(pwd[i] != '"') password += pwd[i];
        }
                   
        WiFi.mode(WIFI_STA); // WiFi Modus Station Mode
        WiFi.begin(networkName.c_str(), password.c_str()); // Versuchen mit gepeichertem Netzwerk Name und Password zu verbinden

        long timeout = millis(); 
        // Für maximal 8 Sekunden auf Verbinung warten
        while(WiFi.status() != WL_CONNECTED && millis() - timeout < 8000) {
          delay(500);
          Serial.print(".");
        }
      } 
    }
  }  
}


/*
 * Verbindung zu neuem Netzwerk
 * 
 * Wird zu Beginn im Setup aufgerufen, falls keine Verbindung zu gespeicherten Netzwerken hergestellt wurden,
 * oder wenn im Laufe des Programms die Verbindung abbricht
 */
void connectToNewWifi() {
  // Falls ssid und password leer sind wird die Methode abgebrochen (ist solange der Fall bis auf Website ssid und password eingegeben werden)
  if(ssid == "" && password == "") return;
  
  WiFi.mode(WIFI_STA); // Setzten des WiFi Modus auf Station Mode
  WiFi.begin(ssid.c_str(), password.c_str()); // versuchen mit eingegebener ssid und pwassword zu verbiden

  long timeout = millis();
  // Für maximal 8 Sekunden auf Verbinung warten
  while(WiFi.status() != WL_CONNECTED && millis() - timeout < 8000) {
    delay(500);
    Serial.print(".");
  }

  // Falls WLAN jetzt verbunden ist 
  if(WiFi.status() == WL_CONNECTED) {
    newConnection = false; // Es wird nicht mehr versucht mit neuem Netzwerk zu verbinden
    
    network[ssid] = password; // Speichern des neuen Netzwerks in JSON Varibale
    
    File f = SPIFFS.open("/networks.json", "w"); // Öffnen des netzwerk.json Dokuments (in dem alle Netzwerke gespeichert sind)
    String j = JSON.stringify(network); // Umwandlung der JSON Varibale in einen String
    f.print(j); // Schreiben des Strings in das Dokument
    f.close(); // Schließen des Dokuments

    showInfo(); // Anzeigen der Standartinformationen
  } 
  // Falls keine Verbindung zum Netzwerk hergestellt werden konnte (weil z.B. SSID oder Passwort falsch waren)
  else {
    WiFi.softAP("hfradio"); // erneutes Starten des Soft Access Point
  }

  ssid = ""; // Zurücksetzten der ssid
  password = ""; // Zurücksetzten des passwortes
}


/*
 * Speichern der aktuellen Sender und Lautstärke in den SPIFFS
 * 
 * Wird immer dann aufgerufen, wenn sich Lautstärke oder Sender ändern
 */
void saveRadioState() {
  File f = SPIFFS.open("/radioState.json", "w"); // Öffnen des radioState.json Dokuments
  String json = "{\n \"currentSender\": \"" + currentSender + "\", \n \"currentSenderCount\": " + currentSenderCount + ", \n \"volume\": " + volume + " \n}"; // Erstellung des JSON Strings mit den gegebenen Informationen
  f.print(json); // Schreiben des Strings ins Dokument
  f.close(); // Schließen des Dokuments  
}


/*
 * Laden der gespeicherten Sender und Lautstärke aus den SPIFFS und ZUweisung zu den entsprechenden Variblen
 *
 * Wird am Anfang im Setup aufgerufen
 */
void loodRadioState() {
  File f = SPIFFS.open("/radioState.json", "r"); // Öffnen des radioState.json Dokuments in dem die letzte Lautstärke und Sender gepeichert sind
  String s = f.readString(); // Auslesen des Dokuments
  f.close(); // Schließen des Dokuments

  state = JSON.parse(s); // Umwandlung und Speicherung in JSON Variable
  
  volume = state["volume"]; // Zuweisung der gespeicherten Lautstärke

  // Entfernen der " vom gespeicherten Sender
  s = JSON.stringify(state["currentSender"]);
  currentSender = "";
  for(int i=0; i<s.length(); i++) {
    if(s[i] != '"') currentSender += s[i]; // gespeicherten Sender char für char in currentSender schreiben
  }
  
  currentChosenSender = currentSender; // currentChosenSender wird auf aktuellen Sender gesetzt (für Senderwahl per Dreh-Encoder)
  currentSenderCount = state["currentSenderCount"]; // currentSenderCount wird auf gespeicherten Count gesetzt (für Senderwahl per Dreh-Encoder)
  lastSenderCount = currentSenderCount; // lasSenderCount wird auf aktuellen Sender Count gesetzt (für Senderwahl per Dreh-Encoder)


  startVolume = volume; // startVolume wird auf gespeicherte Lautstärke gesetzt (für Dreh-Encoder)
  startCount = currentSenderCount; // startCopund wird auf gespeicherte Sender gesetzt (für Dreh-Encoder)  
}


/*
 * Laden aller Sender aus den SPIFFS und Speichern in JSON Variable
 * 
 * Wird am Anfang im Setup aufgerufen
 */
void loadSender() {
  File f = SPIFFS.open("/sender.json", "r");  
  sender = JSON.parse(f.readString());
  JSONVar s = sender.keys();
  String r = JSON.stringify(s);
  r+="/";
  
  for(int i=0; i<senderCount+1; i++) {
    senderTitles[i] = "";
  }
  
  int i = 0;
  int j = 0;
  while((char)r[i] != '/') {
    if((char)r[i] == '[' || (char)r[i] == '"' || (char)r[i] == ']') {
      i++;
      continue;  
    }
    else if((char)r[i] == ',') {
      j++;
      i++;
      continue;
    }
    senderTitles[j] += (char)r[i];
    i++;
  }
}


/*
 * Auslesen der Rotary Encoder 
 * 
 * Wird durchgehend im Loop aufgerufen
 * 
 * VolumeEncoder:
 * - Drehen um Lautstärke zu ändern
 * - Nach 1s nicht gedreht wird Lautstärkeänderung beendet
 * 
 * SenderEncoder:
 * - Drehen um Sender anzuzeigen
 * - Drücken um aktuell angezeigten Sender auszuwählen
 * - Nach 3s nicht gedreht wird Senderauswahl beendet
 * 
 */
void rotary_loop()
{
  // Falls gerade die Lautstärke geändert wird und der Dreh-Encoder vor mehr als 1 Sekunde das letzte mal gedreht wurde
  if ((millis() - lastTime) > 1000 && isVolumeWahl)
  {
    isVolumeWahl = false; // Lautstärke wird nicht mehr geändert
    showInfo(); // Anzeigen der Standartinformationen
  }
  // Falls gerade ein Sender ausgewählt wird und der Dreh-Encoder vor mehr als 3 Sekunde das letzte mal gedreht wurde
  else if((millis() - lastTime) > 3000 && isSenderWahl) {
    isSenderWahl = false; // Sender wird nicht mehr ausgewählt
    currentSenderCount = lastSenderCount; // zurücksetzten auf vorherigen Sender
    showInfo(); // Anzeigen der Standartinformationen
  }
  
  // Falls VolumeEncode gedreht wird
  if (volumeEncoder.encoderChanged())
  {
    isVolumeWahl = true; // Lautstärke wird gerade geändert (für Anzeige auf LCD) 
    isSenderWahl = false; // Sender wird gerade nicht ausgewählt
    currentSenderCount = lastSenderCount; // Senderwahl wird zurückgesetzt (Falls davor gerade Sender ausgwählt wurden)
    volume = startVolume + volumeEncoder.readEncoder(); // Neue Lautstärke wird gespeichert (Startlautstärke + Wert des Dreh-Encoders, ergibt immer maximal 20 und minimal 0)
    audio.setVolume(volume);
    Serial.print("Volume: ");
    Serial.println(volume);
    saveRadioState(); // Änderung wird in SPIFFS gespeichert
    showInfo(); // Informationen werden auf LCD angezeigt
    lastTime = millis(); // Zeitpunkt zu dem gedreht wurde wird gespeichert
  }
  // Falls VolumeEncode gedrückt wird
  if (volumeEncoder.isEncoderButtonClicked())
  {
    volume_rotary_onButtonClick(); // Methode für Drücken
  }
  
  // Falls SenderEncode gedreht wird
  if (senderEncoder.encoderChanged())
  {    
    isSenderWahl = true; // Sender wird gerade ausgewählt (für Anzeige auf LCD) 
    isVolumeWahl = false; // Lautstärke wird gerade nicht geändert
    currentSenderCount = startCount + senderEncoder.readEncoder(); // Index des aktuell angezeigten Senders wird gespeichert (Startindex + Wert des Dreh-Encoders, ergibt immer maximal 55 und minimal 0)
    currentChosenSender = senderTitles[currentSenderCount]; // Name des aktuell angezeigten Senders wird gespeichert
    Serial.print("Sender: ");
    Serial.println(currentChosenSender); 
    showInfo(); // Informationen werden auf LCD angezeigt
    lastTime = millis(); // Zeitpunkt zu dem gedreht wurde wird gespeichert
  }
  // Falls SenderEncode gedrückt wird
  if (senderEncoder.isEncoderButtonClicked())
  {
    sender_rotary_onButtonClick(); // Methode für Drücken
  }
}


/*
 * Starten des aktuell ausgewählten Sender
 * 
 * Wird aufgerufen wenn SenderEncoder gedrückt wird
 */
void sender_rotary_onButtonClick()
{
  // Falls gerade ein Sender ausgewählt wird (nur wenn vor dem Drücken gedreht wurde)
  if(isSenderWahl) {
    // Falls der aktuell ausgewählte Sender nicht der aktuell laufende Sender ist
    if(currentSender != currentChosenSender) {
      currentSender = currentChosenSender; // neuer Sender wird gespeichert
      lastSenderCount = currentSenderCount; // lastSenderCount wird aktualisiert (dass beim nächsten drehen auch wieder von aktuellem Sender gestartet wird)
      Serial.println("Neuer Sender: " + currentSender);
      audio.connecttohost(sender[currentSender]); // Stream des neuen Senders wird gestartet
      saveRadioState(); // Änderungen werden in SPIFFS gespeichert
      isSenderWahl = false; // Senderwahl wird beendet
      showInfo(); // Informationen werden auf LCD angezeigt
    }
  }
}


/*
 * Starten / Stoppen des Streams
 * 
 * Wird aufgerufen wenn VolumeEncoders gedrückt wird
 */
void volume_rotary_onButtonClick()
{
  static unsigned long lastTimePressed = 0;
  //ignore multiple press in that time milliseconds
  if (millis() - lastTimePressed < 500)
  {
    return;
  }
  lastTimePressed = millis();

  audio.pauseResume(); // Starten / Stoppen des Streams
  showInfo(); // Anzeigen der Informationen
}


/*
 * Methode der Audio Bibliothek, die die Info des Radiostreams ausließt
 * 
 * Wird immer automatisch aufgerufen wenn sich die Senderinformationen ändern
 * 
 * - Filtert den StreamTitle (eigentliche Informationen wie Titel/Interpret) heraus (ansonsten kommen auch Informationen über z.B. die Geschwindigkeit des Streams)
 * - Splitet die Info an " / ", " - " oder ":" in mehrere Zeilen auf (weil so meistens Titel und Interpret oder andere Informationen angegeben werden, z.B. "Interpret / Liedname1" oder "Mehr Informationen auf unserer Homepage: swr3.de")
 * - Splitet die entstande Info nochmal in mehrere Zeilen auf, falls diese nicht aufs Display passen
 */
void audio_info(const char *info){
  
  boolean isInfo = false; // Gibt an, ob der gegebene String eine Info ist (StreamTitle="Info")
  String inf = "";
  for(int i=0; i<strlen(info); i++) {
    if(info[i] == '=') {
      if(inf == "StreamTitle") { // Kommt im String StreamTitle= vor, handelt es sich um eine Info 
          inf = "";
          isInfo = true;
      } else {
        return;  
      } 
    }
    else if(info[i] == '\'') continue;
    else inf += info[i];
  }

  // splitInfo wird geleert
  for(int i=0; i<4; i++) {
    splitInfo[i] = "";  
  }

  // Falls es eine Info ist
  if(isInfo) {
    int x = 0;
    bool newWord = false;
    bool dP = false;

    // Info an " / ", " - " oder ":" in mehrere Zeilen splitten
    for(int i=0; i<inf.length(); i++) {
      if(inf[i] == ' ') newWord = true;
      if(inf[i] == ':') dP = true;
      if(newWord && dP) x++;
      if(newWord && (inf[i]=='-' || inf[i]=='/' || inf[i+1]==' ')) {
        newWord = false;
        x++;
      } 
      else {
        if(inf[i] != ' ') {
          newWord = false;  
        }
        if(inf[i] != ':') {
          dP = false;  
        }
        if(inf[i] == ' ' && splitInfo[x].length() == 0) { }
        else {
          splitInfo[x] += inf[i];
        }
      }
    }

    // Automatische Zeilenumbrüche falls Strings zu lang sind und nicht aufs Display passen
    for(int i=0; i<3; i++) {
      // Ist Zeile zu lang?
      if(splitInfo[i].length() > 25) {
        String s = "";
        boolean newRow = true;
        for(int a=splitInfo[i].length(); a>=0; a--) {
          if(newRow && splitInfo[i][a] == ' ') {
            if(splitInfo[i].length() - s.length() < 26) {
              newRow = false;
              if(splitInfo[i+1].length() == 0) {
                splitInfo[i+1] = s;
              } else {
                splitInfo[i+1] = s + " / " + splitInfo[i+1];
              }
              s = "";  
            }  
          }
          s = splitInfo[i][a] + s;
        }
        splitInfo[i] = s;
      }  
    }
  }

  // Falls es eine info ist und diese nicht leer ist
  if(inf != "" && isInfo) {
    currentInfo = inf; // Aktualisieren der Info
    showInfo(); // Anzeigen der Informationen auf dem LCD
  }
}


/*
 * Starten aller Links, über die man Informationen abrufen kann (von Website und App)
 * 
 * - Test ob Verbindung zum ESP hergestellt werden kann (von App)
 * - Ob Radio gerade spielt oder nicht
 * - Aktueller Sender
 * - Aktuelle Lautstärke
 * - Aktuelle Info
 */
void getter() {
  // Überprüfung der Verbindung zu ESP
  server.on("/getConnectionStatus", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String response = "{\"isConnected\": true}";
        
    request->send(200, "text/json", response);

    Serial.println("Connection Status wurde abgefragt");
  });

  // Rückgabe, ob Radio gerade spielt oder nicht
  server.on("/getPlayingStatus", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String response;
    if(audio.isRunning()) {
      response = "{\"isPlaying\": true}";
    } else {
      response = "{\"isPlaying\": false}";
    }
        
    request->send(200, "text/json", response);

    Serial.println("Playing Status wurde abgefragt");
  });

  // Rückgabe des aktuellen Radiosenders
  server.on("/getCurrentRadioStation", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String response;
    response = "{\"station\": \"";
    response += currentSender;
    response += "\"}";
        
    request->send(200, "text/json", response);

    Serial.print("Aktueller Sender wurde abgefragt: ");
    Serial.println(currentSender);
  });

  // Rückgabe der aktuellen Info
  server.on("/getCurrentInfo", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String response;
    response = "{\"info\": \"";
    response += currentInfo;
    response += "\"}";
        
    request->send(200, "text/json", response);

    Serial.print("Aktueller Info wurde abgefragt: ");
    Serial.println(currentInfo);
  });

  // Rückgabe der aktuellen Lautstärke
  server.on("/getVolume", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String response;
    response = "{\"volume\": ";
    response += (String)audio.getVolume();
    response += "}";
        
    request->send(200, "text/json", response);

    Serial.println("Aktuelle Lautstärke wurde abgefragt");
  });
}


/*
 * Starten aller Links, über die man das Radio steuern kann (von Website und App)
 * 
 * - Ändern des Radiosenders
 * - Ändern der Lautstärke
 * - Stoppen / Starten des Streams
 */
void setter() {
  // Ändern des Radiosenders
  // <IP>/setRadioStation?s=SWR3
  server.on("/setRadioStation", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String input;
    if(request->hasParam("s")) {
      input = request->getParam("s")->value();

      // Überprüfung ob eingegebener Name in Sendern enthalten ist
      if(sender[input] != null) {
        currentSender = input;
        audio.connecttohost(sender[input]); // Verbindung zu neuem Stream
        
        for(int i=0; i<sender.keys().length(); i++) {
          if(sender.keys()[i] == input) {
            currentSenderCount = i;
            break;
          }
        }

        saveRadioState(); // Speichern der Änderungen in SPIFFS 
        showInfo(); // Anzeigen der Informationen
        Serial.print("Sender wurde auf ");
        Serial.print(input);
        Serial.println(" gesetzt.");

        request->send(200, "text/plain", "Sender wurde geändert.");
      } else {
        Serial.print("Ungültige Eingabe: ");
        Serial.println(input);
        request->send(200, "text/plain", "Ungültige Eingabe! (für eine Liste aller Sender gehe auf /sender)\nAchtung: Anstatt Leerzeichen verwende +!");
      }
    }
    request->send(200, "text/plain", "Um den Sender festzulegen, schreibe /setRadioStation?s=<name> (für eine Liste aller Sender gehe auf /sender)\nAchtung: Anstatt Leerzeichen verwende +!");
  });

  // Ändern der Lautstärke
  // <IP>/setVolume?v=10
  server.on("/setVolume", HTTP_GET, [] (AsyncWebServerRequest * request) {
    int input;
    if(request->hasParam("v")) {
      input = request->getParam("v")->value().toInt();

      // Überprüfen ob input zwischen 0 und 20 liegt
      if(input>=0 && input <=20) {
        audio.setVolume(input); // Ändern der Lautstärke
        saveRadioState(); // Speichern der Änderungen in SPIFFS
        
        Serial.print("Lautstärke wurde auf ");
        Serial.print(input);
        Serial.println(" gesetzt.");

        request->send(200, "text/plain", "Lautstärke wurde geändert.");
      } else {
        Serial.print("Ungültige Eingabe: ");
        Serial.println(input);
        request->send(200, "text/plain", "Ungültige Eingabe! (0-21)");
      }
    }
    request->send(200, "text/plain", "Um die Lautstärke zu ändern, schreibe /setVolume?v=<lautstärke> (0-21)");
  });

  // Starten / Stoppen des Streams
  // <IP>/pauseResume
  server.on("/pauseResume", HTTP_GET, [] (AsyncWebServerRequest * request) {
    audio.pauseResume(); // Starten / Stoppen des Streams
    showInfo();
    Serial.println("Radio wurde gestoppt/gestartet");
    request->send(200, "text/plain", "Radio wurde gestoppt/gestartet");
  });
}


/*
 * Zuweisung der in SPIFFS geladen Dateien/Bilder, damit diese von Website und App aufgerufen und eingebunden werden können
 * 
 * - Liste der Sender
 * - Bilder der Radiosender
 */
void spiffs() {
  server.on("/sender", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/sender.json", "text/json");
  });

  server.on("/default", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/default.webp", "image/webp");
  });

  server.on("/antenne1", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/antenne1.webp", "image/webp");
  });

  server.on("/antenne180er", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/antenne1.webp", "image/webp");
  });

  server.on("/antenne190er", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/antenne1.webp", "image/webp");
  });

  server.on("/antennebayern", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/antenneb.webp", "image/webp");
  });

  server.on("/antennebayern-chillout", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/antenneb.webp", "image/webp");
  });

  server.on("/antennebayern80erkulthits", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/antenneb.webp", "image/webp");
  });

  server.on("/antennebayerntop40", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/antenneb.webp", "image/webp");
  });

  server.on("/energystuttgart", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/energy.webp", "image/webp");
  });

  server.on("/swr1baden-wuerttemberg", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/swr1.webpng", "image/webp");
  });

  server.on("/swr3", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/swr3.webp", "image/webp");
  });
}


/*
 * Laden von Informationen aus dem Internet
 * 
 * Parameter "serverName" ist der Link von dem die Informationen geladen werden sollen
 * 
 * Wird aktuell nicht genutzt
 */
String httpGETRequest(const char* serverName) {
  WiFiClient client;
  HTTPClient http;
    
  // IP address with path
  http.begin(client, serverName);
    
  // Send HTTP POST request
  int httpResponseCode = http.GET();
  
  String payload = "{}"; 
  
  if (httpResponseCode>0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();

  return payload;
}
