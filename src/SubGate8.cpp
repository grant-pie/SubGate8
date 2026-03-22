#include "plugin.hpp"

struct SubGate8 : Module {
	enum ParamId {
		// 8 steps × 4 subdivisions = 32 toggle buttons
		ENUMS(SUB_TOGGLE_PARAMS, 32),
		GATE_LENGTH_PARAM,
		SWING_PARAM,
		STEP_COUNT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		RUN_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		GATE_OUTPUT,
		EOC_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(STEP_LIGHTS, 8),
		ENUMS(SUB_TOGGLE_LIGHTS, 32),
		RUNNING_LIGHT,
		LIGHTS_LEN
	};

	// Sequencer state
	int currentStep = 0;
	int currentSubdivision = 0;
	float subdivisionPhase = 0.f;
	bool running = true;

	// Trigger detection
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger runTrigger;

	// Gate/trigger generators
	dsp::PulseGenerator gatePulse;
	dsp::PulseGenerator eocPulse;

	// Timing
	float clockPeriod = 0.5f;  // Estimated clock period for subdivision timing
	float timeSinceLastClock = 0.f;
	bool firstClockReceived = false;

	// Swing timing
	float swingAccumulator = 0.f;

	SubGate8() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		// Configure subdivision toggle buttons
		for (int step = 0; step < 8; step++) {
			for (int sub = 0; sub < 4; sub++) {
				int idx = step * 4 + sub;
				configSwitch(SUB_TOGGLE_PARAMS + idx, 0.f, 1.f, 0.f,
					string::f("Step %d Sub %d", step + 1, sub + 1),
					{"Off", "On"});
			}
		}

		configParam(GATE_LENGTH_PARAM, 0.01f, 1.f, 0.5f, "Gate Length", "%", 0.f, 100.f);
		configParam(SWING_PARAM, 0.f, 0.75f, 0.f, "Swing", "%", 0.f, 100.f);
		configParam(STEP_COUNT_PARAM, 1.f, 8.f, 8.f, "Steps");
		getParamQuantity(STEP_COUNT_PARAM)->snapEnabled = true;

		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configInput(RUN_INPUT, "Run/Stop");

		configOutput(GATE_OUTPUT, "Gate");
		configOutput(EOC_OUTPUT, "End of Cycle");
	}

	void onReset() override {
		currentStep = 0;
		currentSubdivision = 0;
		subdivisionPhase = 0.f;
		running = true;
		firstClockReceived = false;
		timeSinceLastClock = 0.f;
		clockPeriod = 0.5f;
	}

	void process(const ProcessArgs& args) override {
		// Handle Run/Stop input (gate mode - high = run, low = stop)
		if (inputs[RUN_INPUT].isConnected()) {
			running = inputs[RUN_INPUT].getVoltage() >= 1.f;
		}

		// Handle Reset
		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			currentStep = 0;
			currentSubdivision = 0;
			subdivisionPhase = 0.f;
		}

		// Track time for clock period estimation
		timeSinceLastClock += args.sampleTime;

		// Handle Clock
		if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
			if (running) {
				// Estimate clock period from previous clock
				if (firstClockReceived && timeSinceLastClock > 0.001f) {
					// Smooth the clock period estimate
					clockPeriod = clockPeriod * 0.7f + timeSinceLastClock * 0.3f;
				}
				firstClockReceived = true;
				timeSinceLastClock = 0.f;

				// Advance to next step
				currentStep++;
				int stepCount = (int)params[STEP_COUNT_PARAM].getValue();
				if (currentStep >= stepCount) {
					currentStep = 0;
					// Trigger end-of-cycle
					eocPulse.trigger(1e-3f);
				}

				// Reset subdivision state for new step
				currentSubdivision = 0;
				subdivisionPhase = 0.f;
				swingAccumulator = 0.f;

				// Fire gate for first subdivision if enabled
				triggerSubdivisionIfEnabled(0);
			}
		}

		// Process subdivisions within the step
		if (running && firstClockReceived && clockPeriod > 0.f) {
			processSubdivisions(args);
		}

		// Process gate output
		float gateVoltage = gatePulse.process(args.sampleTime) ? 10.f : 0.f;
		outputs[GATE_OUTPUT].setVoltage(gateVoltage);

		// Process EOC output
		float eocVoltage = eocPulse.process(args.sampleTime) ? 10.f : 0.f;
		outputs[EOC_OUTPUT].setVoltage(eocVoltage);

		// Update lights
		updateLights(args);
	}

	void processSubdivisions(const ProcessArgs& args) {
		float swing = params[SWING_PARAM].getValue();

		// Calculate subdivision timing with swing
		// Swing affects every other subdivision - odd subdivisions are delayed
		float subdivisionDuration = clockPeriod / 4.f;

		subdivisionPhase += args.sampleTime;

		// Check if we should trigger the next subdivision
		for (int sub = currentSubdivision + 1; sub < 4; sub++) {
			float targetTime = getSubdivisionTime(sub, subdivisionDuration, swing);

			if (subdivisionPhase >= targetTime && currentSubdivision < sub) {
				currentSubdivision = sub;
				triggerSubdivisionIfEnabled(sub);
				break;
			}
		}
	}

	float getSubdivisionTime(int sub, float subdivisionDuration, float swing) {
		// Base time for this subdivision
		float time = sub * subdivisionDuration;

		// Apply swing to odd subdivisions (1 and 3)
		if (sub % 2 == 1) {
			time += swing * subdivisionDuration;
		}

		return time;
	}

	void triggerSubdivisionIfEnabled(int sub) {
		int paramIdx = currentStep * 4 + sub;
		if (params[SUB_TOGGLE_PARAMS + paramIdx].getValue() > 0.5f) {
			// Calculate gate length based on subdivision duration and gate length param
			float gateLength = params[GATE_LENGTH_PARAM].getValue();
			float subdivisionDuration = clockPeriod / 4.f;
			float gateDuration = subdivisionDuration * gateLength;

			// Clamp to reasonable range
			gateDuration = clamp(gateDuration, 0.001f, clockPeriod);

			gatePulse.trigger(gateDuration);
		}
	}

	void updateLights(const ProcessArgs& args) {
		// Step indicator lights
		for (int i = 0; i < 8; i++) {
			lights[STEP_LIGHTS + i].setBrightness(i == currentStep ? 1.f : 0.f);
		}

		// Subdivision toggle lights
		for (int i = 0; i < 32; i++) {
			float brightness = params[SUB_TOGGLE_PARAMS + i].getValue();
			// Make current step's active subdivisions brighter
			int step = i / 4;
			int sub = i % 4;
			if (step == currentStep && sub == currentSubdivision && brightness > 0.5f) {
				brightness = 1.f;
			} else if (brightness > 0.5f) {
				brightness = 0.6f;
			}
			lights[SUB_TOGGLE_LIGHTS + i].setBrightness(brightness);
		}

		// Running indicator
		lights[RUNNING_LIGHT].setBrightness(running ? 1.f : 0.f);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "running", json_boolean(running));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* runningJ = json_object_get(rootJ, "running");
		if (runningJ)
			running = json_boolean_value(runningJ);
	}
};


