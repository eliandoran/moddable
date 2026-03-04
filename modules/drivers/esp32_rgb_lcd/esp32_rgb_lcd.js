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
	16-bit parallel bus, double-buffered framebuffer in PSRAM
*/

export default class RGBLCD @ "xs_rgblcd_destructor" {
	constructor(options) @ "xs_rgblcd";

	begin(x, y, width, height) @ "xs_rgblcd_begin";
	send(pixels, offset, count) @ "xs_rgblcd_send";
	end() @ "xs_rgblcd_end";

	adaptInvalid() {}
	continue() @ "xs_rgblcd_continue";

	pixelsToBytes(count) @ "xs_rgblcd_pixelsToBytes";

	get pixelFormat() @ "xs_rgblcd_get_pixelFormat";
	get width() @ "xs_rgblcd_get_width";
	get height() @ "xs_rgblcd_get_height";
	get async() { return false; }

	get c_dispatch() @ "xs_rgblcd_get_c_dispatch";
	get frameBuffer() @ "xs_rgblcd_get_frameBuffer";

	close() @ "xs_rgblcd_close";
}

Object.freeze(RGBLCD.prototype);
