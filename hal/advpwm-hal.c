/*
 * SPDX-License-Identifier: BSD-2-Clause
 * 
 * Copyright (c) 2022 Vincent DEFERT. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 * notice, this list of conditions and the following disclaimer in the 
 * documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN 
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "project-defs.h"
#include <advpwm-hal.h>
#include <gpio-hal.h>

/**
 * @file advpwm-hal.c
 * 
 * 16-bit advanced PWM abstraction implementation.
 */

typedef enum {
	UNUSED_CHANNEL,
	PWM_CHANNEL,
	ENCODER_CHANNEL,
	CAPTURE_CHANNEL,
} PWM_ChannelUsage;

static PWM_ChannelUsage channelUsage[] = {
	UNUSED_CHANNEL, UNUSED_CHANNEL, UNUSED_CHANNEL, UNUSED_CHANNEL, 
	UNUSED_CHANNEL, UNUSED_CHANNEL, UNUSED_CHANNEL, UNUSED_CHANNEL, 
};

#define PIN_CONFIG_MAX 3
#define UNSUPPORTED_PIN_SWITCH 0xff

static const uint8_t __channelPinConfigurations[][12] = {
	{ 0x10, 0x11, 
#ifdef GPIO_NO_P12
	              0x54, 
#else
	              0x12, 
#endif
	                    0x13, 0x14, 0x15, 0x16, 0x17, 0x20, 0x21, 0x22, 0x23, },
	{ 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x17, 0x54, 0x33, 0x34, },
	{ 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x00, 0x01, 0x02, 0x03, },
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x34, 0x33, 0x74, 0x75, 0x76, 0x77, },
};

static const uint8_t __triggerPinConfigurations[][2] = {
	{ 0x32, 0x32, },
	{ 0x41, 0x06, },
	{ 0x73, 0xff, },
	{ 0xff, 0xff, }, // 0xff means: Unsupported
};

static const uint8_t __faultPinConfigurations[][2] = {
	{ 0x35, 0x35, },
	{ 0x99, 0x99, }, // 0x99 means: Comparator output
};

static uint8_t getChannelPin(PWM_Channel channel, uint8_t pinSwitch, uint8_t offset) {
	if (pinSwitch >= PIN_CONFIG_MAX) {
		pinSwitch = 0;
	}
	
	uint8_t index = channel;
	
#if HAL_PWM_CHANNELS > 4
	if (channel > PWM_Channel4) {
		index--;
	}
	
	if (channel > PWM_Channel5) {
		index--;
	}
	
	if (channel > PWM_Channel6) {
		index--;
	}
	
	if (channel < PWM_Channel4) {
#endif
		index += offset;
#if HAL_PWM_CHANNELS > 4
	}
#endif
	
	return __channelPinConfigurations[pinSwitch][index];
}

static bool configurePin(uint8_t ioPin, GpioPinMode pinMode) {
	GpioPort port = (GpioPort) (ioPin >> 4);
	GpioPin pin = (GpioPin) (ioPin & 0x0f);
	bool rc = (port < 8 && pin < 8);
	
	if (rc) {
		GpioConfig pinConfig = GPIO_PIN_CONFIG(port, pin, pinMode);
		gpioConfigure(&pinConfig);
	}
	
	return rc;
}

static void applyPinSwitch(PWM_Channel channel, uint8_t pinSwitch) {
	if (pinSwitch >= PIN_CONFIG_MAX) {
		pinSwitch = 0;
	}
	
#if HAL_PWM_CHANNELS > 4
	if (channel < PWM_Channel4) {
#endif
		PWMA_PS = (PWMA_PS & (3 << channel)) | (pinSwitch << channel);
#if HAL_PWM_CHANNELS > 4
	} else {
		channel -= 8;
		PWMB_PS = (PWMB_PS & (3 << channel)) | (pinSwitch << channel);
	}
#endif
}

static void enableOutput(PWM_Channel channel, uint8_t offset) {
#if HAL_PWM_CHANNELS > 4
	if (channel < PWM_Channel4) {
#endif
		PWMA_ENO |= 1 << (channel + offset);
#if HAL_PWM_CHANNELS > 4
	} else {
		PWMB_ENO |= 1 << (channel - PWM_Channel4);
	}
#endif
}

