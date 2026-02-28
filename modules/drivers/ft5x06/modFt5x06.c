/*
 * Copyright (c) 2016-2021 Moddable Tech, Inc.
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
	FT5x06 I2C touch driver

	Ported from the Espressif esp_lcd_touch_ft5x06 driver.
	Modeled on the Moddable FT6206 driver.
*/

#include "xsPlatform.h"
#include "xsmc.h"
#include "mc.xs.h"			// for xsID_ values
#include "mc.defines.h"
#include "modI2C.h"

#include "modPreference.h"

#ifndef MODDEF_FT5X06_HZ
	#define MODDEF_FT5X06_HZ 600000
#endif
#ifndef MODDEF_FT5X06_WIDTH
	#define MODDEF_FT5X06_WIDTH (240)
#endif
#ifndef MODDEF_FT5X06_HEIGHT
	#define MODDEF_FT5X06_HEIGHT (320)
#endif
#ifndef MODDEF_FT5X06_FLIPX
	#define MODDEF_FT5X06_FLIPX (false)
#endif
#ifndef MODDEF_FT5X06_FLIPY
	#define MODDEF_FT5X06_FLIPY (false)
#endif
#ifndef MODDEF_FT5X06_ADDR
	#define MODDEF_FT5X06_ADDR 0x38
#endif
#ifndef MODDEF_FT5X06_THRESHOLD
	#define MODDEF_FT5X06_THRESHOLD 70
#endif
#ifndef MODDEF_FT5X06_SDA
	#define MODDEF_FT5X06_SDA -1
#endif
#ifndef MODDEF_FT5X06_SCL
	#define MODDEF_FT5X06_SCL -1
#endif
#ifndef MODDEF_FT5X06_DX
	#define MODDEF_FT5X06_DX 0
#endif
#ifndef MODDEF_FT5X06_DY
	#define MODDEF_FT5X06_DY 0
#endif
#ifndef MODDEF_FT5X06_FITX
	#define MODDEF_FT5X06_FITX 1
#endif
#ifndef MODDEF_FT5X06_FITY
	#define MODDEF_FT5X06_FITY 1
#endif
#ifndef MODDEF_FT5X06_RAW
	#define MODDEF_FT5X06_RAW (false)
#endif
#if MODDEF_FT5X06_RAW || !defined(MODDEF_FT5X06_RAW_LEFT) || !defined(MODDEF_FT5X06_RAW_RIGHT) || !defined(MODDEF_FT5X06_RAW_TOP) || !defined(MODDEF_FT5X06_RAW_BOTTOM)
	#define MODDEF_FT5X06_CALIBRATE (false)
#else
	#define MODDEF_FT5X06_CALIBRATE (true)
#endif

/* FT5x06 register map */
#define FT5x06_REG_DEVICE_MODE		0x00
#define FT5x06_REG_GESTURE_ID		0x01
#define FT5x06_REG_NUMTOUCHES		0x02
#define FT5x06_REG_TOUCH1_XH		0x03

#define FT5x06_REG_THGROUP			0x80
#define FT5x06_REG_THPEAK			0x81
#define FT5x06_REG_THCAL			0x82
#define FT5x06_REG_THWATER			0x83
#define FT5x06_REG_THTEMP			0x84
#define FT5x06_REG_THDIFF			0x85
#define FT5x06_REG_CTRL			0x86
#define FT5x06_REG_TIME_ENTER_MON	0x87
#define FT5x06_REG_PERIODACTIVE		0x88
#define FT5x06_REG_PERIODMONITOR	0x89
#define FT5x06_REG_CIPHER			0xA3
#define FT5x06_REG_VENDID			0xA8

#define FT5x06_MAX_TOUCHES			5

struct ft5x06Record {
	modI2CConfigurationRecord i2c;

#if MODDEF_FT5X06_CALIBRATE
	int16_t			min_x;
	int16_t			max_x;
	int16_t			min_y;
	int16_t			max_y;
#endif
};
typedef struct ft5x06Record ft5x06Record;
typedef ft5x06Record *ft5x06;

