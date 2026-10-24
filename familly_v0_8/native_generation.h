#pragma once
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <cmath>
#include "dither_core.h"
#include "openai_codec.h"
#include "prompt_rules.h"
// Optional Git-ignored project-root openai.h defines FAMILY_OPENAI_API_KEY.
// The key is compiled into firmware and is recoverable from a flash dump.
#if __has_include("openai.h")    // required, local only; see openai.h.example
#include "openai.h"
#endif
#ifndef FAMILY_OPENAI_API_KEY
#define FAMILY_OPENAI_API_KEY ""
#endif
static bool openaiKeyConfigured(){return strlen(FAMILY_OPENAI_API_KEY)>=20;}

extern const uint8_t familyCaStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t familyCaEnd[] asm("_binary_x509_crt_bundle_end");
static std::atomic<bool> generationBusy(false);
static std::atomic<const char*> generationPhase("ready");
static std::atomic<int> lastHttpCode(0);
static std::atomic<uint32_t> generationCount(0),requestCount(0),lastFrameCRC(0),lastJobMs(0);
static uint32_t nextRequestAt=0;
static bool hasRequested=false;
static uint8_t generationButton=255;
static String dashboardToken;
// Worker -> main-loop handoff (NVS writes and scheduling happen on the main loop).
static std::atomic<bool> generationFinishedPending(false),generationFinishedOk(false);
static std::atomic<int> generationFinishedScene(-1);
static const char* generationOrigin="request";   // request | night | subject

class FamilyImageSink:public Stream {
public:
  uint8_t* data; size_t used=0,capacity; bool overflow=false;
  FamilyImageSink(uint8_t* p,size_t n):data(p),capacity(n){}
  int available() override{return 0;} int read() override{return -1;} int peek() override{return -1;}
  void flush() override{}
  size_t write(uint8_t b) override{return write(&b,1);}
  size_t write(const uint8_t* p,size_t n) override {
    if(n>capacity-used){overflow=true;return 0;}
    memcpy(data+used,p,n);used+=n;return n;
  }
};

static bool jpegSize(const uint8_t* p,size_t n,int& w,int& h) {
  if(n<4||p[0]!=0xff||p[1]!=0xd8)return false;
  size_t pos=2;
  while(pos+4<n){
    if(p[pos++]!=0xff)continue;
    while(pos<n&&p[pos]==0xff)++pos;
    if(pos>=n)return false;
    const uint8_t marker=p[pos++];
    if(marker==0xd9||marker==0xda)return false;
    if(marker==0x01||(marker>=0xd0&&marker<=0xd8))continue;
    if(pos+2>n)return false;
    const size_t size=(size_t(p[pos])<<8)|p[pos+1];
    if(size<2||size>n-pos)return false;
    if(marker>=0xc0&&marker<=0xcf&&marker!=0xc4&&marker!=0xc8&&marker!=0xcc){
      if(size<8)return false;
      h=(int(p[pos+3])<<8)|p[pos+4];w=(int(p[pos+5])<<8)|p[pos+6];
      return w>0&&h>0&&w<=2048&&h<=2048;
    }
    pos+=size;
  }
  return false;
}

static void put32(uint8_t* p,uint32_t value){for(int i=0;i<4;++i)p[i]=(value>>(8*i))&255;}

static bool storeNativeFrame(const uint8_t* packed) {
  uint8_t header[20]={'F','A','M','6','V','1',0,0,0x90,1,0x58,2};
  put32(header+12,120000);put32(header+16,crcUpdate(0xffffffff,packed,120000)^0xffffffff);
  uint32_t full=crcUpdate(0xffffffff,header,20);full=crcUpdate(full,packed,120000)^0xffffffff;
  if(SD.exists(INCOMING_PATH)&&!SD.remove(INCOMING_PATH))return false;
  File out=SD.open(INCOMING_PATH,FILE_WRITE);
  if(!out)return false;
  bool ok=out.write(header,20)==20&&out.write(packed,120000)==120000;
  out.flush();out.close();
  uint32_t checked=0,bytes=0;
  if(!ok||!fileCRC(INCOMING_PATH,checked,bytes)||bytes!=FRAME_SIZE||checked!=full||!validFrame(INCOMING_PATH))return false;
  if(SD.exists(PREVIOUS_PATH)&&!SD.remove(PREVIOUS_PATH))return false;
  const bool had=SD.exists(CURRENT_PATH);
  if(had&&!SD.rename(CURRENT_PATH,PREVIOUS_PATH))return false;
  if(!SD.rename(INCOMING_PATH,CURRENT_PATH)){if(had)SD.rename(PREVIOUS_PATH,CURRENT_PATH);return false;}
  if(had&&!SD.remove(PREVIOUS_PATH))Serial.println("WARNING previous_cleanup_failed");
  lastFrameCRC=full;
  Serial.printf("FRAME_COMMITTED source=device bytes=%u crc32=%08x\n",bytes,full);
  return true;
}

