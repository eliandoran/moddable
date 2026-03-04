/*
 * Copyright (c) 2024-2026  Moddable Tech, Inc.
 *
 *   This file is part of the Moddable SDK Runtime.
 *
 *   The Moddable SDK Runtime is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   The Moddable SDK Runtime is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with the Moddable SDK Runtime.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
	ESP32-S3 RGB LCD parallel display driver
	Uses esp_lcd_rgb_panel with double-buffered framebuffer in PSRAM.
	Integrates with Poco via kPocoFrameBuffer=1 (doBeginFrameBuffer path).
*/

#include "xsmc.h"
#include "xsHost.h"

#include "commodettoBitmap.h"
#include "commodettoPocoBlit.h"
#include "commodettoPixelsOut.h"
#include "mc.xs.h"
#include "mc.defines.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_err.h"
#include "driver/gpio.h"

/*
 * ---- Pin validation ----
 */
#ifndef MODDEF_RGBLCD_HSYNC_PIN
	#error MODDEF_RGBLCD_HSYNC_PIN not defined
#endif
#ifndef MODDEF_RGBLCD_VSYNC_PIN
	#error MODDEF_RGBLCD_VSYNC_PIN not defined
#endif
#ifndef MODDEF_RGBLCD_DE_PIN
	#error MODDEF_RGBLCD_DE_PIN not defined
#endif
#ifndef MODDEF_RGBLCD_PCLK_PIN
	#error MODDEF_RGBLCD_PCLK_PIN not defined
#endif

#ifndef MODDEF_RGBLCD_DATA0_PIN
	#error data pins not defined
#endif

/*
 * ---- Defaults ----
 */
#ifndef MODDEF_RGBLCD_WIDTH
	#define MODDEF_RGBLCD_WIDTH 800
#endif
#ifndef MODDEF_RGBLCD_HEIGHT
	#define MODDEF_RGBLCD_HEIGHT 480
#endif
#ifndef MODDEF_RGBLCD_PCLK_HZ
	#define MODDEF_RGBLCD_PCLK_HZ (18000000)
#endif
#ifndef MODDEF_RGBLCD_DATA_WIDTH
	#define MODDEF_RGBLCD_DATA_WIDTH 16
#endif
#ifndef MODDEF_RGBLCD_DISP_PIN
	#define MODDEF_RGBLCD_DISP_PIN (-1)
#endif

/* Timing defaults (can be overridden via manifest defines) */
#ifndef MODDEF_RGBLCD_HSYNC_PULSE_WIDTH
	#define MODDEF_RGBLCD_HSYNC_PULSE_WIDTH 4
#endif
#ifndef MODDEF_RGBLCD_HSYNC_BACK_PORCH
	#define MODDEF_RGBLCD_HSYNC_BACK_PORCH 8
#endif
#ifndef MODDEF_RGBLCD_HSYNC_FRONT_PORCH
	#define MODDEF_RGBLCD_HSYNC_FRONT_PORCH 8
#endif
#ifndef MODDEF_RGBLCD_VSYNC_PULSE_WIDTH
	#define MODDEF_RGBLCD_VSYNC_PULSE_WIDTH 4
#endif
#ifndef MODDEF_RGBLCD_VSYNC_BACK_PORCH
	#define MODDEF_RGBLCD_VSYNC_BACK_PORCH 8
#endif
#ifndef MODDEF_RGBLCD_VSYNC_FRONT_PORCH
	#define MODDEF_RGBLCD_VSYNC_FRONT_PORCH 8
#endif
#ifndef MODDEF_RGBLCD_PCLK_IDLE_HIGH
	#define MODDEF_RGBLCD_PCLK_IDLE_HIGH 1
#endif
#ifndef MODDEF_RGBLCD_PCLK_ACTIVE_NEG
	#define MODDEF_RGBLCD_PCLK_ACTIVE_NEG 1
#endif
#ifndef MODDEF_RGBLCD_NUM_FBS
	#define MODDEF_RGBLCD_NUM_FBS 1
#endif
#ifndef MODDEF_RGBLCD_DMA_BURST_SIZE
	#define MODDEF_RGBLCD_DMA_BURST_SIZE 64
#endif
#ifndef MODDEF_RGBLCD_BOUNCE_BUFFER_SIZE_PX
	#define MODDEF_RGBLCD_BOUNCE_BUFFER_SIZE_PX (MODDEF_RGBLCD_WIDTH * 10)
