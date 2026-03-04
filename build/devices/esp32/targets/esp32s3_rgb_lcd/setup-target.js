/*
 * Setup target for ESP32-S3 RGB LCD board
 *
 * Handles board-specific initialization:
 *   - I2C backlight controller at address 0x30
 *     (0 = max brightness, 245 = off)
 */

import config from "mc/config";

class Backlight {
	#device;

	constructor(brightness = 100) {
		this.#device = new device.io.SMBus({
			data: config.i2c?.sda ?? 15,
			clock: config.i2c?.scl ?? 16,
			hz: 100_000,
			address: 0x30
		});
		this.write(brightness);
	}
	write(value) {
		// Map 0-100 brightness to 245-0 register value (inverted)
		value = Math.max(0, Math.min(100, value));
		const register = Math.round(245 - (value / 100) * 245);
		this.#device.writeByte(register);
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
