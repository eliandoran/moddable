/*
 * Copyright (c) 2016-2026  Moddable Tech, Inc.
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
	SSD1963 display controller driver - 16-bit parallel (i80) interface, RGB565
	Adapted from SSD1963 8-bit parallel driver
*/

#include "xsmc.h"
#include "xsHost.h"

#include "commodettoBitmap.h"
#include "commodettoPocoBlit.h"
#include "commodettoPixelsOut.h"
#include "mc.xs.h"			// for xsID_ values
#include "mc.defines.h"

#include "modGPIO.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_lcd_types.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"

#include "driver/gpio.h"

#if !defined(MODDEF_SSD1963P16_DC_PIN) || !defined(MODDEF_SSD1963P16_PCLK_PIN)
	#error required pin not defined
#endif
#if !defined(MODDEF_SSD1963P16_DATA0_PIN) || !defined(MODDEF_SSD1963P16_DATA1_PIN) || !defined(MODDEF_SSD1963P16_DATA2_PIN) || !defined(MODDEF_SSD1963P16_DATA3_PIN) || !defined(MODDEF_SSD1963P16_DATA4_PIN) || !defined(MODDEF_SSD1963P16_DATA5_PIN) || !defined(MODDEF_SSD1963P16_DATA6_PIN) || !defined(MODDEF_SSD1963P16_DATA7_PIN)
	#error required data pin not defined (D0-D7)
#endif
#if !defined(MODDEF_SSD1963P16_DATA8_PIN) || !defined(MODDEF_SSD1963P16_DATA9_PIN) || !defined(MODDEF_SSD1963P16_DATA10_PIN) || !defined(MODDEF_SSD1963P16_DATA11_PIN) || !defined(MODDEF_SSD1963P16_DATA12_PIN) || !defined(MODDEF_SSD1963P16_DATA13_PIN) || !defined(MODDEF_SSD1963P16_DATA14_PIN) || !defined(MODDEF_SSD1963P16_DATA15_PIN)
	#error required data pin not defined (D8-D15)
#endif
#ifndef MODDEF_SSD1963P16_HZ
	#define MODDEF_SSD1963P16_HZ (10000000)
#endif
#ifndef MODDEF_SSD1963P16_WIDTH
	#define MODDEF_SSD1963P16_WIDTH 480
#endif
#ifndef MODDEF_SSD1963P16_HEIGHT
	#define MODDEF_SSD1963P16_HEIGHT 272
#endif
#ifndef MODDEF_SSD1963P16_FLIPX
	#define MODDEF_SSD1963P16_FLIPX (false)
#endif
#ifndef MODDEF_SSD1963P16_FLIPY
	#define MODDEF_SSD1963P16_FLIPY (false)
#endif
#ifndef MODDEF_SSD1963P16_BACKLIGHT_ON
	#define MODDEF_SSD1963P16_BACKLIGHT_ON (0)
#endif
#if MODDEF_SSD1963P16_BACKLIGHT_ON
	#define MODDEF_SSD1963P16_BACKLIGHT_OFF (0)
#else
	#define MODDEF_SSD1963P16_BACKLIGHT_OFF (1)
#endif
#ifndef MODDEF_SSD1963P16_COLUMN_OFFSET
	#define MODDEF_SSD1963P16_COLUMN_OFFSET 0
#endif
#ifndef MODDEF_SSD1963P16_ROW_OFFSET
	#define MODDEF_SSD1963P16_ROW_OFFSET 0
#endif
#ifndef MODDEF_SSD1963P16_CMD_BITS
	#define MODDEF_SSD1963P16_CMD_BITS 16
#endif
#ifndef MODDEF_SSD1963P16_OPQUEUE
	#define MODDEF_SSD1963P16_OPQUEUE (10)
#endif

