// the familly i never had — M5Stack PaperColor firmware v0.8
// A persistent subject, a new invented household every night. See README.md.
#define setup archivedStorageSetup
#define loop archivedStorageLoop
#include "sd_image_bridge.h"
#undef setup
#undef loop
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_system.h>
#include <time.h>
#if __has_include("wifi.h")      // optional, local only; see wifi.h.example
#include "wifi.h"
#endif

WebServer portal(80);
DNSServer portalDNS;
Preferences networkPrefs;
static bool apActive=false;
static bool connectionPending=false;
static bool onlineShown=false;
static bool saveOnConnect=false;
static uint32_t connectStarted=0,portalStarted=0,closePortalAt=0;
static String setupSSID,setupPassword,csrfToken,pendingSSID,pendingPassword;
static String networkState="unconfigured";

static String randomHex(size_t count) {
  String result; result.reserve(count);
  static const char hex[]="0123456789abcdef";
  for (size_t i=0;i<count;++i) result+=hex[esp_random()&15];
  return result;
}

// E-ink status cards: book serif, the title as a small header, one red rule.
static const lgfx::IFont* fitFont(const String& text,int maxWidth) {
  canvas.setFont(&fonts::FreeSerifItalic18pt7b);
  if(canvas.textWidth(text)<=maxWidth)return &fonts::FreeSerifItalic18pt7b;
  return &fonts::FreeSerifItalic12pt7b;
}

static void drawTitleMark(int y) {
  canvas.setTextColor(BLACK);canvas.setTextDatum(top_center);
  canvas.setFont(&fonts::FreeSerif9pt7b);
  canvas.drawString("THE FAMILLY I NEVER HAD",200,y);
  canvas.fillRect(186,y+28,28,2,canvas.color888(191,0,0));
}

static void message(const String& first,const String& second) {
  canvas.fillSprite(WHITE);
  drawTitleMark(56);
  const lgfx::IFont* font=fitFont(first.length()>second.length()?first:second,360);
  canvas.setFont(font);canvas.setTextColor(BLACK);canvas.setTextDatum(middle_center);
  canvas.drawString(first,200,284);canvas.drawString(second,200,font==&fonts::FreeSerifItalic18pt7b?330:318);
  canvas.setFont(&fonts::FreeSerif9pt7b);canvas.setTextDatum(bottom_center);canvas.setTextColor(BLACK);
  canvas.drawString("The subject remains.",200,556);
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
  canvas.pushSprite(0,0); M5.Display.display(); M5.Display.waitDisplay();
}

static void showSetup() {
  canvas.fillSprite(WHITE);
  drawTitleMark(40);
  canvas.setTextColor(BLACK);canvas.setTextDatum(top_left);
  canvas.setFont(&fonts::FreeSerifItalic18pt7b);canvas.drawString("Establish contact.",28,104);
  canvas.setFont(&fonts::FreeSerif9pt7b);
  canvas.drawString("Join this device's hotspot",28,170);
  canvas.setFont(&fonts::FreeSerifBold12pt7b);canvas.drawString(setupSSID,28,194);
  canvas.setFont(&fonts::FreeSerif9pt7b);canvas.drawString("Hotspot password (not your home one)",28,250);
  canvas.setFont(&fonts::FreeSerifBold12pt7b);canvas.drawString(setupPassword,28,274);
  canvas.setFont(&fonts::FreeSerif9pt7b);canvas.drawString("Then open",28,330);
  canvas.setFont(&fonts::FreeSerifBold12pt7b);canvas.drawString("http://192.168.4.1",28,354);
  canvas.fillRect(28,420,40,2,canvas.color888(191,0,0));
  canvas.setFont(&fonts::FreeSerif9pt7b);
  canvas.drawString("Enter your 2.4 GHz Wi-Fi there.",28,440);
  canvas.drawString("Setup closes after ten minutes.",28,464);
  canvas.drawString("Hold a user button to reopen it.",28,488);
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
  canvas.pushSprite(0,0); M5.Display.display(); M5.Display.waitDisplay();
}

static void noCache();
static bool subjectReferenceReady();
static bool subjectUploadBusy();
static bool loadSubjectReference(uint8_t*& data,size_t& size);
static bool dashboardHostOK();
static void noteActivity();
static String familyStateJson();
#include "web_ui.h"
#include "native_generation.h"
#include "storage_commands.h"

