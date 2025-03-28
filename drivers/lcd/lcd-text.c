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
#include <lcd/lcd-text.h>
#include <lcd/lcd-controller.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/**
 * @file lcd-text.c
 * 
 * Text operations for LCD devices: implementation.
 */

void lcdTxtInitialiseDisplayMode(LCDDevice *device) {
	lcdDisableGraphicsDisplay(device);
	lcdTxtClear(device);
}

void lcdTxtClear(LCDDevice *device) {
	lcdClearTextDisplay(device);
}

void lcdTxtPrintAt(LCDDevice *device, uint8_t row, uint8_t column, const char *__fmt, ...) {
	va_list vaList;
	
	// Using a static buffer avoids consuming most of our scarce stack space.
	// This choice is unlikely to have any side effect on a poor MCS-51.
	// Besides, we're unlikely to have to worry about using a static buffer 
	// on any other MCU architecture (except maybe those cursed legacy PIC).
	static char buffer[81];

	// Strings of latin characters can start at any position provided
	// their length is an even number.
	// 16-bit Chinese characters can only start at an even position.
	// For the sake of simplicity, we do not want to start at odd columns.
	column &= ~1;
	lcdSetTextDisplayPosition(device, row, column);

	// CAUTION! SDCC doesn't provide vsnprintf(), so beware of buffer 
	// overflows if you can print arbitrary long values.
	va_start(vaList, __fmt);
	vsprintf(buffer, __fmt, vaList);
	va_end(vaList);
	
	// Make sure the length of the string is an even number
	// by adding a space if it isn't.
	uint8_t l = strlen(buffer);
	
	if (l & 1) {
		buffer[l] = ' ';
		buffer[++l] = '\0';
	}
	
	// Truncate the string if it would go past the end of the row.
	uint8_t lMax = device->textWidth - column;
	lMax &= ~1;
	
	if (l > lMax) {
		buffer[lMax] = '\0';
		l = lMax;
	}

	// Now we're good to go!
	for (uint8_t i = 0; i < l; i++) {
		lcdWriteByte(device, buffer[i]);
	}
}

#define MENU_LABEL_MAX_LENGTH 18

static void __lcdTxtMenuDisplayOption(LCDMenuData *menuData, uint8_t n) {
	// static => Same trick as in lcdTxtPrintAt().
	static char buffer[MENU_LABEL_MAX_LENGTH + 3];
	uint8_t maxLabelLen = 0;
	// Values have been set by lcdTxtMenuInitialise().
	uint8_t row = menuData->menuOptions[n].row;
	uint8_t col = menuData->menuOptions[n].col;
	
	switch (menuData->displayMode) {
	case LCD_PositionnedMenu:
		if (menuData->lcdDevice->textWidth > col) {
			maxLabelLen = menuData->lcdDevice->textWidth - col;
			maxLabelLen = (maxLabelLen > 2) ? (maxLabelLen - 2) : 0;
		}
		break;
	
	case LCD_ListMenu:
	case LCD_SingleLineMenu:
		maxLabelLen = menuData->menuWidth - 2;
		break;
	}
	
	if (maxLabelLen > MENU_LABEL_MAX_LENGTH) {
		maxLabelLen = MENU_LABEL_MAX_LENGTH;
	}
	
	strncpy(buffer + 1, menuData->menuOptions[n].label, maxLabelLen);
	buffer[++maxLabelLen] = '\0';
	
	// The selected option is highlighted with reverse angle brackets.
	buffer[0] = (n == menuData->selectedOption) ? '[' : ' ';
	
	if (menuData->displayMode != LCD_PositionnedMenu) {
		// Pad menu option text with spaces up to maxLabelLen.
		while (strlen(buffer) < maxLabelLen) {
			strcat(buffer, " ");
		}
	}
	
	if (n == menuData->selectedOption) {
		strcat(buffer, "]");
	} else {
		strcat(buffer, " ");
	}
	
	lcdTxtPrintAt(menuData->lcdDevice, row, col, "%s", buffer);
}