struct SubGate8Panel : Widget {
	SubGate8Panel(Vec size) {
		box.size = size;
	}

	void draw(const DrawArgs& args) override {
		float w = box.size.x;   // 270  (18HP)
		float h = box.size.y;   // 380
		float cx = w / 2.f;     // 135

		// Layout anchors — keep in sync with SubGate8Widget
		const float gridX     = 12.f;   // separator / section left edge
		const float gridRight = 258.f;  // separator / section right edge
		const float stepSpcX  = 25.f;   // step column spacing
		const float startX    = cx - 3.5f * stepSpcX;  // centre 8 columns (47.5)

		// === Background ===
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, w, h);
		nvgFillColor(args.vg, nvgRGB(0x2a, 0x2a, 0x2a));
		nvgFill(args.vg);

		// === Inner panel face ===
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 4.f, 4.f, w - 8.f, h - 8.f, 5.f);
		nvgFillColor(args.vg, nvgRGB(0x22, 0x22, 0x22));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(0x33, 0x33, 0x33));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// === Separator lines ===
		auto sep = [&](float y) {
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, gridX, y);
			nvgLineTo(args.vg, gridRight, y);
			nvgStrokeColor(args.vg, nvgRGB(0x44, 0x44, 0x44));
			nvgStrokeWidth(args.vg, 0.8f);
			nvgStroke(args.vg);
		};
		sep(136.f);  // below grid, above controls
		sep(202.f);  // above I/O section

		// === Font ===
		std::shared_ptr<Font> font = APP->window->loadFont(
			asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

		// === Title ===
		nvgFontSize(args.vg, 11.f);
		nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
		nvgText(args.vg, cx, 22.f, "SubGate8", NULL);

		// === Step number labels 1–8 ===
		// Drawn inside the dark grid rect, above the button rows.
		nvgFontSize(args.vg, 8.f);
		nvgFillColor(args.vg, nvgRGB(0x88, 0x88, 0x88));
		const char* stepNums[] = {"1","2","3","4","5","6","7","8"};
		for (int i = 0; i < 8; i++) {
			nvgText(args.vg, startX + i * stepSpcX, 35.f, stepNums[i], NULL);
		}

		// === Subdivision row labels 1–4 ===
		// Right-aligned with extra gap left of the first button column,
		// y matches each button row centre exactly.
		const float startY = 62.f;
		const float subSpcY = 20.f;
		nvgFontSize(args.vg, 6.f);
		nvgFillColor(args.vg, nvgRGB(0x66, 0x66, 0x66));
		nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		for (int i = 0; i < 4; i++) {
			char buf[2] = {(char)('1' + i), 0};
			nvgText(args.vg, startX - 14.f, startY + i * subSpcY, buf, NULL);
		}
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

		// === Control section labels ===
		nvgFontSize(args.vg, 7.f);
		nvgFillColor(args.vg, nvgRGB(0x88, 0x88, 0x88));
		nvgText(args.vg, 45.f,  153.f, "GATE",   NULL);
		nvgText(args.vg, 45.f,  162.f, "LENGTH", NULL);
		nvgText(args.vg, 105.f, 155.f, "SWING",  NULL);
		nvgText(args.vg, 225.f, 157.f, "RUN",    NULL);
		nvgFontSize(args.vg, 8.f);
		nvgText(args.vg, 165.f, 155.f, "STEPS",  NULL);

		// === I/O section divider labels ===
		nvgFontSize(args.vg, 6.f);
		nvgFillColor(args.vg, nvgRGB(0x55, 0x55, 0x55));
		nvgText(args.vg, 82.f,  212.f, "INPUTS",  NULL);
		nvgText(args.vg, 208.f, 212.f, "OUTPUTS", NULL);

		// === Input port labels ===
		nvgFontSize(args.vg, 7.f);
		nvgFillColor(args.vg, nvgRGB(0x88, 0x88, 0x88));
		nvgText(args.vg, 42.f,  219.f, "CLK", NULL);
		nvgText(args.vg, 82.f,  219.f, "RST", NULL);
		nvgText(args.vg, 122.f, 219.f, "RUN", NULL);

		// === Output port labels ===
		nvgFillColor(args.vg, nvgRGB(0xcc, 0xcc, 0xcc));
		nvgText(args.vg, 188.f, 219.f, "GATE", NULL);
		nvgText(args.vg, 228.f, 219.f, "EOC",  NULL);

		// === VONK brand — "VON" near-white + "K" accent purple, combined word centred ===
		nvgFontSize(args.vg, 7.f);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		float vonAdv = nvgTextBounds(args.vg, 0, 0, "VON", NULL, NULL);
		float kAdv   = nvgTextBounds(args.vg, 0, 0, "K",   NULL, NULL);
		float vonkX  = cx - (vonAdv + kAdv) / 2.f;
		nvgFillColor(args.vg, nvgRGB(0xf4, 0xf5, 0xf7));
		nvgText(args.vg, vonkX,          312.f, "VON", NULL);
		nvgFillColor(args.vg, nvgRGB(0xc0, 0x84, 0xfc));
		nvgText(args.vg, vonkX + vonAdv, 312.f, "K",   NULL);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	}
};