static void networkStatus() {
  Serial.printf("NETSTATUS wifi=%s ap=%u portal_auth=wpa2 credential_logging=0 ip=%s sd=%u\n",
    WiFi.status()==WL_CONNECTED?"connected":networkState.c_str(),apActive,
    WiFi.status()==WL_CONNECTED?WiFi.localIP().toString().c_str():"0.0.0.0",sdReady);
}

static bool localSetupRequest() {
  return apActive&&portal.client().localIP()==WiFi.softAPIP()&&
         (portal.hostHeader()=="192.168.4.1"||portal.hostHeader()=="192.168.4.1:80");
}

static void noCache() {
  portal.sendHeader("Cache-Control","no-store");
  portal.sendHeader("Pragma","no-cache");
  portal.sendHeader("X-Content-Type-Options","nosniff");
}

#include "face_upload.h"
#include "power_manager.h"

static void serveRoot() {
  noCache();
  if (!localSetupRequest()) {
    serveDashboard(); return;
  }
  noteActivity();
  String page(SETUP_HTML);
  page.replace("TOKEN",csrfToken);
  portal.send(200,"text/html",page);
}

static void startPortal() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);
  setupSSID="Family-Setup-"+String(uint32_t(ESP.getEfuseMac()&0xffff),HEX);
  setupPassword=randomHex(16); csrfToken=randomHex(32);
  if (!WiFi.softAP(setupSSID.c_str(),setupPassword.c_str(),1,false,1)) {
    networkState="hotspot_failed"; networkStatus(); return;
  }
  apActive=true; portalStarted=millis(); closePortalAt=0;
  portalDNS.start(53,"*",WiFi.softAPIP());
  Serial.printf("SETUP ssid=%s url=http://192.168.4.1 password=display_only\n",setupSSID.c_str());
  showSetup();
}

static void stopPortal() {
  portalDNS.stop(); WiFi.softAPdisconnect(true); apActive=false;
  setupPassword=""; csrfToken="";faceSetupPinned=false;
}

static void beginConnection(const String& ssid,const String& password,bool save) {
  pendingSSID=ssid; pendingPassword=password; saveOnConnect=save;
  connectionPending=true; onlineShown=false; connectStarted=millis(); networkState="connecting";
  WiFi.begin(pendingSSID.c_str(),pendingPassword.c_str());
}

static void handleWifiPost() {
  noCache();
  if (!localSetupRequest()||portal.arg("csrf")!=csrfToken) {
    portal.send(403,"text/plain","Local setup authorization required."); return;
  }
  const String ssid=portal.arg("ssid"),password=portal.arg("password");
  if (!ssid.length()||ssid.length()>32||password.length()<8||password.length()>63) {
    portal.send(400,"text/plain","Use a 1-32 byte SSID and an 8-63 character Wi-Fi password."); return;
  }
  if (connectionPending) { portal.send(409,"text/plain","Connection attempt already in progress."); return; }
  portal.sendHeader("Location","/"); portal.send(303);
  beginConnection(ssid,password,true);
}

static void maintainWifi() {
  if (WiFi.status()==WL_CONNECTED) {
    if (connectionPending) {
      if (saveOnConnect) {
        networkPrefs.putString("ssid",pendingSSID);
        networkPrefs.putString("password",pendingPassword);
      }
      pendingPassword=""; pendingSSID=""; connectionPending=false;
      networkState="connected";
      configTzTime(FAMILY_TZ,"pool.ntp.org","time.cloudflare.com");
      closePortalAt=faceSetupPinned?0:millis()+15000;
      networkStatus();
    }
    if (!onlineShown && !generationBusy.load()) {
      onlineShown=true;
      if(!subjectReferenceReady())message("We don't know who you are yet.","Visit "+WiFi.localIP().toString()+"/face");
    }
    if (apActive&&closePortalAt&&int32_t(millis()-closePortalAt)>=0) stopPortal();
  } else if (connectionPending&&millis()-connectStarted>30000) {
    connectionPending=false; pendingPassword=""; pendingSSID="";
    networkState="connection_failed_credentials_not_saved";
    WiFi.disconnect(false,false);
    if (!apActive) startPortal();
    networkStatus();
  } else if (!connectionPending&&!apActive&&networkState=="unconfigured") startPortal();
  if (apActive&&millis()-portalStarted>600000&&!generationBusy.load()) {
    stopPortal();
    message("Setup window closed.","Hold a user button to reopen.");
  }
}