void lcdTxtMenuInitialise(
	LCDMenuData *menuData, 
	LCDDevice *device, 
	LCDMenuOption *menuOptions, 
	uint8_t nbOptions, 
	LCDMenuDisplayMode displayMode,
	int8_t menuWidth, 
	uint8_t startRow, 
	uint8_t startCol,
	bool keepSelectedOption
) {
	menuData->lcdDevice = device;
	menuData->menuOptions = menuOptions;
	menuData->nbOptions = nbOptions;
	menuData->displayMode = displayMode;
	menuData->menuWidth = menuWidth;
	menuData->startRow = startRow;
	menuData->startCol = startCol;
	
	if (menuData->menuWidth <= 0) {
		// Autodetect menu width and calculate positions.
		menuData->menuWidth = 0;
		uint8_t optionRow = menuData->startRow;
		
		for (uint8_t i = 0; i < menuData->nbOptions; i ++) {
			if (menuData->menuOptions[i].isEnabled) {
				size_t l = strlen(menuData->menuOptions[i].label);
				
				if (l > MENU_LABEL_MAX_LENGTH) {
					l = MENU_LABEL_MAX_LENGTH;
				}
				
				if (l > menuData->menuWidth) {
					menuData->menuWidth = l;
				}
				
				switch (menuData->displayMode) {
#ifndef __SDCC
				case LCD_PositionnedMenu:
					// row and col are already set, nothing to do.
					break;
#endif // !__SDCC
				
				case LCD_ListMenu:
					menuData->menuOptions[i].row = optionRow;
					menuData->menuOptions[i].col = menuData->startCol;
					optionRow++;
					break;
				
				case LCD_SingleLineMenu:
					menuData->menuOptions[i].row = menuData->startRow;
					menuData->menuOptions[i].col = menuData->startCol;
					break;
				}
			}
		}
		
		// menuWidth includes the current option visual markers.
		menuData->menuWidth += 2;
	}
	
	if (menuData->displayMode != LCD_PositionnedMenu) {
		uint8_t availableWidth = menuData->lcdDevice->textWidth - menuData->startCol;
		
		if (menuData->menuWidth > availableWidth) {
			menuData->menuWidth = availableWidth;
		}
	}
	
	if (!keepSelectedOption) {
		// Find first enabled option.
		for (uint8_t i = 0; i < menuData->nbOptions; i ++) {
			if (menuData->menuOptions[i].isEnabled) {
				menuData->selectedOption = i;
				break;
			}
		}
		
		// Select default option if different.
		for (uint8_t i = menuData->selectedOption; i < menuData->nbOptions; i ++) {
			if (menuData->menuOptions[i].isEnabled && menuData->menuOptions[i].isDefault) {
				menuData->selectedOption = i;
				break;
			}
		}
	}
	
	if (menuData->displayMode == LCD_SingleLineMenu) {
		__lcdTxtMenuDisplayOption(menuData, menuData->selectedOption);
	} else {
		uint8_t n = 0;
		
		for (uint8_t i = 0; i < menuData->nbOptions; i ++) {
			if (menuData->menuOptions[i].isEnabled) {
				__lcdTxtMenuDisplayOption(menuData, n);
				n++;
			}
		}
	}
}

void lcdTxtMenuSelectOptionByValue(LCDMenuData *menuData, int value) {
	uint8_t previousOption = menuData->selectedOption;
	
	for (uint8_t i = 0; i < menuData->nbOptions; i ++) {
		if (menuData->menuOptions[i].isEnabled && menuData->menuOptions[i].value == value) {
			menuData->selectedOption = i;
			break;
		}
	}
	
	if (previousOption != menuData->selectedOption) {
		if (menuData->displayMode != LCD_SingleLineMenu) {
			__lcdTxtMenuDisplayOption(menuData, previousOption);
		}
		
		__lcdTxtMenuDisplayOption(menuData, menuData->selectedOption);
	}
}

void lcdTxtMenuOnChangeSelection(LCDMenuData *menuData, LCDNewSelection newSelection) {
	uint8_t previousOption = menuData->selectedOption;
	
	switch (newSelection) {
	case LCD_SelectNext:
		do {
			menuData->selectedOption++;
			
			if (menuData->selectedOption >= menuData->nbOptions) {
				menuData->selectedOption = 0;
			}
		} while (!menuData->menuOptions[menuData->selectedOption].isEnabled);
		break;
	
	case LCD_SelectPrevious:
		do {
			if (menuData->selectedOption == 0) {
				menuData->selectedOption = menuData->nbOptions;
			}
			
			menuData->selectedOption--;
		} while (!menuData->menuOptions[menuData->selectedOption].isEnabled);
		break;
	
#ifndef __SDCC
	case LCD_RefreshOption:
		break;
#endif // !__SDCC
	}
	
	if (menuData->displayMode != LCD_SingleLineMenu && newSelection != LCD_RefreshOption) {
		__lcdTxtMenuDisplayOption(menuData, previousOption);
	}
	
	__lcdTxtMenuDisplayOption(menuData, menuData->selectedOption);
}

int lcdTxtMenuOnValidateSelection(LCDMenuData *menuData) {
	return menuData->menuOptions[menuData->selectedOption].value;
}