typedef struct {
	PixelsOutDispatch			dispatch;

#ifdef MODDEF_SSD1963P16_RST_PIN
	modGPIOConfigurationRecord	rst;
#endif
#ifdef MODDEF_SSD1963P16_BACKLIGHT_PIN
	modGPIOConfigurationRecord	backlight;
#endif
#ifdef MODDEF_SSD1963P16_READ_PIN
	modGPIOConfigurationRecord	readEn;
#endif
#ifdef MODDEF_SSD1963P16_TEARINGEFFECT_PIN
	SemaphoreHandle_t			startSend;
#endif

	int updateWidth;
	int updateLinesRemaining;
	int yMin;
	int yMax;
	int ping;
	uint8_t nothingSent;

	uint8_t						firstFrame;
#ifdef MODDEF_SSD1963P16_TEARINGEFFECT_PIN
	uint8_t						firstBuffer;
	uint8_t						isContinue;
	uint8_t						waiting;
	uint8_t						syncFrames;
#endif
	uint8_t						memoryAccessControl;	// register 36h initialization value
	uint8_t						rotation;				// 0, 1, 2, 3 => 0, 90, 180, 270
	uint8_t						progressReverse;		// progressive send decrements yMax instead of incrementing yMin

	SemaphoreHandle_t			colorsInFlight;
	esp_lcd_panel_io_handle_t	io_handle;
	esp_lcd_i80_bus_handle_t	i80_bus_handle;

	QueueHandle_t				ops;
	int							opZero;		// async command is zero

	uint8_t data[32];
} spiDisplayRecord, *spiDisplay;

static void ssd1963Init(spiDisplay sd);

// note that sync and async are very different:
//	- async needs data to remain available until completed
//	- async data needs to be endian flipped when swap_color_bytes is enabled
#define ssd1963Command(sd, command, data, count) (esp_lcd_panel_io_tx_param(sd->io_handle, command, data, count))
#define ssd1963CommandAsync(sd, command, data, count) \
	xQueueSend(sd->ops, &sd->opZero, portMAX_DELAY); \
	esp_lcd_panel_io_tx_color(sd->io_handle, command, data, count)

static void ssd1963Begin(void *refcon, CommodettoCoordinate x, CommodettoCoordinate y, CommodettoDimension w, CommodettoDimension h);
static void ssd1963End(void *refcon);
static void ssd1963Continue(void *refcon);

#ifdef MODDEF_SSD1963P16_TEARINGEFFECT_PIN
	static void tearingEffectISR(void *refcon);
#endif

static void ssd1963Send(PocoPixel *pixels, int byteLength, void *refcon);
static const PixelsOutDispatchRecord gPixelsOutDispatch ICACHE_RODATA_ATTR = {
	ssd1963Begin,
	ssd1963Continue,
	ssd1963End,
	ssd1963Send,
	NULL
};

void xs_ssd1963p16_destructor(void *data)
{
	spiDisplay sd = data;
	if (!data) return;

#ifdef MODDEF_SSD1963P16_TEARINGEFFECT_PIN
	if (sd->startSend)
		vSemaphoreDelete(sd->startSend);

	gpio_isr_handler_remove(MODDEF_SSD1963P16_TEARINGEFFECT_PIN);
#endif

	if (sd->io_handle)
		esp_lcd_panel_io_del(sd->io_handle);

	if (sd->i80_bus_handle)
		esp_lcd_del_i80_bus(sd->i80_bus_handle);

#ifdef MODDEF_SSD1963P16_RST_PIN
	modGPIOUninit(&sd->rst);
#endif

	if (sd->ops)
		vQueueDelete(sd->ops);

	if (sd->colorsInFlight)
		vSemaphoreDelete(sd->colorsInFlight);

	c_free(data);
}

static bool colorDone(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
	spiDisplay sd = user_ctx;
	BaseType_t high_task_woken = pdFALSE, doYield = pdFALSE;
	int op;

	if (pdTRUE == xQueueReceiveFromISR(sd->ops, &op, &high_task_woken)) {		// assert - should never be pdFALSE
		if (op)
			xSemaphoreGiveFromISR(sd->colorsInFlight, &doYield);
	}

	return doYield || high_task_woken;
}