static void enableChannel(PWM_Channel channel, uint8_t offset, PWM_Polarity polarity) {
#if HAL_PWM_CHANNELS > 4
	if (channel < PWM_Channel4) {
#endif
		switch (channel) {
		case PWM_Channel0:
			PWMA_CCER1 |= offset ? M_CC1NE : M_CC1E;
			
			if (polarity == PWM_ACTIVE_LOW) {
				PWMA_CCER1 |= offset ? M_CC1NP : M_CC1P;
			}
			break;
#if HAL_PWM_CHANNELS > 1
		case PWM_Channel1:
			PWMA_CCER1 |= offset ? M_CC2NE : M_CC2E;
			
			if (polarity == PWM_ACTIVE_LOW) {
				PWMA_CCER1 |= offset ? M_CC2NP : M_CC2P;
			}
			break;
#endif
#if HAL_PWM_CHANNELS > 2
		case PWM_Channel2:
			PWMA_CCER2 |= offset ? M_CC3NE : M_CC3E;
			
			if (polarity == PWM_ACTIVE_LOW) {
				PWMA_CCER2 |= offset ? M_CC3NP : M_CC3P;
			}
			break;
		case PWM_Channel3:
			PWMA_CCER2 |= offset ? M_CC4NE : M_CC4E;
			
			if (polarity == PWM_ACTIVE_LOW) {
				PWMA_CCER2 |= offset ? M_CC4NP : M_CC4P;
			}
			break;
#endif
		}
#if HAL_PWM_CHANNELS > 4
	} else {
		switch (channel) {
		case PWM_Channel4:
			PWMB_CCER1 |= M_CC5E;
			
			if (polarity == PWM_ACTIVE_LOW) {
				PWMB_CCER1 |= M_CC5P;
			}
			break;
		case PWM_Channel5:
			PWMB_CCER1 |= M_CC6E;
			
			if (polarity == PWM_ACTIVE_LOW) {
				PWMB_CCER1 |= M_CC6P;
			}
			break;
		case PWM_Channel6:
			PWMB_CCER2 |= M_CC7E;
			
			if (polarity == PWM_ACTIVE_LOW) {
				PWMB_CCER2 |= M_CC7P;
			}
			break;
		case PWM_Channel7:
			PWMB_CCER2 |= M_CC8E;
			
			if (polarity == PWM_ACTIVE_LOW) {
				PWMB_CCER2 |= M_CC8P;
			}
			break;
		}
	}
#endif
}

static void enableFaultControl(PWM_Channel channel, uint8_t offset, PWM_FaultControl faultControl, OutputLevel idleLevel) {
	if (faultControl == PWM_ENABLE_FAULT_CONTROL) {
#if HAL_PWM_CHANNELS > 4
		if (channel < PWM_Channel4) {
#endif
			PWMA_IOAUX |= 1 << (channel + offset);
			PWMA_OISR |= idleLevel << (channel + offset);
#if HAL_PWM_CHANNELS > 4
		} else {
			PWMB_IOAUX |= 1 << (channel - PWM_Channel4);
			PWMB_OISR |= idleLevel << (channel - PWM_Channel4);
		}
#endif
	}
}

static void closeChannel(PWM_Channel channel) {
	switch (channel) {
	case PWM_Channel0:
		PWMA_CCER1 &= 0xf0;
		break;
#if HAL_PWM_CHANNELS > 1
	case PWM_Channel1:
		PWMA_CCER1 &= 0x0f;
		break;
#endif
#if HAL_PWM_CHANNELS > 2
	case PWM_Channel2:
		PWMA_CCER2 &= 0xf0;
		break;
	case PWM_Channel3:
		PWMA_CCER2 &= 0x0f;
		break;
#endif
#if HAL_PWM_CHANNELS > 4
	case PWM_Channel4:
		PWMB_CCER1 &= 0xf0;
		break;
	case PWM_Channel5:
		PWMB_CCER1 &= 0x0f;
		break;
	case PWM_Channel6:
		PWMB_CCER2 &= 0xf0;
		break;
	case PWM_Channel7:
		PWMB_CCER2 &= 0x0f;
		break;
#endif
	}
}

void pwmConfigureOutput(
	PWM_Channel channel, 
	uint8_t pinSwitch, 
	GpioPinMode pinMode, 
	PWM_Polarity polarity, 
	PWM_FaultControl faultControl,
	OutputLevel idleLevel,
	PWM_OutputEnable outputEnable
) {
	ENABLE_EXTENDED_SFR()
	
	PWMA_CCER1 |= (0 << P_CC1P) | (0 << P_CC1NP) | (1 << P_CC1E) | (1 << P_CC1NE);
	
	if (pinMode == GPIO_HIGH_IMPEDANCE_MODE) {
		pinMode = GPIO_PUSH_PULL_MODE;
	}
	
	bool ok = false;
	
	if (channel > PWM_Channel3 || (outputEnable & PWM_OUTPUT_P_ONLY)) {
		if (configurePin(getChannelPin(channel, pinSwitch, 0), pinMode)) {
			enableOutput(channel, 0);
			enableChannel(channel, 0, polarity);
			enableFaultControl(channel, 0, faultControl, idleLevel);
			ok = true;
		}
	}
	
	if (channel <= PWM_Channel3 && (outputEnable & PWM_OUTPUT_N_ONLY)) {
		if (configurePin(getChannelPin(channel, pinSwitch, 1), pinMode)) {
			enableOutput(channel, 1);
			enableChannel(channel, 1, polarity);
			enableFaultControl(channel, 1, faultControl, idleLevel);
			ok = true;
		}
	}
	
	if (ok) {
		applyPinSwitch(channel, pinSwitch);
	}
	
	DISABLE_EXTENDED_SFR()
}

