#pragma once
#include "reference_rules.h"

// Local-only persistent subject storage, separate from the disposable family.
// The SD card is NOT encrypted; deleting a file is NOT secure erasure.
static const char* SUBJECT_DIR="/family-subject";
static const char* SUBJECT_PATH="/family-subject/reference.jpg";
static const char* SUBJECT_PART="/family-subject/upload.part";
static const char* SUBJECT_OLD="/family-subject/replace.part";
static bool subjectRegistered=false,faceUploadActive=false,faceSetupPinned=false;
static family::ReferenceInfo subjectInfo;
static size_t subjectBytes=0,faceBytes=0;
static uint32_t faceCRC=0xffffffff;
static unsigned faceParts=0;
static bool faceOwnsStage=false,faceFileEnded=false;
static int faceErrorCode=0;
static const char* faceErrorMessage="";
static File faceFile;

static bool subjectReferenceReady(){return subjectRegistered;}
static bool subjectUploadBusy(){return faceUploadActive;}

// Worker-side read of the persistent subject into PSRAM. Caller frees.
// Called only from the generation worker while uploads are rejected (busy).
static bool loadSubjectReference(uint8_t*& data,size_t& size) {
  data=nullptr;size=0;
  if(!subjectRegistered||!sdReady)return false;
  File file=SD.open(SUBJECT_PATH,FILE_READ);
  if(!file)return false;
  size=file.size();
  if(!size||size>family::MAX_REFERENCE_BYTES){file.close();return false;}
  data=static_cast<uint8_t*>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  const bool ok=data&&file.read(data,size)==size;file.close();
  if(!ok){free(data);data=nullptr;size=0;}
  return ok;
}

static bool inspectSubjectFile(const char* path,family::ReferenceInfo& info,size_t& bytes) {
  if(!sdReady)return false;
  File file=SD.open(path,FILE_READ);
  if(!file)return false;
  bytes=file.size();
  if(!bytes||bytes>family::MAX_REFERENCE_BYTES){file.close();return false;}
  auto* data=static_cast<uint8_t*>(heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!data){file.close();return false;}
  const bool readOK=file.read(data,bytes)==bytes;file.close();
  bool valid=readOK&&family::inspectReferenceJpeg(data,bytes,info);
  if(valid) {
    // Parse and actually decode, not just trust the filename, MIME, or JPEG SOF.
    M5Canvas probe(&M5.Display);probe.setColorDepth(24);probe.setPsram(true);
    valid=probe.createSprite(64,64)!=nullptr;
    if(valid) {
      const float scale=std::min(64.0f/info.width,64.0f/info.height);
      valid=probe.drawJpg(data,bytes,0,0,64,64,0,0,scale,scale);
      probe.deleteSprite();
    }
  }
  heap_caps_free(data);return valid;
}

static void initReferenceStorage() {
  if(!sdReady)return;
  if(!SD.exists(SUBJECT_DIR)&&!SD.mkdir(SUBJECT_DIR))return;
  family::ReferenceInfo info;size_t bytes=0;
  bool valid=inspectSubjectFile(SUBJECT_PATH,info,bytes);
  if(!valid&&SD.exists(SUBJECT_OLD)) {
    family::ReferenceInfo oldInfo;size_t oldBytes=0;
    if(inspectSubjectFile(SUBJECT_OLD,oldInfo,oldBytes)) {
      if(SD.exists(SUBJECT_PATH))SD.remove(SUBJECT_PATH);
      if(SD.rename(SUBJECT_OLD,SUBJECT_PATH)){info=oldInfo;bytes=oldBytes;valid=true;}
    }
  }
  if(valid&&SD.exists(SUBJECT_OLD))SD.remove(SUBJECT_OLD);
  if(SD.exists(SUBJECT_PART))SD.remove(SUBJECT_PART);
  subjectRegistered=valid;subjectInfo=info;subjectBytes=valid?bytes:0;
}

// Which per-boot token protects this request: setup hotspot or LAN dashboard.
// The LAN dashboard token is NOT user authentication: anyone on the same
// trusted network who opens the dashboard can manage the reference.
static String faceTokenForRequest() {
  if(localSetupRequest())return csrfToken;
  if(dashboardHostOK())return dashboardToken;
  return String();
}

static bool faceAuthorized() {
  const String token=faceTokenForRequest();
  if(token.length()!=32||portal.header("X-Family-CSRF")!=token)return false;
  const String origin=portal.header("Origin");
  if(origin.isEmpty())return true;
  if(localSetupRequest())return origin=="http://192.168.4.1"||origin=="http://192.168.4.1:80";
  const String ip=WiFi.localIP().toString();
  return origin=="http://"+ip||origin=="http://"+ip+":80";
}

static void faceResetUpload() {
  if(faceFile)faceFile.close();
  if(faceOwnsStage&&sdReady&&SD.exists(SUBJECT_PART))SD.remove(SUBJECT_PART);
  faceOwnsStage=false;faceUploadActive=false;faceFileEnded=false;
  faceParts=0;faceBytes=0;faceCRC=0xffffffff;faceErrorCode=0;faceErrorMessage="";
}

