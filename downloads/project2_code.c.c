/* ============================================================
 * STM32F401RE — OLED Signal Dashboard
 * ============================================================
 *
 * WHAT THIS PROGRAM DOES:
 *   1. Reads an LDR (light sensor) on PA0 via ADC
 *   2. Reads a potentiometer on PA1 via ADC
 *   3. Sets LED brightness on PA5 via PWM (duty = pot value)
 *   4. Displays on a 1.3" SH1106 OLED:
 *        - Scrolling waveform of LDR readings
 *        - Scrolling waveform of POT readings
 *        - Light trend arrow (rising / falling / stable)
 *        - Live ADC values and PWM duty cycle %
 *
 * WIRING:
 *   OLED VCC → 3.3V
 *   OLED GND → GND
 *   OLED SCL → PB8  with 5.6kΩ resistor to 3.3V (pull-up)
 *   OLED SDA → PB9  with 5.6kΩ resistor to 3.3V (pull-up)
 *   LDR      → PA0  (other LDR leg to 3.3V, 10kΩ from PA0 to GND)
 *   POT wiper→ PA1  (POT ends to 3.3V and GND)
 *   LED      → PA5  → 220Ω resistor → GND
 *
 * ============================================================ */

#include "main.h"
#include <string.h>   /* memset, memcpy, memmove */
#include <stdio.h>    /* snprintf                */

/* ── HAL peripheral handles (used by HAL driver functions) ── */
ADC_HandleTypeDef  hadc1;   /* ADC1  — reads PA0 and PA1        */
I2C_HandleTypeDef  hi2c1;   /* I2C1  — talks to OLED on PB8/PB9 */
TIM_HandleTypeDef  htim2;   /* TIM2  — generates PWM on PA5     */

/* ── Global sensor values, updated every loop iteration ───── */
uint32_t ldrValue = 0;   /* LDR raw ADC result  (0–4095) */
uint32_t potValue = 0;   /* POT raw ADC result  (0–4095) */

/* ── Forward declarations (functions defined below main) ───── */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM2_Init(void);


/* ============================================================
 * SECTION 1 — SH1106 OLED DRIVER
 *
 * The SH1106 is the controller chip inside the 1.3" OLED.
 * It receives commands and pixel data over I2C.
 *
 * We keep a "framebuffer" (fb) in RAM — a 128×64 pixel grid
 * stored as 8 rows of 128 bytes (each bit = one pixel).
 * We draw into fb, then call sh_flush() to send it to the OLED.
 * ============================================================ */

#define OLED_ADDR  (0x3C << 1)  /* I2C address 0x3C, shifted for HAL */
#define OLED_W     128           /* display width  in pixels          */
#define OLED_H     64            /* display height in pixels          */
#define OLED_PAGES 8             /* 8 pages of 8 rows each = 64 rows  */

/* Framebuffer: fb[page][column]
   Each byte represents 8 vertical pixels in one column.
   Bit 0 = topmost pixel of that 8-pixel block.            */
static uint8_t fb[OLED_PAGES][OLED_W];

/* ── Send one command byte to the OLED ─────────────────────
   The SH1106 needs a "control byte" (0x00) before every
   command byte to tell it "this is a command, not data".  */
static void sh_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, buf, 2, HAL_MAX_DELAY);
}

/* ── Send the framebuffer to the OLED ──────────────────────
   The SH1106 has an internal 132-column memory but only
   128 columns are visible. The visible area starts at
   internal column 2, so we set lower nibble = 0x02.
   We send each page (row of 8px) as one I2C transaction.  */
static void sh_flush(void)
{
    uint8_t tx[OLED_W + 1];
    tx[0] = 0x40;  /* control byte: "data follows" */

    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        sh_cmd(0xB0 + page);  /* select page (row band)           */
        sh_cmd(0x02);         /* column start = 2 (SH1106 offset) */
        sh_cmd(0x10);         /* column high nibble = 0            */
        memcpy(&tx[1], fb[page], OLED_W);  /* copy 128 pixels     */
        HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, tx, OLED_W + 1, HAL_MAX_DELAY);
    }
}

/* ── Clear the framebuffer (all pixels OFF) ─────────────── */
static void fb_clear(void)
{
    memset(fb, 0, sizeof(fb));
}

/* ── Set or clear one pixel at (x, y) ──────────────────────
   x = column (0=left, 127=right)
   y = row    (0=top,  63=bottom)
   The bit position within a page byte = y % 8             */
