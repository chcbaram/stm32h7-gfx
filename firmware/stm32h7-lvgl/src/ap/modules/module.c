#include "module.h"

typedef struct
{
  int32_t   count;
  module_t *p_module;
} module_info_t;

static bool moduleBegin(void);
#ifdef _USE_HW_CLI
static void cliModule(cli_args_t *args);
#endif

static module_info_t info;
static bool          is_begin = false;

extern uint32_t _smodule;
extern uint32_t _emodule;

bool moduleInit(void)
{
  bool ret;

  info.count    = ((int)&_emodule - (int)&_smodule) / sizeof(module_t);
  info.p_module = (module_t *)&_smodule;

  logPrintf("[  ] moduleInit()\n");
  logPrintf("       count : %d\n", (int)info.count);

  ret = moduleBegin();

#ifdef _USE_HW_CLI
  cliAdd("module", cliModule);
#endif
  return ret;
}

bool moduleBegin(void)
{
  bool ret = true;

  logPrintf("[  ] moduleBegin()\n");

  for (int i = 0; i < info.count; i++)
  {
    if (info.p_module[i].priority < MODULE_PRI_HIGH || info.p_module[i].priority >= MODULE_PRI_MAX)
    {
      logPrintf("       %s Priority %d Fail\n", info.p_module[i].name, info.p_module[i].priority);
      ret = false;
    }
  }

  for (int pri = MODULE_PRI_HIGH; pri < MODULE_PRI_MAX; pri++)
  {
    for (int i = 0; i < info.count; i++)
    {
      if (info.p_module[i].priority == pri && info.p_module[i].init != NULL)
      {
        bool mod_ret;

        mod_ret  = info.p_module[i].init();
        ret     &= mod_ret;
        if (mod_ret)
        {
          if (info.p_module[i].event_cb != NULL)
          {
            eventSubFunc(info.p_module[i].name, info.p_module[i].event_cb);
          }
        }
        logPrintf("       %s %s\n", info.p_module[i].name, mod_ret ? "OK" : "Fail");
      }
    }
  }

  is_begin = true;

  return ret;
}

bool moduleUpdate(void)
{
  for (int i = 0; i < info.count; i++)
  {
    if (info.p_module[i].update != NULL)
    {
      info.p_module[i].update(info.p_module[i].arg);
    }
  }

  return true;
}

#ifdef _USE_HW_CLI
void cliModule(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    module_t *p_module;

    cliPrintf("is_begin  : %s\n", is_begin ? "True":"False");
    cliPrintf("count     : %d\n", (int)info.count);
    p_module = info.p_module;
    for (int i = 0; i < info.count; i++)
    {
      cliPrintf("%d : %-16s, pri : %d, init : %s, update : %s\n",
                i,
                p_module[i].name,
                p_module[i].priority,
                p_module[i].init   != NULL ? "O":"X",
                p_module[i].update != NULL ? "O":"X");
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("module info\n");
  }
}
#endif