void xs_ssd1963p16(xsMachine *the)
{
	spiDisplay sd;

	if (xsmcHas(xsArg(0), xsID_pixelFormat)) {
		xsmcVars(1);
		xsmcGet(xsVar(0), xsArg(0), xsID_pixelFormat);
		if (kCommodettoBitmapFormat != xsmcToInteger(xsVar(0)))
			xsUnknownError("bad format");
	}

	sd = c_calloc(1, sizeof(spiDisplayRecord));
	if (!sd)
		xsUnknownError("no memory");

	xsmcSetHostData(xsThis, sd);

	int err;
	esp_lcd_i80_bus_config_t bus_config = {
		.dc_gpio_num = MODDEF_SSD1963P16_DC_PIN,
		.wr_gpio_num = MODDEF_SSD1963P16_PCLK_PIN,
		.data_gpio_nums = {
			MODDEF_SSD1963P16_DATA0_PIN,
			MODDEF_SSD1963P16_DATA1_PIN,
			MODDEF_SSD1963P16_DATA2_PIN,
			MODDEF_SSD1963P16_DATA3_PIN,
			MODDEF_SSD1963P16_DATA4_PIN,
			MODDEF_SSD1963P16_DATA5_PIN,
			MODDEF_SSD1963P16_DATA6_PIN,
			MODDEF_SSD1963P16_DATA7_PIN,
			MODDEF_SSD1963P16_DATA8_PIN,
			MODDEF_SSD1963P16_DATA9_PIN,
			MODDEF_SSD1963P16_DATA10_PIN,
			MODDEF_SSD1963P16_DATA11_PIN,
			MODDEF_SSD1963P16_DATA12_PIN,
			MODDEF_SSD1963P16_DATA13_PIN,
			MODDEF_SSD1963P16_DATA14_PIN,
			MODDEF_SSD1963P16_DATA15_PIN,
		},
		.bus_width = 16,
		.max_transfer_bytes = MODDEF_SSD1963P16_WIDTH * 32 * 2,

		.clk_src = LCD_CLK_SRC_DEFAULT,
		.psram_trans_align = 64,
		.sram_trans_align = 4,
	};

	err = esp_lcd_new_i80_bus(&bus_config, &sd->i80_bus_handle);
	if (err)
		xsUnknownError("esp_lcd_new_i80_bus failed");

	esp_lcd_panel_io_i80_config_t io_config = {
#ifdef MODDEF_SSD1963P16_CS_PIN
		.cs_gpio_num = MODDEF_SSD1963P16_CS_PIN,
#else
		.cs_gpio_num = -1,	// "-1 will declaim exclusively use of I80 bus"
#endif
		.pclk_hz = MODDEF_SSD1963P16_HZ,
		.trans_queue_depth = MODDEF_SSD1963P16_OPQUEUE,
		.on_color_trans_done = colorDone,
		.user_ctx = sd,
		.dc_levels = {
			.dc_idle_level = 0,
			.dc_cmd_level = 0,
			.dc_dummy_level = 0,
			.dc_data_level = 1,
		},
		.flags = {
			.cs_active_high = 0,
			.pclk_active_neg = 0,
			.pclk_idle_low = 0,
			.reverse_color_bits = 0,
			.swap_color_bytes = 0,
		},
		.lcd_cmd_bits = MODDEF_SSD1963P16_CMD_BITS,
		.lcd_param_bits = 8,
	};
	err = esp_lcd_new_panel_io_i80(sd->i80_bus_handle, &io_config, &sd->io_handle);
	if (err)
		xsUnknownError("esp_lcd_new_panel_io_i80 failed");

	sd->dispatch = (PixelsOutDispatch)&gPixelsOutDispatch;

#ifdef MODDEF_SSD1963P16_RST_PIN
	modGPIOInit(&sd->rst, NULL, MODDEF_SSD1963P16_RST_PIN, kModGPIOOutput);
	modGPIOWrite(&sd->rst, 1);
#endif

#ifdef MODDEF_SSD1963P16_READ_PIN
	modGPIOInit(&sd->readEn, NULL, MODDEF_SSD1963P16_READ_PIN, kModGPIOOutput);
	modGPIOWrite(&sd->readEn, 1);		// Read high (inactive)
#endif

#ifdef MODDEF_SSD1963P16_TEARINGEFFECT_PIN
	gpio_pad_select_gpio(MODDEF_SSD1963P16_TEARINGEFFECT_PIN);
	gpio_set_direction(MODDEF_SSD1963P16_TEARINGEFFECT_PIN, GPIO_MODE_INPUT);
	gpio_set_pull_mode(MODDEF_SSD1963P16_TEARINGEFFECT_PIN, GPIO_FLOATING);
	gpio_install_isr_service(0);
	gpio_set_intr_type(MODDEF_SSD1963P16_TEARINGEFFECT_PIN, GPIO_INTR_POSEDGE);
	gpio_isr_handler_add(MODDEF_SSD1963P16_TEARINGEFFECT_PIN, tearingEffectISR, sd);

	sd->startSend = xSemaphoreCreateBinary();
	sd->syncFrames = 1;
#endif

	sd->ops = xQueueCreate(MODDEF_SSD1963P16_OPQUEUE, sizeof(int));
	sd->opZero = 0;

	sd->colorsInFlight = xSemaphoreCreateCounting(2, 2);		// async client uses two pixel buffers

	ssd1963Init(sd);

#ifdef MODDEF_SSD1963P16_BACKLIGHT_PIN
	modGPIOInit(&sd->backlight, NULL, MODDEF_SSD1963P16_BACKLIGHT_PIN, kModGPIOOutput);
	modGPIOWrite(&sd->backlight, MODDEF_SSD1963P16_BACKLIGHT_OFF);
#endif
}