static void fb_pixel(uint8_t x, uint8_t y, uint8_t on)
{
    if (x >= OLED_W || y >= OLED_H) return;  /* bounds check */
    if (on)
        fb[y / 8][x] |=  (1u << (y % 8));   /* set bit   */
    else
        fb[y / 8][x] &= ~(1u << (y % 8));   /* clear bit */
}

/* ── Draw a vertical line from y0 to y1 at column x ─────── */
static void fb_vline(uint8_t x, uint8_t y0, uint8_t y1)
{
    if (y0 > y1) { uint8_t t = y0; y0 = y1; y1 = t; } /* swap if needed */
    for (uint8_t y = y0; y <= y1; y++) fb_pixel(x, y, 1);
}

/* ── Draw a horizontal line at row y from x0 to x1 ─────── */
static void fb_hline(uint8_t x0, uint8_t x1, uint8_t y)
{
    for (uint8_t x = x0; x <= x1; x++) fb_pixel(x, y, 1);
}

/* ── 5×7 pixel font, covers ASCII 32 (space) to 90 (Z) ────
   Each character is 5 bytes wide. Each byte is one column
   of 7 pixels (bit 0 = top pixel of that column).
   Only uppercase + digits + basic punctuation needed.     */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' space      */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!'             */
    {0x00,0x07,0x00,0x07,0x00}, /* '"'             */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#'             */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$'             */
    {0x23,0x13,0x08,0x64,0x62}, /* '%'             */
    {0x36,0x49,0x55,0x22,0x50}, /* '&'             */
    {0x00,0x05,0x03,0x00,0x00}, /* '''             */
    {0x00,0x1C,0x22,0x41,0x00}, /* '('             */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')'             */
    {0x14,0x08,0x3E,0x08,0x14}, /* '*'             */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+'             */
    {0x00,0x50,0x30,0x00,0x00}, /* ','             */
    {0x08,0x08,0x08,0x08,0x08}, /* '-'             */
    {0x00,0x60,0x60,0x00,0x00}, /* '.'             */
    {0x20,0x10,0x08,0x04,0x02}, /* '/'             */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0'             */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1'             */
    {0x42,0x61,0x51,0x49,0x46}, /* '2'             */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3'             */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4'             */
    {0x27,0x45,0x45,0x45,0x39}, /* '5'             */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6'             */
    {0x01,0x71,0x09,0x05,0x03}, /* '7'             */
    {0x36,0x49,0x49,0x49,0x36}, /* '8'             */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9'             */
    {0x00,0x36,0x36,0x00,0x00}, /* ':'             */
    {0x00,0x56,0x36,0x00,0x00}, /* ';'             */
    {0x08,0x14,0x22,0x41,0x00}, /* '<'             */
    {0x14,0x14,0x14,0x14,0x14}, /* '='             */
    {0x00,0x41,0x22,0x14,0x08}, /* '>'             */
    {0x02,0x01,0x51,0x09,0x06}, /* '?'             */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@'             */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A'             */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B'             */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C'             */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D'             */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E'             */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F'             */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G'             */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H'             */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I'             */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J'             */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K'             */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L'             */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 'M'             */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N'             */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O'             */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P'             */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q'             */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R'             */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S'             */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T'             */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U'             */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V'             */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 'W'             */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X'             */
    {0x07,0x08,0x70,0x08,0x07}, /* 'Y'             */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z'             */
};

/* ── Draw one character at pixel position (x, y) ──────────
   Characters are 5px wide + 1px gap = 6px per character.
   Only uppercase A-Z, digits, and basic punctuation work. */
static void fb_char(uint8_t x, uint8_t y, char c)
{
    if (c < 32 || c > 'Z') c = ' ';  /* unsupported → space */
    const uint8_t *glyph = font5x7[(uint8_t)(c - 32)];
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (uint8_t row = 0; row < 7; row++)
            fb_pixel(x + col, y + row, (bits >> row) & 1u);
    }
}

/* ── Draw a string starting at (x, y) ──────────────────── */
static void fb_str(uint8_t x, uint8_t y, const char *s)
{
    while (*s && (x + 6) <= OLED_W) {
        fb_char(x, y, *s++);
        x += 6;  /* advance 6px per character */
    }
}

