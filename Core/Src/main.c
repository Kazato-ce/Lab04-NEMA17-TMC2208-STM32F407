/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Điều khiển động cơ bước NEMA 17 qua driver TMC2208 bằng
  *                   STM32F407 và giao tiếp USB CDC
  *                   Lớp: CE224.R12 - GVHD: Trần Ngọc Đức
  *                   Nhóm SV:
  *                   1. Nguyễn Đoàn Thanh Phong - MSSV: 24521338
  *                   2. Đặng Thế Tú            - MSSV: 24521904
  *                   3. Hoàng Minh Quân         - MSSV: 24521428
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "usbd_cdc_if.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
  TIM_HandleTypeDef *htim;
  uint32_t channel;
  GPIO_TypeDef *en_port;
  uint16_t en_pin;
  GPIO_TypeDef *dir_port;
  uint16_t dir_pin;
} Motor_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TIMER_CLK_HZ     1000000UL /* Tần số xung đếm timer 1 MHz (1 tick = 1 us) */
#define F_STEP_MAX_HZ    8000UL    /* Tần số STEP tối đa ứng với 100% tốc độ */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim5;

/* USER CODE BEGIN PV */
volatile uint8_t cmd_ready = 0;
char cmd_buf[64];

/*
 * Cấu hình chân phần cứng theo báo cáo:
 * Motor 1: EN = PA0, DIR = PA1, STEP = PA2 (TIM5_CH3)
 * Motor 2: EN = PA3, DIR = PA4, STEP = PA5 (TIM2_CH1)
 */
static Motor_t motor[2] = {
  { &htim5, TIM_CHANNEL_3, GPIOA, GPIO_PIN_0, GPIOA, GPIO_PIN_1 }, /* M1 */
  { &htim2, TIM_CHANNEL_1, GPIOA, GPIO_PIN_3, GPIOA, GPIO_PIN_4 }  /* M2 */
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
void MX_TIM2_Init(void);
void MX_TIM5_Init(void);
/* USER CODE BEGIN PFP */
static void Motor_Set(uint8_t id, char dir, uint8_t pct);
static void Process_Command(char *line);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
  * @brief  Điều khiển động cơ bước theo chiều quay và phần trăm tốc độ
  * @param  id: 0 cho Động cơ 1, 1 cho Động cơ 2
  * @param  dir: 'F' (thuận), 'R' (nghịch), 'S' (dừng)
  * @param  pct: Tốc độ từ 0 đến 100 (%)
  */
static void Motor_Set(uint8_t id, char dir, uint8_t pct)
{
  if (id >= 2) return;
  Motor_t *m = &motor[id];

  /* Trường hợp dừng (S) hoặc phần trăm tốc độ bằng 0 */
  if (dir == 'S' || pct == 0)
  {
    __HAL_TIM_SET_COMPARE(m->htim, m->channel, 0); /* Ngừng xung STEP */
    return; /* Giữ EN = 0: motor duy trì lực giữ vị trí (holding torque) */
  }

  /* Giới hạn phần trăm tốc độ tối đa 100% */
  if (pct > 100) pct = 100;

  /* Tính toán tần số STEP: f = (pct / 100) * F_STEP_MAX_HZ */
  uint32_t f = (F_STEP_MAX_HZ * pct) / 100;
  if (f == 0) f = 1;

  /* ARR = (TIMER_CLK_HZ / f) - 1 */
  uint32_t arr = (TIMER_CLK_HZ / f) - 1;

  /* Đặt chiều quay trên chân DIR: 'F' = SET, 'R' = RESET */
  HAL_GPIO_WritePin(m->dir_port, m->dir_pin,
                    (dir == 'F') ? GPIO_PIN_SET : GPIO_PIN_RESET);

  /* Bật driver: Chân EN tích cực mức thấp (RESET = 0) */
  HAL_GPIO_WritePin(m->en_port, m->en_pin, GPIO_PIN_RESET);

  /* Cập nhật Auto-reload và Duty cycle 50% */
  __HAL_TIM_SET_AUTORELOAD(m->htim, arr);
  __HAL_TIM_SET_COMPARE(m->htim, m->channel, (arr + 1) / 2);
  __HAL_TIM_SET_COUNTER(m->htim, 0); /* Reset counter tránh CNT > ARR mới */
}

/**
  * @brief  Xử lý chuỗi lệnh nhận từ USB CDC Virtual COM Port
  *         Cú pháp: M<id> <hướng> <phần trăm>
  *         Ví dụ: M1 F 60, M2 R 80, M1 S 0
  */
static void Process_Command(char *line)
{
  unsigned int id = 0, pct = 0;
  char dir = 0;
  char msg[48];
  int n;

  /* Bỏ qua khoảng trắng đầu dòng */
  while (*line == ' ' || *line == '\t') line++;

  /* Parse lệnh điều khiển động cơ */
  if (sscanf(line, "M%u %c %u", &id, &dir, &pct) == 3 ||
      sscanf(line, "m%u %c %u", &id, &dir, &pct) == 3)
  {
    dir = (char)toupper((unsigned char)dir);
    if ((id == 1 || id == 2) && pct <= 100 &&
        (dir == 'F' || dir == 'R' || dir == 'S'))
    {
      Motor_Set((uint8_t)(id - 1), dir, (uint8_t)pct);
      n = snprintf(msg, sizeof(msg), "OK M%u %c %u\r\n", id, dir, pct);
      CDC_Transmit_FS((uint8_t *)msg, (uint16_t)n);
      return;
    }
  }

  /* Nếu lệnh không hợp lệ, phản hồi ERR */
  CDC_Transmit_FS((uint8_t *)"ERR\r\n", 5);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM2_Init();
  MX_TIM5_Init();
  MX_USB_DEVICE_Init();

  /* USER CODE BEGIN 2 */
  /* Bắt đầu tạo PWM cho chân STEP động cơ 1 (PA2) và động cơ 2 (PA5) */
  HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_3); /* STEP motor 1 - PA2 */
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1); /* STEP motor 2 - PA5 */

  /* Đảm bảo trạng thái ban đầu compare = 0 (chưa phát xung) */
  __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_3, 0);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (cmd_ready)
    {
      cmd_ready = 0;
      Process_Command(cmd_buf);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  *        HSE = 8.0 MHz Crystal
  *        SYSCLK = 168 MHz
  *        HCLK = 168 MHz
  *        APB1 = 42 MHz  -> APB1 Timer clock = 84 MHz
  *        APB2 = 84 MHz  -> APB2 Timer clock = 168 MHz
  *        USB OTG FS = 48 MHz
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM2 Initialization Function (Motor 2 STEP - PA5: TIM2_CH1)
  * @param None
  * @retval None
  */
void MX_TIM2_Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* Tính toán Prescaler tự động theo xung clock APB1 Timer để đạt 1 MHz */
  uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
  uint32_t timer_clk = ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1) ? (2 * pclk1) : pclk1;
  uint32_t psc = (timer_clk / TIMER_CLK_HZ) - 1;

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = psc;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_TIM_MspPostInit(&htim2);
}