struct SubGate8Widget : ModuleWidget {
	SubGate8Widget(SubGate8* module) {
		setModule(module);
		box.size = Vec(RACK_GRID_WIDTH * 18, RACK_GRID_HEIGHT);  // 270 × 380
		addChild(new SubGate8Panel(box.size));

		// Screws
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Layout constants — keep in sync with SubGate8Panel draw()
		const float cx        = box.size.x / 2.f;           // 135
		const float stepSpcX  = 25.f;
		const float startX    = cx - 3.5f * stepSpcX;       // centres 8 columns (47.5)
		const float startY    = 62.f;   // First subdivision row
		const float subSpcY   = 20.f;

		// 8×4 grid of subdivision buttons with lights
		for (int step = 0; step < 8; step++) {
			float x = startX + step * stepSpcX;

			// Step indicator light above the grid
			addChild(createLightCentered<MediumLight<GreenLight>>(
				Vec(x, 46.f),
				module,
				SubGate8::STEP_LIGHTS + step));

			for (int sub = 0; sub < 4; sub++) {
				float y = startY + sub * subSpcY;
				int idx = step * 4 + sub;

				// Toggle button with integrated light
				addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(
					Vec(x, y),
					module,
					SubGate8::SUB_TOGGLE_PARAMS + idx,
					SubGate8::SUB_TOGGLE_LIGHTS + idx));
			}
		}

		// Control section
		const float controlY = 180.f;

		addParam(createParamCentered<RoundSmallBlackKnob>(
			Vec(45.f, controlY), module, SubGate8::GATE_LENGTH_PARAM));

		addParam(createParamCentered<RoundSmallBlackKnob>(
			Vec(105.f, controlY), module, SubGate8::SWING_PARAM));

		addParam(createParamCentered<RoundSmallBlackKnob>(
			Vec(165.f, controlY), module, SubGate8::STEP_COUNT_PARAM));

		// Running indicator light
		addChild(createLightCentered<SmallLight<GreenLight>>(
			Vec(225.f, controlY), module, SubGate8::RUNNING_LIGHT));

		// Input/Output section
		const float ioY = 240.f;

		addInput(createInputCentered<PJ301MPort>(Vec(42.f,  ioY), module, SubGate8::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(82.f,  ioY), module, SubGate8::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(122.f, ioY), module, SubGate8::RUN_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(Vec(188.f, ioY), module, SubGate8::GATE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(228.f, ioY), module, SubGate8::EOC_OUTPUT));
	}
};


Model* modelSubGate8 = createModel<SubGate8, SubGate8Widget>("SubGate8");
