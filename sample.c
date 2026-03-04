#include "pins_config.h"

#include <Arduino.h>
#include <lvgl.h>
#include <Wire.h>
#include <freertos/semphr.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <TAMC_GT911.h>

#include <demos/music/lv_demo_music.h>

// ----- Pin definitions -----
#define TOUCH_SDA  15
#define TOUCH_SCL  16
#define TOUCH_INT  -1   // Not connected on this board
#define TOUCH_RST   1

// ----- Objects -----
TAMC_GT911 touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, LCD_H_RES, LCD_V_RES);

static esp_lcd_panel_handle_t panel_handle = NULL;
static SemaphoreHandle_t vsync_sem = NULL;
static lv_disp_draw_buf_t draw_buf;

// --------------------------------------------------------
// VSYNC ISR — signals that a buffer swap has occurred
// --------------------------------------------------------
static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t panel,
                                const esp_lcd_rgb_panel_event_data_t *edata,
                                void *user_ctx) {
  BaseType_t high_task_woken = pdFALSE;
  if (vsync_sem) {
    xSemaphoreGiveFromISR(vsync_sem, &high_task_woken);
  }
  return high_task_woken == pdTRUE;
}

// --------------------------------------------------------
// LVGL display flush — zero-copy buffer swap
// --------------------------------------------------------
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  // Tell the RGB peripheral to scan from the buffer LVGL just finished drawing
  esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, color_p);
  // Wait for VSYNC to confirm the swap happened before LVGL writes to the other buffer
  xSemaphoreTake(vsync_sem, pdMS_TO_TICKS(100));
  lv_disp_flush_ready(disp);
}

// --------------------------------------------------------
// LVGL touch read — polling GT911 via I2C
// --------------------------------------------------------
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  touch.read();
  if (touch.isTouched) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touch.points[0].x;
    data->point.y = touch.points[0].y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// --------------------------------------------------------
// I2C helpers (for the backlight/IO controller at 0x30)
// --------------------------------------------------------
bool i2cScanForAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

void sendI2CCommand(uint8_t command) {
  Wire.beginTransmission(0x30);
  Wire.write(command);
  uint8_t error = Wire.endTransmission();
  if (error == 0) {
    Serial.printf("Command 0x%02X sent OK\n", command);
  } else {
    Serial.printf("Command send error: %d\n", error);
  }
}

// --------------------------------------------------------
// Setup
// --------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(19, OUTPUT);

  // I2C for backlight controller (0x30) and touch (0x5D)
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  delay(50);

  while (1) {
    if (i2cScanForAddress(0x30) && i2cScanForAddress(0x5D)) {
      Serial.println("I2C: 0x30 + 0x5D detected");
      break;
    }
    Serial.println("I2C devices not found, resetting...");
    sendI2CCommand(250);    // Activate touch screen
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(120);
    pinMode(TOUCH_RST, INPUT);
    delay(100);
  }

  // Backlight: 0 = max brightness, 245 = off
  sendI2CCommand(0);

  // Initialize touch controller
  touch.begin(GT911_ADDR1);
  touch.setRotation(ROTATION_INVERTED);

  // -------- Create RGB LCD panel (esp_lcd native driver) --------
  esp_lcd_rgb_panel_config_t panel_config = {};
  panel_config.clk_src        = LCD_CLK_SRC_DEFAULT;
  panel_config.data_width     = 16;
  panel_config.bits_per_pixel = 16;
  panel_config.num_fbs        = 2;       // Double framebuffer in PSRAM
  panel_config.dma_burst_size = 64;

  panel_config.hsync_gpio_num = 40;
  panel_config.vsync_gpio_num = 41;
  panel_config.de_gpio_num    = 42;
  panel_config.pclk_gpio_num  = 39;
  panel_config.disp_gpio_num  = -1;

  panel_config.data_gpio_nums[0]  = 21;  // B0
  panel_config.data_gpio_nums[1]  = 47;  // B1
  panel_config.data_gpio_nums[2]  = 48;  // B2
  panel_config.data_gpio_nums[3]  = 45;  // B3
  panel_config.data_gpio_nums[4]  = 38;  // B4
  panel_config.data_gpio_nums[5]  = 9;   // G0
  panel_config.data_gpio_nums[6]  = 10;  // G1
  panel_config.data_gpio_nums[7]  = 11;  // G2
  panel_config.data_gpio_nums[8]  = 12;  // G3
  panel_config.data_gpio_nums[9]  = 13;  // G4
  panel_config.data_gpio_nums[10] = 14;  // G5
  panel_config.data_gpio_nums[11] = 7;   // R0
  panel_config.data_gpio_nums[12] = 17;  // R1
  panel_config.data_gpio_nums[13] = 18;  // R2
  panel_config.data_gpio_nums[14] = 3;   // R3
  panel_config.data_gpio_nums[15] = 46;  // R4

  // Timing — matches the original LovyanGFX configuration
  panel_config.timings.pclk_hz           = 18000000;  // 18 MHz pixel clock
  panel_config.timings.h_res             = LCD_H_RES;
  panel_config.timings.v_res             = LCD_V_RES;
  panel_config.timings.hsync_pulse_width = 4;
  panel_config.timings.hsync_back_porch  = 8;
  panel_config.timings.hsync_front_porch = 8;
  panel_config.timings.vsync_pulse_width = 4;
  panel_config.timings.vsync_back_porch  = 8;
  panel_config.timings.vsync_front_porch = 8;
  panel_config.timings.flags.pclk_idle_high = 1;

  panel_config.flags.fb_in_psram = 1;

  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));

  // Register VSYNC callback for frame synchronization
  esp_lcd_rgb_panel_event_callbacks_t cbs = {};
  cbs.on_vsync = on_vsync;
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, NULL));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

  // Get the driver-allocated frame buffers (zero-copy — LVGL draws directly into these)
  void *fb0 = NULL, *fb1 = NULL;
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &fb0, &fb1));

  vsync_sem = xSemaphoreCreateBinary();

  // -------- LVGL init --------
  lv_init();

  lv_disp_draw_buf_init(&draw_buf, fb0, fb1, LCD_H_RES * LCD_V_RES);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res      = LCD_H_RES;
  disp_drv.ver_res      = LCD_V_RES;
  disp_drv.flush_cb     = my_disp_flush;
  disp_drv.draw_buf     = &draw_buf;
  disp_drv.full_refresh = 1;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type    = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  lv_demo_music();

  Serial.println("Setup done");
}

void loop() {
  lv_timer_handler();
  delay(1);
}
