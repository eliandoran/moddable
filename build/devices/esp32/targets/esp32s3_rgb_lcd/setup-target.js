/*
 * Setup target for ESP32-S3 RGB LCD board
 *
 * Handles board-specific initialization:
 *   - I2C backlight controller at address 0x30
 *     (0 = max brightness, 245 = off)
 */

import config from "mc/config";
import SMBus from "embedded:io/smbus";

const I2C_SDA = config.i2c?.sda ?? 15;
const I2C_SCL = config.i2c?.scl ?? 16;

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
	done?.();
}
