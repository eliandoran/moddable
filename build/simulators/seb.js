import {
	Screen,
} from "piu/Screen";

import {
	ControlsColumn,
	ButtonsRow,
	PopupRow,
	SliderRow,
	StatusRow,
	SwitchRow,
	TimerRow,
} from "ControlsPane";

import {
	DeviceBehavior,
	DeviceContainer,
	DeviceScreen,
	DeviceWorker,
} from "DevicePane";

const base = import.meta.uri;
const ledSkin = { texture:{ base, path:"moddable_two/led.png" }, x:0, y:0, width:160, height:160 };

class MockupBehavior extends DeviceBehavior {
	onAbort(container, status) {
		if (this.reloadOnAbort) {
			application.defer("doReloadFile");
			return true;
		}
	}
	onCreate(container, device) {
		super.onCreate(container, device);
		this.reloadOnAbort = false;
	}
	onDefaultButtonDown(container) {
		this.postJSON(container, { button:1 });
	}
	onDefaultButtonUp(container) {
		this.postJSON(container, { button:0 });
	}
	onKeyDown(container, key) {
		const code = key.charCodeAt(0);
		if (code == 127)
			this.onDefaultButtonDown(container);
	}
	onKeyUp(container, key) {
		const code = key.charCodeAt(0);
		if (code == 127)
			this.onDefaultButtonUp(container);
	}
	onJSON(container, json) {
		if ("led" in json)
			container.distribute("onLEDChanged", json.led);
		else if ("xsbug" in json) {
			if (json.xsbug == "abort")
				application.defer("doReloadFile");
		}
	}
	onReloadOnAbortChanged(container, data) {
		this.reloadOnAbort = data.value;
	}
}

class LEDBehavior extends Behavior {
	onLEDChanged(content, state) {
		content.visible = state;
	}
}

export default {
	applicationName: "moddable_two/debug/.*",
	sortingTitle:"0000",
	title:"SEB Custom",
	Workers: {
	},
	ControlsTemplate:ControlsColumn.template($ => ({
		contents:[
			ButtonsRow({ 
				label: "Button",
				buttons: [
					{ eventDown:"onDefaultButtonDown", eventUp:"onDefaultButtonUp", label:"Default" },
				],
			}),
			SwitchRow({ 
				event: "onReloadOnAbortChanged",
				label: "Reload On Abort",
				on: "Yes",
				off: "No",
				value: false
			}),
		]
	})),
	DeviceTemplates: {
		0: DeviceContainer.template($ => ({ 
			Behavior: MockupBehavior, 
			contents:[
				Content($, { Behavior: LEDBehavior, left:0, top:0, skin:ledSkin, visible:false }),
				Content($, { skin: { texture:{ base, path:"moddable_two/0.png" }, x:0, y:0, width:310, height:500 } }),
				DeviceScreen($, { left:34, width:272, top:87, height:480, rotation:0 }),
			],
		})),
	}
};