void xs_ssd1963p16_begin(xsMachine *the)
{
	spiDisplay sd = xsmcGetHostData(xsThis);
	CommodettoCoordinate x = (CommodettoCoordinate)xsmcToInteger(xsArg(0));
	CommodettoCoordinate y = (CommodettoCoordinate)xsmcToInteger(xsArg(1));
	CommodettoDimension w = (CommodettoDimension)xsmcToInteger(xsArg(2));
	CommodettoDimension h = (CommodettoDimension)xsmcToInteger(xsArg(3));

	ssd1963Begin(sd, x, y, w, h);
}

void xs_ssd1963p16_send(xsMachine *the)
{
	spiDisplay sd = xsmcGetHostData(xsThis);
	int argc = xsmcArgc;
	const uint8_t *data;
	xsUnsignedValue count;

	xsmcGetBufferReadable(xsArg(0), (void **)&data, &count);

	if (argc > 1) {
		xsIntegerValue offset = xsmcToInteger(xsArg(1));

		if ((xsUnsignedValue)offset >= count)
			xsUnknownError("bad offset");
		data += offset;
		count -= offset;
		if (argc > 2) {
			xsIntegerValue c = xsmcToInteger(xsArg(2));
			if (c > count)
				xsUnknownError("bad count");
			count = c;
		}
	}

	ssd1963Send((PocoPixel *)data, count, sd);
}

void xs_ssd1963p16_end(xsMachine *the)
{
	spiDisplay sd = xsmcGetHostData(xsThis);
	ssd1963End(sd);
}

void xs_ssd1963p16_continue(xsMachine *the)
{
	spiDisplay sd = xsmcGetHostData(xsThis);
	ssd1963Continue(sd);
}

void xs_ssd1963p16_pixelsToBytes(xsMachine *the)
{
	int count = xsmcToInteger(xsArg(0));
	xsmcSetInteger(xsResult, ((count * kCommodettoPixelSize) + 7) >> 3);
}

void xs_ssd1963p16_get_pixelFormat(xsMachine *the)
{
	xsmcSetInteger(xsResult, kCommodettoBitmapFormat);
}

void xs_ssd1963p16_get_width(xsMachine *the)
{
	spiDisplay sd = xsmcGetHostData(xsThis);
	xsmcSetInteger(xsResult, (sd->rotation & 1) ? MODDEF_SSD1963P16_HEIGHT : MODDEF_SSD1963P16_WIDTH);
}

void xs_ssd1963p16_get_height(xsMachine *the)
{
	spiDisplay sd = xsmcGetHostData(xsThis);
	xsmcSetInteger(xsResult, (sd->rotation & 1) ? MODDEF_SSD1963P16_WIDTH : MODDEF_SSD1963P16_HEIGHT);
}

void xs_ssd1963p16_get_c_dispatch(xsMachine *the)
{
	xsResult = xsThis;
}

void xs_ssd1963p16_command(xsMachine *the)
{
	spiDisplay sd = xsmcGetHostData(xsThis);
	uint8_t command = (uint8_t)xsmcToInteger(xsArg(0));
	xsUnsignedValue dataSize = 0;
	uint8_t *data = NULL;

	if (xsmcArgc > 1)
		xsmcGetBufferReadable(xsArg(1), (void **)&data, &dataSize);

	ssd1963Command(sd, command, data, (uint16_t)dataSize);
}