#endif
#ifndef MODDEF_RGBLCD_POWER_PIN
	#define MODDEF_RGBLCD_POWER_PIN 19
#endif

/*
 * ---- Driver state ----
 */
typedef struct {
	PixelsOutDispatch			dispatch;

	esp_lcd_panel_handle_t		panel_handle;
	SemaphoreHandle_t			vsync_sem;

	void						*fb;			// single framebuffer (allocated by esp_lcd)

	uint8_t						firstFrame;
} rgbDisplayRecord, *rgbDisplay;

/*
 * ---- Forward declarations ----
 */
static void rgblcdBeginFrameBuffer(void *refcon, CommodettoPixel **pixels, int16_t *rowBytes);
static void rgblcdEnd(void *refcon);

#if !kPocoFrameBuffer
	#error RGB LCD driver requires kPocoFrameBuffer=1. Set POCO_FRAMEBUFFER=1 in the target manifest build section.
#endif

static const PixelsOutDispatchRecord gPixelsOutDispatch ICACHE_RODATA_ATTR = {
	NULL,			/* doBegin — not used in framebuffer mode */
	rgblcdEnd,		/* doContinue — flush on continue too */
	rgblcdEnd,		/* doEnd */
	NULL,			/* doSend — not used in framebuffer mode */
	NULL,			/* doAdaptInvalid */
	rgblcdBeginFrameBuffer
};

/*
 * ---- VSYNC ISR callback ----
 */
static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t panel,
								const esp_lcd_rgb_panel_event_data_t *edata,
								void *user_ctx)
{
	rgbDisplay rd = (rgbDisplay)user_ctx;
	BaseType_t high_task_woken = pdFALSE;

	if (rd->vsync_sem)
		xSemaphoreGiveFromISR(rd->vsync_sem, &high_task_woken);

	return (high_task_woken == pdTRUE);
}

/*
 * ---- Destructor ----
 */
void xs_rgblcd_destructor(void *data)
{
	rgbDisplay rd = data;
	if (!rd) return;

	if (rd->panel_handle) {
		esp_lcd_panel_del(rd->panel_handle);
		rd->panel_handle = NULL;
	}

	if (rd->vsync_sem) {
		vSemaphoreDelete(rd->vsync_sem);
		rd->vsync_sem = NULL;
	}

	c_free(rd);
}

/*
 * ---- Constructor (called from JS) ----
 */
