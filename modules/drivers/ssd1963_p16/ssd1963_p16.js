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
	SSD1963 - 16-bit parallel
*/

export default class SSD1963 @ "xs_ssd1963p16_destructor" {
	constructor(options) @ "xs_ssd1963p16";

	begin(x, y, width, height) @ "xs_ssd1963p16_begin";
	send(pixels, offset, count) @ "xs_ssd1963p16_send";
	end() @ "xs_ssd1963p16_end";

	adaptInvalid() {}
	continue() @ "xs_ssd1963p16_continue";

	pixelsToBytes(count) @ "xs_ssd1963p16_pixelsToBytes";

	get pixelFormat() @ "xs_ssd1963p16_get_pixelFormat";
	get width() @ "xs_ssd1963p16_get_width";
	get height() @ "xs_ssd1963p16_get_height";
	get async() {return true;}

	get c_dispatch() @ "xs_ssd1963p16_get_c_dispatch";

	// driver specific
	command(id, data) @ "xs_ssd1963p16_command";
	set syncFrames(value) @ "xs_ssd1963p16_set_syncFrames";
	get syncFrames() @ "xs_ssd1963p16_get_syncFrames";
	set rotation(value) @ "xs_ssd1963p16_set_rotation";
	get rotation() @ "xs_ssd1963p16_get_rotation";

	close() @ "xs_ssd1963p16_close";

	pixels(value = 0) {
		const pixels = this.width << 5;		// 32 scan lines
		return (value > pixels) ? value : pixels;
	}
}
