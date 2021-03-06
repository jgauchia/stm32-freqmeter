#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/usb/usbd.h>
#include "lcd1602.h"
#include "usbcdc.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define PACKET_SIZE 64
#define BUFFER_SIZE 256
#define DISP_DELAY  100

/* NOTE: For systems that has SYSCLK != 72MHz, modify mco_val, mco_name and filters_name in addition to clock setup. */

static volatile uint32_t systick_ms   = 0;
static volatile uint32_t freq         = 0; /* 32bit = approx. 4.3G ticks per second. */
static volatile uint32_t freq_scratch = 0; /* scratch pad. */
static volatile bool     hold         = false;

static uint32_t mco_val[] = {
  RCC_CFGR_MCO_NOCLK,
  //RCC_CFGR_MCO_SYSCLK,       /* Will not be able to output. */
  RCC_CFGR_MCO_HSI,
  RCC_CFGR_MCO_HSE,
  RCC_CFGR_MCO_PLL_DIV2,
  //RCC_CFGR_MCO_PLL2CLK,      /* No signal. */
  //RCC_CFGR_MCO_PLL3CLK_DIV2, /* No signal. */
  //RCC_CFGR_MCO_XT1,          /* No signal. */
  //RCC_CFGR_MCO_PLL3,         /* No signal. */
};
static char *mco_name[] = {
  "OFF      ",
  //"72 MHz   ", /* Will not be able to output. */
  " 8 MHz RC",
  " 8 MHz   ",
  "36 MHz   ",
  //"PLL2     ", /* No signal. */
  //"PLL3 DIV2", /* No signal. */
  //"XT1      ", /* No signal. */
  //"PLL3     ", /* No signal. */
};
/* Clock output name for LCD*/
static char *mco_name_lcd[] = {
  " OFF    ",
  " 8 MHz",
  " 8 MHz  ",
  "36 MHz  ",
};    
static int mco_current = 0; /* Default to off. */
static int mco_old = 0;

static enum tim_ic_filter filters_val[] = {
  TIM_IC_OFF,

  TIM_IC_CK_INT_N_2,
  TIM_IC_CK_INT_N_4,
  TIM_IC_CK_INT_N_8,

  TIM_IC_DTF_DIV_2_N_6,
  TIM_IC_DTF_DIV_2_N_8,

  TIM_IC_DTF_DIV_4_N_6,
  TIM_IC_DTF_DIV_4_N_8,

  TIM_IC_DTF_DIV_8_N_6,
  TIM_IC_DTF_DIV_8_N_8,

  TIM_IC_DTF_DIV_16_N_5,
  TIM_IC_DTF_DIV_16_N_6,
  TIM_IC_DTF_DIV_16_N_8,

  TIM_IC_DTF_DIV_32_N_5,
  TIM_IC_DTF_DIV_32_N_6,
  TIM_IC_DTF_DIV_32_N_8,
};
static char *filters_name[] = {
  "OFF       ",

  "36.000 MHz",
  "18.000 MHz",
  " 9.000 MHz",

  " 6.000 MHz",
  " 4.500 MHz",

  " 3.000 MHz",
  " 2.250 MHz",

  " 1.500 MHz",
  " 1.125 MHz",

  "900.00 kHz",
  "750.00 kHz",
  "562.50 kHz",

  "450.00 kHz",
  "375.00 kHz",
  "281.25 kHz",
};
static int filter_current = 0; /* Default to no filter. */
static int filter_old = 0;

static enum tim_ic_psc prescalers_val[] = {
  TIM_IC_PSC_OFF,

  TIM_IC_PSC_2,
  TIM_IC_PSC_4,
  TIM_IC_PSC_8,
};
static char *prescalers_name[] = {
  "OFF",

  "  2",
  "  4",
  "  8",
};
static int prescaler_current = 0; /* Default to no prescaler. */
static int prescaler_old = 0;

static char buffer[BUFFER_SIZE];

static char str[32];

uint8_t custom_char_rc[] = { 0x1C, 0x14, 0x18, 0x14, 0x00, 0x07, 0x04, 0x07 };
static bool is_print_custom = false;

void systick_ms_setup(void) {
  /* 72MHz clock, interrupt for every 72,000 CLKs (1ms). */
  systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
  systick_set_reload(72000 - 1);
  systick_interrupt_enable();
  systick_counter_enable();
}

