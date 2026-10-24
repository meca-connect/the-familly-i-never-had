#pragma once
// Night schedule + battery behaviour for "The Familly I Never Had".
//
// On USB power the installation stays awake and assigns a household when the
// night hour arrives. On battery it stays awake only while someone interacts
// (web page, buttons), then performs a full M5PM1 shutdown (power level L0:
// only PM1 + RX8130 RTC powered) with the RTC countdown armed for the next
// night slot. The e-ink keeps showing the household without power.
// Waking: RTC (night) or the power button. User buttons cannot wake from L0
// because their pull-ups are unpowered (PaperColor docs, M5PM1 L2/L3A).
#include <esp_sntp.h>
#include <sys/time.h>
#include "schedule_rules.h"

#ifndef FAMILY_TZ
#define FAMILY_TZ "CST6"                // POSIX TZ shown to people (CST, no DST)
#endif
#ifndef FAMILY_UTC_OFFSET_MIN
#define FAMILY_UTC_OFFSET_MIN (-360)    // must match FAMILY_TZ; used by schedule maths
#endif
static constexpr uint32_t BATTERY_IDLE_MS=4UL*60UL*1000UL;   // awake after the last interaction
static constexpr uint32_t SETUP_IDLE_MS=10UL*60UL*1000UL;    // while the setup hotspot is open
static constexpr uint32_t NIGHT_LINGER_MS=40UL*1000UL;       // after a night assignment on battery
static constexpr uint32_t NIGHT_WIFI_GRACE_MS=75UL*1000UL;   // night wake: give Wi-Fi this long
static constexpr int NIGHT_RETRY_MINUTES=20;

static Preferences familyPrefs;
static uint8_t nightHour=3;
static uint32_t lastNightKey=0,failureKey=0,failuresTonight=0;
static uint32_t householdNumber=0,lastAssignedAt=0;
static int lastScene=-1;
static String lastOrigin="";
static const char* bootReason="reset";
static bool wokeForNight=false,nightAttemptInFlight=false,rtcSyncedFromNtp=false,nightNetworkGaveUp=false;
static uint32_t bootMs=0,lastActivityMs=0,nextNightAttemptMs=0,powerSampleMs=0;
static bool onExternalPower=true;
static int batteryPercent=-1,batteryMv=-1,vbusMv=-1;

static void noteActivity(){lastActivityMs=millis();}

static int64_t nowEpoch(){return int64_t(time(nullptr));}
static bool clockValid(){return nowEpoch()>1700000000;}

static uint32_t failuresForCurrentNight() {
  if(!clockValid())return 0;
  return failureKey==family::localNightKey(nowEpoch(),FAMILY_UTC_OFFSET_MIN,nightHour)?failuresTonight:0;
}

static uint32_t lastSampleCostMs=0,maxLoopGapMs=0,lastLoopMs=0;
static void samplePower(bool force=false) {
  if(!force&&millis()-powerSampleMs<2000)return;
  const uint32_t t0=millis();
  powerSampleMs=t0;
  vbusMv=M5.Power.getVBUSVoltage();
  batteryMv=M5.Power.getBatteryVoltage();
  batteryPercent=M5.Power.getBatteryLevel();
  onExternalPower=vbusMv>4000;   // USB 5 V present
  lastSampleCostMs=millis()-t0;
}

static String localTimeText(int64_t epoch,const char* fmt) {
  if(epoch<=1700000000)return "unknown";
  time_t t=time_t(epoch);tm local{};localtime_r(&t,&local);
  char buf[40];strftime(buf,sizeof(buf),fmt,&local);return String(buf);
}

static uint32_t idleLimitMs() {
  if(apActive)return SETUP_IDLE_MS;
  return wokeForNight?NIGHT_LINGER_MS:BATTERY_IDLE_MS;
}

static int32_t sleepInSeconds() {
  if(onExternalPower)return -1;
  const uint32_t idle=millis()-lastActivityMs,limit=idleLimitMs();
  return idle>=limit?0:int32_t((limit-idle)/1000);
}

static String jsonEscape(const String& in) {
  String out;out.reserve(in.length()+8);
  for(size_t i=0;i<in.length();++i){const char c=in[i];if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(uint8_t(c)>=32)out+=c;}
  return out;
}

