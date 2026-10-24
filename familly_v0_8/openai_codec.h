#pragma once
// Pure helpers for the OpenAI Images "edits" call. No I/O, no allocation, no
// Arduino dependencies, so they are unit-tested on the host
// (spikes/006-openai-device/test_openai_codec.py).
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace family {

static constexpr const char* OPENAI_IMAGE_MODEL="gpt-image-2.5-sunburst";
static constexpr const char* OPENAI_IMAGE_SIZE="1024x1536";   // scales exactly to 400x600
static constexpr const char* OPENAI_IMAGE_QUALITY="medium";

// Appends bytes when out!=nullptr; always advances pos so a nullptr pass
// measures the exact body size. Returns false once capacity is exceeded.
inline bool mpAppend(uint8_t* out,size_t cap,size_t& pos,const void* data,size_t n) {
  if(out){ if(n>cap-pos||pos>cap)return false; memcpy(out+pos,data,n); }
  pos+=n; return true;
}
inline bool mpText(uint8_t* out,size_t cap,size_t& pos,const char* s){return mpAppend(out,cap,pos,s,strlen(s));}
inline bool mpField(uint8_t* out,size_t cap,size_t& pos,const char* boundary,const char* name,const char* value) {
  return mpText(out,cap,pos,"--")&&mpText(out,cap,pos,boundary)&&
         mpText(out,cap,pos,"\r\nContent-Disposition: form-data; name=\"")&&mpText(out,cap,pos,name)&&
         mpText(out,cap,pos,"\"\r\n\r\n")&&mpText(out,cap,pos,value)&&mpText(out,cap,pos,"\r\n");
}

// Builds the complete multipart/form-data body. Call with out=nullptr to get
// the required size. Returns 0 if out is too small (never overflows).
inline size_t buildImageEditMultipart(uint8_t* out,size_t cap,const char* boundary,const char* prompt,
                                      const uint8_t* jpeg,size_t jpegLen) {
  size_t pos=0;
  const bool ok=
    mpField(out,cap,pos,boundary,"model",OPENAI_IMAGE_MODEL)&&
    mpField(out,cap,pos,boundary,"prompt",prompt)&&
    mpField(out,cap,pos,boundary,"n","1")&&
    mpField(out,cap,pos,boundary,"size",OPENAI_IMAGE_SIZE)&&
    mpField(out,cap,pos,boundary,"quality",OPENAI_IMAGE_QUALITY)&&
    mpField(out,cap,pos,boundary,"output_format","jpeg")&&
    mpField(out,cap,pos,boundary,"output_compression","90")&&
    mpText(out,cap,pos,"--")&&mpText(out,cap,pos,boundary)&&
    mpText(out,cap,pos,"\r\nContent-Disposition: form-data; name=\"image\"; filename=\"reference.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n")&&
    mpAppend(out,cap,pos,jpeg,jpegLen)&&
    mpText(out,cap,pos,"\r\n--")&&mpText(out,cap,pos,boundary)&&mpText(out,cap,pos,"--\r\n");
  return ok?pos:0;
}

inline int b64Value(uint8_t c) {
  if(c>='A'&&c<='Z')return c-'A';
  if(c>='a'&&c<='z')return c-'a'+26;
  if(c>='0'&&c<='9')return c-'0'+52;
  if(c=='+')return 62;
  if(c=='/')return 63;
  return -1;
}

// Finds data[0].b64_json in an Images API JSON response and base64-decodes it
// IN PLACE to the start of buf (decoded bytes are always fewer than encoded).
// Strict: rejects missing/unterminated strings, illegal characters, partial
// quanta, misplaced padding and empty payloads. Accepts JSON "\/" escapes.
inline bool extractB64Image(uint8_t* buf,size_t len,size_t& outLen) {
  outLen=0;
  static const char key[]="\"b64_json\"";
  const size_t keyLen=sizeof(key)-1;
  size_t i=0;
  for(;;++i){ if(i+keyLen>len)return false; if(!memcmp(buf+i,key,keyLen))break; }
  i+=keyLen;
  while(i<len&&(buf[i]==' '||buf[i]=='\t'||buf[i]=='\r'||buf[i]=='\n'))++i;
  if(i>=len||buf[i]!=':')return false;
  ++i;
  while(i<len&&(buf[i]==' '||buf[i]=='\t'||buf[i]=='\r'||buf[i]=='\n'))++i;
  if(i>=len||buf[i]!='"')return false;
  ++i;
  uint32_t acc=0; int bits=0,quantum=0,pad=0; size_t w=0;
  for(;;++i){
    if(i>=len)return false;                       // unterminated string
    uint8_t c=buf[i];
    if(c=='"')break;
    if(c=='\\'){ if(i+1<len&&buf[i+1]=='/'){++i;c='/';} else return false; }
    if(c=='='){ if(quantum<2)return false; ++pad; ++quantum; if(quantum==4){quantum=0;} continue; }
    if(pad)return false;                          // data after padding
    const int v=b64Value(c); if(v<0)return false;
    acc=(acc<<6)|uint32_t(v); bits+=6; ++quantum; if(quantum==4)quantum=0;
    if(bits>=8){ bits-=8; buf[w++]=uint8_t(acc>>bits); }  // w<=i always: in-place safe
  }
  if(quantum!=0||w==0)return false;
  outLen=w; return true;
}

// Finds the first unsigned integer value for "<name>": in a JSON document.
inline bool jsonUint(const char* s,size_t len,const char* name,uint32_t& value) {
  const size_t n=strlen(name);
  for(size_t i=0;i+n+2<len;++i){
    if(s[i]!='"'||memcmp(s+i+1,name,n)||s[i+1+n]!='"')continue;
    size_t j=i+n+2;
    while(j<len&&(s[j]==' '||s[j]==':'))++j;
    if(j>=len||s[j]<'0'||s[j]>'9')return false;
    uint64_t v=0; while(j<len&&s[j]>='0'&&s[j]<='9'){v=v*10+uint64_t(s[j]-'0'); if(v>0xffffffffULL)return false; ++j;}
    value=uint32_t(v); return true;
  }
  return false;
}

inline bool extractUsage(const char* json,size_t len,uint32_t& inputTokens,uint32_t& outputTokens) {
  return jsonUint(json,len,"input_tokens",inputTokens)&&jsonUint(json,len,"output_tokens",outputTokens);
}

// Copies the error "message" string (bounded, NUL-terminated, simple escapes
// dropped) for a short diagnostic. Callers must still redact secrets.
inline bool extractErrorMessage(const char* json,size_t len,char* out,size_t cap) {
  if(!cap)return false;
  out[0]=0;
  static const char key[]="\"message\"";
  const size_t keyLen=sizeof(key)-1;
  for(size_t i=0;i+keyLen<len;++i){
    if(memcmp(json+i,key,keyLen))continue;
    size_t j=i+keyLen;
    while(j<len&&(json[j]==' '||json[j]==':'))++j;
    if(j>=len||json[j]!='"')return false;
    ++j;
    size_t w=0;
    while(j<len&&json[j]!='"'&&w+1<cap){
      if(json[j]=='\\'&&j+1<len){j+=2;continue;}
      const char c=json[j++]; out[w++]=(c>=32&&c<127)?c:'?';
    }
    out[w]=0; return w>0;
  }
  return false;
}

// Replaces every occurrence of secret (and any "sk-" token) with [REDACTED].
inline void redactSecret(char* text,const char* secret) {
  static const char mark[]="[REDACTED]";
  const size_t markLen=sizeof(mark)-1;
  auto redactAt=[&](char* at,size_t n){
    const size_t tail=strlen(at+n);
    if(markLen<=n){ memcpy(at,mark,markLen); memmove(at+markLen,at+n,tail+1); }
    else { memmove(at+markLen,at+n,tail+1); memcpy(at,mark,markLen); } // only when room exists
  };
  if(secret&&strlen(secret)>=8){
    const size_t n=strlen(secret);
    for(char* at=strstr(text,secret);at;at=strstr(at+markLen,secret))redactAt(at,n);
  }
  for(char* at=strstr(text,"sk-");at;at=strstr(at+markLen,"sk-")){
    size_t n=3; while(at[n]&&at[n]!=' '&&at[n]!='.'&&at[n]!=','&&at[n]!='"')++n;
    if(n<markLen)break;   // too short to be a key; avoid growing the buffer
    redactAt(at,n);
  }
}

}  // namespace family
