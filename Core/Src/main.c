/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE BEGIN PTD */
typedef enum { LED_MODE_OFF, LED_MODE_ON, LED_MODE_BLINK } LedMode_t;

typedef struct {
    LedMode_t mode;
    uint32_t  period_ms;
} LedCmd_t;

typedef struct {
    uint32_t cmds_ok;
    uint32_t cmds_err;
    uint32_t jobs_done;
} Stats_t;

typedef struct {
    const char *name;
    int (*fn)(int argc, char **argv);   /* return 0 = OK, -1 = error */
    const char *help;
} Command_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE BEGIN PD */
#define LED_PORT        GPIOA
#define LED_PIN         GPIO_PIN_5

/* Task priorities (higher number = higher priority) */
#define PRIO_WORKER     1
#define PRIO_LED        2
#define PRIO_CMD        3

/* Stack sizes in WORDS (4 bytes) */
#define STACK_CMD       384
#define STACK_LED       128
#define STACK_WORKER    384

#define RX_QUEUE_LEN    64
#define LED_QUEUE_LEN   4
#define JOB_POOL_MAX    5      /* counting semaphore max count (N) */
#define CMD_BUF_LEN     64
#define CMD_MAX_ARGS    4
#define TX_BUF_LEN      128
#define NELEMS(a)       (sizeof(a) / sizeof((a)[0]))
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 128 * 4
};
/* USER CODE BEGIN PV */
/* USER CODE BEGIN PV */
QueueHandle_t     qRxBytes;     /* used from it.c (extern) */
QueueHandle_t     qLed;
SemaphoreHandle_t mtxUart;      /* mutex: UART TX */
SemaphoreHandle_t mtxData;      /* mutex: shared stats */
SemaphoreHandle_t semTxDone;    /* binary semaphore (given from ISR) */
SemaphoreHandle_t semJobs;      /* counting semaphore (given from ISR + task) */
TaskHandle_t      hCmdTask, hLedTask, hWorkerTask;

uint8_t rx_byte;                /* single-byte buffer for HAL_UART_Receive_IT */
static Stats_t g_stats;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
/* USER CODE BEGIN PFP */
void StartCmdTask(void *argument);
void StartLedTask(void *argument);
void StartWorkerTask(void *argument);
void uart_printf(const char *fmt, ...);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  /* USER CODE BEGIN 2 */
  HAL_UART_Transmit(&huart2, (uint8_t *)"hello\r\n", 7, 100);
  qRxBytes  = xQueueCreate(RX_QUEUE_LEN, sizeof(uint8_t));
  qLed      = xQueueCreate(LED_QUEUE_LEN, sizeof(LedCmd_t));
  mtxUart   = xSemaphoreCreateMutex();
  mtxData   = xSemaphoreCreateMutex();
  semTxDone = xSemaphoreCreateBinary();                 /* starts empty */
  semJobs   = xSemaphoreCreateCounting(JOB_POOL_MAX, 0); /* max N, initial 0 */

  if (!qRxBytes || !qLed || !mtxUart || !mtxData || !semTxDone || !semJobs)
      Error_Handler();

  if (xTaskCreate(StartCmdTask,    "cmd",    STACK_CMD,    NULL, PRIO_CMD,    &hCmdTask)    != pdPASS) Error_Handler();
  if (xTaskCreate(StartLedTask,    "led",    STACK_LED,    NULL, PRIO_LED,    &hLedTask)    != pdPASS) Error_Handler();
  if (xTaskCreate(StartWorkerTask, "worker", STACK_WORKER, NULL, PRIO_WORKER, &hWorkerTask) != pdPASS) Error_Handler();

  HAL_UART_Receive_IT(&huart2, &rx_byte, 1);   /* start RX interrupt chain */

  vTaskStartScheduler();                        /* never returns */
  Error_Handler();
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 8;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin   = GPIO_PIN_5;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;             /* push-pull output */
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &gpio);

  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* USER CODE BEGIN 4 */
