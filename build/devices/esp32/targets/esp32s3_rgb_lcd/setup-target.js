/*
 * Setup target for ESP32-S3 RGB LCD board
 *
 * Handles board-specific initialization:
 *   - I2C backlight controller at address 0x30
 *     (0 = max brightness, 245 = off)
 */

import config from "mc/config";
import Digital from "embedded:io/digital";
import SMBus from "embedded:io/smbus";
import Timer from "timer";

const I2C_SDA = config.i2c?.sda ?? 15;
const I2C_SCL = config.i2c?.scl ?? 16;
const TOUCH_RST = 1;

function resetTouchController() {
	// Toggle GPIO 1 (TOUCH_RST) to reset the GT911 into a known state
	// and select I2C address 0x5D (INT pin left floating/low during reset)
	const rst = new Digital({
		pin: TOUCH_RST,
		mode: Digital.Output,
	});
	rst.write(0);		// Pull reset LOW
	Timer.delay(120);
	rst.close();

	// Release reset by switching to input (float high via pull-up)
	const rstIn = new Digital({
		pin: TOUCH_RST,
		mode: Digital.Input,
	});
	Timer.delay(100);
	rstIn.close();
}

class Backlight {
	#device;

	constructor(brightness = 100) {
		// Open SMBus to backlight/IO controller at 0x30
		this.#device = new SMBus({
			data: I2C_SDA,
			clock: I2C_SCL,
			hz: 100_000,
			address: 0x30
		});

		// Activate the controller (same as sample: sendI2CCommand(250))
		try {
			this.#device.writeByte(250);
		} catch (e) {
			trace(`Backlight activate failed: ${e}\n`);
		}

		// Set initial brightness
		this.write(brightness);
	}
	write(value) {
		// Map 0-100 brightness to 245-0 register value (inverted)
		value = Math.max(0, Math.min(100, value));
		const regVal = Math.round(245 - (value / 100) * 245);
		this.#device.writeByte(regVal);
	}
	close() {
		this.#device?.close();
		this.#device = undefined;
	}
}

globalThis.Host = Object.freeze({
	Backlight
}, true);

export default function (done) {
	// Reset the GT911 touch controller via GPIO 1 before it is instantiated.
	// This mirrors the reset sequence in sample.c and ensures the chip is
	// responsive on I2C address 0x5D when the driver later reads its ID.
	resetTouchController();

	done?.();
}
