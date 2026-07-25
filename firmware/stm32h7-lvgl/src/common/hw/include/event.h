#ifndef EVENT_H_
#define EVENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"
#include "evt_code.h"


#ifdef _USE_HW_EVENT


typedef struct
{
  EventCode_t code;
  uint32_t    data;
  const char *p_name;
} event_t;

typedef bool (*event_func_t)(event_t *);


#define eventPub(event_code, event_data)  eventPubFunc(#event_code, event_code, event_data)
#define eventSub(sub_func)                eventSubFunc(#sub_func, sub_func)


bool eventInit(void);
bool eventUpdate(void);
bool eventSubFunc(const char *p_name, event_func_t sub_func);
bool eventPubFunc(const char *p_name, EventCode_t event_code, uint32_t event_data);

#endif

#ifdef __cplusplus
}
#endif

#endif