static void faceFail(int code,const char* message) {
  if(!faceErrorCode){faceErrorCode=code;faceErrorMessage=message;}
  if(faceFile)faceFile.close();
  if(faceOwnsStage&&SD.exists(SUBJECT_PART))SD.remove(SUBJECT_PART);
  faceOwnsStage=false;faceFileEnded=false;
}

static void faceStatus() {
  noCache();
  if(!faceAuthorized()){portal.send(403,"text/plain","Use this device's protected setup hotspot.");return;}
  String body="{\"registered\":"+String(subjectRegistered?"true":"false")+",\"bytes\":"+String(subjectBytes)+",\"width\":"+String(subjectInfo.width)+",\"height\":"+String(subjectInfo.height)+",\"storage\":\"unencrypted_device_sd\",\"provider\":\"openai\",\"sent_on_each_generation\":true,\"api_key_configured\":"+String(openaiKeyConfigured()?"true":"false")+"}";
  portal.send(200,"application/json",body);
}

static void faceSerialStatus() {
  Serial.printf("FACESTATUS registered=%u bytes=%u width=%u height=%u upload_busy=%u uptime_ms=%lu storage=unencrypted_sd provider=openai openai_key=%s\n",
    subjectRegistered,unsigned(subjectBytes),unsigned(subjectInfo.width),unsigned(subjectInfo.height),faceUploadActive,(unsigned long)millis(),
    openaiKeyConfigured()?"configured":"missing");
}

static bool faceMultipartRequest() {
  return portal.header("Content-Type").startsWith("multipart/form-data");
}

static void faceUploadChunk() {
  // Core 3.3.9 routes NON-multipart POST bodies to this callback in "raw" mode,
  // where portal.upload() is a null reference. Touching it there crashes the
  // board, so ignore raw bodies; the finish handler then rejects the request.
  if(!faceMultipartRequest())return;
  HTTPUpload& upload=portal.upload();
  if(upload.status==UPLOAD_FILE_START) {
    ++faceParts;faceUploadActive=true;
    if(faceParts!=1){faceFail(400,"Upload exactly one reference photograph.");return;}
    if(!faceAuthorized()){faceFail(403,"Local setup authorization required.");return;}
    if(generationBusy.load()){faceFail(409,"Wait until household reassignment finishes.");return;}
    if(!sdReady){faceFail(503,"Reference storage is unavailable.");return;}
    if(upload.name!="face"){faceFail(400,"Expected one face photograph.");return;}
    if(!SD.exists(SUBJECT_DIR)&&!SD.mkdir(SUBJECT_DIR)){faceFail(500,"Cannot open reference storage.");return;}
    if(SD.exists(SUBJECT_PART)&&!SD.remove(SUBJECT_PART)){faceFail(500,"Cannot clear reference staging.");return;}
    faceFile=SD.open(SUBJECT_PART,FILE_WRITE);
    if(!faceFile){faceFail(500,"Cannot stage reference photograph.");return;}
    faceOwnsStage=true;faceBytes=0;faceCRC=0xffffffff;
  } else if(upload.status==UPLOAD_FILE_WRITE) {
    if(faceErrorCode)return;
    if(!faceFile){faceFail(400,"Reference upload was not initialized.");return;}
    if(upload.currentSize>family::MAX_REFERENCE_BYTES-faceBytes){faceFail(413,"Reference must be at most 1 MiB after conversion.");return;}
    if(faceFile.write(upload.buf,upload.currentSize)!=upload.currentSize){faceFail(500,"Reference write failed.");return;}
    faceCRC=crcUpdate(faceCRC,upload.buf,upload.currentSize);faceBytes+=upload.currentSize;
  } else if(upload.status==UPLOAD_FILE_END) {
    if(faceErrorCode)return;
    if(faceFile){faceFile.flush();faceFile.close();}
    if(!faceOwnsStage||upload.totalSize!=faceBytes){faceFail(400,"Incomplete reference upload.");return;}
    faceFileEnded=true;
  } else if(upload.status==UPLOAD_FILE_ABORTED) {
    faceResetUpload();
  }
}

