#pragma once
// SD + panel helpers (originally the USB test bridge sketch). Its setup()/loop()
// are renamed by the includer and left unused.
// API-to-SD proof of concept. No Wi-Fi credentials or API keys in firmware.
// All writes restricted to /family-poc/. Never autoformats or lists other files.
// Test storage is NOT secure erasure; never use personal/ephemeral images here.
#include <M5Unified.h>
#include <M5PM1.h>
#include <SPI.h>
#include <SD.h>

M5PM1 pm1;
M5Canvas canvas(&M5.Display);
static bool sdReady=false;
static bool displayReady=false;
static uint32_t lastRefreshMs=0;
static const char* initStage="starting";
static const char* ROOT_DIR="/family-poc";
static const char* CURRENT_PATH="/family-poc/current.fam6";
static const char* INCOMING_PATH="/family-poc/incoming.part";
static const char* PREVIOUS_PATH="/family-poc/previous.part";
static const uint32_t PALETTE[]={0x000000,0xffffff,0xfff338,0xbf0000,0x6440ff,0x438a1c};
static constexpr uint32_t FRAME_SIZE=120020;

static uint32_t crcUpdate(uint32_t crc,const uint8_t* data,size_t count) {
  for (size_t i=0;i<count;++i) {
    crc^=data[i];
    for (int bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320U:0);
  }
  return crc;
}

static uint32_t get32(const uint8_t* p) {
  return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}

static bool fileCRC(const char* path,uint32_t& crc,uint32_t& bytes) {
  File f=SD.open(path,FILE_READ);
  if (!f) return false;
  uint8_t buffer[512];
  uint32_t state=0xffffffff;
  bytes=0;
  while (f.available()) {
    const size_t count=f.read(buffer,sizeof(buffer));
    if (!count) { f.close(); return false; }
    state=crcUpdate(state,buffer,count); bytes+=count;
  }
  f.close(); crc=state^0xffffffff;
  return true;
}

static bool validFrame(const char* path) {
  File f=SD.open(path,FILE_READ);
  if (!f||f.size()!=FRAME_SIZE) { f.close(); return false; }
  uint8_t header[20];
  if (f.read(header,20)!=20||memcmp(header,"FAM6V1\0\0",8)||
      header[8]!=0x90||header[9]!=1||header[10]!=0x58||header[11]!=2||get32(header+12)!=120000) {
    f.close(); return false;
  }
  uint32_t state=0xffffffff;
  uint8_t block[512];
  while (f.available()) {
    const size_t count=f.read(block,sizeof(block));
    if (!count) { f.close(); return false; }
    for (size_t i=0;i<count;++i) {
      if ((block[i]>>4)>=6||(block[i]&15)>=6) { f.close(); return false; }
    }
    state=crcUpdate(state,block,count);
  }
  f.close();
  return (state^0xffffffff)==get32(header+16);
}

static void status() {
  Serial.printf("STATUS sd=%u display=%u board=%u width=%d height=%d psram=%u card_bytes=%llu format_enabled=0 frame_exists=%u refresh_ms=%u busy=%u stage=%s\n",
    sdReady,displayReady,unsigned(M5.getBoard()),M5.Display.width(),M5.Display.height(),
    ESP.getPsramSize(),sdReady?(unsigned long long)SD.cardSize():0ULL,
    sdReady&&SD.exists(CURRENT_PATH),lastRefreshMs,M5.Display.displayBusy(),initStage);
}

static void frameInfo() {
  if (!sdReady) { Serial.println("ERROR sd_not_ready"); return; }
  if (!SD.exists(CURRENT_PATH)) { Serial.println("FRAME_INFO exists=0"); return; }
  uint32_t crc=0,bytes=0;
  if (!fileCRC(CURRENT_PATH,crc,bytes)) { Serial.println("ERROR sd_read"); return; }
  Serial.printf("FRAME_INFO exists=1 bytes=%u crc32=%08x valid=%u\n",bytes,crc,validFrame(CURRENT_PATH));
}