/* ── Draw UP arrow (light getting brighter) ─────────────── */
static void fb_arrow_up(uint8_t x, uint8_t y)
{
    fb_pixel(x+2, y+0, 1);                                      /* tip   */
    fb_pixel(x+1, y+1, 1); fb_pixel(x+2, y+1, 1); fb_pixel(x+3, y+1, 1);
    fb_pixel(x+0, y+2, 1); fb_pixel(x+2, y+2, 1); fb_pixel(x+4, y+2, 1);
    fb_pixel(x+2, y+3, 1);                                      /* stem  */
    fb_pixel(x+2, y+4, 1);
    fb_pixel(x+2, y+5, 1);
}

/* ── Draw DOWN arrow (light getting dimmer) ─────────────── */
static void fb_arrow_dn(uint8_t x, uint8_t y)
{
    fb_pixel(x+2, y+0, 1);                                      /* stem  */
    fb_pixel(x+2, y+1, 1);
    fb_pixel(x+2, y+2, 1);
    fb_pixel(x+0, y+3, 1); fb_pixel(x+2, y+3, 1); fb_pixel(x+4, y+3, 1);
    fb_pixel(x+1, y+4, 1); fb_pixel(x+2, y+4, 1); fb_pixel(x+3, y+4, 1);
    fb_pixel(x+2, y+5, 1);                                      /* tip   */
}

/* ── Draw dash (light level stable) ─────────────────────── */
static void fb_dash(uint8_t x, uint8_t y)
{
    for (uint8_t i = 0; i < 5; i++) fb_pixel(x+i, y+3, 1);
}

/* ── Initialise the SH1106 OLED ─────────────────────────────
   This sequence powers on the display and configures it.
   Key difference from SSD1306: uses charge pump 0x8D/0x14. */
static void sh_init(void)
{
    HAL_Delay(100);                      /* let power rail settle     */
    sh_cmd(0xAE);                        /* display OFF               */
    sh_cmd(0xD5); sh_cmd(0x80);         /* clock divider             */
    sh_cmd(0xA8); sh_cmd(0x3F);         /* mux ratio = 64 rows       */
    sh_cmd(0xD3); sh_cmd(0x00);         /* display vertical offset=0 */
    sh_cmd(0x40);                        /* start line = row 0        */
    sh_cmd(0x8D); sh_cmd(0x14);         /* charge pump ON (key line) */
    sh_cmd(0xA1);                        /* segment remap (flip H)    */
    sh_cmd(0xC8);                        /* COM scan direction        */
    sh_cmd(0xDA); sh_cmd(0x12);         /* COM pin config            */
    sh_cmd(0x81); sh_cmd(0xFF);         /* contrast = maximum        */
    sh_cmd(0xD9); sh_cmd(0xF1);         /* pre-charge period         */
    sh_cmd(0xDB); sh_cmd(0x40);         /* VCOMH level               */
    sh_cmd(0xA4);                        /* display from RAM          */
    sh_cmd(0xA6);                        /* normal (not inverted)     */
    sh_cmd(0xAF);                        /* display ON                */
    HAL_Delay(100);                      /* wait for DC-DC to start   */
    fb_clear();
    sh_flush();                          /* blank the screen          */
}


/* ============================================================
 * SECTION 2 — ADC READING
 *
 * The STM32F401RE ADC is 12-bit, so values are 0–4095.
 * We reconfigure the channel each read so we can use a
 * single ADC peripheral for both PA0 and PA1.
 * ============================================================ */
static uint16_t adc_read(uint32_t channel)
{
    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Channel      = channel;
    cfg.Rank         = 1;
    cfg.SamplingTime = ADC_SAMPLETIME_84CYCLES; /* longer = more stable */

    HAL_ADC_ConfigChannel(&hadc1, &cfg); /* point ADC at this channel */
    HAL_ADC_Start(&hadc1);               /* start one conversion      */
    HAL_ADC_PollForConversion(&hadc1, 10); /* wait for it to finish   */
    uint16_t result = (uint16_t)HAL_ADC_GetValue(&hadc1); /* read it  */
    HAL_ADC_Stop(&hadc1);                /* stop the ADC              */
    return result;
}


/* ============================================================
 * SECTION 3 — SCROLLING GRAPH + LIGHT TREND
 *
 * We keep a 128-sample history for both LDR and POT.
 * Each new sample is normalised to fit the graph height,
 * then appended on the right (older samples shift left).
 *
 * OLED layout:
 *   y  0– 7   LDR label | trend arrow | level text | POT label
 *   y  8–27   LDR scrolling waveform  (20px tall)
 *   y 28       horizontal divider
 *   y 29–55   POT scrolling waveform  (27px tall)
 *   y 57–63   L:xxxx  P:xxxx  PWM:xx%
 * ============================================================ */

