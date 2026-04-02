/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include <string.h>

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart2;

/* ─── OLED defines ───────────────────────────────────────────── */
#define OLED_ADDR  0x78   // SSD1306 I2C address (0x3C << 1)

/* ─── Simple 5x7 font (space to Z) ──────────────────────────── */
static const uint8_t font[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // space
    {0x00,0x00,0x5F,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00}, // "
    {0x14,0x7F,0x14,0x7F,0x14}, // #
    {0x24,0x2A,0x7F,0x2A,0x12}, // $
    {0x23,0x13,0x08,0x64,0x62}, // %
    {0x36,0x49,0x55,0x22,0x50}, // &
    {0x00,0x05,0x03,0x00,0x00}, // '
    {0x00,0x1C,0x22,0x41,0x00}, // (
    {0x00,0x41,0x22,0x1C,0x00}, // )
    {0x08,0x2A,0x1C,0x2A,0x08}, // *
    {0x08,0x08,0x3E,0x08,0x08}, // +
    {0x00,0x50,0x30,0x00,0x00}, // ,
    {0x08,0x08,0x08,0x08,0x08}, // -
    {0x00,0x60,0x60,0x00,0x00}, // .
    {0x20,0x10,0x08,0x04,0x02}, // /
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x36,0x36,0x00,0x00}, // :
    {0x00,0x56,0x36,0x00,0x00}, // ;
    {0x08,0x14,0x22,0x41,0x00}, //
    {0x14,0x14,0x14,0x14,0x14}, // =
    {0x00,0x41,0x22,0x14,0x08}, // >
    {0x02,0x01,0x51,0x09,0x06}, // ?
    {0x32,0x49,0x79,0x41,0x3E}, // @
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x04,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
};

/* ─── OLED helper functions ──────────────────────────────────── */
void OLED_SendCmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, buf, 2, HAL_MAX_DELAY);
}

void OLED_SendData(uint8_t data)
{
    uint8_t buf[2] = {0x40, data};
    HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, buf, 2, HAL_MAX_DELAY);
}

void OLED_Init(void)
{
    HAL_Delay(100);
    OLED_SendCmd(0xAE); // display off
    OLED_SendCmd(0x20); // memory mode
    OLED_SendCmd(0x00); // horizontal
    OLED_SendCmd(0xB0); // page start
    OLED_SendCmd(0xC8); // COM scan
    OLED_SendCmd(0x00); // low column
    OLED_SendCmd(0x10); // high column
    OLED_SendCmd(0x40); // start line
    OLED_SendCmd(0x81); // contrast
    OLED_SendCmd(0xFF);
    OLED_SendCmd(0xA1); // segment remap
    OLED_SendCmd(0xA6); // normal display
    OLED_SendCmd(0xA8); // multiplex
    OLED_SendCmd(0x3F);
    OLED_SendCmd(0xA4); // output RAM
    OLED_SendCmd(0xD3); // offset
    OLED_SendCmd(0x00);
    OLED_SendCmd(0xD5); // clock
    OLED_SendCmd(0xF0);
    OLED_SendCmd(0xD9); // pre-charge
    OLED_SendCmd(0x22);
    OLED_SendCmd(0xDA); // COM pins
    OLED_SendCmd(0x12);
    OLED_SendCmd(0xDB); // VCOMH
    OLED_SendCmd(0x20);
    OLED_SendCmd(0x8D); // charge pump
    OLED_SendCmd(0x14);
    OLED_SendCmd(0xAF); // display on
}

void OLED_Clear(void)
{
    for (uint8_t page = 0; page < 8; page++)
    {
        OLED_SendCmd(0xB0 + page);
        OLED_SendCmd(0x00);
        OLED_SendCmd(0x10);
        for (uint8_t col = 0; col < 128; col++)
        {
            OLED_SendData(0x00);
        }
    }
}

void OLED_ShowString(uint8_t row, const char* text)
{
    OLED_SendCmd(0xB0 + row);
    OLED_SendCmd(0x00);
    OLED_SendCmd(0x10);

    while (*text)
    {
        char c = *text;
        if (c >= 'a' && c <= 'z') c = c - 32; // to uppercase
        if (c >= ' ' && c <= 'Z')
        {
            for (uint8_t i = 0; i < 5; i++)
                OLED_SendData(font[c - ' '][i]);
            OLED_SendData(0x00); // character spacing
        }
        text++;
    }
}

/* ─── Function prototypes ────────────────────────────────────── */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);

/* ─── Main ───────────────────────────────────────────────────── */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_I2C1_Init();

    // Initialise OLED
    OLED_Init();
    OLED_Clear();
    OLED_ShowString(0, "SYSTEM READY");

    // All LEDs off
    HAL_GPIO_WritePin(GPIOC, LED1_Pin|LED2_Pin|LED3_Pin, GPIO_PIN_RESET);

    // Show system ready once at startup
    OLED_Clear();
    OLED_ShowString(0, "SYSTEM READY");
    HAL_GPIO_WritePin(GPIOC, LED1_Pin|LED2_Pin|LED3_Pin, GPIO_PIN_RESET);

    while (1)
    {
        uint8_t btn1 = (HAL_GPIO_ReadPin(GPIOA, BTN2A1_Pin) == GPIO_PIN_RESET);
        uint8_t btn2 = (HAL_GPIO_ReadPin(GPIOA, BTN2_Pin)   == GPIO_PIN_RESET);

        if (btn1 && btn2)
        {
            HAL_GPIO_WritePin(GPIOC, LED1_Pin|LED2_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOC, LED3_Pin, GPIO_PIN_SET);
            OLED_Clear();
            OLED_ShowString(0, "BOTH BUTTONS");
            OLED_ShowString(1, "PRESSED");
            HAL_Delay(5000);
            HAL_GPIO_WritePin(GPIOC, LED3_Pin, GPIO_PIN_RESET);
            OLED_Clear();
            OLED_ShowString(0, "SYSTEM READY");
        }
        else if (btn1)
        {
            HAL_GPIO_WritePin(GPIOC, LED2_Pin|LED3_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOC, LED1_Pin, GPIO_PIN_SET);
            OLED_Clear();
            OLED_ShowString(0, "BUTTON 1");
            OLED_ShowString(1, "PRESSED");
            HAL_Delay(5000);
            HAL_GPIO_WritePin(GPIOC, LED1_Pin, GPIO_PIN_RESET);
            OLED_Clear();
            OLED_ShowString(0, "SYSTEM READY");
        }
        else if (btn2)
        {
            HAL_GPIO_WritePin(GPIOC, LED1_Pin|LED3_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOC, LED2_Pin, GPIO_PIN_SET);
            OLED_Clear();
            OLED_ShowString(0, "BUTTON 2");
            OLED_ShowString(1, "PRESSED");
            HAL_Delay(5000);
            HAL_GPIO_WritePin(GPIOC, LED2_Pin, GPIO_PIN_RESET);
            OLED_Clear();
            OLED_ShowString(0, "SYSTEM READY");
        }

        HAL_Delay(50);
    }
}
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

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

static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_USART2_UART_Init(void)
{
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
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOC, LED1_Pin|LED2_Pin|LED3_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LED1_Pin|LED2_Pin|LED3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = BTN2_Pin|BTN2A1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