void xs_rgblcd(xsMachine *the)
{
	rgbDisplay rd;
	esp_err_t err;

	if (xsmcHas(xsArg(0), xsID_pixelFormat)) {
		xsmcVars(1);
		xsmcGet(xsVar(0), xsArg(0), xsID_pixelFormat);
		if (kCommodettoBitmapFormat != xsmcToInteger(xsVar(0)))
			xsUnknownError("bad format");
	}

	rd = c_calloc(1, sizeof(rgbDisplayRecord));
	if (!rd)
		xsUnknownError("no memory");

	xsmcSetHostData(xsThis, rd);

	rd->dispatch = (PixelsOutDispatch)&gPixelsOutDispatch;
	rd->firstFrame = 1;

	/* ---- Drive power-enable pin (e.g. GPIO 19) ---- */
#if MODDEF_RGBLCD_POWER_PIN >= 0
	gpio_config_t pwr_cfg = {
		.pin_bit_mask = 1ULL << MODDEF_RGBLCD_POWER_PIN,
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE
	};
	gpio_config(&pwr_cfg);
	gpio_set_level(MODDEF_RGBLCD_POWER_PIN, 0);
	vTaskDelay(pdMS_TO_TICKS(50));
#endif

	/* ---- Configure RGB panel ---- */
	esp_lcd_rgb_panel_config_t panel_config = {};
	panel_config.clk_src        = LCD_CLK_SRC_DEFAULT;
	panel_config.data_width     = MODDEF_RGBLCD_DATA_WIDTH;
	panel_config.bits_per_pixel = 16;
	panel_config.num_fbs        = MODDEF_RGBLCD_NUM_FBS;
	panel_config.dma_burst_size = MODDEF_RGBLCD_DMA_BURST_SIZE;

	panel_config.hsync_gpio_num = MODDEF_RGBLCD_HSYNC_PIN;
	panel_config.vsync_gpio_num = MODDEF_RGBLCD_VSYNC_PIN;
	panel_config.de_gpio_num    = MODDEF_RGBLCD_DE_PIN;
	panel_config.pclk_gpio_num  = MODDEF_RGBLCD_PCLK_PIN;
	panel_config.disp_gpio_num  = MODDEF_RGBLCD_DISP_PIN;

	panel_config.data_gpio_nums[0]  = MODDEF_RGBLCD_DATA0_PIN;
	panel_config.data_gpio_nums[1]  = MODDEF_RGBLCD_DATA1_PIN;
	panel_config.data_gpio_nums[2]  = MODDEF_RGBLCD_DATA2_PIN;
	panel_config.data_gpio_nums[3]  = MODDEF_RGBLCD_DATA3_PIN;
	panel_config.data_gpio_nums[4]  = MODDEF_RGBLCD_DATA4_PIN;
	panel_config.data_gpio_nums[5]  = MODDEF_RGBLCD_DATA5_PIN;
	panel_config.data_gpio_nums[6]  = MODDEF_RGBLCD_DATA6_PIN;
	panel_config.data_gpio_nums[7]  = MODDEF_RGBLCD_DATA7_PIN;
	panel_config.data_gpio_nums[8]  = MODDEF_RGBLCD_DATA8_PIN;
	panel_config.data_gpio_nums[9]  = MODDEF_RGBLCD_DATA9_PIN;
	panel_config.data_gpio_nums[10] = MODDEF_RGBLCD_DATA10_PIN;
	panel_config.data_gpio_nums[11] = MODDEF_RGBLCD_DATA11_PIN;
	panel_config.data_gpio_nums[12] = MODDEF_RGBLCD_DATA12_PIN;
	panel_config.data_gpio_nums[13] = MODDEF_RGBLCD_DATA13_PIN;
	panel_config.data_gpio_nums[14] = MODDEF_RGBLCD_DATA14_PIN;
	panel_config.data_gpio_nums[15] = MODDEF_RGBLCD_DATA15_PIN;

	/* Timing parameters */
	panel_config.timings.pclk_hz           = MODDEF_RGBLCD_PCLK_HZ;
	panel_config.timings.h_res             = MODDEF_RGBLCD_WIDTH;
	panel_config.timings.v_res             = MODDEF_RGBLCD_HEIGHT;
	panel_config.timings.hsync_pulse_width = MODDEF_RGBLCD_HSYNC_PULSE_WIDTH;
	panel_config.timings.hsync_back_porch  = MODDEF_RGBLCD_HSYNC_BACK_PORCH;
	panel_config.timings.hsync_front_porch = MODDEF_RGBLCD_HSYNC_FRONT_PORCH;
	panel_config.timings.vsync_pulse_width = MODDEF_RGBLCD_VSYNC_PULSE_WIDTH;
	panel_config.timings.vsync_back_porch  = MODDEF_RGBLCD_VSYNC_BACK_PORCH;
	panel_config.timings.vsync_front_porch = MODDEF_RGBLCD_VSYNC_FRONT_PORCH;
	panel_config.timings.flags.pclk_idle_high = MODDEF_RGBLCD_PCLK_IDLE_HIGH;
	panel_config.timings.flags.pclk_active_neg = MODDEF_RGBLCD_PCLK_ACTIVE_NEG;

	panel_config.flags.fb_in_psram = 1;
	panel_config.bounce_buffer_size_px = MODDEF_RGBLCD_BOUNCE_BUFFER_SIZE_PX;

	err = esp_lcd_new_rgb_panel(&panel_config, &rd->panel_handle);
	if (ESP_OK != err) {
		xs_rgblcd_destructor(rd);
		xsmcSetHostData(xsThis, NULL);
		xsUnknownError("esp_lcd_new_rgb_panel failed");
	}

	/* Register VSYNC callback */
	esp_lcd_rgb_panel_event_callbacks_t cbs = {};
	cbs.on_vsync = on_vsync;
	err = esp_lcd_rgb_panel_register_event_callbacks(rd->panel_handle, &cbs, rd);
	if (ESP_OK != err) {
		xs_rgblcd_destructor(rd);
		xsmcSetHostData(xsThis, NULL);
		xsUnknownError("register VSYNC callback failed");
	}

	err = esp_lcd_panel_reset(rd->panel_handle);
	if (ESP_OK != err) {
		xs_rgblcd_destructor(rd);
		xsmcSetHostData(xsThis, NULL);
		xsUnknownError("panel reset failed");
	}

	err = esp_lcd_panel_init(rd->panel_handle);
	if (ESP_OK != err) {
		xs_rgblcd_destructor(rd);
		xsmcSetHostData(xsThis, NULL);
		xsUnknownError("panel init failed");
	}

	/* Get driver-allocated framebuffer from PSRAM */
	err = esp_lcd_rgb_panel_get_frame_buffer(rd->panel_handle, 1, &rd->fb);
	if (ESP_OK != err) {
		xs_rgblcd_destructor(rd);
		xsmcSetHostData(xsThis, NULL);
		xsUnknownError("get framebuffer failed");
	}

	rd->vsync_sem = xSemaphoreCreateBinary();
	if (!rd->vsync_sem) {
		xs_rgblcd_destructor(rd);
		xsmcSetHostData(xsThis, NULL);
		xsUnknownError("no memory for semaphore");
	}

	/* Clear framebuffer to black */
	c_memset(rd->fb, 0, MODDEF_RGBLCD_WIDTH * MODDEF_RGBLCD_HEIGHT * 2);
}

/*
 * ---- PixelsOutDispatch: doBeginFrameBuffer ----
 *
 * Called by Poco/Piu at the start of a frame. Returns a pointer to the
 * "back" framebuffer for Poco to render into.
 */
void rgblcdBeginFrameBuffer(void *refcon, CommodettoPixel **pixels, int16_t *rowBytes)
{
	rgbDisplay rd = refcon;

	/* Wait for VSYNC *before* rendering so Poco draws during the
	   blanking interval, minimizing the chance the LCD scans a
	   partially-rendered dirty rectangle (white flash artifacts). */
	xSemaphoreTake(rd->vsync_sem, pdMS_TO_TICKS(100));

	/* Always return the single persistent framebuffer */
	*pixels = (CommodettoPixel *)rd->fb;
	*rowBytes = MODDEF_RGBLCD_WIDTH * sizeof(CommodettoPixel);
}

/*
 * ---- PixelsOutDispatch: doEnd ----
 *
 * Called by Poco/Piu when rendering is complete.
 */
void rgblcdEnd(void *refcon)
{
	rgbDisplay rd = refcon;
	rd->firstFrame = 0;
}

/*
 * ---- JS bindings ----
 */

void xs_rgblcd_begin(xsMachine *the)
{
	/* In framebuffer mode, begin is handled via c_dispatch->doBeginFrameBuffer.
	   This JS binding exists for API compatibility but is not normally called. */
	rgbDisplay rd = xsmcGetHostData(xsThis);
	CommodettoPixel *pixels;
	int16_t rowBytes;

	rgblcdBeginFrameBuffer(rd, &pixels, &rowBytes);
}

void xs_rgblcd_send(xsMachine *the)
{
	/* Not used in framebuffer mode — Poco renders directly into the framebuffer */
}

void xs_rgblcd_end(xsMachine *the)
{
	rgbDisplay rd = xsmcGetHostData(xsThis);
	rgblcdEnd(rd);
}

void xs_rgblcd_continue(xsMachine *the)
{
	/* Nothing to do — framebuffer mode handles continue via doEnd dispatch */
}

void xs_rgblcd_pixelsToBytes(xsMachine *the)
{
	int count = xsmcToInteger(xsArg(0));
	xsmcSetInteger(xsResult, ((count * kCommodettoPixelSize) + 7) >> 3);
}

void xs_rgblcd_get_pixelFormat(xsMachine *the)
{
	xsmcSetInteger(xsResult, kCommodettoBitmapFormat);
}

void xs_rgblcd_get_width(xsMachine *the)
{
	xsmcSetInteger(xsResult, MODDEF_RGBLCD_WIDTH);
}

void xs_rgblcd_get_height(xsMachine *the)
{
	xsmcSetInteger(xsResult, MODDEF_RGBLCD_HEIGHT);
}

void xs_rgblcd_get_c_dispatch(xsMachine *the)
{
	xsResult = xsThis;
}

void xs_rgblcd_close(xsMachine *the)
{
	rgbDisplay rd = xsmcGetHostData(xsThis);
	if (!rd) return;
	xs_rgblcd_destructor(rd);
	xsmcSetHostData(xsThis, NULL);
}

void xs_rgblcd_get_frameBuffer(xsMachine *the)
{
	xsmcSetTrue(xsResult);
}