// Fields appended to /generation-status (no surrounding braces).
static String familyStateJson() {
  const int64_t next=clockValid()?family::nextWakeEpoch(nowEpoch(),FAMILY_UTC_OFFSET_MIN,nightHour,lastNightKey,failuresForCurrentNight(),NIGHT_RETRY_MINUTES):0;
  String s="\"household\":"+String(householdNumber);
  s+=",\"scene\":\""+jsonEscape(lastScene>=0&&size_t(lastScene)<family::SCENE_COUNT?String(family::SCENES[lastScene]):String(""))+"\"";
  s+=",\"assigned_at\":\""+jsonEscape(lastAssignedAt?localTimeText(lastAssignedAt,"%H:%M · %a %d %b"):String(""))+"\"";
  s+=",\"origin\":\""+jsonEscape(lastOrigin)+"\"";
  s+=",\"subject\":"+String(subjectReferenceReady()?"true":"false");
  s+=",\"night_hour\":"+String(nightHour);
  s+=",\"next\":\""+jsonEscape(next?localTimeText(next,"%H:%M · %a %d %b"):String("when the clock is known"))+"\"";
  s+=",\"power\":\""+String(onExternalPower?"usb":"battery")+"\"";
  s+=",\"battery_pct\":"+String(batteryPercent)+",\"battery_mv\":"+String(batteryMv);
  s+=",\"sleep_in\":"+String(sleepInSeconds());
  s+=",\"boot\":\""+String(bootReason)+"\"";
  return s;
}

static void seedClockFromRtc() {
  if(!M5.Rtc.isEnabled())return;
  m5::rtc_datetime_t dt=M5.Rtc.getDateTime();
  if(dt.date.year<2025||dt.date.year>2099)return;   // never set / lost backup power
  const int64_t epoch=family::civilToEpoch(dt.date.year,dt.date.month,dt.date.date,dt.time.hours,dt.time.minutes,dt.time.seconds);
  timeval tv{time_t(epoch),0};settimeofday(&tv,nullptr);   // RTC keeps UTC
  Serial.printf("CLOCK_FROM_RTC utc=%04d-%02d-%02dT%02d:%02d:%02d\n",dt.date.year,dt.date.month,dt.date.date,dt.time.hours,dt.time.minutes,dt.time.seconds);
}

static void writeRtcFromSystemClock() {
  const time_t now=time(nullptr);tm utc{};gmtime_r(&now,&utc);
  M5.Rtc.setDateTime(&utc);
  rtcSyncedFromNtp=true;
  Serial.printf("RTC_SET_FROM_NTP utc=%lld\n",(long long)now);
}

static void powerInit() {
  bootMs=millis();lastActivityMs=bootMs;
  setenv("TZ",FAMILY_TZ,1);tzset();
  familyPrefs.begin("family",false);
  nightHour=familyPrefs.getUChar("nighthr",3);if(nightHour>23)nightHour=3;
  lastNightKey=familyPrefs.getUInt("night",0);
  failureKey=familyPrefs.getUInt("failkey",0);failuresTonight=familyPrefs.getUInt("fails",0);
  householdNumber=familyPrefs.getUInt("count",0);lastScene=familyPrefs.getInt("scene",-1);
  lastAssignedAt=familyPrefs.getUInt("at",0);lastOrigin=familyPrefs.getString("origin","");
  uint8_t source=0;
  const bool pm1ok=pm1.begin(&M5.In_I2C)==M5PM1_OK;
  if(pm1ok)pm1.getWakeSource(&source,M5PM1_CLEAN_ALL);
  const bool rtcIrq=M5.Rtc.isEnabled()&&M5.Rtc.getIRQstatus();
  // Only enterShutdown() arms the PM1 timer / RTC, so either one means "scheduled".
  if(source&M5PM1_WAKE_SRC_TIM)bootReason="scheduled_pm1_timer";
  else if(rtcIrq||(source&M5PM1_WAKE_SRC_EXT_WAKE))bootReason="scheduled_rtc";
  else if(source&M5PM1_WAKE_SRC_PWRBTN)bootReason="power_button";
  else if(source&(M5PM1_WAKE_SRC_VIN|M5PM1_WAKE_SRC_5VINOUT))bootReason="usb_inserted";
  else if(source&M5PM1_WAKE_SRC_RSTBTN)bootReason="reset_button";
  wokeForNight=!strncmp(bootReason,"scheduled",9);
  if(pm1ok)pm1.timerClear();   // a stale timer must never power the board on/off later
  if(M5.Rtc.isEnabled()){M5.Rtc.disableIRQ();M5.Rtc.clearIRQ();}
  seedClockFromRtc();
  samplePower(true);
  Serial.printf("BOOT reason=%s pm1=%u wake_src=0x%02x rtc_irq=%u power=%s vbus_mv=%d battery_mv=%d battery_pct=%d night_hour=%u household=%u\n",
    bootReason,pm1ok,source,rtcIrq,onExternalPower?"usb":"battery",vbusMv,batteryMv,batteryPercent,nightHour,householdNumber);
}