void pwmConfigureInput(
	PWM_Channel channel, 
	uint8_t pinSwitch, 
	PWM_Polarity polarity,
	PWM_CaptureSource captureSource,
	PWM_Filter filter
) {
	ENABLE_EXTENDED_SFR()
	
	applyPinSwitch(channel, pinSwitch);
	configurePin(getChannelPin(channel, pinSwitch, 0), GPIO_HIGH_IMPEDANCE_MODE);
	uint8_t ccmr = (filter << 4) | captureSource;
	
	switch (channel) {
	case PWM_Channel0:
		PWMA_CCMR1 = ccmr;
		break;
#if HAL_PWM_CHANNELS > 1
	case PWM_Channel1:
		PWMA_CCMR2 = ccmr;
		break;
#endif
#if HAL_PWM_CHANNELS > 2
	case PWM_Channel2:
		PWMA_CCMR3 = ccmr;
		break;
	case PWM_Channel3:
		PWMA_CCMR4 = ccmr;
		break;
#endif
#if HAL_PWM_CHANNELS > 4
	case PWM_Channel4:
		PWMB_CCMR1 = ccmr;
		break;
	case PWM_Channel5:
		PWMB_CCMR2 = ccmr;
		break;
	case PWM_Channel6:
		PWMB_CCMR3 = ccmr;
		break;
	case PWM_Channel7:
		PWMB_CCMR4 = ccmr;
		break;
#endif
	}
	
	enableChannel(channel, 0, polarity);
	
	DISABLE_EXTENDED_SFR()
}

/**
 * @param counterFreq is the frequency at the output of the prescaler.
 * It determines the resolution of the counter, i.e. counterFreq = 10kHz
 * means the counter has a resolution of 100us.
 * 
 * Ignored when mode is PWM_EVENT_COUNTER1, PWM_EVENT_COUNTER2, or
 * PWM_QUADRATURE_ENCODER.
 * 
 * Lower 16 bits used as prescaler value when mode is PWM_EXTERNAL_CLOCK.
 * 
 * @param signalFreq is the frequency of the signal on the PWM outputs
 * of the counter. It determines the reload value of the counter, i.e.
 * the division factor applied to counterFreq.
 * 
 * Lower 16 bits used as reload value when mode is PWM_EVENT_COUNTER1, 
 * PWM_EVENT_COUNTER2, PWM_QUADRATURE_ENCODER, or PWM_EXTERNAL_CLOCK.
 */