void xs_ssd1963p16_get_syncFrames(xsMachine *the)
{
#ifdef MODDEF_SSD1963P16_TEARINGEFFECT_PIN
	spiDisplay sd = xsmcGetHostData(xsThis);
	xsmcSetBoolean(xsResult, sd->syncFrames);
#else
	xsmcSetFalse(xsResult);
#endif
}

void xs_ssd1963p16_set_syncFrames(xsMachine *the)
{
#ifdef MODDEF_SSD1963P16_TEARINGEFFECT_PIN
	spiDisplay sd = xsmcGetHostData(xsThis);
	sd->syncFrames = xsmcToBoolean(xsArg(0));
#endif
}

void xs_ssd1963p16_close(xsMachine *the)
{
	spiDisplay sd = xsmcGetHostData(xsThis);
	if (!sd) return;
	xs_ssd1963p16_destructor(sd);
	xsmcSetHostData(xsThis, NULL);
}

void xs_ssd1963p16_get_rotation(xsMachine *the)
{
	spiDisplay sd = xsmcGetHostData(xsThis);
	xsmcSetInteger(xsResult, sd->rotation * 90);
}

void xs_ssd1963p16_set_rotation(xsMachine *the)
{
	spiDisplay sd = xsmcGetHostData(xsThis);
	int32_t rotation = xsmcToInteger(xsArg(0));
	uint8_t value;
	static const uint8_t masks[] ICACHE_RODATA_ATTR = {0x00, 0x60, 0xc0, 0xa0};
	if ((0 != rotation) && (90 != rotation) && (180 != rotation) && (270 != rotation))
		xsRangeError("invalid rotation");

	sd->rotation = (uint8_t)(rotation / 90);
	value = sd->memoryAccessControl ^ c_read8(masks + sd->rotation);
	ssd1963Command(sd, 0x36, &value, 1);

	// Determine if the progressive send axis is reversed.
	// For odd rotations (MV=1), progressive axis is columns: check MX (bit 6).
	// For even rotations, progressive axis is pages: check MY (bit 7).
	if (sd->rotation & 1)
		sd->progressReverse = (value >> 6) & 1;
	else
		sd->progressReverse = (value >> 7) & 1;
}

void ssd1963Send(PocoPixel *pixels, int byteLength, void *refcon)
{
	spiDisplay sd = refcon;
	uint8_t sync = byteLength > 0;
	if (!sync)
		byteLength = -byteLength;

	sd->nothingSent = 0;

#ifdef MODDEF_SSD1963P16_TEARINGEFFECT_PIN
	if (sd->firstBuffer && sd->syncFrames) {
		sd->firstBuffer = 0;
		sd->waiting = 1;
		xSemaphoreTake(sd->startSend, portMAX_DELAY);
	}
#endif
	{
		int one = 1;
		xQueueSend(sd->ops, &one, portMAX_DELAY);
		esp_lcd_panel_io_tx_color(sd->io_handle, 0x2C, pixels, byteLength);

		int lines = (byteLength >> 1) / sd->updateWidth;
		if (sd->progressReverse)
			sd->yMax -= lines;
		else
			sd->yMin += lines;
		sd->updateLinesRemaining -= lines;

		if (sd->updateLinesRemaining) {
			uint8_t data[4];
			data[0] = sd->yMin >> 8;
			data[1] = sd->yMin & 0xff;
			data[2] = sd->yMax >> 8;
			data[3] = sd->yMax & 0xff;
			ssd1963Command(sd, (sd->rotation & 1) ? 0x2A : 0x2B, data, 4);
		}
	}

	if (sync) {
		xSemaphoreTake(sd->colorsInFlight, portMAX_DELAY);
		xSemaphoreTake(sd->colorsInFlight, portMAX_DELAY);
		xSemaphoreGive(sd->colorsInFlight);
		xSemaphoreGive(sd->colorsInFlight);
	}
	else if (sd->updateLinesRemaining)
		xSemaphoreTake(sd->colorsInFlight, portMAX_DELAY);
}

// delay of 0 is end of commands
#define kDelayMS (255)

