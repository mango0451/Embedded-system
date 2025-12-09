/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    p2p_server_app.c
  * @author  MCD Application Team / Rhett
  * @brief   Peer to peer Server Application
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
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "app_common.h"
#include "dbg_trace.h"
#include "ble.h"
#include "p2p_server_app.h"
#include "stm32_seq.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* Clock_SetTime / Clock_SetAlarm are implemented in main.c */
extern void Clock_SetTime (uint8_t hour24, uint8_t minute, uint8_t second);
extern void Clock_SetAlarm(uint8_t hour24, uint8_t minute);
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef struct{
  uint8_t             Device_Led_Selection;
  uint8_t             Led1;
} P2P_LedCharValue_t;

typedef struct{
  uint8_t             Device_Button_Selection;
  uint8_t             ButtonStatus;
} P2P_ButtonCharValue_t;

typedef struct
{
  uint8_t               Notification_Status; /* used to check if P2P Server is enabled to Notify */
  P2P_LedCharValue_t    LedControl;
  P2P_ButtonCharValue_t ButtonControl;
  uint16_t              ConnectionHandle;
} P2P_Server_App_Context_t;

/* USER CODE END PTD */

/* Private defines ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macros -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
/**
 * START of Section BLE_APP_CONTEXT
 */
static P2P_Server_App_Context_t P2P_Server_App_Context;
/**
 * END of Section BLE_APP_CONTEXT
 */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static void P2PS_Send_Notification(void);
static void P2PS_APP_LED_BUTTON_context_Init(void);


void P2PS_APP_PublishTime(uint8_t hour24, uint8_t minute, uint8_t second);
void P2PS_APP_PublishSensors(int16_t mv1, int16_t ma1,
                             int16_t mv2, int16_t ma2);
/* USER CODE END PFP */

/* Functions Definition ------------------------------------------------------*/
void P2PS_STM_App_Notification(P2PS_STM_App_Notification_evt_t *pNotification)
{
/* USER CODE BEGIN P2PS_STM_App_Notification_1 */

/* USER CODE END P2PS_STM_App_Notification_1 */
  switch(pNotification->P2P_Evt_Opcode)
  {
/* USER CODE BEGIN P2PS_STM_App_Notification_P2P_Evt_Opcode */
#if (BLE_CFG_OTA_REBOOT_CHAR != 0)
    case P2PS_STM_BOOT_REQUEST_EVT:
      APP_DBG_MSG("-- P2P APPLICATION SERVER : BOOT REQUESTED\n");
      APP_DBG_MSG(" \n\r");

      *(uint32_t*)SRAM1_BASE = *(uint32_t*)pNotification->DataTransfered.pPayload;
      NVIC_SystemReset();
      break;
#endif
/* USER CODE END P2PS_STM_App_Notification_P2P_Evt_Opcode */

    case P2PS_STM__NOTIFY_ENABLED_EVT:
/* USER CODE BEGIN P2PS_STM__NOTIFY_ENABLED_EVT */
      P2P_Server_App_Context.Notification_Status = 1;
      APP_DBG_MSG("-- P2P APPLICATION SERVER : NOTIFICATION ENABLED\n");
      APP_DBG_MSG(" \n\r");
/* USER CODE END P2PS_STM__NOTIFY_ENABLED_EVT */
      break;

    case P2PS_STM_NOTIFY_DISABLED_EVT:
/* USER CODE BEGIN P2PS_STM_NOTIFY_DISABLED_EVT */
      P2P_Server_App_Context.Notification_Status = 0;
      APP_DBG_MSG("-- P2P APPLICATION SERVER : NOTIFICATION DISABLED\n");
      APP_DBG_MSG(" \n\r");
/* USER CODE END P2PS_STM_NOTIFY_DISABLED_EVT */
      break;

    case P2PS_STM_WRITE_EVT:
/* USER CODE BEGIN P2PS_STM_WRITE_EVT */
    {
      /*
       * 2-byte protocol from the Web UI:
       *
       *   p[0] = hour (0–23) + optional command bit in bit 7
       *   p[1] = minute (0–59)
       *
       * If bit7 of p[0] == 0 → TIME frame:  Clock_SetTime(hour, minute, 0)
       * If bit7 of p[0] == 1 → ALARM frame: Clock_SetAlarm(hour & 0x7F, minute)
       */

      uint8_t len = pNotification->DataTransfered.Length;
      uint8_t *p  = pNotification->DataTransfered.pPayload;

      APP_DBG_MSG("-- P2P SERVER : WRITE EVT len=%d\n", len);

      if (len != 2)
      {
        APP_DBG_MSG("-- P2P SERVER : INVALID LENGTH %d (expect 2)\n", (int)len);
        APP_DBG_MSG(" \n\r");
        break;
      }

      uint8_t rawHour = p[0];
      uint8_t minute  = p[1];

      uint8_t isAlarm = (rawHour & 0x80) ? 1 : 0;
      uint8_t hour    = (rawHour & 0x7F);  /* strip command bit if present */

      /* Clamp ranges */
      if (hour   >= 24) hour   = 0;
      if (minute >= 60) minute = 0;

      if (!isAlarm)
      {
        /* TIME frame */
        Clock_SetTime(hour, minute, 0);
        APP_DBG_MSG("-- P2P SERVER : TIME SYNC %02d:%02d:00\n",
                    (int)hour, (int)minute);
      }
      else
      {
        /* ALARM frame */
        Clock_SetAlarm(hour, minute);
        APP_DBG_MSG("-- P2P SERVER : ALARM SET %02d:%02d\n",
                    (int)hour, (int)minute);
      }

      APP_DBG_MSG(" \n\r");
    }
/* USER CODE END P2PS_STM_WRITE_EVT */
      break;

    default:
/* USER CODE BEGIN P2PS_STM_App_Notification_default */
      /* No other events handled here */
/* USER CODE END P2PS_STM_App_Notification_default */
      break;
  }
/* USER CODE BEGIN P2PS_STM_App_Notification_2 */

/* USER CODE END P2PS_STM_App_Notification_2 */
  return;
}