uint16_t pwmConfigureCounter(
	PWM_Counter counter, 
	uint32_t counterFreq, 
	uint32_t signalFreq, 
	PWM_CounterMode mode, 
	PWM_TriggerSource trigger,
	uint8_t repeatCount, 
	PWM_RegisterUpdate registerUpdateMode,
	PWM_CounterRunMode counterRunMode,
	PWM_CounterDirection counterDirection,
	PWM_UpdateEventEnable updateEventEnable,
	InterruptEnable comInterrupt
) {
	ENABLE_EXTENDED_SFR()
	
	uint16_t prescaler = 0;
	uint16_t reloadValue = 0;
	
	switch (mode) {
	case PWM_EVENT_COUNTER1:
	case PWM_EVENT_COUNTER2:
	case PWM_QUADRATURE_ENCODER:
		break;
	
	case PWM_EXTERNAL_CLOCK:
		prescaler = counterFreq;
		break;
	
	default:
		prescaler = MCU_FREQ / counterFreq;
		break;
	}
	
	if (prescaler) {
		prescaler -= 1;
	}
	
	switch (mode) {
	case PWM_EVENT_COUNTER1:
	case PWM_EVENT_COUNTER2:
	case PWM_QUADRATURE_ENCODER:
	case PWM_EXTERNAL_CLOCK:
		reloadValue = signalFreq;
		break;
	
	default:
		reloadValue = counterFreq / signalFreq;
		break;
	}
	
	switch (counterDirection) {
	case PWM_CENTRE_ALIGNED_INT_DOWN:
	case PWM_CENTRE_ALIGNED_INT_UP:
	case PWM_CENTRE_ALIGNED_INT_UP_DOWN:
		// In centre-aligned mode, we need to count twice as fast
		// to get the same output signal frequency.
		reloadValue >>= 1;
		break;
	}
	
	if (reloadValue) {
		reloadValue -= 1;
	}
	
	uint8_t slaveMode = mode | (trigger << 4);
	
	uint8_t cr1 = registerUpdateMode << P_ARPE
		| counterRunMode << P_OPM
		| counterDirection << P_DIR
		| updateEventEnable << P_UDIS;
	
	uint8_t ier = (comInterrupt == ENABLE_INTERRUPT ? M_COMIE : 0)
		| (updateEventEnable != PWM_DISABLE_ALL_UE ? M_UIE : 0);
	
	
	switch (counter) {
	case PWM_COUNTER_A:
		PWMA_PSCRH = prescaler >> 8;
		PWMA_PSCRL = prescaler;
		PWMA_ARRH = reloadValue >> 8;
		PWMA_ARRL = reloadValue;
		PWMA_SMCR = slaveMode;
		PWMA_RCR = repeatCount;
		PWMA_CR1 = cr1;
		PWMA_IER = (PWMA_IER & ~(M_COMIE | M_UIE)) | ier;
		break;
	
#if HAL_PWM_CHANNELS > 4
	case PWM_COUNTER_B:
		PWMB_PSCRH = prescaler >> 8;
		PWMB_PSCRL = prescaler;
		PWMB_ARRH = reloadValue >> 8;
		PWMB_ARRL = reloadValue;
		PWMB_SMCR = slaveMode;
		PWMB_RCR = repeatCount;
		PWMB_CR1 = cr1;
		PWMB_IER = (PWMB_IER & ~(M_COMIE | M_UIE)) | ier;
		break;
#endif
	}
	
	DISABLE_EXTENDED_SFR()
	
	return reloadValue + 1;
}

void pwmEnableMainOutput(PWM_Counter counter) {
	ENABLE_EXTENDED_SFR()
	
	switch (counter) {
	case PWM_COUNTER_A:
		PWMA_BKR |= M_MOE;
		break;
	
#if HAL_PWM_CHANNELS > 4
	case PWM_COUNTER_B:
		PWMB_BKR |= M_MOE;
		break;
#endif
	}
	
	DISABLE_EXTENDED_SFR()
}

void pwmEnableCounter(PWM_Counter counter) {
	ENABLE_EXTENDED_SFR()
	
	switch (counter) {
	case PWM_COUNTER_A:
		PWMA_CR1 |= M_CEN;
		break;
	
#if HAL_PWM_CHANNELS > 4
	case PWM_COUNTER_B:
		PWMB_CR1 |= M_CEN;
		break;
#endif
	}
	
	DISABLE_EXTENDED_SFR()
}

void pwmDisableCounter(PWM_Counter counter) {
	ENABLE_EXTENDED_SFR();
	
	switch (counter) {
	case PWM_COUNTER_A:
		PWMA_CR1 &= ~M_CEN;
		break;
	
#if HAL_PWM_CHANNELS > 4
	case PWM_COUNTER_B:
		PWMB_CR1 &= ~M_CEN;
		break;
#endif
	}
	
	DISABLE_EXTENDED_SFR();
}

/**
 * @param clockPulses is the duration of the dead time expressed in 
 * number of clock pulses on the input of the prescaler, i.e. sysclk
 * unless you use an external PWM clock.
 */
void pwmConfigureDeadTime(PWM_Counter counter, uint16_t clockPulses) {
	ENABLE_EXTENDED_SFR()
	
	uint8_t dtr = 255;
	
	if (clockPulses < 128) {
		dtr = clockPulses;
	} else if (clockPulses < 255) {
		dtr = ((clockPulses >> 1) - 64) | 0x80;
	} else if (clockPulses < 505) {
		dtr = ((clockPulses >> 3) - 32) | 0xc0;
	} else if (clockPulses < 1009) {
		dtr = ((clockPulses >> 4) - 32) | 0xe0;
	}
	
	switch (counter) {
	case PWM_COUNTER_A:
		PWMA_DTR = dtr;
		break;
	
#if HAL_PWM_CHANNELS > 4
	case PWM_COUNTER_B:
		PWMB_DTR = dtr;
		break;
#endif
	}
	
	DISABLE_EXTENDED_SFR()
}