/* ---------- Thread-safe UART printf (mutex + binary semaphore) ---------- */
void uart_printf(const char *fmt, ...)
{
    static char buf[TX_BUF_LEN];            /* protected by mtxUart */

    if (xSemaphoreTake(mtxUart, pdMS_TO_TICKS(200)) != pdTRUE) return;

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) {
        if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
        xSemaphoreTake(semTxDone, 0);       /* clear any stale signal */
        if (HAL_UART_Transmit_IT(&huart2, (uint8_t *)buf, (uint16_t)n) == HAL_OK) {
            /* block until HAL_UART_TxCpltCallback (it.c) gives the semaphore */
            xSemaphoreTake(semTxDone, pdMS_TO_TICKS(200));
        }
    }
    xSemaphoreGive(mtxUart);
}

/* ---------- Shared data helper (mutex) ---------- */
static void stats_inc(uint32_t *field)
{
    if (xSemaphoreTake(mtxData, portMAX_DELAY) == pdTRUE) {
        (*field)++;
        xSemaphoreGive(mtxData);
    }
}

/* ---------- Command handlers: return 0 = OK, -1 = error ---------- */
static int cmd_help(int argc, char **argv);
static int cmd_led(int argc, char **argv);
static int cmd_job(int argc, char **argv);
static int cmd_status(int argc, char **argv);

/* To add a command: write a handler and add one line here */
static const Command_t cmdTable[] = {
    { "help",   cmd_help,   "list commands" },
    { "led",    cmd_led,    "led on|off|blink [period_ms]" },
    { "job",    cmd_job,    "job [n]  - queue n jobs for the worker" },
    { "status", cmd_status, "show stats and stack usage" },
};

static int cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    for (size_t i = 0; i < NELEMS(cmdTable); i++)
        uart_printf("  %-7s %s\r\n", cmdTable[i].name, cmdTable[i].help);
    return 0;
}

static int cmd_led(int argc, char **argv)
{
    LedCmd_t m = { LED_MODE_OFF, 0 };

    if (argc < 2) goto usage;

    if      (strcmp(argv[1], "on")  == 0) m.mode = LED_MODE_ON;
    else if (strcmp(argv[1], "off") == 0) m.mode = LED_MODE_OFF;
    else if (strcmp(argv[1], "blink") == 0) {
        m.mode = LED_MODE_BLINK;
        m.period_ms = (argc >= 3) ? strtoul(argv[2], NULL, 10) : 500;
        if (m.period_ms < 10 || m.period_ms > 10000) {
            uart_printf("period must be 10..10000 ms\r\n");
            return -1;
        }
    } else goto usage;

    if (xQueueSend(qLed, &m, pdMS_TO_TICKS(50)) != pdTRUE) {
        uart_printf("ERR: LED queue full\r\n");
        return -1;
    }
    uart_printf("OK\r\n");
    return 0;

usage:
    uart_printf("usage: led on|off|blink [period_ms]\r\n");
    return -1;
}

static int cmd_job(int argc, char **argv)
{
    uint32_t n = (argc >= 2) ? strtoul(argv[1], NULL, 10) : 1;
    uint32_t queued = 0;

    while (queued < n && xSemaphoreGive(semJobs) == pdTRUE) queued++;

    uart_printf("queued %lu/%lu job(s)%s\r\n", (unsigned long)queued,
                (unsigned long)n, (queued < n) ? " (pool full)" : "");
    return (queued == n) ? 0 : -1;
}

static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    Stats_t s;

    xSemaphoreTake(mtxData, portMAX_DELAY);
    s = g_stats;
    xSemaphoreGive(mtxData);

    uart_printf("uptime : %lu ms\r\n", (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS));
    uart_printf("cmds   : ok=%lu err=%lu\r\n", (unsigned long)s.cmds_ok, (unsigned long)s.cmds_err);
    uart_printf("jobs   : done=%lu pending=%lu\r\n", (unsigned long)s.jobs_done,
                (unsigned long)uxSemaphoreGetCount(semJobs));
    uart_printf("stack free (words): cmd=%lu led=%lu worker=%lu\r\n",
                (unsigned long)uxTaskGetStackHighWaterMark(hCmdTask),
                (unsigned long)uxTaskGetStackHighWaterMark(hLedTask),
                (unsigned long)uxTaskGetStackHighWaterMark(hWorkerTask));
    return 0;
}