static bool mountCard() {
  if (sdReady) return true;
  if (pm1.begin(&M5.In_I2C)!=M5PM1_OK) { initStage="pm1_begin"; return false; }
  if (pm1.setLdoEnable(true)!=M5PM1_OK ||
      pm1.gpioSet(M5PM1_GPIO_NUM_0,M5PM1_GPIO_MODE_OUTPUT,1,M5PM1_GPIO_PULL_NONE,M5PM1_GPIO_DRIVE_PUSHPULL)!=M5PM1_OK ||
      pm1.gpioSet(M5PM1_GPIO_NUM_4,M5PM1_GPIO_MODE_OUTPUT,1,M5PM1_GPIO_PULL_NONE,M5PM1_GPIO_DRIVE_PUSHPULL)!=M5PM1_OK ||
      pm1.gpioSet(M5PM1_GPIO_NUM_3,M5PM1_GPIO_MODE_OUTPUT,1,M5PM1_GPIO_PULL_NONE,M5PM1_GPIO_DRIVE_PUSHPULL)!=M5PM1_OK ||
      pm1.gpioSet(M5PM1_GPIO_NUM_1,M5PM1_GPIO_MODE_INPUT,0,M5PM1_GPIO_PULL_UP,M5PM1_GPIO_DRIVE_PUSHPULL)!=M5PM1_OK) {
    initStage="pm1_sd_power"; return false;
  }
  delay(50);
  if (pm1.digitalRead(M5PM1_GPIO_NUM_1)!=LOW)
    Serial.println("WARNING card_detect_high_trying_nonformatting_spi_mount");
  // Official PaperColor silicon GPIOs: SCK=15 MISO=14 MOSI=13 CS=47.
  SPI.begin(15,14,13,47);
  sdReady=SD.begin(47,SPI,4000000,"/sd",5,false);
  if (!sdReady) { initStage="sd_mount_failed_no_format_attempted"; return false; }
  if (!SD.exists(ROOT_DIR)&&!SD.mkdir(ROOT_DIR)) {
    sdReady=false; initStage="project_directory"; return false;
  }
  initStage="ready";
  return true;
}

static void renderStored() {
  if (!sdReady||!displayReady||!validFrame(CURRENT_PATH)) {
    Serial.println("ERROR no_valid_stored_frame"); return;
  }
  File f=SD.open(CURRENT_PATH,FILE_READ);
  if (!f||!f.seek(20)) { f.close(); Serial.println("ERROR frame_open"); return; }
  uint8_t packed[200];
  lgfx::rgb888_t row[400];
  for (int y=0;y<600;++y) {
    if (f.read(packed,200)!=200) { f.close(); Serial.println("ERROR frame_row_read"); return; }
    for (int x=0;x<400;++x) {
      const uint8_t pair=packed[x>>1];
      const uint8_t index=(x&1)?(pair&15):(pair>>4);
      const uint32_t rgb=PALETTE[index];
      row[x].r=rgb>>16; row[x].g=rgb>>8; row[x].b=rgb;
    }
    canvas.pushImage(0,y,400,1,row);
  }
  f.close(); // Finish all SD traffic before physical display refresh on shared SPI.
  Serial.println("REFRESH_START source=sd method=C"); Serial.flush();
  const uint32_t start=millis();
  M5.Display.setEpdMode(epd_mode_t::epd_fastest); // Already dithered. Do not dither twice.
  canvas.pushSprite(0,0);
  M5.Display.display(); M5.Display.waitDisplay();
  lastRefreshMs=millis()-start;
  Serial.printf("REFRESH_DONE source=sd method=C elapsed_ms=%u busy=%u\n",lastRefreshMs,M5.Display.displayBusy());
  Serial.flush();
}

