#include <stdio.h>
#include "platform.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xgpio.h"
#include "xintc.h"
#include "xtmrctr.h"
#include "sleep.h"

#define BUTTON_DEBOUNCE_TIME 10000*1000

// 16 LED control
XGpio per_leds;

// RGB leds (2 of 'em)
XGpio per_rgbleds;

// Encoder
XGpio per_encoder;

// Interrupt controller
XIntc per_intc;

// Timer
XTmrCtr per_timer;

// [0-15]: which led to turn on
volatile unsigned int led_num = 0;
volatile unsigned int led_enable = 1;
volatile unsigned int button_enable = 1;

volatile unsigned int encoderState = 0;

void moveLedLeft() {
	led_num = (led_num + led_enable) % 16;
}

void moveLedRight() {
	led_num = (led_num - led_enable) % 16;
}

void enableLed() {
	led_enable = 1;
}

void disableLed() {
	led_enable = 0;
}

void toggleLed() {
	led_enable = !led_enable;
}

void dispLed() {
	// which leds to turn on
	u32 led_mask;

	// make sure the leds are enabled
	if (led_enable == 0) {
		led_mask = 0;
	} else {
		led_mask = 1<<led_num;
	}

	XGpio_DiscreteWrite(&per_leds, 1, led_mask);
}

void updateState(u32 encoderData) {
	u32 ab = encoderData & 0b11;

	u32 button = encoderData & 0b100;

	if (button && button_enable) {
		toggleLed();
		dispLed();

		button_enable = 0;
		XTmrCtr_Start(&per_timer, 0);
		return;
	}

	switch(encoderState) {
	case 0:
		if        (ab == 0b01) {
			encoderState = 1;
		} else if (ab == 0b10) {
			encoderState = 4;
		}
		break;
	case 1:
		if        (ab == 0b11) {
			encoderState = 0;
		} else if (ab == 0b00) {
			encoderState = 2;
		}
		break;
	case 2:
		if        (ab == 0b01) {
			encoderState = 1;
		} else if (ab == 0b10) {
			encoderState = 3;
		}
		break;
	case 3:
		if        (ab == 0b11) {
			encoderState = 0;

			// move clockwise
			moveLedRight();
			dispLed();
		} else if (ab == 0b00) {
			encoderState = 2;
		}
		break;
	case 4:
		if        (ab == 0b00) {
			encoderState = 5;
		} else if (ab == 0b11) {
			encoderState = 0;
		}
		break;
	case 5:
		if        (ab == 0b01) {
			encoderState = 6;
		} else if (ab == 0b10) {
			encoderState = 4;
		}
		break;
	case 6:
		if        (ab == 0b11) {
			encoderState = 0;

			// move counter clockwise
			moveLedLeft();
			dispLed();
		} else if (ab == 0b00) {
			encoderState = 5;
		}
		break;
	}
}

void encoderHandler() {
	u32 encoderData = XGpio_DiscreteRead(&per_encoder, 1);

	// XGpio_DiscreteWrite(&per_leds, 1, encoderData & 0b11);

	updateState(encoderData);

	// mark interrupt as handled
	XGpio_InterruptClear(&per_encoder, 0xFFFFFFFF);
}

void timerHandler() {
	// handler code
	button_enable = 1;

	// acknowledge that interrupt handled
	u32 controlReg = XTimerCtr_ReadReg(per_timer.BaseAddress, 0, XTC_TCSR_OFFSET);
	XTmrCtr_WriteReg(
		per_timer.BaseAddress,
		0,
		XTC_TCSR_OFFSET,
		controlReg | XTC_CSR_INT_OCCURED_MASK
	);
}

void initPeripherals() {
	// 16 leds
	XGpio_Initialize(&per_leds, XPAR_LEDS_DEVICE_ID);

	// 2 rgb leds
	XGpio_Initialize(&per_rgbleds, XPAR_RGB_LED_DEVICE_ID);

	// encoder
	XGpio_Initialize(&per_encoder, XPAR_ENCODER_DEVICE_ID);

	// interrupt controller
	XIntc_Initialize(&per_intc, XPAR_MICROBLAZE_0_AXI_INTC_DEVICE_ID);

	// timer
	XTmrCtr_Initialize(&per_timer, XPAR_AXI_TIMER_0_DEVICE_ID);
}

void registerInterruptHandlers() {
	// register encoder handler
	XIntc_Connect(
		&per_intc,
		XPAR_MICROBLAZE_0_AXI_INTC_ENCODER_IP2INTC_IRPT_INTR,
		encoderHandler,
		&per_encoder
	);

	XIntc_Enable(&per_intc, XPAR_MICROBLAZE_0_AXI_INTC_ENCODER_IP2INTC_IRPT_INTR);

	// register timer handler
	XIntc_Connect(
		&per_intc,
		XPAR_MICROBLAZE_0_AXI_INTC_AXI_TIMER_0_INTERRUPT_INTR,
		timerHandler,
		&per_timer
	);

	XIntc_Enable(&per_intc, XPAR_MICROBLAZE_0_AXI_INTC_AXI_TIMER_0_INTERRUPT_INTR);
}

void finishInterruptEnable() {
	XIntc_Start(&per_intc, XIN_REAL_MODE);

	// set up the encoder
	XGpio_InterruptEnable(&per_encoder, 1);
	XGpio_InterruptGlobalEnable(&per_encoder);

	XGpio_SetDataDirection(&per_encoder, 1, 0xFFFFFFFF);

	// set up the timer
	XTmrCtr_SetOptions(
		&per_timer,
		0,
		XTC_INT_MODE_OPTION
	);

	// timer time
	XTmrCtr_SetResetValue(&per_timer, 0, 0xFFFFFFFF-BUTTON_DEBOUNCE_TIME);

	// connect interrupt controller to microblaze
	microblaze_register_handler(
			(XInterruptHandler)XIntc_DeviceInterruptHandler,
		(void*)XPAR_MICROBLAZE_0_AXI_INTC_DEVICE_ID
	);

	microblaze_enable_interrupts();
}

void testLeds() {
	for (int i = 0; i < 20; i++) {
		dispLed();
		moveLedLeft();
		usleep(2000);
	}
	for (int i = 0; i < 20; i++) {
		dispLed();
		moveLedRight();
		usleep(2000);
	}
	dispLed();

	disableLed();
	dispLed();
	usleep(20000);

	for (int i = 0; i < 5; i++) {
		moveLedLeft();
	}

	enableLed();
	dispLed();
}

void blinkRGBLeds() {
	while (1) {
		XGpio_DiscreteWrite(&per_rgbleds, 1, 0b010000);
		usleep(10000);
		XGpio_DiscreteWrite(&per_rgbleds, 1, 0b000000);
		usleep(10000);
	}
}

int main() {
	print("Starting\n");
    init_platform();
    initPeripherals();
    registerInterruptHandlers();
    finishInterruptEnable();

    print("Enabled Interrupts\n");

    //testLeds();
    dispLed();

    // start infinite loop of blinking leds
    blinkRGBLeds();

    cleanup_platform();
    return 0;
}
