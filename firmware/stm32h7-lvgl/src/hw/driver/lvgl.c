#include "lvgl.h"

#ifdef _USE_HW_LVGL
#include "lvgl/lv_port_disp.h"
#include "lvgl/lv_port_indev.h"
#include "cli.h"


#ifdef _USE_HW_CLI
static void cliCmd(cli_args_t *args);
#endif

static bool is_init = false;
static bool is_enable = true;




bool lvglInit(void)
{
  if (is_init == true)
    return true;

  lv_init();
  lv_tick_set_cb(millis);

  lv_port_disp_init();
  lv_port_indev_init();

  is_init = true;

  logPrintf("[OK] lvglInit()\n");
  logPrintf("     ver  : %d.%d.%d\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  logPrintf("     mem  : %d KB\n", LV_MEM_SIZE/1024);
#if LV_USE_DRAW_DMA2D == 1
  logPrintf("     dma2d: use\n");
#endif

#ifdef _USE_HW_CLI
  cliAdd("lvgl", cliCmd);
#endif
  return true;
}

#if LV_USE_DRAW_DMA2D == 1
/* lv_draw_dma2d_init() 이 LV_USE_DRAW_DMA2D_INTERRUPT 설정과 무관하게
 * NVIC_EnableIRQ(DMA2D_IRQn) 을 호출한다. 핸들러가 없으면 남아있던 상태
 * 플래그 하나만으로도 Default_Handler 무한루프에 빠지므로 반드시 정의한다.
 */
void DMA2D_IRQHandler(void)
{
  uint32_t isr = DMA2D->ISR;

  DMA2D->IFCR = DMA2D_IFCR_CTEIF  | DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTWIF |
                DMA2D_IFCR_CAECIF | DMA2D_IFCR_CCTCIF | DMA2D_IFCR_CCEIF;

#if LV_USE_DRAW_DMA2D_INTERRUPT
  if (isr & DMA2D_ISR_TCIF)
  {
    lv_draw_dma2d_transfer_complete_interrupt_handler();
  }
#else
  (void)isr;
#endif
}
#endif

bool lvglUpdate(void)
{
  if (is_init == false)
    return false;

  if (is_enable == true)
  {
    lv_task_handler();
  }
  return true;
}

bool lvglSetEnable(bool enable)
{
  is_enable = enable;
  return true;
}

bool lvglGetEnable(void)
{
  return is_enable;
}

#ifdef _USE_HW_CLI
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    lv_mem_monitor_t mon;

    lv_mem_monitor(&mon);

    cliPrintf("ver       : %d.%d.%d\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    cliPrintf("is_init   : %s\n", is_init   ? "True":"False");
    cliPrintf("is_enable : %s\n", is_enable ? "True":"False");
    cliPrintf("mem total : %d KB\n", (int)mon.total_size/1024);
    cliPrintf("mem used  : %d KB (%d %%)\n", (int)(mon.total_size - mon.free_size)/1024, (int)mon.used_pct);
    cliPrintf("mem frag  : %d %%\n", (int)mon.frag_pct);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "enable"))
  {
    is_enable = args->isStr(1, "on") ? true : false;
    cliPrintf("is_enable : %s\n", is_enable ? "True":"False");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("lvgl info\n");
    cliPrintf("lvgl enable on:off\n");
  }
}
#endif

#endif
