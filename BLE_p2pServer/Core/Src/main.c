/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @author  MCD Application Team / Rhett
  * @brief   BLE application with BLE core + LCD time display + INA219
  *
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2019-2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  @verbatim
  ==============================================================================
                    ##### IMPORTANT NOTE #####
  ==============================================================================
  This application requests having the stm32wb5x_BLE_Stack_fw.bin binary
  flashed on the Wireless Coprocessor.
  @endverbatim
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "p2p_server_app.h"      // p2p calls Clock_SetTime / Clock_SetAlarm in this file

/* BLE helper to publish INA219 sensor data (implemented in p2p_server_app.c) */
extern void P2PS_APP_PublishSensors(int16_t mv1, int16_t ma1,
                                    int16_t mv2, int16_t ma2);
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct task
{
  int state;
  unsigned long period;       // ms between runs
  unsigned long elapsedTime;  // last run time (HAL_GetTick)
  int (*Function) (int);
} task;

enum TL_states { TL0, TL1, TL2 };
enum UL_states { UL0, UL1 };
enum SL_states { SL0 };         // sensor task state
enum DA_states { DA0 };
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

// LCD pins
#define LCD_RS_GPIO_PORT   GPIOA
#define LCD_RS_PIN         GPIO_PIN_2

#define LCD_E_GPIO_PORT    GPIOA
#define LCD_E_PIN          GPIO_PIN_3

#define LCD_D4_GPIO_PORT   GPIOA
#define LCD_D4_PIN         GPIO_PIN_5
#define LCD_D5_GPIO_PORT   GPIOA
#define LCD_D5_PIN         GPIO_PIN_0
#define LCD_D6_GPIO_PORT   GPIOA
#define LCD_D6_PIN         GPIO_PIN_1
#define LCD_D7_GPIO_PORT   GPIOA
#define LCD_D7_PIN         GPIO_PIN_4

// INA219 addresses (shifted for HAL)
#define INA1_ADDR   (0x40 << 1)
#define INA2_ADDR   (0x41 << 1)

#define INA219_REG_CONFIG   0x00
#define INA219_REG_SHUNT_V  0x01
#define INA219_REG_BUS_V    0x02
#define INA219_REG_POWER    0x03
#define INA219_REG_CURRENT  0x04
#define INA219_REG_CALIB    0x05

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c3;

IPCC_HandleTypeDef hipcc;

UART_HandleTypeDef hlpuart1;
UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_lpuart1_tx;
DMA_HandleTypeDef hdma_usart1_tx;

RNG_HandleTypeDef hrng;

RTC_HandleTypeDef hrtc;

/* USER CODE BEGIN PV */

// hi2c1 is used for INA219 access

// simple scheduler
const unsigned int  numTasks   = 4;
const unsigned long basePeriod = 1;      // not actually used with HAL_GetTick
const unsigned long TL_period  = 1000;   // 1s
const unsigned long UL_period  = 1000;   // 1s
const unsigned long SL_period  = 1000;   // 1s sensor update
const unsigned long DA_period = 10;


task tasks[4];

/* Global clock (24-hour) that BLE will set via Clock_SetTime */
volatile uint8_t  g_hours         = 0;
volatile uint8_t  g_minutes       = 0;
volatile uint8_t  g_seconds       = 0;
volatile uint32_t g_bleWriteCount = 0;   // increments every time BLE sets time

/* Alarm (24-hour) controlled by BLE via Clock_SetAlarm */
volatile uint8_t  g_alarmHour     = 7;
volatile uint8_t  g_alarmMinute   = 0;
volatile uint8_t  g_alarmEnabled  = 0;   // 1 = armed
volatile uint8_t  g_alarmLatched  = 0;   // 1 = already fired

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_RTC_Init(void);
static void MX_IPCC_Init(void);
static void MX_RNG_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C3_Init(void);
static void MX_RF_Init(void);
/* USER CODE BEGIN PFP */

/* LCD helpers */
static void LCD_EnablePulse(void);
static void LCD_Send4Bits(uint8_t data);
static void LCD_Send(uint8_t value, uint8_t isData);
static void LCD_Command(uint8_t cmd);
static void LCD_Data(uint8_t data);
static void LCD_Init(void);
static void LCD_SetCursor(uint8_t col, uint8_t row);
static void LCD_Print(char *str);

/* INA219 helpers */
static uint16_t INA219_ReadReg(uint16_t addr, uint8_t reg);
static void     INA219_WriteReg(uint16_t addr, uint8_t reg, uint16_t val);
static void     INA219_Init(void);
static float    INA219_GetBusVoltage_V_Addr(uint16_t addr);
static float    INA219_GetCurrent(uint16_t addr);

/* Clock API: BLE calls these when it receives new time / alarm */
void Clock_SetTime(uint8_t hour24, uint8_t minute, uint8_t second);
void Clock_SetAlarm(uint8_t hour24, uint8_t minute);

/* task functions */
int ThreeLED(int state);
int UpdateLCD(int state);
int UpdateSensors(int state);   // INA219 -> BLE
int DisableAlarm(int state);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*********** CLOCK API ***********/
void Clock_SetTime(uint8_t hour24, uint8_t minute, uint8_t second)
{
  if (hour24 >= 24)  hour24  = 0;
  if (minute >= 60)  minute  = 0;
  if (second >= 60)  second  = 0;

  g_hours   = hour24;
  g_minutes = minute;
  g_seconds = second;

  g_bleWriteCount++;    // count how many times BLE called us
}

void Clock_SetAlarm(uint8_t hour24, uint8_t minute)
{
  if (hour24 >= 24)  hour24 = 0;
  if (minute >= 60)  minute = 0;

  g_alarmHour    = hour24;
  g_alarmMinute  = minute;
  g_alarmEnabled = 1;
  g_alarmLatched = 0;

  /* Start with LED low */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);

  /* DEBUG: blink PA6 three times whenever an alarm is programmed */
  for (int i = 0; i < 3; i++)
  {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
    HAL_Delay(150);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
    HAL_Delay(150);
  }
}