static void enableChannelInterrupt(PWM_Channel channel) {
#if HAL_PWM_CHANNELS > 4
	if (channel < PWM_Channel4) {
#endif
		PWMA_IER |= 1 << ((channel >> 1) + 1);
#if HAL_PWM_CHANNELS > 4
	} else {
		PWMB_IER |= 1 << (((channel - PWM_Channel4) >> 1) + 1);
	}
#endif
}

void pwmInitialisePWM(
	PWM_Channel channel, 
	OutputLevel initialLevel, 
	InterruptEnable enableInterrupt, 
	PWM_RegisterUpdate registerUpdateMode,
	uint16_t ticks
) {
	ENABLE_EXTENDED_SFR()
	
	// Channel must be closed before writing to CCMRx
	closeChannel(channel);
	
	pwmSetDutyCycle(channel, ticks);
	
	// Enable/disable preload
	uint8_t ccmr = (registerUpdateMode == PWM_BUFFERED_UPDATE) ? M_OC_PE : 0;
	
	if (initialLevel == OUTPUT_HIGH) {
		// OC_M == 6 is PWM mode 1, i.e. wave form starts with a high level.
		ccmr |= 6 << P_OC_M;
	} else {
		// OC_M == 7 is PWM mode 2, i.e. wave form starts with a low level.
		ccmr |= 7 << P_OC_M;
	}
	
	switch (channel) {
	case PWM_Channel0:
		PWMA_CCMR1 = ccmr;
		break;
#if HAL_PWM_CHANNELS > 1
	case PWM_Channel1:
		PWMA_CCMR2 = ccmr;
		break;
#endif
#if HAL_PWM_CHANNELS > 2
	case PWM_Channel2:
		PWMA_CCMR3 = ccmr;
		break;
	case PWM_Channel3:
		PWMA_CCMR4 = ccmr;
		break;
#endif
#if HAL_PWM_CHANNELS > 4
	case PWM_Channel4:
		PWMB_CCMR1 = ccmr;
		break;
	case PWM_Channel5:
		PWMB_CCMR2 = ccmr;
		break;
	case PWM_Channel6:
		PWMB_CCMR3 = ccmr;
		break;
	case PWM_Channel7:
		PWMB_CCMR4 = ccmr;
		break;
#endif
	}
	
	channelUsage[channel] = PWM_CHANNEL;
	
	if (enableInterrupt == ENABLE_INTERRUPT) {
		enableChannelInterrupt(channel);
	}
	
	DISABLE_EXTENDED_SFR()
}

void pwmStopPWM(PWM_Channel channel) {
	ENABLE_EXTENDED_SFR()
	
	// OC_M == 0 freezes PWM output.
	switch (channel) {
	case PWM_Channel0:
		PWMA_CCMR1 = 0;
		break;
#if HAL_PWM_CHANNELS > 1
	case PWM_Channel1:
		PWMA_CCMR2 = 0;
		break;
#endif
#if HAL_PWM_CHANNELS > 2
	case PWM_Channel2:
		PWMA_CCMR3 = 0;
		break;
	case PWM_Channel3:
		PWMA_CCMR4 = 0;
		break;
#endif
#if HAL_PWM_CHANNELS > 4
	case PWM_Channel4:
		PWMB_CCMR1 = 0;
		break;
	case PWM_Channel5:
		PWMB_CCMR2 = 0;
		break;
	case PWM_Channel6:
		PWMB_CCMR3 = 0;
		break;
	case PWM_Channel7:
		PWMB_CCMR4 = 0;
		break;
#endif
	}
	
	DISABLE_EXTENDED_SFR()
}

void pwmLockPWM(PWM_Channel channel, OutputLevel outputLevel) {
	uint8_t ccmr = (outputLevel == OUTPUT_HIGH) ? (5 << P_OC_M) : (4 << P_OC_M);
	
	ENABLE_EXTENDED_SFR()
	
	switch (channel) {
	case PWM_Channel0:
		PWMA_CCMR1 = (PWMA_CCMR1 & ~M_OC_M) | ccmr;
		break;
#if HAL_PWM_CHANNELS > 1
	case PWM_Channel1:
		PWMA_CCMR2 = (PWMA_CCMR2 & ~M_OC_M) | ccmr;
		break;
#endif
#if HAL_PWM_CHANNELS > 2
	case PWM_Channel2:
		PWMA_CCMR3 = (PWMA_CCMR3 & ~M_OC_M) | ccmr;
		break;
	case PWM_Channel3:
		PWMA_CCMR4 = (PWMA_CCMR4 & ~M_OC_M) | ccmr;
		break;
#endif
#if HAL_PWM_CHANNELS > 4
	case PWM_Channel4:
		PWMB_CCMR1 = (PWMB_CCMR1 & ~M_OC_M) | ccmr;
		break;
	case PWM_Channel5:
		PWMB_CCMR2 = (PWMB_CCMR2 & ~M_OC_M) | ccmr;
		break;
	case PWM_Channel6:
		PWMB_CCMR3 = (PWMB_CCMR3 & ~M_OC_M) | ccmr;
		break;
	case PWM_Channel7:
		PWMB_CCMR4 = (PWMB_CCMR4 & ~M_OC_M) | ccmr;
		break;
#endif
	}
	
	DISABLE_EXTENDED_SFR()
}

