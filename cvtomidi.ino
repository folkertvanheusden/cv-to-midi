// (C) 2020 by folkert@vanheusden.com
// released on AGPL v3.0 in 2021

//#define DEBUG

#define PROGRAM_VERSION 1

#define ADC_NOTE	A0
#define GATE		2
#define MODE_SEL	3
#define LED_ACK		13

#define DT_NOTE_OFF	500 /// ms
#define DT_MIN_NOTE_INTERVAL (1000/75)

constexpr double steps = 1024.0 / (10 * 12); // / 10 * 12 when mapping -5...5 to 0...5

#include "digitalWriteFast.h"

volatile bool gate_triggered = false, gate_direction = false;

void gate_change(void) {
	gate_direction = !digitalReadFast(GATE);
	gate_triggered = true;
}

uint16_t get_sample_low() {
	return 1023 - analogRead(ADC_NOTE); // INVERTING(!) op-amp
}

uint16_t get_filtered_sample() {
	uint16_t s1 = get_sample_low();
	uint16_t s2 = get_sample_low();

	if (s1 != s2)
		return get_sample_low();

	return s1;
}

uint8_t adc_to_note(uint16_t v) {
	return 5 + v / steps;
}

void flashLED() {
	static bool la_status = false;
	digitalWriteFast(LED_ACK, !la_status);
	la_status = !la_status;
}

void midiMsg(uint8_t cmd[], uint8_t len) {
	static uint8_t prev_cmd = 255;

	if (cmd[0] == prev_cmd)
		Serial.write(&cmd[1], len - 1);
	else {
		Serial.write(cmd, len);
		prev_cmd = cmd[0];
	}

	flashLED();
}

void midiReset() {
	uint8_t msg[] = { 0xff };
	midiMsg(msg, sizeof msg);
}

void noteOn(uint8_t note, uint8_t vol) {
	uint8_t msg[] = { 0x90, note, vol };
	midiMsg(msg, sizeof msg);
}

void noteOff(const uint8_t note) {
	uint8_t msg[] = { 0x80, note, 0 };
	midiMsg(msg, sizeof msg);
}

void midiSelectInstrument() {
	uint8_t msg[] = { 0xc0, 81 };
	midiMsg(msg, sizeof msg);
}

void midiSendProgramVersion(uint8_t v) {
	uint8_t msg[10];

	msg[0] = msg[9] = 0xff;

	for(uint8_t i=0; i<8; i++)
		msg[1 + i] = ((v >> i) & 1) ? 0xfe : 0xf8;

	midiMsg(msg, sizeof msg);

	//

	uint8_t msg2[] = {
			0xf0, 0x7e, 		// header
			0x00, 0x7f, 0x7f,	// device id
			0x06,			// General Information (sub-ID#1)
			0x02,			// Identity Reply (sub-ID#2) 
			0x01,			// Manufacturers System Exclusive id code
			0x01, 0x00,		// Device family code (14 bits, LSB first)
			0x01, 0x00, 		// Device family member code (14 bits, LSB first)
			v, 0x00, 0x00, 0x00,	// Software revision level. Format device specific
			0xf7			// EOC
		};

	midiMsg(msg2, sizeof msg2);
}

void setup() {
#ifdef DEBUG
	Serial.begin(115200);
#else
	Serial.begin(31250);
#endif

	pinMode(GATE, INPUT);
	attachInterrupt(0, gate_change, CHANGE);

	pinMode(MODE_SEL, INPUT);

	pinMode(LED_ACK, OUTPUT);

	digitalWriteFast(LED_ACK, HIGH);

	midiSendProgramVersion(PROGRAM_VERSION);

	midiReset();

	midiSelectInstrument();

	noteOn(80, 127);
	delay(100);
	noteOff(80);

	digitalWriteFast(LED_ACK, LOW);
}

uint8_t prevNote = 255;

uint16_t prevVal = 0;
uint32_t notePlayingSince = 0;

void loop() {
	if (digitalReadFast(MODE_SEL)) {
		if (gate_triggered) {
			gate_triggered = false;

			if (prevNote != 255) {
#ifdef DEBUG
				Serial.println(F("OFF"));
#else
				noteOff(prevNote);
#endif
				delayMicroseconds(1000000ll / 31250 * 2 * 10);

				prevNote = 255;
			}

			if (gate_direction) { // note on
				uint16_t nv = get_filtered_sample();
				uint8_t newNote = adc_to_note(nv);

				// send note on for newNote
#ifdef DEBUG
				Serial.print(F("ON: "));
				Serial.println(newNote);
#else
				noteOn(newNote, 127);
#endif
				delayMicroseconds(DT_MIN_NOTE_INTERVAL);

				prevNote = newNote;
			}
		}
	}
	else {
		delay(DT_MIN_NOTE_INTERVAL);

		if (DT_NOTE_OFF > 0 && millis() - notePlayingSince >= DT_NOTE_OFF && prevNote != 255) {
			noteOff(prevNote);
			prevNote = 255;
		}

		uint16_t nv = get_filtered_sample();

		int16_t diff = nv - prevVal;
		if (abs(diff) >= steps || (nv == 0 && abs(diff) >= steps /2)) {
			uint8_t newNote = adc_to_note(nv);

			// send note off for prevNote
			if (prevNote != 255)
				noteOff(prevNote);

			// send note on for newNote
#ifdef DEBUG
			Serial.print(F("ON: "));
			Serial.println(newNote);
#else
			noteOn(newNote, 127);
#endif
			notePlayingSince = millis();

			prevVal = nv;
			prevNote = newNote;
		}
	}
}
