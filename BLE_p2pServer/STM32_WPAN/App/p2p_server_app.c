/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    p2p_server_app.c
  * @author  Rhett
  * @brief   Peer to peer Server Application
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
/* Time/alarm handlers in main.c */
extern void Clock_SetTime (uint8_t hour24, uint8_t minute, uint8_t second);
extern void Clock_SetAlarm(uint8_t hour24, uint8_t minute);
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef struct {
  uint8_t Device_Led_Selection;
  uint8_t Led1;
} P2P_LedCharValue_t;

typedef struct {
  uint8_t Device_Button_Selection;
  uint8_t ButtonStatus;
} P2P_ButtonCharValue_t;

typedef struct
{
  uint8_t               Notification_Status;
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
static P2P_Server_App_Context_t P2P_Server_App_Context;
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
      /* Payload:
       *   p[0]: hour (0–23), bit7 = 1 → alarm frame
       *   p[1]: minute (0–59)
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
      uint8_t hour    = (rawHour & 0x7F);

      if (hour   >= 24) hour   = 0;
      if (minute >= 60) minute = 0;

      if (!isAlarm)
      {
        Clock_SetTime(hour, minute, 0);
        APP_DBG_MSG("-- P2P SERVER : TIME SYNC %02d:%02d:00\n",
                    (int)hour, (int)minute);
      }
      else
      {
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
      /* unused */
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
  UTIL_SEQ_RegTask(1 << CFG_TASK_SW1_BUTTON_PUSHED_ID,
                   UTIL_SEQ_RFU,
                   P2PS_Send_Notification);

  P2P_Server_App_Context.Notification_Status = 0;
  P2P_Server_App_Context.ConnectionHandle    = 0xFFFF;
  P2PS_APP_LED_BUTTON_context_Init();
/* USER CODE END P2PS_APP_Init */
  return;
}

/* USER CODE BEGIN FD */

void P2PS_APP_LED_BUTTON_context_Init(void)
{
  BSP_LED_Off(LED_BLUE);
  APP_DBG_MSG("LED BLUE OFF\n");

#if(P2P_SERVER1 != 0)
  P2P_Server_App_Context.LedControl.Device_Led_Selection       = 0x01;
  P2P_Server_App_Context.LedControl.Led1                       = 0x00;
  P2P_Server_App_Context.ButtonControl.Device_Button_Selection = 0x01;
  P2P_Server_App_Context.ButtonControl.ButtonStatus            = 0x00;
#endif
#if(P2P_SERVER2 != 0)
  P2P_Server_App_Context.LedControl.Device_Led_Selection       = 0x02;
  P2P_Server_App_Context.LedControl.Led1                       = 0x00;
  P2P_Server_App_Context.ButtonControl.Device_Button_Selection = 0x02;
  P2P_Server_App_Context.ButtonControl.ButtonStatus            = 0x00;
#endif
#if(P2P_SERVER3 != 0)
  P2P_Server_App_Context.LedControl.Device_Led_Selection       = 0x03;
  P2P_Server_App_Context.LedControl.Led1                       = 0x00;
  P2P_Server_App_Context.ButtonControl.Device_Button_Selection = 0x03;
  P2P_Server_App_Context.ButtonControl.ButtonStatus            = 0x00;
#endif
#if(P2P_SERVER4 != 0)
  P2P_Server_App_Context.LedControl.Device_Led_Selection       = 0x04;
  P2P_Server_App_Context.LedControl.Led1                       = 0x00;
  P2P_Server_App_Context.ButtonControl.Device_Button_Selection = 0x04;
  P2P_Server_App_Context.ButtonControl.ButtonStatus            = 0x00;
#endif
#if(P2P_SERVER5 != 0)
  P2P_Server_App_Context.LedControl.Device_Led_Selection       = 0x05;
  P2P_Server_App_Context.LedControl.Led1                       = 0x00;
  P2P_Server_App_Context.ButtonControl.Device_Button_Selection = 0x05;
  P2P_Server_App_Context.ButtonControl.ButtonStatus            = 0x00;
#endif
#if(P2P_SERVER6 != 0)
  P2P_Server_App_Context.LedControl.Device_Led_Selection       = 0x06;
  P2P_Server_App_Context.LedControl.Led1                       = 0x00;
  P2P_Server_App_Context.ButtonControl.Device_Button_Selection = 0x06;
  P2P_Server_App_Context.ButtonControl.ButtonStatus            = 0x00;
#endif
}

/**
  * @brief  SW1 button action.
  */
void P2PS_APP_SW1_Button_Action(void)
{
  UTIL_SEQ_SetTask(1 << CFG_TASK_SW1_BUTTON_PUSHED_ID, CFG_SCH_PRIO_0);
}

/**
  * @brief  Publish current time over notify characteristic.
  *         Payload: [ hour24, minute, second ].
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
  * @brief  Publish INA219 data over notify characteristic.
  *         Payload: 4 x int16_t = 8 bytes (mv1, ma1, mv2, ma2).
  */
void P2PS_APP_PublishSensors(int16_t mv1, int16_t ma1,
                             int16_t mv2, int16_t ma2)
{
  if (P2P_Server_App_Context.Notification_Status)
  {
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
  * @brief  Send button status to client.
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