void timer_setup(void) {
  /* NOTE: Digital input pins have Schmitt filter. */

  rcc_periph_clock_enable(RCC_TIM2);
  rcc_periph_reset_pulse(RST_TIM2);

  /* Disable inputs. */
  timer_ic_disable(TIM2, TIM_IC1);
  timer_ic_disable(TIM2, TIM_IC2);
  timer_ic_disable(TIM2, TIM_IC3);
  timer_ic_disable(TIM2, TIM_IC4);

  /* Disable outputs. */
  timer_disable_oc_output(TIM2, TIM_OC1);
  timer_disable_oc_output(TIM2, TIM_OC2);
  timer_disable_oc_output(TIM2, TIM_OC3);
  timer_disable_oc_output(TIM2, TIM_OC4);

  /* Timer mode: no divider, edge, count up */
  timer_disable_preload(TIM2);
  timer_continuous_mode(TIM2);
  timer_set_period(TIM2, 65535);
  timer_slave_set_mode(TIM2, TIM_SMCR_SMS_ECM1);
  timer_slave_set_filter(TIM2, filters_val[filter_current]);
  timer_slave_set_polarity(TIM2, TIM_ET_RISING);
  timer_slave_set_prescaler(TIM2, prescalers_val[prescaler_current]);
  timer_slave_set_trigger(TIM2, TIM_SMCR_TS_ETRF);
  timer_update_on_overflow(TIM2);

  nvic_enable_irq(NVIC_TIM2_IRQ);
  timer_enable_counter(TIM2);
  timer_enable_irq(TIM2, TIM_DIER_CC1IE);
}

void mco_setup(void) {
  /* Outputs 36MHz clock on PA8, for calibration. */
  gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO8);
  rcc_set_mco(mco_val[mco_current]); /* This merely sets RCC_CFGR. */
}

void usbcdc_printf(const char *fmt, ...) {
  int len;
  uint16_t written = 0;
  va_list args;
  va_start(args, fmt);

  /* TODO: The following line costs approx. 20KB. Find an alternative if necessary. */
  len = vsnprintf(
    buffer,
    BUFFER_SIZE,
    fmt,
    args
  );
  while (written < len) {
    if ((len - written) > PACKET_SIZE) {
      written += usbcdc_write(buffer + written, PACKET_SIZE);
    } else {
      written += usbcdc_write(buffer + written, len - written);
    }
  }
}

void usbcdc_clear_screen(void) {
  usbcdc_printf("\033c\r");
}

void poll_command(void) {
  char cmd = usbcdc_getc();

  switch (cmd) {
    case '\0': {
      /* No input available. */
      return;
    }

    case 'o':
    case 'O': {
      /* Switch MCO. */
      mco_current ++;
      if (mco_current >= ARRAY_SIZE(mco_val)) {
        mco_current = 0;
      }

      rcc_set_mco(mco_val[mco_current]);

      return;
    }

    case 'h':
    case 'H': {
      /* Toggle hold. */
      hold = !hold;

      return;
    }

    case 'f':
    case 'F': {
      /* Configure digital filter. */
      filter_current ++;
      if (filter_current >= ARRAY_SIZE(filters_val)) {
        filter_current = 0;
      }

      timer_slave_set_filter(TIM2, filters_val[filter_current]);

      return;
    }

    case 'p':
    case 'P': {
      /* Configure prescaler. */
      prescaler_current ++;
      if (prescaler_current >= ARRAY_SIZE(prescalers_val)) {
        prescaler_current = 0;
      }
    
      timer_slave_set_prescaler(TIM2, prescalers_val[prescaler_current]);

      return;
    }

    case '\n':
    case '\r': {
      /* Remote echo for newline -- for convenient data recording. */
      usbcdc_write("\r\n", 1); /* This works since buffer is not modified. */

      return;
    }

    default: {
      /* Invalid command. */
      return;
    }
  }
}