void P2PS_APP_Notification(P2PS_APP_ConnHandle_Not_evt_t *pNotification)
{
/* USER CODE BEGIN P2PS_APP_Notification_1 */

/* USER CODE END P2PS_APP_Notification_1 */
  switch(pNotification->P2P_Evt_Opcode)
  {
/* USER CODE BEGIN P2PS_APP_Notification_P2P_Evt_Opcode */

/* USER CODE END P2PS_APP_Notification_P2P_Evt_Opcode */
  case PEER_CONN_HANDLE_EVT :
/* USER CODE BEGIN PEER_CONN_HANDLE_EVT */
    P2P_Server_App_Context.ConnectionHandle = pNotification->ConnectionHandle;
    APP_DBG_MSG("-- P2P APPLICATION SERVER : PEER CONNECTED, HANDLE=0x%04X\n",
                P2P_Server_App_Context.ConnectionHandle);
    APP_DBG_MSG(" \n\r");
/* USER CODE END PEER_CONN_HANDLE_EVT */
    break;

    case PEER_DISCON_HANDLE_EVT :
/* USER CODE BEGIN PEER_DISCON_HANDLE_EVT */
    APP_DBG_MSG("-- P2P APPLICATION SERVER : PEER DISCONNECTED\n");
    APP_DBG_MSG(" \n\r");

    P2P_Server_App_Context.ConnectionHandle = 0xFFFF;
    /* Reset context and LED on disconnect */
    P2PS_APP_LED_BUTTON_context_Init();
/* USER CODE END PEER_DISCON_HANDLE_EVT */
    break;

    default:
/* USER CODE BEGIN P2PS_APP_Notification_default */
/* USER CODE END P2PS_APP_Notification_default */
      break;
  }
/* USER CODE BEGIN P2PS_APP_Notification_2 */

/* USER CODE END P2PS_APP_Notification_2 */
  return;
}

void P2PS_APP_Init(void)
{
/* USER CODE BEGIN P2PS_APP_Init */
  UTIL_SEQ_RegTask( 1<< CFG_TASK_SW1_BUTTON_PUSHED_ID, UTIL_SEQ_RFU, P2PS_Send_Notification );

  /**
   * Initialize LedButton Service
   */
  P2P_Server_App_Context.Notification_Status = 0;
  P2P_Server_App_Context.ConnectionHandle    = 0xFFFF;
  P2PS_APP_LED_BUTTON_context_Init();
/* USER CODE END P2PS_APP_Init */
  return;
}

/* USER CODE BEGIN FD */

/**
  * @brief Initialize LED/button context.
  */
void P2PS_APP_LED_BUTTON_context_Init(void)
{
  /* Turn LED off */
  BSP_LED_Off(LED_BLUE);
  APP_DBG_MSG("LED BLUE OFF\n");

#if(P2P_SERVER1 != 0)
  P2P_Server_App_Context.LedControl.Device_Led_Selection      = 0x01; /* Device1 */
  P2P_Server_App_Context.LedControl.Led1                      = 0x00; /* led OFF */
  P2P_Server_App_Context.ButtonControl.Device_Button_Selection= 0x01; /* Device1 */
  P2P_Server_App_Context.ButtonControl.ButtonStatus           = 0x00;
#endif
#if(P2P_SERVER2 != 0)
  P2P_Server_App_Context.LedControl.Device_Led_Selection      = 0x02; /* Device2 */
  P2P_Server_App_Context.LedControl.Led1                      = 0x00; /* led OFF */
  P2P_Server_App_Context.ButtonControl.Device_Button_Selection= 0x02; /* Device2 */
  P2P_Server_App_Context.ButtonControl.ButtonStatus           = 0x00;
#endif
#if(P2P_SERVER3 != 0)
  P2P_Server_App_Context.LedControl.Device_Led_Selection      = 0x03; /* Device3 */
  P2P_Server_App_Context.LedControl.Led1                      = 0x00; /* led OFF */
  P2P_Server_App_Context.ButtonControl.Device_Button_Selection= 0x03; /* Device3 */
  P2P_Server_App_Context.ButtonControl.ButtonStatus           = 0x00;
#endif
#if(P2P_SERVER4 != 0)
  P2P_Server_App_Context.LedControl.Device_Led_Selection      = 0x04; /* Device4 */
  P2P_Server_App_Context.LedControl.Led1                      = 0x00; /* led OFF */
  P2P_Server_App_Context.ButtonControl.Device_Button_Selection= 0x04; /* Device4 */
  P2P_Server_App_Context.ButtonControl.ButtonStatus           = 0x00;
#endif
#if(P2P_SERVER5 != 0)
  P2P_Server_App_Context.LedControl.Device_Led_Selection      = 0x05; /* Device5 */
  P2P_Server_App_Context.LedControl.Led1                      = 0x00; /* led OFF */
  P2P_Server_App_Context.ButtonControl.Device_Button_Selection= 0x05; /* Device5 */
  P2P_Server_App_Context.ButtonControl.ButtonStatus           = 0x00;
#endif
#if(P2P_SERVER6 != 0)
  P2P_Server_App_Context.LedControl.Device_Led_Selection      = 0x06; /* Device6 */
  P2P_Server_App_Context.LedControl.Led1                      = 0x00; /* led OFF */
  P2P_Server_App_Context.ButtonControl.Device_Button_Selection= 0x06; /* Device6 */
  P2P_Server_App_Context.ButtonControl.ButtonStatus           = 0x00;
#endif
}