static uint8_t ft5x06WriteRegister(ft5x06 ft, uint8_t reg, uint8_t value)
{
	uint8_t data[2];
	data[0] = reg;
	data[1] = value;
	return modI2CWrite(&ft->i2c, data, 2, true);
}

void xs_FT5x06_destructor(void *data)
{
	if (data) {
		ft5x06 ft = data;
		modI2CUninit(&ft->i2c);
		c_free(data);
	}
}

void xs_FT5x06(xsMachine *the)
{
	ft5x06 ft;
	uint8_t data[2];
	uint8_t err;

	ft = c_calloc(1, sizeof(ft5x06Record));
	if (!ft) xsUnknownError("out of memory");
	xsmcSetHostData(xsThis, ft);

	xsmcVars(1);

	modI2CConfig(ft->i2c, MODDEF_FT5X06_HZ, MODDEF_FT5X06_SDA, MODDEF_FT5X06_SCL, MODDEF_FT5X06_ADDR, 250);
	modI2CInit(&ft->i2c);

	data[0] = FT5x06_REG_VENDID;
	err = modI2CWrite(&ft->i2c, data, 1, false);
	if (err) xsUnknownError("write FT5x06_REG_VENDID failed");
	err = modI2CRead(&ft->i2c, data, 1, true);
	if (err) xsUnknownError("read FT5x06_REG_VENDID failed");

	data[0] = FT5x06_REG_CIPHER;
	err = modI2CWrite(&ft->i2c, data, 1, false);
	if (err) xsUnknownError("write FT5x06_REG_CIPHER failed");
	err = modI2CRead(&ft->i2c, data, 1, true);
	if (err) xsUnknownError("read FT5x06_REG_CIPHER failed");

	/* Initialize touch thresholds and timing (from Espressif FT5x06 driver) */
	ft5x06WriteRegister(ft, FT5x06_REG_THGROUP, MODDEF_FT5X06_THRESHOLD);
	ft5x06WriteRegister(ft, FT5x06_REG_THPEAK, 60);
	ft5x06WriteRegister(ft, FT5x06_REG_THCAL, 16);
	ft5x06WriteRegister(ft, FT5x06_REG_THWATER, 60);
	ft5x06WriteRegister(ft, FT5x06_REG_THTEMP, 10);
	ft5x06WriteRegister(ft, FT5x06_REG_THDIFF, 20);
	ft5x06WriteRegister(ft, FT5x06_REG_TIME_ENTER_MON, 2);
	ft5x06WriteRegister(ft, FT5x06_REG_PERIODACTIVE, 12);
	ft5x06WriteRegister(ft, FT5x06_REG_PERIODMONITOR, 40);

	/* Switch to monitor mode when no touch */
	ft5x06WriteRegister(ft, FT5x06_REG_CTRL, 1);

#if MODDEF_FT5X06_CALIBRATE
	ft->min_x = MODDEF_FT5X06_RAW_LEFT;
	ft->max_x = MODDEF_FT5X06_RAW_RIGHT;
	ft->min_y = MODDEF_FT5X06_RAW_TOP;
	ft->max_y = MODDEF_FT5X06_RAW_BOTTOM;

	uint8_t prefType;
	int16_t values[4];
	uint16_t byteCountOut;
	if (modPreferenceGet("ft5x06", "calibrate", &prefType, (uint8_t *)values, sizeof(values), &byteCountOut)) {
		ft->min_x = values[0];
		ft->max_x = values[1];
		ft->min_y = values[2];
		ft->max_y = values[3];
	}
#endif
}