int main(void) {
  rcc_clock_setup_in_hse_8mhz_out_72mhz();
  rcc_periph_clock_enable(RCC_GPIOA); /* For MCO. */
  rcc_periph_clock_enable(RCC_GPIOB); /* For LED, USB pull-up and TIM2. */
  rcc_periph_clock_enable(RCC_AFIO); /* For MCO. */

  /* Setup PB1 for the LED. */
  gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO1);
  gpio_set(GPIOB, GPIO1);

  /* Pull PA1 down to GND, which is adjascent to timer imput and can be used as an convenient return path. */
  gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO1);
  gpio_clear(GPIOA, GPIO1);

  /* Setup PB9 to pull up the D+ high. The circuit is active low. */
  gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, GPIO9);
  gpio_clear(GPIOB, GPIO9);

  /* Setup Pins for LCD */
  gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,GPIO_CNF_OUTPUT_PUSHPULL, GPIO12); 
  gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,GPIO_CNF_OUTPUT_PUSHPULL, GPIO13); 
  gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,GPIO_CNF_OUTPUT_PUSHPULL, GPIO14); 
  gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,GPIO_CNF_OUTPUT_PUSHPULL, GPIO15); 
  gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,GPIO_CNF_OUTPUT_PUSHPULL, GPIO10); 
  gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,GPIO_CNF_OUTPUT_PUSHPULL, GPIO8); 
  gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,GPIO_CNF_OUTPUT_PUSHPULL, GPIO7); 
  usbcdc_init();

  gpio_clear(GPIOB, GPIO1);

  timer_setup();
  systick_ms_setup();
  mco_setup();
  
  LCD_Init();
  create_custom_char(0,custom_char_rc);
  ClearLcdData();
  SetLcdXY(0, 0);
  puts_lcd("STM32 freqmeter");

  /* Wait 500ms for USB setup to complete before trying to send anything. */
  /* Takes ~ 130ms on my machine */
  // TODO: a better way?
  while (systick_ms < 2000);
  
  ClearLcdData();
  SetLcdXY(0, 0);
  
  /* The loop. */
  uint32_t last_ms = 0;

  while (freq == 0);

  /* The loop (for real). */
  while (true) {
    // TODO: whether to support dividers? Any meaningful use?
    poll_command();

    // TODO: currently missing 20 ticks out of 36,000,000 ticks (<0.6ppm error).
    //       However, before we use TCXO to supply clock to the MCU, fixing it will not improve precision.

    usbcdc_clear_screen();

    /* NOTE: Subtract one extra overflow (65536 ticks) occurred during counter reset. */
    /* TODO: The following line costs approx. 20KB. Find an alternative if necessary. */
    usbcdc_printf("%4lu.%06lu MHz %c [Hold: %s]\r\n\r\n",
      (freq - 65536) / 1000000,
      (freq - 65536) % 1000000,
      gpio_get(GPIOB, GPIO1) ? '.' : ' ',
      hold ? "ON " : "OFF"
    );

    usbcdc_printf("Clock output: %s\r\n", mco_name[mco_current]);
    usbcdc_printf("Digital Filter: %s\r\n", filters_name[filter_current]);
    usbcdc_printf("Pre-scaler: %s\r\n", prescalers_name[prescaler_current]);

    /* Output to LCD */
    sprintf(str,"%4lu.%06lu MHz%c",(freq - 65536) / 1000000,(freq - 65536) % 1000000, gpio_get(GPIOB, GPIO1) ? '.' : ' ');
    SetLcdXY(0,0);
    puts_lcd(str);
    SetLcdXY(0,1);
    sprintf(str,"%sOut %s",hold ? "HLD " : "    ",mco_name_lcd[mco_current]);
    puts_lcd(str);  
    /* is_print_custom ---> Flag to know if custom char is printed */
    /* Only print custom char when mco change to 8 Mhz RC */
    if (mco_current == 1 && is_print_custom == false && mco_current == mco_old)
    {
        SetLcdXY(15,1);
        puts_custom_char(0);
        is_print_custom = true;
        SetLcdXY(0,1);
    }
    /* Show Pre-scaler info */
    if (prescaler_old != prescaler_current)
    {
        is_print_custom = false;
        sprintf(str,"Pre-scaler: %s ",prescalers_name[prescaler_current]);
        prescaler_old = prescaler_current;
        SetLcdXY(0,1);
        puts_lcd(str);
        /* Delay for output message */
        while (systick_ms < (last_ms + 2000));
    }
    /* Show Digital Filter info */
    if (filter_old != filter_current)
    {
        is_print_custom = false;
        sprintf(str,"Fltr: %s",filters_name[filter_current]);
        filter_old = filter_current;
        SetLcdXY(0,1);
        puts_lcd(str);
        /* Delay for output message */
        while (systick_ms < (last_ms + 2000));
    }
    /* Show Clock Output info */
    if (mco_old != mco_current)
    {
        is_print_custom = false;
        sprintf(str,"Clk: %s  ",mco_name[mco_current]);
        mco_old = mco_current;
        SetLcdXY(0,1);
        puts_lcd(str);
        /* Delay for output message */
        while (systick_ms < (last_ms + 2000));
    }
    while (systick_ms < (last_ms + DISP_DELAY));
    last_ms = systick_ms;
  }

  return 0;
}

/* Interrupts */

void tim2_isr(void) {
  if (timer_get_flag(TIM2, TIM_SR_CC1IF)) {
    freq_scratch += 65536; /* TIM2 is 16-bit and overflows every 65536 events. */
    timer_clear_flag(TIM2, TIM_SR_CC1IF); /* Clear interrupt flag. */
  }
}

void sys_tick_handler(void) {
  systick_ms ++;

  if (systick_ms % 1000 == 0) {
    /* Scratch pad to finalized result */
    if (!hold) {
      freq = freq_scratch + timer_get_counter(TIM2);
      if (prescaler_current)
        freq *= (1 << prescaler_current);
    }
    /* Reset the counter. This will generate one extra overflow for next measurement. */
    /* In case of nothing got counted, manually generate a reset to keep consistency. */
    timer_set_counter(TIM2, 1);
    timer_set_counter(TIM2, 0);
    freq_scratch = 0;
    gpio_toggle(GPIOB, GPIO1);
  }
}