static void recordGenerationOutcome(bool ok,int scene) {
  const int64_t now=nowEpoch();
  const uint32_t key=clockValid()?family::localNightKey(now,FAMILY_UTC_OFFSET_MIN,nightHour):0;
  const bool inNight=clockValid()&&family::nightDue(now,FAMILY_UTC_OFFSET_MIN,nightHour,lastNightKey);
  if(ok){
    ++householdNumber;lastScene=scene;lastAssignedAt=uint32_t(now);lastOrigin=generationOrigin;
    familyPrefs.putUInt("count",householdNumber);familyPrefs.putInt("scene",scene);
    familyPrefs.putUInt("at",lastAssignedAt);familyPrefs.putString("origin",lastOrigin);
    if(inNight||nightAttemptInFlight){lastNightKey=key;familyPrefs.putUInt("night",key);}
  } else if(nightAttemptInFlight&&key) {
    if(failureKey!=key){failureKey=key;failuresTonight=0;}
    ++failuresTonight;
    familyPrefs.putUInt("failkey",failureKey);familyPrefs.putUInt("fails",failuresTonight);
    nextNightAttemptMs=millis()+uint32_t(NIGHT_RETRY_MINUTES)*60000UL;
  }
  Serial.printf("HOUSEHOLD ok=%u number=%u origin=%s night_key=%u failures_tonight=%u\n",ok,householdNumber,generationOrigin,lastNightKey,failuresForCurrentNight());
  nightAttemptInFlight=false;
  noteActivity();   // start the linger/idle countdown after the refresh
}

static bool nightWorkPossible() {
  return subjectReferenceReady()&&openaiKeyConfigured()&&sdReady&&clockValid()&&
         failuresForCurrentNight()<=family::NIGHT_MAX_RETRIES;
}

// Full power-off. Returns only if the PM1 refused to shut down.
static void enterShutdown(const char* why,uint32_t testSeconds=0) {
  if(generationBusy.load())return;
  M5.Display.waitDisplay();
  uint32_t seconds=testSeconds;
  if(!seconds&&clockValid()) {
    const int64_t now=nowEpoch();
    int64_t wake=family::nextWakeEpoch(now,FAMILY_UTC_OFFSET_MIN,nightHour,lastNightKey,failuresForCurrentNight(),NIGHT_RETRY_MINUTES);
    // Nothing can be assigned (no subject / key): skip to the next night
    // instead of waking every minute through the window.
    if(!nightWorkPossible())wake=family::lastSlotStart(now,FAMILY_UTC_OFFSET_MIN,nightHour)+86400;
    int64_t delta=wake-now;if(delta<60)delta=60;
    seconds=uint32_t(((delta+59)/60)*60);   // whole minutes: exact on the RX8130 1/60 Hz timer
  }
  WiFi.disconnect(true,false);WiFi.mode(WIFI_OFF);
  // Primary wake: the PM1's own power-on timer (vendor "M5PM1 Timer Wakeup").
  // Backup wake: RX8130 countdown on PM1 GPIO2 (vendor "RTC M5PM1 Wake-Up").
  const int timerErr=seconds?int(pm1.timerSet(seconds,M5PM1_TIM_ACTION_POWERON)):-99;
  uint32_t armedMs=0;
  if(M5.Rtc.isEnabled()){
    M5.Rtc.disableIRQ();M5.Rtc.clearIRQ();
    if(seconds)armedMs=M5.Rtc.setTimerIRQ(seconds*1000UL);
  }
  const int wakeEnErr=int(pm1.gpioSetWakeEnable(M5PM1_GPIO_NUM_2,true));   // PYG2 = RTC_IRQ
  const int wakeEdgeErr=int(pm1.gpioSetWakeEdge(M5PM1_GPIO_NUM_2,M5PM1_GPIO_WAKE_FALLING));
  const int ldoErr=int(pm1.setLdoEnable(true)),holdErr=int(pm1.ldoSetPowerHold(true)),ledErr=int(pm1.setLedEnLevel(true));
  Serial.printf("SHUTDOWN reason=%s wake_in_s=%u pm1_timer_err=%d rtc_armed_ms=%u rtc_wake_err=%d/%d ldo_err=%d/%d/%d (power button also wakes)\n",
    why,seconds,timerErr,armedMs,wakeEnErr,wakeEdgeErr,ldoErr,holdErr,ledErr);
  Serial.flush();
  delay(100);
  const int offErr=int(pm1.shutdown());
  Serial.printf("SHUTDOWN_CMD err=%d\n",offErr);Serial.flush();
  delay(3000);   // still alive => PM1 refused (e.g. external power quirk)
  Serial.println("SHUTDOWN_REFUSED falling_back_to_esp32_deep_sleep");
  Serial.flush();
  if(seconds)esp_sleep_enable_timer_wakeup(uint64_t(seconds)*1000000ULL);
  esp_deep_sleep_start();
}

static void powerTick() {
  const uint32_t nowMs=millis();
  if(lastLoopMs&&nowMs-lastLoopMs>maxLoopGapMs)maxLoopGapMs=nowMs-lastLoopMs;
  lastLoopMs=nowMs;
  samplePower();
  if(generationFinishedPending.exchange(false))recordGenerationOutcome(generationFinishedOk.load(),generationFinishedScene.load());
  // Keep the RTC honest whenever NTP has run (RTC is the clock across shutdowns).
  if(!rtcSyncedFromNtp&&WiFi.status()==WL_CONNECTED&&sntp_get_sync_status()==SNTP_SYNC_STATUS_COMPLETED)writeRtcFromSystemClock();

  // Nightly assignment (plugged in, or woken by the RTC on battery).
  const bool due=clockValid()&&family::nightDue(nowEpoch(),FAMILY_UTC_OFFSET_MIN,nightHour,lastNightKey);
  if(due&&nightWorkPossible()&&!generationBusy.load()&&!nightAttemptInFlight&&!apActive&&
     WiFi.status()==WL_CONNECTED&&int32_t(millis()-nextNightAttemptMs)>=0){
    Serial.println("NIGHT_ASSIGNMENT_START");
    nightAttemptInFlight=true;
    if(!requestGeneration(true,"night")){generationOrigin="night";recordGenerationOutcome(false,-1);}  // bounded retries
  }
  // Night wake without network: count the night as failed and sleep until the retry.
  if(wokeForNight&&due&&!nightNetworkGaveUp&&WiFi.status()!=WL_CONNECTED&&millis()-bootMs>NIGHT_WIFI_GRACE_MS&&!nightAttemptInFlight){
    nightNetworkGaveUp=true;
    generationOrigin="night";nightAttemptInFlight=true;recordGenerationOutcome(false,-1);
    message("Your family","is unreachable right now.");
  }

  // Battery: sleep when nobody is interacting and nothing is in progress.
  if(onExternalPower)return;
  if(generationBusy.load()||nightAttemptInFlight||subjectUploadBusy()||connectionPending||M5.Display.displayBusy())return;
  if(wokeForNight&&due&&nightWorkPossible()&&!nightNetworkGaveUp&&millis()-bootMs<NIGHT_WIFI_GRACE_MS)return;  // still waiting to work
  if(millis()-lastActivityMs<idleLimitMs())return;
  enterShutdown(wokeForNight?"night_done":"idle_on_battery");
}

static void powerSerialStatus() {
  samplePower(true);
  Serial.printf("LOOPSTATUS sample_cost_ms=%u max_loop_gap_ms=%u wifi_rssi=%d wifi_ps=%d\n",lastSampleCostMs,maxLoopGapMs,WiFi.RSSI(),int(WiFi.getSleep()));
  maxLoopGapMs=0;
  Serial.printf("POWERSTATUS power=%s vbus_mv=%d battery_mv=%d battery_pct=%d boot=%s sleep_in_s=%d night_hour=%u last_night=%u failures_tonight=%u clock=%lld rtc_from_ntp=%u next=%s\n",
    onExternalPower?"usb":"battery",vbusMv,batteryMv,batteryPercent,bootReason,int(sleepInSeconds()),nightHour,lastNightKey,
    failuresForCurrentNight(),(long long)nowEpoch(),rtcSyncedFromNtp,
    clockValid()?localTimeText(family::nextWakeEpoch(nowEpoch(),FAMILY_UTC_OFFSET_MIN,nightHour,lastNightKey,failuresForCurrentNight(),NIGHT_RETRY_MINUTES),"%Y-%m-%dT%H:%M").c_str():"unknown");
}

static void registerPowerRoutes() {
  portal.on("/schedule",HTTP_POST,[]{
    noCache();
    if(!dashboardHostOK()||portal.arg("token")!=dashboardToken){portal.send(403,"text/plain","Local dashboard authorization required");return;}
    const int hour=portal.arg("hour").toInt();
    if(portal.arg("hour").isEmpty()||hour<0||hour>23){portal.send(400,"text/plain","Choose an hour between 0 and 23.");return;}
    nightHour=uint8_t(hour);familyPrefs.putUChar("nighthr",nightHour);noteActivity();
    portal.send(200,"text/plain","The household will be reassigned at "+String(hour<10?"0":"")+String(hour)+":00.");
  });
  portal.on("/awake",HTTP_POST,[]{
    noCache();
    if(!dashboardHostOK()||portal.arg("token")!=dashboardToken){portal.send(403,"text/plain","Local dashboard authorization required");return;}
    noteActivity();wokeForNight=false;portal.send(200,"text/plain","Awake.");
  });
}