#define GRAPH_H        20u   /* height in pixels of each waveform   */
#define GRAPH_BOT_LDR  27u   /* y coordinate of LDR waveform bottom */
#define GRAPH_BOT_POT  55u   /* y coordinate of POT waveform bottom */
#define TREND_WIN       8u   /* samples to compare for trend calc   */

static uint8_t ldr_hist[128];  /* LDR normalised sample history */
static uint8_t pot_hist[128];  /* POT normalised sample history */

/* ── Add new samples to history buffers ────────────────────
   memmove shifts everything left by 1, then we place the
   new sample at position [127] (rightmost column).        */
static void push_sample(void)
{
    memmove(ldr_hist, ldr_hist + 1, 127);
    memmove(pot_hist, pot_hist + 1, 127);

    /* normalise 0–4095 to 0–(GRAPH_H-1) pixels */
    ldr_hist[127] = (uint8_t)(ldrValue * (GRAPH_H - 1) / 4095);
    pot_hist[127] = (uint8_t)(potValue * (GRAPH_H - 1) / 4095);
}

/* ── Draw waveform from history buffer ──────────────────────
   For each column, draw a vertical line connecting this
   sample to the previous sample. This makes a smooth wave
   even when values change quickly.
   bot = y coordinate of the bottom of the graph area.    */
static void draw_wave(const uint8_t *h, uint8_t bot)
{
    for (uint8_t x = 1; x < 128; x++)
        fb_vline(x, bot - h[x], bot - h[x - 1]);
}

/* ── Calculate light trend ──────────────────────────────────
   Compares the average of the newest TREND_WIN samples
   against the previous TREND_WIN samples.
   Returns:  1 = rising,  -1 = falling,  0 = stable        */
static int8_t calc_trend(void)
{
    int16_t recent = 0, older = 0;

    /* sum the newest 8 samples */
    for (uint8_t i = 0; i < TREND_WIN; i++)
        recent += ldr_hist[127 - i];

    /* sum the 8 samples before those */
    for (uint8_t i = TREND_WIN; i < TREND_WIN * 2; i++)
        older += ldr_hist[127 - i];

    int16_t diff = recent - older;
    if (diff >  (int16_t)TREND_WIN) return  1;  /* clearly rising  */
    if (diff < -(int16_t)TREND_WIN) return -1;  /* clearly falling */
    return 0;                                    /* stable          */
}


/* ============================================================
 * SECTION 4 — MAIN PROGRAM
 * ============================================================ */
int main(void)
{
    /* --- Initialise all hardware --- */
    HAL_Init();               /* HAL library + systick timer  */
    SystemClock_Config();     /* 84 MHz system clock          */
    MX_GPIO_Init();           /* user button on PC13          */
    MX_ADC1_Init();           /* ADC1 for PA0 and PA1         */
    MX_I2C1_Init();           /* I2C1 on PB8/PB9 for OLED    */
    MX_TIM2_Init();           /* TIM2 PWM on PA5 for LED      */

    sh_init();                /* power on and blank the OLED  */
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);  /* start PWM  */

    /* --- Splash screen for 2 seconds --- */
    fb_clear();
    fb_str( 4, 10, "STM32 DASHBOARD");
    fb_str(10, 26, "LDR + POT + PWM");
    fb_str(22, 42, "SIGNAL MONITOR");
    sh_flush();
    HAL_Delay(2000);

    /* --- Main loop: read → process → display → repeat --- */
    while (1)
    {
        /* STEP 1: Read both sensors */
        ldrValue = adc_read(ADC_CHANNEL_0);   /* LDR on PA0 */
        potValue = adc_read(ADC_CHANNEL_1);   /* POT on PA1 */

        /* STEP 2: Set LED brightness from pot
           Maps 0–4095 ADC range onto 0–1000 PWM timer range */
        uint32_t duty = potValue * 1000UL / 4095UL;
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);

        /* STEP 3: Push new readings into scrolling history */
        push_sample();

        /* STEP 4: Calculate derived display values */
        int8_t  trend   = calc_trend();
        uint8_t pwm_pct = (uint8_t)(potValue * 100UL / 4095UL);

        /* STEP 5: Build the frame ───────────────────────── */
        fb_clear();

        /* --- Top label row (y=0) --- */
        fb_str(0, 0, "LDR");   /* left label for LDR graph  */

        /* Trend arrow immediately after "LDR" text */
        if      (trend ==  1) fb_arrow_up(22, 0);  /* ↑ rising  */
        else if (trend == -1) fb_arrow_dn(22, 0);  /* ↓ falling */
        else                  fb_dash(22, 0);       /* — stable  */

        /* Light level word: depends on raw ADC value */
        if      (ldrValue > 3000) fb_str(34, 0, "BRIGHT");
        else if (ldrValue > 1500) fb_str(34, 0, "MID");
        else                      fb_str(34, 0, "DIM");

        fb_str(88, 0, "POT");  /* right label for POT graph */

        /* --- LDR waveform (y 8–27) --- */
        draw_wave(ldr_hist, GRAPH_BOT_LDR);

        /* --- Divider line between the two graphs --- */
        fb_hline(0, 127, 28);

        /* --- POT waveform (y 29–55) --- */
        draw_wave(pot_hist, GRAPH_BOT_POT);

        /* --- Bottom strip: live values (y=57) ---
           Format:  L:3241 P:2180  73%
           L = LDR raw value, P = POT raw value, % = PWM duty */
        char buf[32];
        snprintf(buf, sizeof(buf), "L:%4lu P:%4lu %3u%%",
                 ldrValue, potValue, pwm_pct);
        fb_str(0, 57, buf);

        /* STEP 6: Send completed frame to OLED (~50ms per frame) */
        sh_flush();
        HAL_Delay(50);
    }
}


