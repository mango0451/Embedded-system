/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct task
{
    int state;
    unsigned long period;
    unsigned long elaspedTime;
    int (*Function) (int);
} task;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;

/* USER CODE BEGIN PV */
const unsigned int numTasks = 2;
const unsigned long period = 1;
const unsigned long TL_period = 1000;
const unsigned long UL_period = 1000;

volatile uint8_t timerFlag = 0;

task tasks[2];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM1_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

//LCD Display

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

void LCD_EnablePulse(void)
{
	HAL_GPIO_WritePin(LCD_E_GPIO_PORT, LCD_E_PIN, GPIO_PIN_SET);
	HAL_Delay(1);
	HAL_GPIO_WritePin(LCD_E_GPIO_PORT, LCD_E_PIN, GPIO_PIN_RESET);
}

void LCD_ToggleEnablePulse(void)
{
	HAL_GPIO_TogglePin(LCD_E_GPIO_PORT, LCD_E_PIN);
}

void LCD_Send4Bits(uint8_t data)
{
	HAL_GPIO_WritePin(LCD_D4_GPIO_PORT, LCD_D4_PIN, (data & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
	HAL_GPIO_WritePin(LCD_D5_GPIO_PORT, LCD_D5_PIN, (data & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
	HAL_GPIO_WritePin(LCD_D6_GPIO_PORT, LCD_D6_PIN, (data & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
	HAL_GPIO_WritePin(LCD_D7_GPIO_PORT, LCD_D7_PIN, (data & 0x08) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void LCD_Send(uint8_t value, uint8_t isData)
{
	// RS
	HAL_GPIO_WritePin(LCD_RS_GPIO_PORT, LCD_RS_PIN,
					  isData ? GPIO_PIN_SET : GPIO_PIN_RESET);

	// high nibble
	LCD_Send4Bits(value >> 4);
	LCD_EnablePulse();

	// low nibble
	LCD_Send4Bits(value & 0x0F);
	LCD_EnablePulse();
}

void LCD_Command(uint8_t cmd)
{
	LCD_Send(cmd, 0);
}

void LCD_Data(uint8_t data)
{
	LCD_Send(data, 1);
}

void LCD_Init(void)
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

	// function set: 4-bit, 2 lines, 5x8 font
	LCD_Command(0x28);
	// display off
	LCD_Command(0x08);
	// clear
	LCD_Command(0x01);
	HAL_Delay(2);
	// entry mode: increment, no shift
	LCD_Command(0x06);
	// display on, cursor off, blink off
	LCD_Command(0x0C);
}

void LCD_SetCursor(uint8_t col, uint8_t row)
{
	uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
	LCD_Command(0x80 | (col + row_offsets[row]));
}

void LCD_Print(char *str)
{
	while (*str)
	{
		LCD_Data((uint8_t)*str++);
	}
}

//I2C

#define INA1_ADDR   (0x40 << 1)
#define INA2_ADDR   (0x41 << 1)

#define INA219_REG_CONFIG   0x00
#define INA219_REG_SHUNT_V  0x01
#define INA219_REG_BUS_V    0x02
#define INA219_REG_POWER    0x03
#define INA219_REG_CURRENT  0x04
#define INA219_REG_CALIB    0x05

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

static void INA219_Init()
{
    float currentLSB_A = 0.0001f;
    uint16_t calib = (uint16_t)(0.04096f / (currentLSB_A * 0.1));
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



enum TL_states {TL0, TL1, TL2};
int ThreeLED(int state)
{
    switch(state)
    {
        case(TL0):
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
        	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
        	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
        	state = TL1;
            break;
        case(TL1):
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
			state = TL2;
            break;
        case(TL2):
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
            state = TL0;
            break;
    }
    return state;
}

enum UL_states {UL0, UL1};

int UpdateLCD(int state)
{
	GPIO_PinState s = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7);
	if (s == GPIO_PIN_SET) HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_6);
    switch (state)
    {
        case UL0:
        {
        	float v = INA219_GetBusVoltage_V_Addr(INA1_ADDR);
        	float i = INA219_GetCurrent(INA1_ADDR);

        	if(v < 0 ) v = 0;
        	if(i < 0) i = 0;

            char l1[17], l2[17];
            snprintf(l1, sizeof(l1), "CH1 V: %.2fV", v);
            snprintf(l2, sizeof(l2), "CH1 I: %.2fmA", i);

            LCD_Command(0x01);
            LCD_SetCursor(0, 0);
            LCD_Print(l1);
            LCD_SetCursor(0, 1);
            LCD_Print(l2);

            state = UL1;
            break;
        }
        case UL1:
        {
        	float v = INA219_GetBusVoltage_V_Addr(INA2_ADDR);
        	float i = INA219_GetCurrent(INA2_ADDR);

        	if(v < 0 ) v = 0;
			if(i < 0) i = 0;

            char l1[17], l2[17];
            snprintf(l1, sizeof(l1), "CH2 V: %.2fV", v);
            snprintf(l2, sizeof(l2), "CH2 I: %.2fmA", i);

            LCD_Command(0x01);
            LCD_SetCursor(0, 0);
            LCD_Print(l1);
            LCD_SetCursor(0, 1);
            LCD_Print(l2);

            state = UL0;
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

	tasks[0].state = TL0;
	tasks[0].period = TL_period;
	tasks[0].elaspedTime = TL_period;
	tasks[0].Function = &ThreeLED;

	tasks[1].state = UL0;
	tasks[1].period = UL_period;
	tasks[1].elaspedTime = UL_period;
	tasks[1].Function = &UpdateLCD;

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM1_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */

  LCD_Init();
//  LCD_SetCursor(0, 0);
//  LCD_Print("Hello, STM32!");
//
//  LCD_SetCursor(0, 1);
//  LCD_Print("LCD 16x2 demo");

  INA219_Init();

  HAL_TIM_Base_Start_IT(&htim1);


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	if(timerFlag)
	{
		timerFlag = 0;
		unsigned char i;
		for(i = 0; i < numTasks; i++)
		  {
			  if(tasks[i].elaspedTime >= tasks[i].period)
			  {
				  tasks[i].state = tasks[i].Function (tasks[i].state);
				  tasks[i].elaspedTime = 0;
			  }
			  tasks[i].elaspedTime += period;
		  }
	}
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_10;
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
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
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
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SMPS;
  PeriphClkInitStruct.SmpsClockSelection = RCC_SMPSCLKSOURCE_HSI;
  PeriphClkInitStruct.SmpsDivSelection = RCC_SMPSCLKDIV_RANGE0;

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
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 47;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 999;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pins : PA0 PA1 PA2 PA3
                           PA4 PA5 PA6 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PA7 */
  GPIO_InitStruct.Pin = GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1)
    {
    	timerFlag = 1;
    }
}


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
#ifdef USE_FULL_ASSERT
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