static void receiveFrame(uint32_t expectedBytes,uint32_t expectedCRC) {
  if (!sdReady||expectedBytes!=FRAME_SIZE) { Serial.println("ERROR invalid_upload_or_sd"); return; }
  // Only reserved project staging files may be replaced. Never format the card.
  if (SD.exists(INCOMING_PATH)&&!SD.remove(INCOMING_PATH)) { Serial.println("ERROR staging_remove"); return; }
  File out=SD.open(INCOMING_PATH,FILE_WRITE);
  if (!out) { Serial.println("ERROR staging_open"); return; }
  Serial.println("READY_PUT chunk=512"); Serial.flush();
  uint8_t buffer[512];
  uint32_t offset=0,state=0xffffffff;
  while (offset<expectedBytes) {
    const size_t need=min(uint32_t(sizeof(buffer)),expectedBytes-offset);
    const size_t count=Serial.readBytes(buffer,need);
    const size_t written=count==need?out.write(buffer,count):0;
    if (count!=need||written!=count) {
      out.close(); SD.remove(INCOMING_PATH);
      Serial.printf("ERROR upload_timeout_or_write offset=%u need=%u received=%u written=%u\n",offset,unsigned(need),unsigned(count),unsigned(written)); return;
    }
    state=crcUpdate(state,buffer,count); offset+=count;
    Serial.printf("ACK %u\n",offset); Serial.flush();
  }
  out.flush(); out.close();
  uint32_t diskCRC=0,diskBytes=0;
  if ((state^0xffffffff)!=expectedCRC||!fileCRC(INCOMING_PATH,diskCRC,diskBytes)||
      diskCRC!=expectedCRC||diskBytes!=expectedBytes||!validFrame(INCOMING_PATH)) {
    SD.remove(INCOMING_PATH); Serial.println("ERROR upload_integrity"); return;
  }
  // Roll back a failed final rename. This is a PoC, not power-loss-safe secure storage.
  if (SD.exists(PREVIOUS_PATH)&&!SD.remove(PREVIOUS_PATH)) { Serial.println("ERROR previous_remove"); return; }
  const bool hadCurrent=SD.exists(CURRENT_PATH);
  if (hadCurrent&&!SD.rename(CURRENT_PATH,PREVIOUS_PATH)) { Serial.println("ERROR previous_rename"); return; }
  if (!SD.rename(INCOMING_PATH,CURRENT_PATH)) {
    if (hadCurrent) SD.rename(PREVIOUS_PATH,CURRENT_PATH);
    Serial.println("ERROR current_rename"); return;
  }
  if (hadCurrent&&!SD.remove(PREVIOUS_PATH)) Serial.println("WARNING previous_cleanup_failed");
  Serial.printf("STORED bytes=%u crc32=%08x\n",diskBytes,diskCRC); Serial.flush();
}

static void readChunk(uint32_t offset,uint32_t count) {
  if (!sdReady||!count||count>512||offset>FRAME_SIZE||count>FRAME_SIZE-offset) {
    Serial.println("ERROR invalid_read"); return;
  }
  File f=SD.open(CURRENT_PATH,FILE_READ);
  uint8_t buffer[512];
  if (!f||!f.seek(offset)||f.read(buffer,count)!=count) {
    f.close(); Serial.println("ERROR read_failed"); return;
  }
  f.close();
  Serial.printf("DATA %u %u\n",offset,count);
  Serial.write(buffer,count);
  Serial.print("\nEND\n"); Serial.flush();
}

void setup() {
  Serial.setRxBufferSize(2048); // HWCDC defaults to 256; receive complete 512-byte protocol chunks.
  Serial.begin(115200); Serial.setTimeout(5000);
  const uint32_t start=millis();
  while (!Serial&&millis()-start<4000) delay(10);
  auto cfg=M5.config(); cfg.clear_display=false; cfg.internal_spk=false; cfg.internal_mic=false;
  M5.begin(cfg);
  M5.Display.setAutoDisplay(false); M5.Display.setRotation(0);
  displayReady=M5.getBoard()==m5::board_t::board_M5PaperColor&&
               M5.Display.width()==400&&M5.Display.height()==600&&ESP.getPsramSize()>0;
  if (!displayReady) { initStage="unexpected_hardware"; return; }
  canvas.setColorDepth(24); canvas.setPsram(true);
  if (!canvas.createSprite(400,600)) { displayReady=false; initStage="allocation_failed"; return; }
  mountCard();
  Serial.println("READY commands STATUS MOUNT INFO PUT <bytes> <crc32> READ <offset> <count> DISPLAY");
  status();
  if (sdReady&&SD.exists(CURRENT_PATH)) renderStored();
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed()||M5.BtnB.wasPressed()||M5.BtnC.wasPressed()) renderStored();
  static char command[80]; static uint8_t length=0; static bool overflow=false;
  while (Serial.available()) {
    const char c=Serial.read();
    if (c=='\r') continue;
    if (c!='\n') {
      if (length<sizeof(command)-1) command[length++]=c; else overflow=true;
      continue;
    }
    command[length]=0;
    uint32_t first=0,second=0; char extra=0;
    if (overflow) Serial.println("ERROR command_too_long");
    else if (!strcmp(command,"STATUS")) status();
    else if (!strcmp(command,"MOUNT")) { mountCard(); status(); }
    else if (!strcmp(command,"INFO")) frameInfo();
    else if (!strcmp(command,"DISPLAY")) renderStored();
    else if (sscanf(command,"PUT %u %x %c",&first,&second,&extra)==2) receiveFrame(first,second);
    else if (sscanf(command,"READ %u %u %c",&first,&second,&extra)==2) readChunk(first,second);
    else Serial.println("ERROR unknown_command");
    length=0; overflow=false;
  }
  delay(10);
}