static const uint8_t gInit[] ICACHE_RODATA_ATTR = {
	// ---- Software reset (clean slate for warm reboots) ----
	0x01, 0,									// Software reset
	kDelayMS, 100,

	// ---- PLL configuration ----
	0xE2, 3, 0x23, 0x02, 0x54,					// Set PLL MN: M=35, N=2, dummy
	0xE0, 1, 0x01,								// Start PLL
	kDelayMS, 1,
	0xE0, 1, 0x03,								// Lock PLL
	kDelayMS, 1,
	0x01, 0,									// Software reset
	kDelayMS, 100,

	// ---- Pixel clock ----
	0xE6, 3, 0x03, 0x33, 0x33,					// Set pixel clock frequency

	// ---- LCD mode ----
	0xB0, 7,
		0x20,									// TFT panel data width 24-bit, FRC & dithering disable, LSHIFT falling edge, LLINE active low, LFRAME active low
		0x00,									// TFT mode
		(MODDEF_SSD1963P16_WIDTH - 1) >> 8,
		(MODDEF_SSD1963P16_WIDTH - 1) & 0xFF,
		(MODDEF_SSD1963P16_HEIGHT - 1) >> 8,
		(MODDEF_SSD1963P16_HEIGHT - 1) & 0xFF,
		0x00,									// RGB sequence

	// ---- Horizontal timing (defaults for 480x272) ----
	0xB4, 8,
		0x04, 0x1F,								// HT: horizontal total period = 1055
		0x00, 0xD2,								// HPS: horizontal sync pulse start = 210
		0x00,									// HPW: horizontal sync pulse width - 1 = 0
		0x00, 0x00,								// LPS: horizontal display period start = 0
		0x00,									// LPSPP: horizontal sync pulse subpixel start = 0

	// ---- Vertical timing (defaults for 480x272) ----
	0xB6, 7,
		0x02, 0x0C,								// VT: vertical total period = 524
		0x00, 0x22,								// VPS: vertical sync pulse start = 34
		0x00,									// VPW: vertical sync pulse width - 1 = 0
		0x00, 0x00,								// FPS: vertical display period start = 0

	// ---- GPIO ----
	0xBA, 1, 0x01,								// Set GPIO value
	kDelayMS, 1,
	0xB8, 2, 0x0F, 0x01,						// Set GPIO config

	// ---- Address mode ----
	0x36, 1,
		(MODDEF_SSD1963P16_FLIPY ? 0x80 : 0) | (MODDEF_SSD1963P16_FLIPX ? 0x40 : 0),

	// ---- Pixel data interface: 16-bit RGB565 ----
	0xF0, 1, 0x03,

	// ---- Post processing ----
	0xBC, 4, 0x40, 0x80, 0x40, 0x01,
	kDelayMS, 1,

	// ---- PWM / backlight control ----
	0xBE, 6, 0x06, 0xFF, 0x01, 0xF0, 0x00, 0x00,

	// ---- Dynamic backlight ----
	0xD0, 1, 0x0D,

	// ---- Tearing effect line on ----
	0x35, 1, 0x00,

	// ---- Column address set ----
	0x2A, 4,
		0x00, 0x00,
		(MODDEF_SSD1963P16_WIDTH - 1) >> 8, (MODDEF_SSD1963P16_WIDTH - 1) & 0xFF,

	// ---- Row address set ----
	0x2B, 4,
		0x00, 0x00,
		(MODDEF_SSD1963P16_HEIGHT - 1) >> 8, (MODDEF_SSD1963P16_HEIGHT - 1) & 0xFF,

	kDelayMS, 80,
	kDelayMS, 0
};

void ssd1963Init(spiDisplay sd)
{
	const uint8_t *cmds;

#ifdef MODDEF_SSD1963P16_RST_PIN
	// Three-phase reset: ensure a clean falling edge regardless of
	// the pin's prior state (e.g. warm reboot without power cycle).
	modGPIOWrite(&sd->rst, 1);
	modDelayMilliseconds(200);
	modGPIOWrite(&sd->rst, 0);
	modDelayMilliseconds(200);
	modGPIOWrite(&sd->rst, 1);
	modDelayMilliseconds(200);
#endif

	cmds = gInit;
	while (true) {
		uint8_t cmd = c_read8(cmds++);
		if (kDelayMS == cmd) {
			uint8_t ms = c_read8(cmds++);
			if (0 == ms)
				break;
			modDelayMilliseconds(ms);
		}
		else {
			if (0x36 == cmd)
				sd->memoryAccessControl = c_read8(cmds + 1);
			uint8_t count = c_read8(cmds++);
			ssd1963Command(sd, cmd, cmds, count);
			cmds += count;
		}
	}

	sd->firstFrame = true;
}