static void generationWorker(void*) {
  const uint32_t started=millis();
  uint8_t* jpeg=nullptr;uint8_t* rgb=nullptr;uint8_t* packed=nullptr;
  const char* failure=nullptr;
  generationPhase="requesting";
  message("Your household","is being reassigned.");
  int finishedScene=-1;
  do {
    if(WiFi.status()!=WL_CONNECTED){failure="wifi_unreachable";break;}
    if(time(nullptr)<1700000000){failure="clock_not_synchronized";break;}
    if(!subjectReferenceReady()){failure="no_subject";break;}
    if(!openaiKeyConfigured()){failure="api_key_missing";break;}
    const family::PromptChoice choice=family::choosePrompt(esp_random());
    char promptText[1024];
    if(!family::buildPrompt(choice,promptText,sizeof(promptText))){failure="prompt_build_failed";break;}
    const String prompt(promptText);
    const size_t scenario=choice.scene;
    Serial.printf("PROMPT scene=%u pose=%u framing=%u origin=%s\n",choice.scene,choice.pose,choice.framing,generationOrigin);
    generationPhase="reading_reference";
    uint8_t* reference=nullptr;size_t referenceBytes=0;
    if(!loadSubjectReference(reference,referenceBytes)){failure="reference_unreadable";break;}
    const String boundary="familyBoundary"+randomHex(24);
    const size_t bodyBytes=family::buildImageEditMultipart(nullptr,0,boundary.c_str(),prompt.c_str(),reference,referenceBytes);
    uint8_t* body=static_cast<uint8_t*>(heap_caps_malloc(bodyBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!body){free(reference);failure="request_memory";break;}
    const bool built=family::buildImageEditMultipart(body,bodyBytes,boundary.c_str(),prompt.c_str(),reference,referenceBytes)==bodyBytes;
    free(reference);
    if(!built){free(body);failure="request_build_failed";break;}
    NetworkClientSecure tls;
    tls.setCACertBundle(familyCaStart,familyCaEnd-familyCaStart);
    tls.setHandshakeTimeout(20);
    HTTPClient http;
    http.setConnectTimeout(15000);http.setTimeout(65000);  // uint16 ms: core maximum
    http.setReuse(false);
    http.setUserAgent("the-familly-i-never-had/0.2 (M5Stack PaperColor)");
    if(!http.begin(tls,"https://api.openai.com/v1/images/edits")){free(body);failure="https_begin_failed";break;}
    http.addHeader("Authorization",String("Bearer ")+FAMILY_OPENAI_API_KEY);  // never logged
    http.addHeader("Content-Type","multipart/form-data; boundary="+boundary);
    http.addHeader("Accept","application/json");
    generationPhase="openai_generating";
    Serial.printf("API_REQUEST provider=openai model=%s size=%s quality=%s scenario=%u reference_bytes=%u body_bytes=%u tls=verified\n",
      family::OPENAI_IMAGE_MODEL,family::OPENAI_IMAGE_SIZE,family::OPENAI_IMAGE_QUALITY,unsigned(scenario),unsigned(referenceBytes),unsigned(bodyBytes));
    const uint32_t apiStarted=millis();
    const int code=http.sendRequest("POST",body,bodyBytes);
    free(body);lastHttpCode=code;
    Serial.printf("API_HTTP code=%d content_length=%d elapsed_ms=%u\n",code,http.getSize(),unsigned(millis()-apiStarted));
    if(code<0){http.end();failure="https_or_network_failed";break;}
    constexpr size_t capacity=3*1024*1024;  // JSON with base64 JPEG; medium 1024x1536 is ~0.3 MB
    jpeg=static_cast<uint8_t*>(heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!jpeg){http.end();failure="download_memory";break;}
    generationPhase="downloading";
    FamilyImageSink sink(jpeg,capacity);
    const int transferred=http.writeToStream(&sink);http.end();
    if(code!=200){
      char detail[160]={0};
      family::extractErrorMessage(reinterpret_cast<const char*>(jpeg),sink.used,detail,sizeof(detail));
      family::redactSecret(detail,FAMILY_OPENAI_API_KEY);
      Serial.printf("API_ERROR code=%d message=%s\n",code,detail[0]?detail:"(none)");
      failure=code==401?"api_key_rejected":code==429?"api_quota_or_rate_limited":code==400?"api_rejected_request":"api_error";
      break;
    }
    if(transferred<=0||sink.overflow){failure="response_too_large_or_truncated";break;}
    uint32_t inTokens=0,outTokens=0;
    if(family::extractUsage(reinterpret_cast<const char*>(jpeg),sink.used,inTokens,outTokens))
      Serial.printf("API_USAGE input_tokens=%u output_tokens=%u\n",unsigned(inTokens),unsigned(outTokens));
    size_t jpegLen=0;
    int width=0,height=0;
    if(!family::extractB64Image(jpeg,sink.used,jpegLen)||!jpegSize(jpeg,jpegLen,width,height)){failure="invalid_image_response";break;}
    Serial.printf("JPEG_DECODED_FROM_BASE64 bytes=%u width=%d height=%d\n",unsigned(jpegLen),width,height);
    generationPhase="decoding";
    canvas.fillSprite(WHITE);
    const float scale=std::max(400.0f/width,600.0f/height);
    const int x=(400-int(std::ceil(width*scale)))/2,y=(600-int(std::ceil(height*scale)))/2;
    if(!canvas.drawJpg(jpeg,jpegLen,x,y,0,0,0,0,scale,scale)){failure="jpeg_decode_failed";break;}
    free(jpeg);jpeg=nullptr;
    rgb=static_cast<uint8_t*>(heap_caps_malloc(400*600*3,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    packed=static_cast<uint8_t*>(heap_caps_malloc(120000,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!rgb||!packed){failure="render_memory";break;}
    RGBColor row[400];
    for(int line=0;line<600;++line){
      canvas.readRectRGB(0,line,400,1,row);
      for(int col=0;col<400;++col){size_t off=(line*400+col)*3;rgb[off]=row[col].r;rgb[off+1]=row[col].g;rgb[off+2]=row[col].b;}
    }
    generationPhase="dithering_C";
    if(!family::floydSteinberg(rgb,400*600*3,packed,120000)){failure="dither_failed";break;}
    free(rgb);rgb=nullptr;
    generationPhase="saving_sd";
    if(!storeNativeFrame(packed)){failure="sd_commit_failed";break;}
    free(packed);packed=nullptr;
    generationPhase="refreshing_display";
    lastRefreshMs=0;renderStored();
    if(!lastRefreshMs||M5.Display.displayBusy()){failure="display_busy_after_refresh";break;}
    ++generationCount;generationPhase="complete";finishedScene=int(scenario);
  }while(false);
  free(jpeg);free(rgb);free(packed);
  if(failure){
    generationPhase=failure;
    Serial.printf("GENERATION_FAILED reason=%s\n",failure);
    // The previous household is already gone from the screen; say so plainly.
    if(!strcmp(failure,"wifi_unreachable"))message("Your family","is unreachable right now.");
    else if(!strcmp(failure,"no_subject"))message("We don't know","who you are yet.");
    else if(!strcmp(failure,"api_key_missing")||!strcmp(failure,"api_key_rejected"))message("Your authorization","could not be verified.");
    else message("No family could","be assigned tonight.");
  }
  generationFinishedScene=finishedScene;generationFinishedOk=failure==nullptr;generationFinishedPending=true;
  lastJobMs=millis()-started;
  Serial.printf("GENERATION_FINISHED success=%u count=%u elapsed_ms=%u\n",failure==nullptr,generationCount.load(),lastJobMs.load());
  generationBusy=false;vTaskDelete(nullptr);
}

static bool requestGeneration(bool force=false,const char* origin="request") {
  noteActivity();
  if(apActive){generationPhase="setup_open";Serial.println("GENERATION_IGNORED setup_open=1");return false;}
  if(subjectUploadBusy()){Serial.println("GENERATION_IGNORED reference_upload_busy=1");return false;}
  if(generationBusy.load()){Serial.println("GENERATION_IGNORED busy=1");return false;}
  // No subject, no family: never fall back to a generic or free provider.
  if(!subjectReferenceReady()){generationPhase="no_subject";Serial.println("GENERATION_IGNORED no_subject=1");message("We don't know","who you are yet.");return false;}
  if(!openaiKeyConfigured()){generationPhase="api_key_missing";Serial.println("GENERATION_IGNORED api_key_missing=1");message("Your authorization","could not be verified.");return false;}
  if(generationBusy.load()){Serial.println("GENERATION_IGNORED busy=1");return false;}
  if(!force&&hasRequested&&int32_t(millis()-nextRequestAt)<0){Serial.println("GENERATION_IGNORED cooldown=1");return false;}
  if(WiFi.status()!=WL_CONNECTED){generationPhase="wifi_unreachable";message("Your family","is unreachable right now.");return false;}
  if(!sdReady){generationPhase="sd_not_ready";return false;}
  if(time(nullptr)<1700000000){generationPhase="clock_not_synchronized";return false;}
  hasRequested=true;nextRequestAt=millis()+45000;
  generationOrigin=origin;
  generationBusy=true;++requestCount;
  if(xTaskCreatePinnedToCore(generationWorker,"family_image",16384,nullptr,1,nullptr,0)!=pdPASS){
    generationBusy=false;generationPhase="worker_allocation_failed";return false;
  }
  return true;
}

static void generationStatus() {
  Serial.printf("GENSTATUS busy=%u phase=%s requests=%u completed=%u http=%d frame_crc=%08x top=%c clock_ready=%u\n",
    generationBusy.load(),generationPhase.load(),requestCount.load(),generationCount.load(),lastHttpCode.load(),lastFrameCRC.load(),
    generationButton<3?'A'+generationButton:'?',time(nullptr)>=1700000000);
}

static bool dashboardHostOK(){const String ip=WiFi.localIP().toString();return WiFi.status()==WL_CONNECTED&&(portal.hostHeader()==ip||portal.hostHeader()==ip+":80");}

static void serveGenerationStatus() {
  noCache();
  String out="{\"busy\":"+String(generationBusy.load()?"true":"false")+",\"phase\":\""+generationPhase.load()+"\",\"requests\":"+String(requestCount.load())+",\"completed\":"+String(generationCount.load())+",\"http\":"+String(lastHttpCode.load())+",\"frame_crc\":\""+String(lastFrameCRC.load(),HEX)+"\",\"top\":\""+(generationButton<3?String(char('A'+generationButton)):String("unassigned"))+"\","+familyStateJson()+"}";
  portal.send(200,"application/json",out);
}

static void serveDashboard() {
  noCache();if(!dashboardHostOK()){portal.send(403,"text/plain","Open the device's local IP address.");return;}
  noteActivity();
  String html=FPSTR(DASHBOARD_HTML);
  html.replace("TOKEN",dashboardToken);portal.send(200,"text/html",html);
}

static void servePreview() {
  noCache();
  if(!dashboardHostOK()){portal.send(403,"text/plain","Local IP required");return;}
  if(generationBusy.load()){portal.send(409,"text/plain","Image is being reassigned.");return;}
  if(!sdReady||!validFrame(CURRENT_PATH)){portal.send(404,"text/plain","No stored photograph");return;}
  File f=SD.open(CURRENT_PATH,FILE_READ);if(!f){portal.send(500,"text/plain","SD read failed");return;}
  constexpr uint32_t headerSize=118,bodySize=120000;
  uint8_t header[headerSize]={0};header[0]='B';header[1]='M';put32(header+2,headerSize+bodySize);put32(header+10,headerSize);put32(header+14,40);put32(header+18,400);put32(header+22,600);header[26]=1;header[28]=4;put32(header+34,bodySize);put32(header+46,16);
  for(int i=0;i<6;++i){header[54+i*4]=PALETTE[i]&255;header[55+i*4]=(PALETTE[i]>>8)&255;header[56+i*4]=(PALETTE[i]>>16)&255;}
  portal.setContentLength(headerSize+bodySize);portal.send(200,"image/bmp","");
  auto client=portal.client();client.write(header,headerSize);
  uint8_t row[200];
  for(int y=599;y>=0;--y){if(!f.seek(20+y*200)||f.read(row,200)!=200)break;client.write(row,200);}
  f.close();
}

static void registerGenerationRoutes() {
  dashboardToken=randomHex(32);generationButton=networkPrefs.getUChar("topbtn",255);
  const size_t roots=familyCaEnd-familyCaStart;
  if(esp_crt_bundle_set(familyCaStart,roots)!=ESP_OK)generationPhase="tls_bundle_error";
  uint32_t bytes=0,crc=0;if(sdReady&&fileCRC(CURRENT_PATH,crc,bytes)&&bytes==FRAME_SIZE)lastFrameCRC=crc;
  portal.on("/generation-status",HTTP_GET,serveGenerationStatus);
  portal.on("/preview.bmp",HTTP_GET,servePreview);
  portal.on("/generate",HTTP_POST,[]{
    noCache();
    if(!dashboardHostOK()||portal.arg("token")!=dashboardToken){portal.send(403,"text/plain","Local dashboard authorization required");return;}
    const bool accepted=requestGeneration(false,"request");portal.send(accepted?202:409,"text/plain",accepted?"Your household is being reassigned.":generationPhase.load());
  });
}

static void handleGenerationButton(uint8_t key) {
  Serial.printf("BUTTON key=%c\n",'A'+key);
  noteActivity();
  if(generationBusy.load()){Serial.println("GENERATION_IGNORED busy=1");return;}
  if(generationButton>2){generationButton=key;networkPrefs.putUChar("topbtn",key);Serial.printf("TOP_BUTTON_ASSIGNED key=%c\n",'A'+key);}
  if(key==generationButton)requestGeneration(false,"request");
  else if(sdReady)renderStored();
}
