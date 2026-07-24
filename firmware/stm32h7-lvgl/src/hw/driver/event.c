#include "event.h"


#ifdef _USE_HW_EVENT
#include "qbuffer.h"
#include "cli.h"

#define EVENT_Q_MAX       HW_EVENT_Q_MAX
#define EVENT_NODE_MAX    HW_EVENT_NODE_MAX


typedef struct
{
  int32_t count;
  event_func_t event_func[EVENT_NODE_MAX];
  const char *func_name[EVENT_NODE_MAX];
} event_node_t;


static bool eventGet(event_t *p_event);
static bool eventAvailble(void);
#ifdef _USE_HW_CLI
static void cliEvent(cli_args_t *args);
#endif

static bool is_init = false;
static bool is_log = true;
static qbuffer_t event_q;
static event_t event_buf[EVENT_Q_MAX];
static event_node_t event_node;




bool eventInit(void)
{
  event_node.count = 0;
  for (int i=0; i<EVENT_NODE_MAX; i++)
  {
    event_node.event_func[i] = NULL;
    event_node.func_name[i] = NULL;
  }
  is_init = qbufferCreateBySize(&event_q, (uint8_t *)event_buf, sizeof(event_t), EVENT_Q_MAX);

#ifdef _USE_HW_CLI
  cliAdd("event", cliEvent);
#endif
  return is_init;
}

bool eventSubFunc(const char *p_name, event_func_t sub_func)
{
  bool ret;

  if (event_node.count < EVENT_NODE_MAX)
  {
    event_node.event_func[event_node.count] = sub_func;
    event_node.func_name[event_node.count] = p_name;
    event_node.count++;
    ret = true;
  }
  else
  {
    ret = false;
  }

  return ret;
}

bool eventAvailble(void)
{
  if (is_init != true)
    return false;

  if (qbufferAvailable(&event_q) > 0)
    return true;
  else
    return false;
}

bool eventPubFunc(const char *p_name, EventCode_t event_code, uint32_t event_data)
{
  bool ret;
  event_t event_msg;

  if (is_init != true)
    return false;

  event_msg.code = event_code;
  event_msg.data = event_data;
  event_msg.p_name = p_name;
  ret = qbufferWrite(&event_q, (uint8_t *)&event_msg, 1);

  return ret;
}

bool eventGet(event_t *p_event)
{
  bool ret;

  if (is_init != true)
    return false;

  ret = qbufferRead(&event_q, (uint8_t *)p_event, 1);

  return ret;
}

bool eventUpdate(void)
{
  if (is_init != true)
    return false;


  while(eventAvailble())
  {
    event_t evt;

    if (eventGet(&evt) == true)
    {
      if (is_log == true)
      {
        logPrintf("[  ] Event %s:%d\n", evt.p_name, (int)evt.data);
      }

      for (int i=0; i<event_node.count; i++)
      {
        if (event_node.event_func[i] != NULL)
        {
          event_node.event_func[i](&evt);
        }
      }
    }
  }

  return true;
}

#ifdef _USE_HW_CLI
void cliEvent(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("init : %s\n", is_init ? "True":"False");
    cliPrintf("log  : %s\n", is_log  ? "On":"Off");
    cliPrintf("node : %d\n", (int)event_node.count);
    for (int i=0; i<event_node.count; i++)
    {
      cliPrintf("       %d - %s\n", i, event_node.func_name[i]);
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "log"))
  {
    if (args->isStr(1, "on"))
    {
      is_log = true;
    }
    else
    {
      is_log = false;
    }
    cliPrintf("log  : %s\n", is_log  ? "On":"Off");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("event info\n");
    cliPrintf("event log on:off\n");
  }
}
#endif

#endif