/* ============================================================
 * SECTION 5 — PERIPHERAL CONFIGURATION
 *
 * These functions set up the STM32 hardware peripherals.
 * Generated by STM32CubeMX and kept here for completeness.
 * ============================================================ */

/* System clock: 84 MHz from HSI via PLL */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

    /* Configure HSI oscillator + PLL to get 84 MHz */
    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState            = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM            = 16;   /* divide HSI by 16 = 1 MHz   */
    osc.PLL.PLLN            = 336;  /* multiply by 336 = 336 MHz  */
    osc.PLL.PLLP            = RCC_PLLP_DIV4; /* divide by 4 = 84MHz */
    osc.PLL.PLLQ            = 7;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    /* Set bus clock dividers */
    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;   /* AHB  = 84 MHz */
    clk.APB1CLKDivider = RCC_HCLK_DIV2;     /* APB1 = 42 MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;     /* APB2 = 84 MHz */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

/* ADC1: 12-bit, single conversion, software trigger */
static void MX_ADC1_Init(void)
{
    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;  /* 0–4095 */
    hadc1.Init.ScanConvMode          = DISABLE;   /* one channel at a time */
    hadc1.Init.ContinuousConvMode    = DISABLE;   /* single shot only      */
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();
}

/* I2C1: 400 kHz fast mode, master, for OLED on PB8/PB9 */
static void MX_I2C1_Init(void)
{
    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = 400000;           /* 400 kHz fast mode */
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2     = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

/* TIM2: 1 kHz PWM on PA5
   Prescaler=83, Period=1000 → 84MHz / 84 / 1001 ≈ 1 kHz
   Duty cycle 0–1000 maps to 0–100% brightness            */
static void MX_TIM2_Init(void)
{
    TIM_ClockConfigTypeDef  sc = {0};
    TIM_MasterConfigTypeDef mc = {0};
    TIM_OC_InitTypeDef      oc = {0};

    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 83;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 1000;  /* ARR: counts 0–1000 */
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK) Error_Handler();

    sc.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim2, &sc) != HAL_OK) Error_Handler();
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) Error_Handler();

    mc.MasterOutputTrigger = TIM_TRGO_RESET;
    mc.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &mc) != HAL_OK) Error_Handler();

    oc.OCMode       = TIM_OCMODE_PWM1;       /* high while counter < CCR */
    oc.Pulse        = 0;                      /* start at 0% duty         */
    oc.OCPolarity   = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode   = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim2, &oc, TIM_CHANNEL_1) != HAL_OK) Error_Handler();

    HAL_TIM_MspPostInit(&htim2);  /* configures PA5 as AF1 (TIM2_CH1) */
}

/* GPIO: user button on PC13 (not used in main loop,
   kept so the MSP file button interrupt stays valid)     */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin  = B1_Pin;
    g.Mode = GPIO_MODE_IT_FALLING;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(B1_GPIO_Port, &g);
}

/* Error handler: disable interrupts and halt.
   If the program stops working, it ends up here.        */
void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif
