/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  * --------------------------------
  * UART DMAパケット通信の流れ
  * RX DMAがdma_rx_bufferへ循環受信
  *  ↓
  * Half / Full / IDLEイベント
  *  ↓
  * 新着データだけをring_bufferへ追加
  *  ↓
  * main()がring_bufferから1バイトずつ取り出す
  *  ↓
  * STX / LEN / SEQ / TYPE / PAYLOAD / CRC16として解析
  *  ↓
  * CRCが正しければpayloadをTX DMAでエコーバック
  * --------------------------------
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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
  PACKET_STATE_WAIT_STX = 0,
  PACKET_STATE_READ_LEN,
  PACKET_STATE_READ_SEQ,
  PACKET_STATE_READ_TYPE,
  PACKET_STATE_READ_PAYLOAD,
  PACKET_STATE_READ_CRC_LOW,
  PACKET_STATE_READ_CRC_HIGH
} PacketParserState;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DMA_RX_BUFFER_SIZE 64U
#define RING_BUFFER_SIZE 256U
#define TX_BUFFER_SIZE 64U
#define PACKET_STX 0x02U
#define PACKET_MAX_PAYLOAD_SIZE 48U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_rx;
DMA_HandleTypeDef hdma_usart2_tx;

/* USER CODE BEGIN PV */
static uint8_t dma_rx_buffer[DMA_RX_BUFFER_SIZE];
static uint8_t ring_buffer[RING_BUFFER_SIZE];
static uint8_t tx_buffer[TX_BUFFER_SIZE];

static volatile uint16_t dma_rx_old_position;
static volatile uint16_t ring_head;
static volatile uint16_t ring_tail;
static volatile uint8_t tx_busy;

static volatile uint32_t rx_event_count;
static volatile uint32_t ring_overflow_count;
static volatile uint32_t tx_complete_count;
static volatile uint32_t main_loop_count;
static volatile uint32_t packet_ok_count;
static volatile uint32_t packet_crc_error_count;
static volatile uint32_t packet_format_error_count;

static PacketParserState packet_state;
static uint8_t packet_length;
static uint8_t packet_sequence;
static uint8_t packet_type;
static uint8_t packet_payload[PACKET_MAX_PAYLOAD_SIZE];
static uint8_t packet_payload_index;
static uint16_t packet_received_crc;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
static void UART_StartCircularReceiveDMA(void);
static void RingBuffer_WriteFromISR(uint8_t data);
static void DMA_CopyNewDataToRingBuffer(uint16_t position);
static uint8_t RingBuffer_ReadByte(uint8_t *data);
static uint16_t CRC16_CCITT_Update(uint16_t crc, uint8_t data);
static uint16_t Packet_CalculateCRC(void);
static void PacketParser_Reset(void);
static void PacketParser_ProcessByte(uint8_t data);
static void PacketParser_Process(void);
static void Packet_HandleValidPacket(void);
static void UART_TryTransmit(const uint8_t *data, uint16_t length);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void UART_StartCircularReceiveDMA(void)
{
  /*
   * Circular DMAなので、この関数は起動時に1回だけ呼ぶ。
   * Half / Full / IDLEの各イベントでDMA自体は停止しない。
   */
  if (HAL_UARTEx_ReceiveToIdle_DMA(&huart2,
                                   dma_rx_buffer,
                                   DMA_RX_BUFFER_SIZE) != HAL_OK)
  {
    Error_Handler();
  }
}

static void RingBuffer_WriteFromISR(uint8_t data)
{
  uint16_t next_head = ring_head + 1U;

  if (next_head >= RING_BUFFER_SIZE)
  {
    next_head = 0U;
  }

  /* headの次がtailなら満杯。未処理データを守り、新着1バイトを捨てる。 */
  if (next_head == ring_tail)
  {
    ring_overflow_count++;
    return;
  }

  ring_buffer[ring_head] = data;
  ring_head = next_head;
}

static uint8_t RingBuffer_ReadByte(uint8_t *data)
{
  if (ring_tail == ring_head)
  {
    return 0U;
  }

  *data = ring_buffer[ring_tail];
  ring_tail++;

  if (ring_tail >= RING_BUFFER_SIZE)
  {
    ring_tail = 0U;
  }

  return 1U;
}

static void DMA_CopyNewDataToRingBuffer(uint16_t position)
{
  uint16_t index;

  if (position > DMA_RX_BUFFER_SIZE)
  {
    return;
  }

  if (position > dma_rx_old_position)
  {
    /* DMAバッファ内で折り返していない場合。 */
    for (index = dma_rx_old_position; index < position; index++)
    {
      RingBuffer_WriteFromISR(dma_rx_buffer[index]);
    }
  }
  else if (position < dma_rx_old_position)
  {
    /* DMAバッファ終端から先頭へ折り返した場合。 */
    for (index = dma_rx_old_position; index < DMA_RX_BUFFER_SIZE; index++)
    {
      RingBuffer_WriteFromISR(dma_rx_buffer[index]);
    }

    for (index = 0U; index < position; index++)
    {
      RingBuffer_WriteFromISR(dma_rx_buffer[index]);
    }
  }
  else
  {
    /* 同じ位置の重複イベントなので、新着データはない。 */
  }

  /* FullイベントのSizeは64なので、次の周回位置0に正規化する。 */
  dma_rx_old_position =
      (position == DMA_RX_BUFFER_SIZE) ? 0U : position;
}

static uint16_t CRC16_CCITT_Update(uint16_t crc, uint8_t data)
{
  uint8_t bit;

  crc ^= (uint16_t)data << 8;

  for (bit = 0U; bit < 8U; bit++)
  {
    if ((crc & 0x8000U) != 0U)
    {
      crc = (crc << 1) ^ 0x1021U;
    }
    else
    {
      crc <<= 1;
    }
  }

  return crc;
}

static uint16_t Packet_CalculateCRC(void)
{
  uint16_t crc = 0xFFFFU;
  uint8_t index;

  crc = CRC16_CCITT_Update(crc, PACKET_STX);
  crc = CRC16_CCITT_Update(crc, packet_length);
  crc = CRC16_CCITT_Update(crc, packet_sequence);
  crc = CRC16_CCITT_Update(crc, packet_type);

  for (index = 0U; index < packet_length; index++)
  {
    crc = CRC16_CCITT_Update(crc, packet_payload[index]);
  }

  return crc;
}

static void PacketParser_Reset(void)
{
  packet_state = PACKET_STATE_WAIT_STX;
  packet_length = 0U;
  packet_sequence = 0U;
  packet_type = 0U;
  packet_payload_index = 0U;
  packet_received_crc = 0U;
}

static void PacketParser_ProcessByte(uint8_t data)
{
  switch (packet_state)
  {
    case PACKET_STATE_WAIT_STX:
      if (data == PACKET_STX)
      {
        packet_state = PACKET_STATE_READ_LEN;
      }
      break;

    case PACKET_STATE_READ_LEN:
      packet_length = data;
      packet_payload_index = 0U;

      if (packet_length > PACKET_MAX_PAYLOAD_SIZE)
      {
        packet_format_error_count++;
        PacketParser_Reset();
      }
      else
      {
        packet_state = PACKET_STATE_READ_SEQ;
      }
      break;

    case PACKET_STATE_READ_SEQ:
      packet_sequence = data;
      packet_state = PACKET_STATE_READ_TYPE;
      break;

    case PACKET_STATE_READ_TYPE:
      packet_type = data;
      packet_state = (packet_length == 0U)
                         ? PACKET_STATE_READ_CRC_LOW
                         : PACKET_STATE_READ_PAYLOAD;
      break;

    case PACKET_STATE_READ_PAYLOAD:
      packet_payload[packet_payload_index] = data;
      packet_payload_index++;

      if (packet_payload_index >= packet_length)
      {
        packet_state = PACKET_STATE_READ_CRC_LOW;
      }
      break;

    case PACKET_STATE_READ_CRC_LOW:
      packet_received_crc = data;
      packet_state = PACKET_STATE_READ_CRC_HIGH;
      break;

    case PACKET_STATE_READ_CRC_HIGH:
      packet_received_crc |= (uint16_t)data << 8;

      if (packet_received_crc == Packet_CalculateCRC())
      {
        packet_ok_count++;
        Packet_HandleValidPacket();
      }
      else
      {
        packet_crc_error_count++;
      }

      PacketParser_Reset();
      break;

    default:
      PacketParser_Reset();
      break;
  }
}

static void PacketParser_Process(void)
{
  uint8_t data;

  while (RingBuffer_ReadByte(&data) != 0U)
  {
    PacketParser_ProcessByte(data);
  }
}

static void UART_TryTransmit(const uint8_t *data, uint16_t length)
{
  uint16_t index;

  if ((length == 0U) || (length > TX_BUFFER_SIZE) || (tx_busy != 0U))
  {
    return;
  }

  for (index = 0U; index < length; index++)
  {
    tx_buffer[index] = data[index];
  }

  tx_busy = 1U;

  if (HAL_UART_Transmit_DMA(&huart2, tx_buffer, length) != HAL_OK)
  {
    tx_busy = 0U;
    Error_Handler();
  }
}

static void Packet_HandleValidPacket(void)
{
  /*
   * 最初の段階では、正しいパケットのpayloadだけを返す。
   * ACK/NAKや再送制御は次のステップで追加する。
   */
  UART_TryTransmit(packet_payload, packet_length);
}

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
  MX_DMA_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  PacketParser_Reset();
  UART_StartCircularReceiveDMA();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    main_loop_count++;
    PacketParser_Process();
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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

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
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
  /* DMA1_Stream6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);

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
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  if (huart->Instance == USART2)
  {
    rx_event_count++;
    DMA_CopyNewDataToRingBuffer(Size);
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    tx_complete_count++;
    tx_busy = 0U;
    HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
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