/**
  * @brief TIM5 Initialization Function (Motor 1 STEP - PA2: TIM5_CH3)
  * @param None
  * @retval None
  */
void MX_TIM5_Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* Tính toán Prescaler tự động theo xung clock APB1 Timer để đạt 1 MHz */
  uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
  uint32_t timer_clk = ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1) ? (2 * pclk1) : pclk1;
  uint32_t psc = (timer_clk / TIMER_CLK_HZ) - 1;

  htim5.Instance = TIM5;
  htim5.Init.Prescaler = psc;
  htim5.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim5.Init.Period = 999;
  htim5.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim5) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim5, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim5) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim5, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim5, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_TIM_MspPostInit(&htim5);
}

/**
  * @brief GPIO Initialization Function
  *        PA0: M1_EN  (Output PP, Mức ban đầu = HIGH -> Driver tắt)
  *        PA1: M1_DIR (Output PP, Mức ban đầu = LOW)
  *        PA3: M2_EN  (Output PP, Mức ban đầu = HIGH -> Driver tắt)
  *        PA4: M2_DIR (Output PP, Mức ban đầu = LOW)
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*
   * Mức ban đầu:
   * EN (PA0, PA3) = HIGH (1): Driver tắt, tránh cuộn dây bị cấp dòng sinh nhiệt khi vừa khởi động
   * DIR (PA1, PA4) = LOW (0): Mặc định
   */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0 | GPIO_PIN_3, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1 | GPIO_PIN_4, GPIO_PIN_RESET);

  /* Configure GPIO pins : PA0 (EN1), PA1 (DIR1), PA3 (EN2), PA4 (DIR2) */
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