void ssd1963Begin(void *refcon, CommodettoCoordinate x, CommodettoCoordinate y, CommodettoDimension w, CommodettoDimension h)
{
	spiDisplay sd = refcon;
	uint16_t xMin, xMax, yMin, yMax;
	if (sd->nothingSent)
		xSemaphoreGive(sd->colorsInFlight);
	sd->nothingSent = 1;

	xMin = x + MODDEF_SSD1963P16_COLUMN_OFFSET;
	yMin = y + MODDEF_SSD1963P16_ROW_OFFSET;

	xMax = xMin + w - 1;
	yMax = yMin + h - 1;

	// When the progressive axis direction is reversed (MX=1 for odd rotations,
	// MY=1 for even), remap y coordinates so partial updates target the correct
	// physical region. Full-screen updates are unaffected since the range spans
	// the entire axis regardless of direction.
	if (sd->progressReverse) {
		uint16_t physDim = (sd->rotation & 1) ? MODDEF_SSD1963P16_WIDTH : MODDEF_SSD1963P16_HEIGHT;
		uint16_t newYMin = physDim - 1 - yMax;
		yMax = physDim - 1 - yMin;
		yMin = newYMin;
	}

	sd->updateWidth = w;
	sd->updateLinesRemaining = h;
	sd->yMin = yMin;
	sd->yMax = yMax;
#ifdef MODDEF_SSD1963P16_TEARINGEFFECT_PIN
	sd->firstBuffer = !sd->firstFrame && !sd->isContinue;
#endif

	// Use synchronous tx_param for address commands: on a 16-bit bus,
	// tx_color packs bytes into 16-bit words (2 WR cycles for 4 bytes),
	// but the SSD1963 expects one parameter byte per WR cycle.
	//
	// When MV is set (odd rotations), the SSD1963 changes the fill direction
	// but does NOT remap the address space. We must swap the column/page
	// commands so that Poco's x maps to physical pages and y to physical columns.
	uint8_t colCmd = 0x2A, pageCmd = 0x2B;
	if (sd->rotation & 1) {
		colCmd = 0x2B;
		pageCmd = 0x2A;
	}

	uint8_t data[4];
	data[0] = xMin >> 8;
	data[1] = xMin & 0xff;
	data[2] = xMax >> 8;
	data[3] = xMax & 0xff;
	ssd1963Command(sd, colCmd, data, 4);

	data[0] = yMin >> 8;
	data[1] = yMin & 0xff;
	data[2] = yMax >> 8;
	data[3] = yMax & 0xff;
	ssd1963Command(sd, pageCmd, data, 4);

	xSemaphoreTake(sd->colorsInFlight, portMAX_DELAY);
}

void ssd1963Continue(void *refcon)
{
	spiDisplay sd = refcon;
#ifdef MODDEF_SSD1963P16_TEARINGEFFECT_PIN
	sd->isContinue = true;
#endif
}

void ssd1963End(void *refcon)
{
	spiDisplay sd = refcon;

#ifdef MODDEF_SSD1963P16_TEARINGEFFECT_PIN
	sd->isContinue = false;
#endif
	if (sd->firstFrame) {
		sd->firstFrame = false;

		ssd1963CommandAsync(sd, 0x11, NULL, 0);		// Sleep out
		ssd1963CommandAsync(sd, 0x29, NULL, 0);		// Display on

#ifdef MODDEF_SSD1963P16_BACKLIGHT_PIN
		modGPIOWrite(&sd->backlight, MODDEF_SSD1963P16_BACKLIGHT_ON);
#endif
	}

	if (sd->nothingSent) {
		xSemaphoreGive(sd->colorsInFlight);
		sd->nothingSent = 0;
	}
}

#ifdef MODDEF_SSD1963P16_TEARINGEFFECT_PIN

void tearingEffectISR(void *refcon)
{
	spiDisplay sd = refcon;

	if (sd->waiting) {
		BaseType_t high_task_woken = pdFALSE;
		sd->waiting = 0;
		xSemaphoreGiveFromISR(sd->startSend, &high_task_woken);
		portYIELD_FROM_ISR(high_task_woken);
	}
}

#endif