/* ---------- Command extraction + dispatch ---------- */
static void Cmd_Process(char *line)
{
    char *argv[CMD_MAX_ARGS];
    char *save = NULL;
    int   argc = 0;

    /* split on spaces; strtok_r is re-entrant, so it is safe with RTOS */
    for (char *t = strtok_r(line, " \t", &save);
         t && argc < CMD_MAX_ARGS;
         t = strtok_r(NULL, " \t", &save))
    {
        for (char *p = t; *p; p++) *p = (char)tolower((unsigned char)*p);
        argv[argc++] = t;
    }
    if (argc == 0) return;

    for (size_t i = 0; i < NELEMS(cmdTable); i++) {
        if (strcmp(argv[0], cmdTable[i].name) == 0) {
            if (cmdTable[i].fn(argc, argv) == 0) stats_inc(&g_stats.cmds_ok);
            else                                 stats_inc(&g_stats.cmds_err);
            return;
        }
    }
    uart_printf("unknown command '%s' (try: help)\r\n", argv[0]);
    stats_inc(&g_stats.cmds_err);
}

/* ---------- TASK 1: command task (prio 3) ---------- */
void StartCmdTask(void *argument)
{
    (void)argument;
    char    line[CMD_BUF_LEN];
    uint8_t idx = 0;
    uint8_t c;

    uart_printf("\r\n== STM32G070 + FreeRTOS ready, type 'help' ==\r\n> ");

    for (;;)
    {
        /* sleeps here until the RX ISR pushes a byte */
        if (xQueueReceive(qRxBytes, &c, portMAX_DELAY) != pdTRUE) continue;

        if (c == '\r' || c == '\n') {
            if (idx > 0) {
                line[idx] = '\0';
                uart_printf("\r\n");
                Cmd_Process(line);
                idx = 0;
                uart_printf("> ");
            }
        }
        else if (c == 0x7F || c == '\b') {          /* backspace */
            if (idx > 0) { idx--; uart_printf("\b \b"); }
        }
        else if (idx < CMD_BUF_LEN - 1) {           /* extra chars are dropped */
            line[idx++] = (char)c;
            uart_printf("%c", c);                   /* echo */
        }
    }
}

/* ---------- TASK 2: LED task (prio 2) ---------- */
void StartLedTask(void *argument)
{
    (void)argument;
    LedCmd_t  cmd;
    LedMode_t mode   = LED_MODE_BLINK;
    uint32_t  period = 500;

    for (;;)
    {
        /* the queue timeout doubles as the blink timer */
        TickType_t wait = (mode == LED_MODE_BLINK) ? pdMS_TO_TICKS(period) : portMAX_DELAY;

        if (xQueueReceive(qLed, &cmd, wait) == pdTRUE) {
            mode = cmd.mode;
            if (mode == LED_MODE_BLINK) period = cmd.period_ms;
            if (mode == LED_MODE_ON)  HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
            if (mode == LED_MODE_OFF) HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
        } else {
            HAL_GPIO_TogglePin(LED_PORT, LED_PIN);   /* timeout -> toggle */
        }
    }
}

/* ---------- TASK 3: worker task (prio 1, lowest) ---------- */
void StartWorkerTask(void *argument)
{
    (void)argument;

    for (;;)
    {
        /* blocks until a job is available (from 'job n' or button press) */
        xSemaphoreTake(semJobs, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(500));              /* simulate work */

        xSemaphoreTake(mtxData, portMAX_DELAY);
        uint32_t id = ++g_stats.jobs_done;
        xSemaphoreGive(mtxData);

        uart_printf("[worker] job #%lu done\r\n", (unsigned long)id);
    }
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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
