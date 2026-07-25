#include "pwm.h"
#include "cli.h"

#ifdef _USE_HW_PWM



typedef struct
{
  uint16_t max_value;
  uint16_t duty;
  uint32_t channel;
} pwm_tbl_t;


#ifdef _USE_HW_CLI
static void cliPwm(cli_args_t *args);
#endif

static bool is_init = false;

static pwm_tbl_t  pwm_tbl[PWM_MAX_CH];
static TIM_HandleTypeDef htim13;





bool pwmInit(void)
{
  bool ret = true;
  TIM_OC_InitTypeDef sConfigOC = {0};



  for (int i=0; i<PWM_MAX_CH; i++)
  {
    pwm_tbl[i].duty = 0;
    pwm_tbl[i].max_value = 100;
  }

  __HAL_RCC_TIM13_CLK_ENABLE();

  htim13.Instance = TIM13;
  htim13.Init.Prescaler = 274;
  htim13.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim13.Init.Period = 99;
  htim13.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim13.Init.RepetitionCounter = 0;
  htim13.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim13) != HAL_OK)
  {
    return false;
  }
  if (HAL_TIM_PWM_Init(&htim13) != HAL_OK)
  {
    return false;
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim13, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    return false;
  }

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOF_CLK_ENABLE();
  /**TIM13 GPIO Configuration
  PF8     ------> TIM13_CH1
  */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF9_TIM13;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);


  HAL_TIM_PWM_Start(&htim13, TIM_CHANNEL_1);

  is_init = ret;

  logPrintf("[%s] pwmInit()\n", ret ? "OK" : "NG");

#ifdef _USE_HW_CLI
  cliAdd("pwm", cliPwm);
#endif
  return ret;
}

bool pwmIsInit(void)
{
  return is_init;
}

void pwmWrite(uint8_t ch, uint16_t pwm_data)
{
  if (ch >= PWM_MAX_CH) return;

  pwm_tbl[ch].duty = constrain(pwm_data, 0, pwm_tbl[ch].max_value);

  switch(ch)
  {
    case _DEF_PWM1:
      htim13.Instance->CCR1 = pwm_tbl[ch].duty;
      break;
  }
}

uint16_t pwmRead(uint8_t ch)
{
  if (ch >= HW_PWM_MAX_CH) return 0;

  return pwm_tbl[ch].duty;
}

uint16_t pwmGetMax(uint8_t ch)
{
  if (ch >= HW_PWM_MAX_CH) return 255;

  return pwm_tbl[ch].max_value;
}



#ifdef _USE_HW_CLI
void cliPwm(cli_args_t *args)
{
  bool ret = true;
  uint8_t  ch;
  uint32_t pwm;


  if (args->argc == 3)
  {
    ch  = (uint8_t)args->getData(1);
    pwm = (uint8_t)args->getData(2);

    ch = constrain(ch, 0, PWM_MAX_CH);

    if(args->isStr(0, "set"))
    {
      pwmWrite(ch, pwm);
      cliPrintf("pwm ch%d %d\n", ch, pwm);
    }
    else
    {
      ret = false;
    }
  }
  else if (args->argc == 2)
  {
    ch = (uint8_t)args->getData(1);

    if(args->isStr(0, "get"))
    {
      cliPrintf("pwm ch%d %d\n", ch, pwmRead(ch));
    }
    else
    {
      ret = false;
    }
  }
  else
  {
    ret = false;
  }


  if (ret == false)
  {
    cliPrintf( "pwm set 0~%d 0~255 \n", PWM_MAX_CH-1);
    cliPrintf( "pwm get 0~%d \n", PWM_MAX_CH-1);
  }

}
#endif

#endif