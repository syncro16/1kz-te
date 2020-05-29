/*  
 * Piggyback for Toyota 1kzte diesel engine (c) 2020 Juho Pesonen, syncro16@outlook.com 
 * 
 * Modifies (expands) spill of solenoid pulse according to analog input (MAP sensor for example) to allow more fueling
 * 
 * This is code for Arduino Mega / Uno
 */


volatile uint8_t interruptCounter;
volatile uint16_t injectionDuration;
volatile uint16_t newInjectionDuration;
volatile uint16_t fullCycleDuration;
volatile uint16_t check;
volatile uint8_t enrichment = 0;

#define INPUT_PIN 2
#define OUTPUT_PIN 7
#define ANALOG_ADJUST_PIN A0

void setup() {
	Serial.begin(115200);

	// set up pin modes
  	pinMode(13,OUTPUT);
	pinMode(INPUT_PIN,INPUT);
	pinMode(OUTPUT_PIN,OUTPUT);
	pinMode(ANALOG_ADJUST_PIN, INPUT);
	attachInterrupt(digitalPinToInterrupt(INPUT_PIN), rpmTrigger, CHANGE);  // 0 == Input #2 on arduino uno

	// https://circuitdigest.com/microcontroller-projects/arduino-timer-tutorial
	// timer 1 prescaler set to 64 = 250 000Hz
	TCCR1A = 0;
	TCCR1B = 0;
	TCNT1 = 0;
	TIMSK1 = 0;
	TIMSK1 = ( 1 <<TOIE1); // Enable overflow interrupt
	TCCR1B |= (1 << CS11) | (1 << CS10);  // prescaler 16MHz/64

}

// called when external input happens (either goes high or low)
void rpmTrigger() {
	uint8_t state = digitalRead(INPUT_PIN);
	if (state == 0) {
		// input is low - ECU operator solenoid turned ON	
		fullCycleDuration = TCNT1;
		digitalWrite(OUTPUT_PIN,HIGH);
		TCNT1 = 0;
	} else {
		// state is high, ECU operated solenoid turned OFF	
		injectionDuration = TCNT1;
		if (enrichment > 0) {
			// Wait until TCNT1 passes calculated enrichment amount
			// Enrichment amount is calculated with fixed point wizardry

			// this is more aggressive curve
			// uint32_t adjust = ((uint32_t)injectionDuration * enrichment) >> 8;
			
			uint32_t adjust = ((uint32_t)injectionDuration * enrichment) >> 9;

			// Do not expand adjust over full cycle, because we miss next interrupt
			if (adjust < fullCycleDuration-100) {
				// It is generally bad idea to "jam" interrupt handler
				// but is ok for this purpose. 
				while (TCNT1 < injectionDuration+adjust) 
					__asm("nop");
			}
		}
		digitalWrite(OUTPUT_PIN,LOW);
		newInjectionDuration = TCNT1;				
	}
}

// This called when interrupt overflow happends (no RPM input -> declare engine as stopped)
ISR(TIMER1_OVF_vect) {
	fullCycleDuration = 0xffff;
	newInjectionDuration = 0;
	injectionDuration = 0;
	digitalWrite(OUTPUT_PIN,LOW);
}

// Convert duration between triggers to RPM
uint16_t getRPM() {
	// One timer tick is 250kHz (prescaler 64)
	
	if (fullCycleDuration == 0xffff)
		return 0;
	// pump have 2 triggers per 360° engine rotation
	return (uint32_t)((16000000/2/64/(uint32_t)fullCycleDuration)*60);
}

void loop() {

	// read enrichment value from analog input 
	uint16_t val = analogRead(ANALOG_ADJUST_PIN);
	// map input voltages from ~2v to ~4v enrichment range. 
	// Probably works fine for 3bar map (we dont care first 1bar range, because that's below atmosphere pressure)

	int16_t eVal = map(val,406,819,0,255); 
	if (eVal < 0) 
		eVal = 0;
	if (eVal > 255) 
		eVal = 255;
	enrichment = eVal;
		
	// generate reference pulse to pin13 (led). 
	// This is 1000rpm (16.66r/s = 60ms) - if there is one trigger per 360° rotation
	// Because there is 4 triggers per rotation, and pump rotates two times slower than engine
	// so the measured reference RPM should be about 500rpm
	
	char buf[60];
	
	digitalWrite(13,LOW);
	delay(46);
	digitalWrite(13,HIGH);
	delay(24);
	
	sprintf(buf,"RPM:%4d, injectionDuration: %4d->%4d, enrichment:%3d",getRPM(),injectionDuration,newInjectionDuration,enrichment);
	Serial.println(buf);

}