void xs_FT5x06_read(xsMachine *the)
{
	ft5x06 ft = xsmcGetHostData(xsThis);
	uint8_t data[32], err;
	uint8_t count, maxID = 0, i;

	xsmcVars(2);

	data[0] = FT5x06_REG_NUMTOUCHES;
	err = modI2CWrite(&ft->i2c, data, 1, false);
	if (err) xsUnknownError("write FT5x06_REG_NUMTOUCHES");
	modI2CRead(&ft->i2c, data, 1, true);
	count = data[0] & 0x0F;

	xsmcGet(xsVar(0), xsArg(0), xsID_length);
	maxID = xsmcToInteger(xsVar(0)) - 1;

	xsmcSetInteger(xsVar(0), 0);
	for (i = 0; i <= maxID; i++) {
		xsmcGetIndex(xsVar(1), xsArg(0), i);
		xsmcSet(xsVar(1), xsID_state, xsVar(0));
	}

	if (count == 0 || count > FT5x06_MAX_TOUCHES)
		return;

	data[0] = FT5x06_REG_TOUCH1_XH;	// touch data registers start at 0x03
	modI2CWrite(&ft->i2c, data, 1, false);
	modI2CRead(&ft->i2c, data, count * 6, true);

	for (i = 0; i < count; i++) {
		uint8_t id = data[(i * 6) + 2] >> 4;
		uint8_t event = data[(i * 6) + 0] >> 6, state = 0;
		int16_t x = ((data[(i * 6) + 0] & 0x0F) << 8) | data[(i * 6) + 1];
		int16_t y = ((data[(i * 6) + 2] & 0x0F) << 8) | data[(i * 6) + 3];

		if (id > maxID)
			continue;

		if (0 == event)			// down
			state = 1;
		else if (2 == event)	// contact
			state = 2;
		else if (1 == event)	// lift
			state = 3;
		else
			continue;

		// reflect
		if (MODDEF_FT5X06_FLIPX)
			x = MODDEF_FT5X06_WIDTH - x;

		if (MODDEF_FT5X06_FLIPY)
			y = MODDEF_FT5X06_HEIGHT - y;

		// scale
		if (MODDEF_FT5X06_FITX && (240 != MODDEF_FT5X06_WIDTH))
			x = (x * MODDEF_FT5X06_WIDTH) / 240;
		if (MODDEF_FT5X06_FITY && (320 != MODDEF_FT5X06_HEIGHT))
			y = (y * MODDEF_FT5X06_HEIGHT) / 320;

#if MODDEF_FT5X06_DX
		x += MODDEF_FT5X06_DX;
		if (x > (MODDEF_FT5X06_WIDTH - 1))
			x = (MODDEF_FT5X06_WIDTH - 1);
		else if (x < 0)
			x = 0;
#endif

#if MODDEF_FT5X06_DY
		y += MODDEF_FT5X06_DY;
		if (y > (MODDEF_FT5X06_HEIGHT - 1))
			y = (MODDEF_FT5X06_HEIGHT - 1);
		else if (y < 0)
			y = 0;
#endif

#if MODDEF_FT5X06_CALIBRATE
		x = (x - ft->min_x) * ((float)(MODDEF_FT5X06_WIDTH - 1)) / (ft->max_x - ft->min_x);
		y = (y - ft->min_y) * ((float)(MODDEF_FT5X06_HEIGHT - 1)) / (ft->max_y - ft->min_y);

		if (x < 0)
			x = 0;
		else if (x > (MODDEF_FT5X06_WIDTH - 1))
			x = MODDEF_FT5X06_WIDTH - 1;

		if (y < 0)
			y = 0;
		else if (y > (MODDEF_FT5X06_HEIGHT - 1))
			y = MODDEF_FT5X06_HEIGHT - 1;
#endif

		// result
		xsmcGetIndex(xsVar(0), xsArg(0), id);
		xsmcSetInteger(xsVar(1), x);
		xsmcSet(xsVar(0), xsID_x, xsVar(1));
		xsmcSetInteger(xsVar(1), y);
		xsmcSet(xsVar(0), xsID_y, xsVar(1));
		xsmcSetInteger(xsVar(1), state);
		xsmcSet(xsVar(0), xsID_state, xsVar(1));
	}
}
