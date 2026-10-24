#pragma once
// Pure nightly-schedule maths for "The Familly I Never Had". No Arduino or
// libc time-zone dependencies (fixed UTC offset in minutes), so it is
// unit-tested on the host: spikes/007-night-power/test_schedule_rules.py
#include <stdint.h>

namespace family {

static constexpr int NIGHT_WINDOW_HOURS=3;   // a household is assigned only within 3 h of the night hour
static constexpr uint32_t NIGHT_MAX_RETRIES=3; // failures tolerated per night before giving up

struct Civil { int year,month,day,hour,minute,second; };

// Howard Hinnant's days_from_civil / civil_from_days (proleptic Gregorian).
inline int64_t daysFromCivil(int64_t y,unsigned m,unsigned d) {
  y-=m<=2;
  const int64_t era=(y>=0?y:y-399)/400;
  const unsigned yoe=unsigned(y-era*400);
  const unsigned doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
  const unsigned doe=yoe*365+yoe/4-yoe/100+doy;
  return era*146097+int64_t(doe)-719468;
}

inline int64_t civilToEpoch(int y,int mo,int d,int h,int mi,int s) {
  return daysFromCivil(y,unsigned(mo),unsigned(d))*86400+int64_t(h)*3600+int64_t(mi)*60+s;
}

inline int64_t floorDiv(int64_t a,int64_t b){ int64_t q=a/b; if((a%b!=0)&&((a<0)!=(b<0)))--q; return q; }

inline Civil epochToCivil(int64_t epoch) {
  const int64_t days=floorDiv(epoch,86400);
  int64_t secs=epoch-days*86400;
  const int64_t z=days+719468;
  const int64_t era=(z>=0?z:z-146096)/146097;
  const unsigned doe=unsigned(z-era*146097);
  const unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
  const unsigned doy=doe-(365*yoe+yoe/4-yoe/100);
  const unsigned mp=(5*doy+2)/153;
  const unsigned d=doy-(153*mp+2)/5+1;
  const unsigned m=mp<10?mp+3:mp-9;
  const int64_t y=int64_t(yoe)+era*400+(m<=2);
  Civil c{int(y),int(m),int(d),int(secs/3600),int((secs%3600)/60),int(secs%60)};
  return c;
}

// Start (UTC epoch) of the most recent local night slot at or before now.
inline int64_t lastSlotStart(int64_t now,int offsetMin,int nightHour) {
  const int64_t local=now+int64_t(offsetMin)*60;
  const int64_t sinceSlot=local-int64_t(nightHour)*3600;
  const int64_t slotLocal=floorDiv(sinceSlot,86400)*86400+int64_t(nightHour)*3600;
  return slotLocal-int64_t(offsetMin)*60;
}

// YYYYMMDD (local) of the slot a moment belongs to. Stored after a successful
// assignment so each night produces exactly one household.
inline uint32_t localNightKey(int64_t now,int offsetMin,int nightHour) {
  const Civil c=epochToCivil(lastSlotStart(now,offsetMin,nightHour)+int64_t(offsetMin)*60);
  return uint32_t(c.year*10000+c.month*100+c.day);
}

inline bool nightDue(int64_t now,int offsetMin,int nightHour,uint32_t lastKey) {
  const int64_t slot=lastSlotStart(now,offsetMin,nightHour);
  return now-slot<int64_t(NIGHT_WINDOW_HOURS)*3600 && localNightKey(now,offsetMin,nightHour)!=lastKey;
}

// When the device should next be awake to assign a household (UTC epoch):
// - inside an unfinished window with no failures: in one minute;
// - after a failure: retry after retryMinutes while the window is open and
//   retries remain;
// - otherwise: the next night slot.
inline int64_t nextWakeEpoch(int64_t now,int offsetMin,int nightHour,uint32_t lastKey,
                             uint32_t failuresTonight,int retryMinutes) {
  const int64_t slot=lastSlotStart(now,offsetMin,nightHour);
  const int64_t windowEnd=slot+int64_t(NIGHT_WINDOW_HOURS)*3600;
  const int64_t nextSlot=slot+86400;
  if(now<windowEnd&&localNightKey(now,offsetMin,nightHour)!=lastKey) {
    if(failuresTonight==0)return now+60;
    if(failuresTonight<=NIGHT_MAX_RETRIES) {
      const int64_t retry=now+int64_t(retryMinutes)*60;
      if(retry<windowEnd)return retry;
    }
  }
  return nextSlot;
}

}  // namespace family