void setup() {
  Serial.setRxBufferSize(2048); Serial.begin(115200); Serial.setTimeout(5000);
  auto cfg=M5.config(); cfg.clear_display=false; cfg.internal_spk=false; cfg.internal_mic=false;
  M5.begin(cfg); M5.Display.setAutoDisplay(false); M5.Display.setRotation(0);
  displayReady=M5.getBoard()==m5::board_t::board_M5PaperColor&&ESP.getPsramSize()>0;
  if (!displayReady) return;
  canvas.setColorDepth(24); canvas.setPsram(true);
  if (!canvas.createSprite(400,600)) { displayReady=false; return; }
  mountCard();
  powerInit();
  networkPrefs.begin("familywifi",false);
  initReferenceStorage();
  registerGenerationRoutes();
  registerFaceRoutes();
  registerPowerRoutes();
  WiFi.persistent(false); WiFi.mode(WIFI_STA);
  portal.on("/",HTTP_GET,serveRoot);
  portal.on("/wifi",HTTP_POST,handleWifiPost);
  portal.on("/status",HTTP_GET,[]{noCache();portal.send(200,"text/plain",WiFi.status()==WL_CONNECTED?"Connection established. Device IP: "+WiFi.localIP().toString():networkState);});
  portal.onNotFound([]{if(apActive){portal.sendHeader("Location","http://192.168.4.1/");portal.send(302);}else portal.send(404,"text/plain","Not found");});
  portal.begin();
  String ssid=networkPrefs.getString("ssid","");
  String password=networkPrefs.getString("password","");
#if defined(FAMILY_WIFI_SSID) && defined(FAMILY_WIFI_PASSWORD)
  // Nonempty local header takes priority; never print either value.
  if (strlen(FAMILY_WIFI_SSID)>0) { ssid=FAMILY_WIFI_SSID; password=FAMILY_WIFI_PASSWORD; }
#endif
  if (ssid.length()) beginConnection(ssid,password,false); else startPortal();
  networkStatus();
}

void loop() {
  M5.update(); portal.handleClient();
  if (apActive) portalDNS.processNextRequest();
  maintainWifi();
  powerTick();
  if (M5.BtnA.pressedFor(3000)||M5.BtnB.pressedFor(3000)||M5.BtnC.pressedFor(3000)) {
    noteActivity();
    if (!apActive&&!generationBusy.load()) startPortal();
  }
  // Releasing a long setup press must not trigger generation.
  if (!apActive&&M5.BtnA.wasClicked()) handleGenerationButton(0);
  if (!apActive&&M5.BtnB.wasClicked()) handleGenerationButton(1);
  if (!apActive&&M5.BtnC.wasClicked()) handleGenerationButton(2);
  static char command[40]; static uint8_t length=0; static bool overflow=false;
  while (Serial.available()) {
    const char c=Serial.read(); if(c=='\r')continue;
    if(c!='\n'){if(length<sizeof(command)-1)command[length++]=c;else overflow=true;continue;}
    command[length]=0;
    if(overflow)Serial.println("ERROR command_too_long");
    else if(!strcmp(command,"NETSTATUS"))networkStatus();
    else if(!strcmp(command,"FACESTATUS"))faceSerialStatus();
    else if(!strcmp(command,"POWERSTATUS"))powerSerialStatus();
    else if(!strncmp(command,"SLEEPTEST ",10)){const long s=atol(command+10);
      if(s<15||s>3600)Serial.println("ERROR sleeptest_range_15_3600");else enterShutdown("sleeptest",uint32_t(s));}
    else if(!strcmp(command,"STATUS")||!strcmp(command,"GENSTATUS"))generationStatus();
    else if(!strcmp(command,"GENERATE"))requestGeneration();
    else if(!strcmp(command,"SETUP")&&!generationBusy.load())startPortal();
    else if(!strcmp(command,"INFO")&&!generationBusy.load())frameInfo();
    else if(dispatchStorageCommand(command,generationBusy.load())) {
      if(!generationBusy.load()&&!strncmp(command,"PUT ",4)) {
        uint32_t crc=0,bytes=0;
        if(fileCRC(CURRENT_PATH,crc,bytes)&&bytes==FRAME_SIZE&&validFrame(CURRENT_PATH)&&crc!=lastFrameCRC.load()) {
          lastFrameCRC=crc;generationPhase="imported_sample";
        }
      }
    }
    else Serial.println("ERROR unknown_command");
    length=0;overflow=false;
  }
  delay(5);
}
