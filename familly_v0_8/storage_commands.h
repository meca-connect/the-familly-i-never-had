#pragma once
#include <cstdio>
#include <cstring>
#include <cstdint>

// Reuse the previously hardware-verified archive helpers. Never operate on SD or
// the display while the HTTPS generation worker owns them. No API credentials
// are accepted over this diagnostic protocol.
static bool dispatchStorageCommand(const char* command,bool busy) {
  uint32_t first=0,second=0;
  char extra=0;
  enum Operation {StorageUnknown,StorageStatus,StorageDisplay,StoragePut,StorageRead};
  Operation operation=StorageUnknown;
  if(!strcmp(command,"STORESTATUS"))operation=StorageStatus;
  else if(!strcmp(command,"DISPLAY"))operation=StorageDisplay;
  else if(sscanf(command,"PUT %u %x %c",&first,&second,&extra)==2)operation=StoragePut;
  else if(sscanf(command,"READ %u %u %c",&first,&second,&extra)==2)operation=StorageRead;
  if(operation==StorageUnknown)return false;
  if(busy){Serial.println("ERROR generation_busy");return true;}
  switch(operation) {
    case StorageStatus:status();break;
    case StorageDisplay:renderStored();break;
    case StoragePut:receiveFrame(first,second);break;
    case StorageRead:readChunk(first,second);break;
    default:break;
  }
  return true;
}
