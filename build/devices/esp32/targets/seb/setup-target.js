import Digital from "pins/digital";

export default function (done) {
    // Power on the display (active low)
    globalThis.displayPower = new Digital({
        pin: 3,
        mode: Digital.Output
    });
    globalThis.displayPower.write(0);
    trace("Display powered on\n");

    // Turn on backlight (active high)
    globalThis.backlight = new Digital({
        pin: 46,
        mode: Digital.Output
    });
    globalThis.backlight.write(1);
    trace("Backlight on\n");

    done?.();
}