void pwmSetDutyCycle(PWM_Channel channel, uint16_t ticks) {
	uint8_t ticksH = ticks >> 8;
	uint8_t ticksL = ticks;
	
	ENABLE_EXTENDED_SFR()
	
	switch (channel) {
	case PWM_Channel0:
		PWMA_CCR1H = ticksH;
		PWMA_CCR1L = ticksL;
		break;
#if HAL_PWM_CHANNELS > 1
	case PWM_Channel1:
		PWMA_CCR2H = ticksH;
		PWMA_CCR2L = ticksL;
		break;
#endif
#if HAL_PWM_CHANNELS > 2
	case PWM_Channel2:
		PWMA_CCR3H = ticksH;
		PWMA_CCR3L = ticksL;
		break;
	case PWM_Channel3:
		PWMA_CCR4H = ticksH;
		PWMA_CCR4L = ticksL;
		break;
#endif
#if HAL_PWM_CHANNELS > 4
	case PWM_Channel4:
		PWMB_CCR1H = ticksH;
		PWMB_CCR1L = ticksL;
		break;
	case PWM_Channel5:
		PWMB_CCR2H = ticksH;
		PWMB_CCR2L = ticksL;
		break;
	case PWM_Channel6:
		PWMB_CCR3H = ticksH;
		PWMB_CCR3L = ticksL;
		break;
	case PWM_Channel7:
		PWMB_CCR4H = ticksH;
		PWMB_CCR4L = ticksL;
		break;
#endif
	}
	
	DISABLE_EXTENDED_SFR()
}

void pwmConfigureFaultDetection(
	PWM_Counter counter, 
	PWM_FaultTrigger faultTrigger, 
	PWM_Polarity faultPolarity, 
	PWM_FaultResponse faultResponse, 
	PWM_FaultResume faultResume, 
	InterruptEnable enableInterrupt
) {
	uint8_t pinSwitch = (faultTrigger == PWM_FAULT_COMPARATOR) ? 1 : 0;
	uint8_t pin = __faultPinConfigurations[pinSwitch][counter];
	configurePin(pin, GPIO_HIGH_IMPEDANCE_MODE);
	uint8_t bkr = 
		M_MOE
		| (faultResume << P_AOE)
		| (faultPolarity << P_BKP)
		| M_BKE
		| (faultResponse << P_OSSI);
	
	ENABLE_EXTENDED_SFR()
	
#if HAL_PWM_CHANNELS > 4
	switch (counter) {
	case PWM_COUNTER_A:
#endif
		PWMA_ETRPS = (PWMA_ETRPS & ~M_BRK_PS) | (pinSwitch << P_BRK_PS);
		PWMA_BKR = bkr;
		
		if (enableInterrupt == ENABLE_INTERRUPT) {
			PWMA_IER |= M_BIE;
		}
#if HAL_PWM_CHANNELS > 4
		break;
	case PWM_COUNTER_B:
		PWMB_ETRPS = (PWMB_ETRPS & ~M_BRK_PS) | (pinSwitch << P_BRK_PS);
		PWMB_BKR = bkr;
		
		if (enableInterrupt == ENABLE_INTERRUPT) {
			PWMB_IER |= M_BIE;
		}
		break;
	}
#endif
	
	DISABLE_EXTENDED_SFR()
}