/*********** LCD DRIVER ***********/
static void LCD_EnablePulse(void)
{
  HAL_GPIO_WritePin(LCD_E_GPIO_PORT, LCD_E_PIN, GPIO_PIN_SET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(LCD_E_GPIO_PORT, LCD_E_PIN, GPIO_PIN_RESET);
}

static void LCD_Send4Bits(uint8_t data)
{
  HAL_GPIO_WritePin(LCD_D4_GPIO_PORT, LCD_D4_PIN, (data & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_D5_GPIO_PORT, LCD_D5_PIN, (data & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_D6_GPIO_PORT, LCD_D6_PIN, (data & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_D7_GPIO_PORT, LCD_D7_PIN, (data & 0x08) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void LCD_Send(uint8_t value, uint8_t isData)
{
  HAL_GPIO_WritePin(LCD_RS_GPIO_PORT, LCD_RS_PIN,
                    isData ? GPIO_PIN_SET : GPIO_PIN_RESET);

  LCD_Send4Bits(value >> 4);
  LCD_EnablePulse();

  LCD_Send4Bits(value & 0x0F);
  LCD_EnablePulse();
}

static void LCD_Command(uint8_t cmd)
{
  LCD_Send(cmd, 0);
}

static void LCD_Data(uint8_t data)
{
  LCD_Send(data, 1);
}

static void LCD_Init(void)
{
  HAL_Delay(50);

  LCD_Send4Bits(0x03);
  LCD_EnablePulse();
  HAL_Delay(5);

  LCD_Send4Bits(0x03);
  LCD_EnablePulse();
  HAL_Delay(5);

  LCD_Send4Bits(0x03);
  LCD_EnablePulse();
  HAL_Delay(5);

  LCD_Send4Bits(0x02);
  LCD_EnablePulse();
  HAL_Delay(5);

  LCD_Command(0x28); // 4-bit, 2-line
  LCD_Command(0x08); // display off
  LCD_Command(0x01); // clear
  HAL_Delay(2);
  LCD_Command(0x06); // entry mode
  LCD_Command(0x0C); // display on, cursor off
}

static void LCD_SetCursor(uint8_t col, uint8_t row)
{
  uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
  LCD_Command(0x80 | (col + row_offsets[row]));
}

static void LCD_Print(char *str)
{
  while (*str)
  {
    LCD_Data((uint8_t)*str++);
  }
}

/*********** INA219 ***********/
static uint16_t INA219_ReadReg(uint16_t addr, uint8_t reg)
{
  uint8_t buf[2];
  if (HAL_I2C_Mem_Read(&hi2c1, addr, reg, I2C_MEMADD_SIZE_8BIT, buf, 2, HAL_MAX_DELAY) != HAL_OK)
    return 0;
  return (uint16_t)((buf[0] << 8) | buf[1]);
}

static void INA219_WriteReg(uint16_t addr, uint8_t reg, uint16_t val)
{
  uint8_t buf[2] = { (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
  HAL_I2C_Mem_Write(&hi2c1, addr, reg, I2C_MEMADD_SIZE_8BIT, buf, 2, HAL_MAX_DELAY);
}

static void INA219_Init(void)
{
  float currentLSB_A = 0.0001f;
  uint16_t calib = (uint16_t)(0.04096f / (currentLSB_A * 0.1f));

  INA219_WriteReg(INA1_ADDR, INA219_REG_CONFIG, 0x399F);
  INA219_WriteReg(INA1_ADDR, INA219_REG_CALIB, calib);

  INA219_WriteReg(INA2_ADDR, INA219_REG_CONFIG, 0x399F);
  INA219_WriteReg(INA2_ADDR, INA219_REG_CALIB, calib);
}

static float INA219_GetBusVoltage_V_Addr(uint16_t addr)
{
  uint16_t v = INA219_ReadReg(addr, INA219_REG_BUS_V);
  v >>= 3;
  return v * 0.004f;
}

static float INA219_GetCurrent(uint16_t addr)
{
  int16_t raw = (int16_t)INA219_ReadReg(addr, INA219_REG_CURRENT);
  return raw * 0.1f;
}

/*********** TASKS ***********/
int ThreeLED(int state)
{
  switch (state)
  {
    case TL0:
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
      state = TL1;
      break;

    case TL1:
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
      state = TL2;
      break;

    case TL2:
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
      state = TL0;
      break;

    default:
      state = TL0;
      break;
  }
  return state;
}

int UpdateLCD(int state)
{
  switch (state)
  {
    case UL0:
    {
      /* Local 1 Hz software clock tick */
      g_seconds++;
      if (g_seconds >= 60)
      {
        g_seconds = 0;
        g_minutes++;
      }
      if (g_minutes >= 60)
      {
        g_minutes = 0;
        g_hours = (g_hours + 1) % 24;
      }

      /* Alarm check */
      if (g_alarmEnabled && !g_alarmLatched &&
          g_hours == g_alarmHour &&
          g_minutes == g_alarmMinute)
      {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);  // alarm LED ON
        g_alarmLatched = 1;
      }

      /* Snapshot for display */
      uint8_t  h24 = g_hours;
      uint8_t  m   = g_minutes;
      uint32_t cnt = g_bleWriteCount;

      /* Convert to 12-hour + AM/PM */
      uint8_t h12;
      const char *ampm;

      if (h24 == 0)
      {
        h12  = 12;
        ampm = "AM";
      }
      else if (h24 < 12)
      {
        h12  = h24;
        ampm = "AM";
      }
      else if (h24 == 12)
      {
        h12  = 12;
        ampm = "PM";
      }
      else
      {
        h12  = h24 - 12;
        ampm = "PM";
      }

      char line1[17];
      char line2[17];

      /* Top line: BLE write count (debug) */
      snprintf(line1, sizeof(line1), "BLE:%2lu        ", (unsigned long)cnt);
      /* Second line: time */
      snprintf(line2, sizeof(line2), "%2u:%02u %s     ", h12, m, ampm);

      LCD_SetCursor(0, 0);
      LCD_Print(line1);

      LCD_SetCursor(0, 1);
      LCD_Print(line2);

      break;
    }

    default:
      break;
  }
  return state;
}

/* INA219 periodic read + BLE publish */
/* INA219 periodic read + BLE publish */
int UpdateSensors(int state)
{
  switch (state)
  {
    case SL0:
    {
      /* Visual proof the sensor task is running at 1 Hz */
      HAL_GPIO_TogglePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin);

      /* Read real INA219 values */
      float v1_V   = INA219_GetBusVoltage_V_Addr(INA1_ADDR);  // volts
      float i1_mA  = INA219_GetCurrent(INA1_ADDR);            // milliamps

      float v2_V   = INA219_GetBusVoltage_V_Addr(INA2_ADDR);  // volts
      float i2_mA  = INA219_GetCurrent(INA2_ADDR);            // milliamps

      /* Convert to mV / mA for BLE payload */
      int16_t mv1 = (int16_t)(v1_V * 1000.0f);  // volts -> mV
      int16_t ma1 = (int16_t)(i1_mA);           // already mA

      int16_t mv2 = (int16_t)(v2_V * 1000.0f);
      int16_t ma2 = (int16_t)(i2_mA);

      P2PS_APP_PublishSensors(mv1, ma1, mv2, ma2);
      break;
    }

    default:
      break;
  }

  return state;
}

int DisableAlarm(int state)
{
	switch (state)
	{
		case DA0:
		{
			GPIO_PinState s = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7);
			if(s == GPIO_PIN_SET) HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
			break;
		}
	}
	return state;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  // init tasks
  tasks[0].state       = TL0;
  tasks[0].period      = TL_period;
  tasks[0].elapsedTime = 0;
  tasks[0].Function    = &ThreeLED;

  tasks[1].state       = UL0;
  tasks[1].period      = UL_period;
  tasks[1].elapsedTime = 0;
  tasks[1].Function    = &UpdateLCD;

  /* sensor task */
  tasks[2].state       = SL0;
  tasks[2].period      = SL_period;
  tasks[2].elapsedTime = 0;
  tasks[2].Function    = &UpdateSensors;

  tasks[3].state       = DA0;
  tasks[3].period      = DA_period;
  tasks[3].elapsedTime = 0;
  tasks[3].Function    = &DisableAlarm;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();
  /* Config code for STM32_WPAN (HSE Tuning must be done before system clock configuration) */
  MX_APPE_Config();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* IPCC initialisation */
  MX_IPCC_Init();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_RTC_Init();
  MX_RNG_Init();
  MX_I2C1_Init();
  MX_I2C3_Init();
  MX_RF_Init();
  /* USER CODE BEGIN 2 */

  LCD_Init();

  /* INA219 for sensors */
  INA219_Init();

  /* Alarm LED quick blink at startup */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
  HAL_Delay(200);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
  HAL_Delay(200);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);

  /* Initial LCD message */
  LCD_SetCursor(0, 0);
  LCD_Print("BLE: 0         ");
  LCD_SetCursor(0, 1);
  LCD_Print("12:00 AM       ");

  /* USER CODE END 2 */

  /* Init code for STM32_WPAN */
  MX_APPE_Init();

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    MX_APPE_Process();

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();
    for (unsigned int i = 0; i < numTasks; i++)
    {
      if ((now - tasks[i].elapsedTime) >= tasks[i].period)
      {
        tasks[i].state = tasks[i].Function(tasks[i].state);
        tasks[i].elapsedTime = now;
      }
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI
                              |RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the SYSCLKSource, HCLK, PCLK1 and PCLK2 clocks dividers
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK4|RCC_CLOCKTYPE_HCLK2
                              |RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.AHBCLK2Divider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLK4Divider = RCC_SYSCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SMPS|RCC_PERIPHCLK_RFWAKEUP;
  PeriphClkInitStruct.RFWakeUpClockSelection = RCC_RFWKPCLKSOURCE_LSE;
  PeriphClkInitStruct.SmpsClockSelection = RCC_SMPSCLKSOURCE_HSE;
  PeriphClkInitStruct.SmpsDivSelection = RCC_SMPSCLKDIV_RANGE1;

  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN Smps */
  /* USER CODE END Smps */
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00B07CB4;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2C3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C3_Init(void)
{

  /* USER CODE BEGIN I2C3_Init 0 */

  /* USER CODE END I2C3_Init 0 */

  /* USER CODE BEGIN I2C3_Init 1 */

  /* USER CODE END I2C3_Init 1 */
  hi2c3.Instance = I2C3;
  hi2c3.Init.Timing = 0x00B07CB4;
  hi2c3.Init.OwnAddress1 = 0;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0;
  hi2c3.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c3, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c3, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C3_Init 2 */

  /* USER CODE END I2C3_Init 2 */

}

/**
  * @brief IPCC Initialization Function
  * @param None
  * @retval None
  */
static void MX_IPCC_Init(void)
{

  /* USER CODE BEGIN IPCC_Init 0 */

  /* USER CODE END IPCC_Init 0 */

  /* USER CODE BEGIN IPCC_Init 1 */

  /* USER CODE END IPCC_Init 1 */
  hipcc.Instance = IPCC;
  if (HAL_IPCC_Init(&hipcc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IPCC_Init 2 */

  /* USER CODE END IPCC_Init 2 */

}

/**
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
void MX_LPUART1_UART_Init(void)
{

  /* USER CODE BEGIN LPUART1_Init 0 */

  /* USER CODE END LPUART1_Init 0 */

  /* USER CODE BEGIN LPUART1_Init 1 */

  /* USER CODE END LPUART1_Init 1 */
  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 115200;
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart1.Init.StopBits = UART_STOPBITS_1;
  hlpuart1.Init.Parity = UART_PARITY_NONE;
  hlpuart1.Init.Mode = UART_MODE_TX_RX;
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  hlpuart1.FifoMode = UART_FIFOMODE_DISABLE;
  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&hlpuart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&hlpuart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPUART1_Init 2 */

  /* USER CODE END LPUART1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_8;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief RF Initialization Function
  * @param None
  * @retval None
  */
static void MX_RF_Init(void)
{

  /* USER CODE BEGIN RF_Init 0 */

  /* USER CODE END RF_Init 0 */

  /* USER CODE BEGIN RF_Init 1 */

  /* USER CODE END RF_Init 1 */
  /* USER CODE BEGIN RF_Init 2 */

  /* USER CODE END RF_Init 2 */

}

/**
  * @brief RNG Initialization Function
  * @param None
  * @retval None
  */
static void MX_RNG_Init(void)
{

  /* USER CODE BEGIN RNG_Init 0 */

  /* USER CODE END RNG_Init 0 */

  /* USER CODE BEGIN RNG_Init 1 */

  /* USER CODE END RNG_Init 1 */
  hrng.Instance = RNG;
  hrng.Init.ClockErrorDetection = RNG_CED_ENABLE;
  if (HAL_RNG_Init(&hrng) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RNG_Init 2 */

  /* USER CODE END RNG_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = CFG_RTC_ASYNCH_PRESCALER;
  hrtc.Init.SynchPrediv = CFG_RTC_SYNCH_PRESCALER;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the WakeUp
  */
  if (HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, 0, RTC_WAKEUPCLOCK_RTCCLK_DIV16) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel4_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 15, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
  /* DMA2_Channel4_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Channel4_IRQn, 15, 0);
  HAL_NVIC_EnableIRQ(DMA2_Channel4_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  //Button

  GPIO_InitStruct.Pin = GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* --- PA6: alarm LED --- */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* --- On-board blue LED --- */
  HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = LED_BLUE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_BLUE_GPIO_Port, &GPIO_InitStruct);

  /* --- LCD pins (all on GPIOA) --- */
  HAL_GPIO_WritePin(LCD_RS_GPIO_PORT, LCD_RS_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_E_GPIO_PORT,  LCD_E_PIN,  GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_D4_GPIO_PORT, LCD_D4_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_D5_GPIO_PORT, LCD_D5_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_D6_GPIO_PORT, LCD_D6_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_D7_GPIO_PORT, LCD_D7_PIN, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin =
      LCD_RS_PIN |
      LCD_E_PIN  |
      LCD_D4_PIN |
      LCD_D5_PIN |
      LCD_D6_PIN |
      LCD_D7_PIN;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */
/* no extra MX_I2C1_Init here; we use the CubeMX one above */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