static void faceUploadFinish() {
  noCache();
  if(!faceAuthorized())faceFail(403,"Use this device's protected setup hotspot.");
  else if(generationBusy.load())faceFail(409,"Wait until household reassignment finishes.");
  else if(faceParts!=1||!faceFileEnded)faceFail(400,"Choose one photograph and upload it completely.");
  else if(portal.arg("consent")!="openai")faceFail(400,"Confirm that this photograph may be sent to OpenAI.");
  family::ReferenceInfo info;size_t bytes=0;
  uint32_t checked=0,diskBytes=0;
  if(!faceErrorCode&&(!inspectSubjectFile(SUBJECT_PART,info,bytes)||
      !fileCRC(SUBJECT_PART,checked,diskBytes)||diskBytes!=faceBytes||checked!=(faceCRC^0xffffffff)))
    faceFail(400,"Invalid JPEG. Use a clear photo, 128-1024 pixels per edge, up to 1 MiB.");
  if(!faceErrorCode) {
    const bool hadCurrent=SD.exists(SUBJECT_PATH);
    if(SD.exists(SUBJECT_OLD)&&!SD.remove(SUBJECT_OLD))faceFail(500,"Cannot prepare reference replacement.");
    else if(hadCurrent&&!SD.rename(SUBJECT_PATH,SUBJECT_OLD))faceFail(500,"Cannot preserve current reference.");
    else if(!SD.rename(SUBJECT_PART,SUBJECT_PATH)) {
      if(hadCurrent)SD.rename(SUBJECT_OLD,SUBJECT_PATH);
      faceFail(500,"Cannot commit new reference; previous reference retained.");
    } else {
      faceOwnsStage=false;
      uint32_t committed=0,committedBytes=0;
      if(!fileCRC(SUBJECT_PATH,committed,committedBytes)||committed!=checked||committedBytes!=bytes) {
        SD.remove(SUBJECT_PATH);
        if(hadCurrent)SD.rename(SUBJECT_OLD,SUBJECT_PATH);
        faceFail(500,"Reference verification failed; previous reference restored.");
      } else {
        if(hadCurrent&&!SD.remove(SUBJECT_OLD))Serial.println("WARNING subject_replacement_cleanup_failed");
        subjectRegistered=true;subjectInfo=info;subjectBytes=bytes;
      }
    }
  }
  const int code=faceErrorCode;
  const String error=faceErrorMessage;
  faceResetUpload();
  if(code){portal.send(code,"text/plain",error);return;}
  // A new subject means the current household no longer belongs to anyone:
  // assign a new one now (bypasses the request cooldown, not the busy lock).
  noteActivity();
  const bool triggered=WiFi.status()==WL_CONNECTED&&!apActive&&requestGeneration(true,"subject");
  Serial.printf("REFERENCE_SAVED consent=openai generation_triggered=%u\n",triggered);
  portal.send(200,"application/json",String("{\"saved\":true,\"generation_triggered\":")+(triggered?"true":"false")+
    ",\"note\":\""+(triggered?"":"A new household will be assigned when the device is online.")+"\"}");
}

static void faceDelete() {
  noCache();
  if(!faceAuthorized()){portal.send(403,"text/plain","Local setup authorization required.");return;}
  if(generationBusy.load()||faceUploadActive){portal.send(409,"text/plain","Wait for the active operation.");return;}
  bool ok=true;
  for(const char* path:{SUBJECT_PATH,SUBJECT_PART,SUBJECT_OLD})if(SD.exists(path)&&!SD.remove(path))ok=false;
  if(!ok){initReferenceStorage();portal.send(500,"text/plain","Some reference files could not be removed.");return;}
  subjectRegistered=false;subjectBytes=0;subjectInfo=family::ReferenceInfo{};
  portal.send(200,"application/json","{\"removed\":true,\"secure_erasure\":false}");
}

static void facePreview() {
  noCache();
  if(!faceAuthorized()){portal.send(403,"text/plain","Local setup authorization required.");return;}
  if(generationBusy.load()||faceUploadActive){portal.send(409,"text/plain","Reference is temporarily unavailable.");return;}
  if(!subjectRegistered){portal.send(404,"text/plain","No subject has been registered.");return;}
  File file=SD.open(SUBJECT_PATH,FILE_READ);
  if(!file){portal.send(500,"text/plain","Reference storage is unavailable.");return;}
  portal.streamFile(file,"image/jpeg");file.close();
}

static void serveFacePage() {
  noCache();
  const String token=faceTokenForRequest();
  if(token.length()!=32){portal.send(403,"text/plain","Open this page from the device's own address on your network, or from its setup hotspot.");return;}
  if(localSetupRequest()){faceSetupPinned=true;portalStarted=millis();closePortalAt=0;}
  noteActivity();
  portal.sendHeader("X-Frame-Options","DENY");
  portal.sendHeader("Referrer-Policy","no-referrer");
  portal.sendHeader("Content-Security-Policy","default-src 'none'; script-src 'nonce-"+token+"'; style-src 'unsafe-inline'; img-src blob: data:; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
  String page(FACE_HTML);
  page.replace("TOKEN",token);portal.send(200,"text/html",page);
}

static void registerFaceRoutes() {
  const char* headers[]={"X-Family-CSRF","Origin","Content-Type"};portal.collectHeaders(headers,3);
  portal.on("/face",HTTP_GET,serveFacePage);
  portal.on("/face/status",HTTP_GET,faceStatus);
  portal.on("/face/upload",HTTP_POST,faceUploadFinish,faceUploadChunk);
  portal.on("/face/reference.jpg",HTTP_GET,facePreview);
  portal.on("/face/delete",HTTP_POST,faceDelete);
}