void pwmConfigureExternalTrigger(
	PWM_Counter counter, 
	uint8_t pinSwitch, 
	PWM_TriggerEdge triggerEdge,
	PWM_ExternalClock externalClock,
	PWM_TriggerPrescaler prescaler,
	PWM_Filter filter,
	InterruptEnable enableInterrupt
) {
	if (pinSwitch >= PIN_CONFIG_MAX) {
		pinSwitch = 0;
	}
	
	uint8_t pin = __triggerPinConfigurations[pinSwitch][counter];
	
	if (pin != UNSUPPORTED_PIN_SWITCH) {
		configurePin(pin, GPIO_HIGH_IMPEDANCE_MODE);
		uint8_t etr = 
			(triggerEdge << P_ETP)
			| (externalClock << P_ECE)
			| (prescaler << P_ETPS)
			| (filter << P_ETF);
		
		ENABLE_EXTENDED_SFR()
		
#if HAL_PWM_CHANNELS > 4
		switch (counter) {
		case PWM_COUNTER_A:
#endif
			PWMA_ETRPS = (PWMA_ETRPS & ~M_ETR_PS) | (pinSwitch << P_ETR_PS);
			PWMA_ETR = etr;
			
			if (enableInterrupt == ENABLE_INTERRUPT) {
				PWMA_IER |= M_TIE;
			}
#if HAL_PWM_CHANNELS > 4
			break;
		case PWM_COUNTER_B:
			PWMB_ETRPS = (PWMB_ETRPS & ~M_ETR_PS) | (pinSwitch << P_ETR_PS);
			PWMB_ETR = etr;
			
			if (enableInterrupt == ENABLE_INTERRUPT) {
				PWMB_IER |= M_TIE;
			}
			break;
		}
#endif
		
		DISABLE_EXTENDED_SFR()
	}
}

void pwmInitialiseQuadratureEncoder(
	PWM_Channel firstChannel, 
	uint8_t pinSwitch, 
	PWM_Polarity polarity, 
	PWM_Filter filter
) {
#if HAL_PWM_CHANNELS > 4
	PWM_Counter counter = (firstChannel >= PWM_Channel4) ? PWM_COUNTER_B : PWM_COUNTER_A;
#else
	PWM_Counter counter = PWM_COUNTER_A;
#endif
	PWM_Channel secondChannel = firstChannel + 1;
	
	switch (firstChannel) {
	case PWM_Channel1:
#if HAL_PWM_CHANNELS > 2
	case PWM_Channel3:
#endif
#if HAL_PWM_CHANNELS > 4
	case PWM_Channel5:
	case PWM_Channel7:
#endif
		// Both channels must belong to the same pair.
		secondChannel = firstChannel - 1;
		break;
	}
	
	pwmConfigureCounter(
		counter, 
		0, // Prescaler
		0xffffUL, // Auto-reload
		PWM_QUADRATURE_ENCODER, 
		PWM_NO_TRIGGER,
		0, // Repeat counter
		PWM_IMMEDIATE_UPDATE,
		PWM_CONTINUOUS,
		PWM_EDGE_ALIGNED_UP,
		PWM_DISABLE_ALL_UE,
		DISABLE_INTERRUPT
	);

	pwmConfigureInput(
		firstChannel, 
		pinSwitch,
		polarity,
		PWM_CAPTURE_SAME_PIN,
		filter
	);

	pwmConfigureInput(
		secondChannel, 
		pinSwitch,
		polarity,
		PWM_CAPTURE_SAME_PIN,
		filter
	);
	
	channelUsage[firstChannel] = PWM_CHANNEL;
	channelUsage[secondChannel] = PWM_CHANNEL;
	
	enableChannelInterrupt(firstChannel);
	
	pwmEnableCounter(counter);
}

void pwmInitialiseCapture(
	PWM_Channel channel, 
	uint8_t pinSwitch, 
	PWM_Polarity polarity, 
	PWM_Filter filter
) {
	pwmConfigureInput(
		channel, 
		pinSwitch,
		polarity,
		PWM_CAPTURE_SAME_PIN,
		filter
	);

	channelUsage[channel] = CAPTURE_CHANNEL;
	enableChannelInterrupt(channel);
}

