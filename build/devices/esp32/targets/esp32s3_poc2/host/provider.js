/*
 * Copyright (c) 2022  Moddable Tech, Inc.
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

import Analog from "embedded:io/analog";
import Digital from "embedded:io/digital";
import DigitalBank from "embedded:io/digitalbank";
import I2C from "embedded:io/i2c";
import PulseCount from "embedded:io/pulsecount";
import PWM from "embedded:io/pwm";
import Serial from "embedded:io/serial";
import SMBus from "embedded:io/smbus";
import SPI from "embedded:io/spi";
import Timer from "timer";

const notes = new Map;
notes.set("C", 65406);
notes.set("C#", 69296);
notes.set("D", 73416);
notes.set("Db", 69296);
notes.set("D#", 77782);
notes.set("E", 82406);
notes.set("Eb", 77782);
notes.set("F", 87310);
notes.set("F#", 92498);
notes.set("G", 97998);
notes.set("Gb", 92498);
notes.set("G#", 103826);
notes.set("A", 110000);
notes.set("Ab", 103826);
notes.set("A#", 116540);
notes.set("B", 123470);
notes.set("Bb", 116540);

class Tone {
	#io;
	#timer;

	constructor() {
		this.#io = new PWM({pin: device.pin.buzzer});
	}
	close() {
		this.#io?.close();
		if (this.#timer)
			Timer.clear(this.#timer);
		this.#io = this.#timer = undefined;
	}
	tone(hz, duration) {
		const io = this.#io = new PWM({from: this.#io, hz});
		io.write(512);

		if (duration) {
			if (this.#timer)
				Timer.schedule(this.#timer, duration);
			else
				this.#timer = Timer.set(() => {
					this.#timer = undefined;
					this.mute();
				}, duration);
		}
		else if (this.#timer) {
			Timer.clear(this.#timer);
			this.#timer = undefined;
		}
	}
	note(note, octave = 4, duration) {
		note = notes.get(note);
		if (!note || (octave > 8))
			throw new Error;
		this.tone(Math.idiv(note, (1 << (8 - octave))), duration);
	}
	mute() {
		this.#io.write(0);
	}
}

const device = {
	I2C: {
		default: {
			io: I2C,
			data: 4,
			clock: 5
		}
	},
	Serial: {
		default: {
			io: Serial,
			port: 0,
			receive: 44,
			transmit: 43
		}
	},
	SPI: {
		default: {
			io: SPI,
			clock: 12,
			in: 13,
			out: 11,
			port: 2
		}
	},
	Analog: {
		default: {
			io: Analog,
			pin: 1
		}
	},
	io: {Analog, Digital, DigitalBank, I2C, PulseCount, PWM, Serial, SMBus, SPI},
	peripheral: {
		tone: {
			Default: Tone
		}
	},
	pin: {
		button: 0,
		buzzer: 6
	}
};

export default device;