/**
  * @brief  Button SW1 action – sends notification to client.
  */
void P2PS_APP_SW1_Button_Action(void)
{
  UTIL_SEQ_SetTask( 1<<CFG_TASK_SW1_BUTTON_PUSHED_ID, CFG_SCH_PRIO_0);
  return;
}

/**
  * @brief  Publish current time to client via notify characteristic.
  *         Payload format: [ hour24, minute, second ]
  *         (Currently not used by main.c but left for future use.)
  */
void P2PS_APP_PublishTime(uint8_t hour24, uint8_t minute, uint8_t second)
{
  uint8_t payload[3];

  payload[0] = hour24;
  payload[1] = minute;
  payload[2] = second;

  if (P2P_Server_App_Context.Notification_Status)
  {
    APP_DBG_MSG("-- P2P APPLICATION SERVER : PUBLISH TIME %02d:%02d:%02d\n",
                (int)hour24, (int)minute, (int)second);
    P2PS_STM_App_Update_Char(P2P_NOTIFY_CHAR_UUID, payload);
  }
}

/**
  * @brief  Publish INA219 sensor data to client via notify characteristic.
  *
  *         Payload format (8 bytes, little-endian int16_t):
  *           [ mv1_L, mv1_H, ma1_L, ma1_H, mv2_L, mv2_H, ma2_L, ma2_H ]
  *
  *         mv = millivolts, ma = milliamps
  */
/* USER CODE BEGIN FD_LOCAL_FUNCTIONS */
void P2PS_APP_PublishSensors(int16_t mv1, int16_t ma1,
                             int16_t mv2, int16_t ma2)
{
  if (P2P_Server_App_Context.Notification_Status)
  {
    /* 4 x int16_t = 8 bytes payload */
    int16_t payload[4];
    payload[0] = mv1;
    payload[1] = ma1;
    payload[2] = mv2;
    payload[3] = ma2;

    P2PS_STM_App_Update_Char(P2P_NOTIFY_CHAR_UUID, (uint8_t *)payload);
  }
  else
  {
    APP_DBG_MSG("-- P2P APP : sensors NOT sent, notifications disabled\n");
  }
}




/* USER CODE END FD */

/*************************************************************
 *
 * LOCAL FUNCTIONS
 *
 *************************************************************/
/* USER CODE BEGIN FD_LOCAL_FUNCTIONS*/

/**
  * @brief  Send button-press notification to client.
  *
  * Note: this still uses the same notify characteristic. If you never
  *       trigger SW1, you’ll only ever see the sensor payloads (8 bytes)
  *       on the client side.
  */
static void P2PS_Send_Notification(void)
{
  if(P2P_Server_App_Context.ButtonControl.ButtonStatus == 0x00)
  {
    P2P_Server_App_Context.ButtonControl.ButtonStatus = 0x01;
  }
  else
  {
    P2P_Server_App_Context.ButtonControl.ButtonStatus = 0x00;
  }

  if(P2P_Server_App_Context.Notification_Status)
  {
    APP_DBG_MSG("-- P2P APPLICATION SERVER  : INFORM CLIENT BUTTON 1 PUSHED \n ");
    APP_DBG_MSG(" \n\r");
    P2PS_STM_App_Update_Char(P2P_NOTIFY_CHAR_UUID,
                             (uint8_t *)&P2P_Server_App_Context.ButtonControl);
  }
  else
  {
    APP_DBG_MSG("-- P2P APPLICATION SERVER : CAN'T INFORM CLIENT -  NOTIFICATION DISABLED\n ");
  }

  return;
}

/* USER CODE END FD_LOCAL_FUNCTIONS*/