INTERRUPT(pwmA_isr, PWMA_INTERRUPT) {
	uint8_t channel = 255;
	uint8_t event = 255;
	
	ENABLE_EXTENDED_SFR()
	
	if (PWMA_SR1 & M_CC1IF) {
		PWMA_SR1 &= ~M_CC1IF;
		channel = PWM_Channel0;
	}
	
#if HAL_PWM_CHANNELS > 1
	if (PWMA_SR1 & M_CC2IF) {
		PWMA_SR1 &= ~M_CC2IF;
		channel = PWM_Channel1;
	}
#endif
	
#if HAL_PWM_CHANNELS > 2
	if (PWMA_SR1 & M_CC3IF) {
		PWMA_SR1 &= ~M_CC3IF;
		channel = PWM_Channel2;
	}
	
	if (PWMA_SR1 & M_CC4IF) {
		PWMA_SR1 &= ~M_CC4IF;
		channel = PWM_Channel3;
	}
#endif
	
	if (PWMA_SR1 & M_TIF) {
		PWMA_SR1 &= ~M_TIF;
		event = PWM_INTERRUPT_TRIGGER;
	}
	
	if (PWMA_SR1 & M_COMIF) {
		PWMA_SR1 &= ~M_COMIF;
		event = PWM_INTERRUPT_COM;
	}
	
	if (PWMA_SR1 & M_UIF) {
		PWMA_SR1 &= ~M_UIF;
		event = PWM_INTERRUPT_UPDATE;
	}
	
	if (PWMA_SR1 & M_BIF) {
		// Note: the brake interrupt flag can only be cleared
		// if the brake input is inactive, so it may have to
		// be cleared outside of the ISR.
		PWMA_SR1 &= ~M_BIF;
		event = PWM_INTERRUPT_FAULT;
	}
	
	if (channel != 255) {
		if (channelUsage[channel] == PWM_CHANNEL) {
			pwmOnChannelInterrupt(channel, 0, 0);
		} else {
			uint16_t counterValue = 0;
			
			switch (channel) {
			case PWM_Channel0:
				counterValue = PWMA_CCR1H << 8;
				counterValue |= PWMA_CCR1L;
				break;
#if HAL_PWM_CHANNELS > 1
			case PWM_Channel1:
				counterValue = PWMA_CCR2H << 8;
				counterValue |= PWMA_CCR2L;
				break;
#endif
#if HAL_PWM_CHANNELS > 2
			case PWM_Channel2:
				counterValue = PWMA_CCR3H << 8;
				counterValue |= PWMA_CCR3L;
				break;
			case PWM_Channel3:
				counterValue = PWMA_CCR4H << 8;
				counterValue |= PWMA_CCR4L;
				break;
#endif
			}
			
			uint8_t countDown = PWMA_CR1 & M_DIR;
			pwmOnChannelInterrupt(channel, counterValue, countDown);
		}
	}
	
	if (event != 255) {
		pwmOnCounterInterrupt(PWM_COUNTER_A, event);
	}
	
	DISABLE_EXTENDED_SFR()
}

#if HAL_PWM_CHANNELS > 4
INTERRUPT(pwmB_isr, PWMB_INTERRUPT) {
	uint8_t channel = 255;
	uint8_t event = 255;
	
	ENABLE_EXTENDED_SFR()
	
	if (PWMB_SR1 & M_CC5IF) {
		PWMB_SR1 &= ~M_CC5IF;
		channel = PWM_Channel4;
	}
	
	if (PWMB_SR1 & M_CC6IF) {
		PWMB_SR1 &= ~M_CC6IF;
		channel = PWM_Channel5;
	}
	
	if (PWMB_SR1 & M_CC7IF) {
		PWMB_SR1 &= ~M_CC7IF;
		channel = PWM_Channel6;
	}
	
	if (PWMB_SR1 & M_CC8IF) {
		PWMB_SR1 &= ~M_CC8IF;
		channel = PWM_Channel7;
	}
	
	if (PWMB_SR1 & M_TIF) {
		PWMB_SR1 &= ~M_TIF;
		event = PWM_INTERRUPT_TRIGGER;
	}
	
	if (PWMB_SR1 & M_COMIF) {
		PWMB_SR1 &= ~M_COMIF;
		event = PWM_INTERRUPT_COM;
	}
	
	if (PWMB_SR1 & M_UIF) {
		PWMB_SR1 &= ~M_UIF;
		event = PWM_INTERRUPT_UPDATE;
	}
	
	if (PWMB_SR1 & M_BIF) {
		// Note: the brake interrupt flag can only be cleared
		// if the brake input is inactive, so it may have to
		// be cleared outside of the ISR.
		PWMB_SR1 &= ~M_BIF;
		event = PWM_INTERRUPT_FAULT;
	}
	
	if (channel != 255) {
		if (channelUsage[channel] == PWM_CHANNEL) {
			pwmOnChannelInterrupt(channel, 0, 0);
		} else {
			uint16_t counterValue = 0;
			
			switch (channel) {
			case PWM_Channel4:
				counterValue = PWMB_CCR1H << 8;
				counterValue |= PWMB_CCR1L;
				break;
			case PWM_Channel5:
				counterValue = PWMB_CCR2H << 8;
				counterValue |= PWMB_CCR2L;
				break;
			case PWM_Channel6:
				counterValue = PWMB_CCR3H << 8;
				counterValue |= PWMB_CCR3L;
				break;
			case PWM_Channel7:
				counterValue = PWMB_CCR4H << 8;
				counterValue |= PWMB_CCR4L;
				break;
			}
			
			uint8_t countDown = PWMB_CR1 & M_DIR;
			pwmOnChannelInterrupt(channel, counterValue, countDown);
		}
	}
	
	if (event != 255) {
		pwmOnCounterInterrupt(PWM_COUNTER_B, event);
	}
	
	DISABLE_EXTENDED_SFR()
}
#endif
