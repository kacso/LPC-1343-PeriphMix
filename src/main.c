/*
===============================================================================
 Name        : Peripheral Mix
 Author      : Danijel Sokač
 Version     : 1.3
 Copyright   : Copyright (C)
 Description : main definition
===============================================================================
*/

#include "LPC13xx.h"
#include <cr_section_macros.h>
#include <NXP/crp.h>
#include "gpio.h"
#include "i2c.h"
#include "ssp.h"
#include "type.h"
#include "timer32.h"
#include "timer16.h"

#include "adc.h" //za trim
#include "pca9532.h"
#include "acc.h"
#include "light.h"
#include "oled.h"
#include "temp.h"
#include "led7seg.h"
#include "rotary.h"
#include "rgb.h"

__CRP const unsigned int CRP_WORD = CRP_NO_CRP ;

#define stdMaxTemp 125
#define stdMinTemp -40
#define P1_2_HIGH() (LPC_GPIO1->DATA |= (0x1<<2))
#define P1_2_LOW()  (LPC_GPIO1->DATA &= ~(0x1<<2))
#define JOYSTICK_CENTER 0x01
#define JOYSTICK_UP     0x02
#define JOYSTICK_DOWN   0x04
#define JOYSTICK_LEFT   0x08
#define JOYSTICK_RIGHT  0x10
#define g_1 0x3F
#define maxScreen  7

static uint32_t msTicks = 0;
static uint8_t buf[15];
static const char *Ascii = "0123456789";
volatile int32_t min_temp_mjer, max_temp_mjer;
volatile uint32_t min_light_mjer, max_light_mjer;
volatile int8_t hh = 0, mm = 0, ss = 0;
volatile uint8_t joy = 0, sw3 = 1, screen = 1, exscreen = 0, clk_flag = 32; /*  change |  hh  |  mm  | ss |  dan | mj | god |
							                               	  	  	  	          6    |  5   |   4  |  3 |   2  | 1  |  0  |*/
volatile uint8_t alarm_flag = 4; /* on/off | - |  h  | min | sec |
				               	       4   | 3 |  2  |  1  |  0  |*/
volatile uint8_t dan = 1, mj = 1, pgod = 1;
volatile uint16_t god = 2012;


static uint32_t notes[] = {
        2272, // A - 440 Hz
        2024, // B - 494 Hz
        3816, // C - 262 Hz
        3401, // D - 294 Hz
        3030, // E - 330 Hz
        2865, // F - 349 Hz
        2551, // G - 392 Hz
        1136, // a - 880 Hz
        1012, // b - 988 Hz
        1912, // c - 523 Hz
        1703, // d - 587 Hz
        1517, // e - 659 Hz
        1432, // f - 698 Hz
        1275, // g - 784 Hz
};

static uint8_t *song = (uint8_t *)"C2,d2+";

static void playNote(uint32_t note, uint32_t durationMs) {

    uint32_t t = 0;

    if (note > 0) {

        while (t < (durationMs*1000)) {
            P1_2_HIGH();
            //delay32Us(1, note / 2);
            delayMs(1, note/1000);

            P1_2_LOW();
            //delay32Us(1, note / 2);
            delayMs(1, note/1000);

            t += note;
        }

    }
    else {
        //delay32Ms(1, durationMs);
    	delayMs(1, durationMs);
    }
}

static uint32_t getNote(uint8_t ch)
{
    if (ch >= 'A' && ch <= 'G')
        return notes[ch - 'A'];

    if (ch >= 'a' && ch <= 'g')
        return notes[ch - 'a' + 7];

    return 0;
}

static uint32_t getDuration(uint8_t ch)
{
    if (ch < '0' || ch > '9')
        return 400;

    /* number of ms */

    return (ch - '0') * 200;
}

static uint32_t getPause(uint8_t ch)
{
    switch (ch) {
    case '+':
        return 0;
    case ',':
        return 5;
    case '.':
        return 20;
    case '_':
        return 30;
    default:
        return 5;
    }
}


static void playSong(uint8_t *song) {
    uint32_t note = 0;
    uint32_t dur  = 0;
    uint32_t pause = 0;

    /*
     * A song is a collection of tones where each tone is
     * a note, duration and pause, e.g.
     *
     * "E2,F4,"
     */

    while(*song != '\0') {
        note = getNote(*song++);
        if (*song == '\0')
            break;
        dur  = getDuration(*song++);
        if (*song == '\0')
            break;
        pause = getPause(*song++);

        playNote(note, dur);
        //delay32Ms(1, pause);
        delayMs(1, pause);
    }
}

/*===============================Konverzija int-a u string=====================================*/

void IntToString(int32_t value, uint8_t* pbuf, uint32_t length){
	uint32_t pos = 0;
	int tmp = value;
	if (!pbuf)
		return;

	if (value < 0){
		pbuf[pos] = '-';
		value = -value;
	}

	pos = length;
	pbuf[pos] = '\0';

	do{
		pbuf[--pos] = Ascii[value % 10];
		value /= 10;
	}while(value && pos);

	while(pos > 0){
		if(--pos)
			pbuf[pos] = ' ';
		else if (tmp >= 0)
			pbuf[pos] = ' ';
	}
}

/*=========================Konverzija float to string ===================================================*/
void FloatToString(float value, uint8_t* pbuf, uint32_t length, uint8_t br_dec){
	uint32_t pos = 0, mul = 1;
	uint8_t i;
	int tmp;

	if (!pbuf)
		return;

	if(br_dec){
		mul *= 10;
		for(i = 1; i < br_dec; i++)
			mul *= 10;
	}
	tmp = value * mul;

	if (value < 0){
		pbuf[pos] = '-';
		tmp = -tmp;
	}
	pos = length;
	pbuf[pos] = '\0';
	for(i = 0; i < br_dec; i++){
		pbuf[--pos] = Ascii[tmp % 10];
		tmp /= 10;
	}
	pbuf[--pos] = '.';
	do{
		pbuf[--pos] = Ascii[tmp % 10];
		tmp /= 10;
	}while(tmp);

	while(pos > 0){
		if(--pos)
			pbuf[pos] = ' ';
		else if (value >= 0)
			pbuf[pos] = ' ';
	}
}

/*===============================Clock string================================================*/
void clockString (int8_t h, int8_t min, int8_t sec, uint8_t *pbuf){
	uint8_t pos = 8;

	if (!pbuf)
		return;

	pbuf[pos--] = '\0';
	pbuf[pos--] = Ascii[sec % 10];
	sec /= 10;
	pbuf[pos--] = Ascii[sec % 10];
	pbuf[pos--] = ':';
	pbuf[pos--] = Ascii[min % 10];
	min /= 10;
	pbuf[pos--] = Ascii[min % 10];
	pbuf[pos--] = ':';
	pbuf[pos--] = Ascii[h % 10];
	h /=10;
	pbuf[pos] = Ascii[h % 10];
}
/*===============================Date to string================================================*/
void dateString (uint8_t pdan, uint8_t pmj, uint16_t pgod, uint8_t * pbuf){
	uint8_t pos = 11, i;

	if (!pbuf)
		return;

	pbuf[pos--] = '\0';
	pbuf[pos--] = '.';
	for(i = 0; i < 4; i++){
		pbuf[pos--] = Ascii[pgod % 10];
		pgod /= 10;
	}
	pbuf[pos--] = '.';
	pbuf[pos--] = Ascii[pmj % 10];
	pmj /= 10;
	pbuf[pos--] = Ascii[pmj % 10];
	pbuf[pos--] = '.';
	pbuf[pos--] = Ascii[pdan % 10];
	pdan /= 10;
	pbuf[pos] = Ascii[pdan % 10];
}

/*===============================Led Skala===================================================*/
void ledScale (int value, int max, int min, uint8_t led, uint8_t maxLed){
	int div;
	uint8_t i, brLed;
	uint16_t ledOn, ledOff;

	if (maxLed > 8) return;

	div = (max - min + 1) / maxLed;
	brLed = (value - min) / div + 1;

	if ((brLed < 0) || (value - min) < 0) brLed = 0;
	if (brLed > maxLed) brLed = maxLed;

	if (led == 'y'){			//svijetle žute ledice
		if (!brLed) ledOn = 0;
		else ledOn = 0x8000;
		for (i = 1; i < brLed; i++){
			ledOn |= ledOn >> 1;
		}
		ledOff = ~ledOn & 0xFF00;
	}
	else if (led == 'r'){		//svijetle crvene ledice
		if (!brLed) ledOn = 0;
		else ledOn = 0x01;
		for (i = 1; i < brLed; i++){
			ledOn |= ledOn << 1;
		}
		ledOff = ~ledOn & 0x00FF;
	}
	else return;

	pca9532_setLeds(ledOn, ledOff);
}

void SysTick_Handler(void) {
    msTicks++;
}

static uint32_t getTicks(void)
{
    return msTicks;
}

void  TIMER32_1_IRQHandler(void){
	ss++;
	if(ss >= 60){
		ss = 0;
		mm++;
		if(mm >= 60){
			mm = 0;
			hh++;
			if(hh >= 24){
				hh = 0;
				dan++;
				if (dan > 31){
					dan = 1;
					mj++;
				}
				else if (dan > 30){
					if((mj == 4) || (mj == 6) || (mj == 9) || (mj == 11)){
						dan = 1;
						mj++;
					}
				}
				else if(mj == 2){
					if (((dan > 28) && !pgod) || (dan > 29)){
						dan = 1;
						mj++;
					}
				}
				if(mj > 12){
					mj = 1;
					god++;
					if((!(god % 4) && (god % 100)) || !(god % 400))
						pgod = 1;
					else
						pgod = 0;
				}
			}
		}
	}
	LPC_TMR32B1->IR = 1;			// clear interrupt flag
}

void PIOINT2_IRQHandler(void){
		/*Joystick Center*/
	if(GPIOIntStatus(PORT2, 0)){
		if ((screen == 6) && (exscreen != 0)){
			if(clk_flag & 64){
				clk_flag &= ~64;
				enable_timer32(1);
			}
			else{
				clk_flag |= 64;
				reset_timer32(1);
				disable_timer32(1);
			}
		}
		else if ((screen == 7) && (exscreen != 0)){
			if (alarm_flag & 16){
				oled_fillRect(5, 20, 23, 26, OLED_COLOR_WHITE);
				oled_putString(5, 20, (uint8_t *) "Off", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				alarm_flag &= ~16;
			}
			else{
				oled_fillRect(5, 20, 23, 26, OLED_COLOR_WHITE);
				oled_putString(5, 20, (uint8_t *) "On", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				alarm_flag |= 16;
			}

		}
		GPIOIntClear(PORT2, 0);
	}
		/*Joystick Down*/
	else if(GPIOIntStatus(PORT2, 1)){
		if(GPIOGetValue(PORT2, 1))
			joy &= ~JOYSTICK_DOWN;
		else
			joy |= JOYSTICK_DOWN;
		GPIOIntClear(PORT2, 1);
	}
		/*Joystick Right*/
	else if(GPIOIntStatus(PORT2, 2)){
		if(GPIOGetValue(PORT2, 2))
			joy &= ~JOYSTICK_RIGHT;
		else
			joy |= JOYSTICK_RIGHT;
		GPIOIntClear(PORT2, 2);
	}
		/*Joystick Up*/
	else if(GPIOIntStatus(PORT2, 3)){
		if(GPIOGetValue(PORT2, 3))
			joy &= ~JOYSTICK_UP;
		else
			joy |= JOYSTICK_UP;
		GPIOIntClear(PORT2, 3);
	}
		/*Joystick Left*/
	else if(GPIOIntStatus(PORT2, 4)){
		if(GPIOGetValue(PORT2, 4))
			joy &= ~JOYSTICK_LEFT;
		else
			joy |= JOYSTICK_LEFT;
		GPIOIntClear(PORT2, 4);
	}
}

void PIOINT0_IRQHandler(void){
	if(GPIOIntStatus(PORT0, 1)){
		sw3 = GPIOGetValue(PORT0, 1);
		GPIOIntClear(PORT0, 1);
	}
}

/*================================Glavni program==========================================*/
int main(void) {
	int8_t x, y, z, xoff, yoff, zoff, ex_x, ex_y, ex_z;
	int8_t alarm_h = 0, alarm_min = 0, alarm_sec = 0, rgb = 5;
	uint32_t light, exlight;
	int32_t temp, extemp, trim, extrim, maxLight = 64000, minLight = 0, exMaxLight, exMinLight;
	float maxTemp = stdMaxTemp, minTemp = stdMinTemp, exMaxTemp, exMinTemp;
	uint8_t led7seg_cnt = '0';//, screenMax = 7;
	uint8_t ctrl_temp = 0, ctrl_light  = 0, rot;
	uint8_t mjer_flag = 0; /*  min_temp | max_temp | min_light | max_light|
	 	 	 	 	 	 	 	  3     |     2    |     1     |     0    |*/


	/*======================================================================================
	**
	**maxTemp = 40;
	**minTemp = -10;
	**======================================================================================*/

	/*===========================Inicijalizacija komponenti=================================*/
	GPIOInit();
	I2CInit((uint32_t)I2CMASTER, 0);
	SSPInit();
	pca9532_init();
	ADCInit(ADC_CLK);
	acc_init();
	oled_init();
	led7seg_init();
	led7seg_setChar(led7seg_cnt, FALSE);
	init_timer32(1, SystemCoreClock);		//interrupt svake sekunde...timer interval?
											//SystemCoreClock/1 -> sec, SystemCoreClock/1000 -> msec
	init_timer16(1, 10);

	temp_init(&getTicks);

	/* setup sys Tick. Elapsed time is e.g. needed by temperature sensor */
	SysTick_Config(SystemCoreClock / 1000);
	if ( !(SysTick->CTRL & (1<<SysTick_CTRL_CLKSOURCE_Msk)) )
	{
	      /* When external reference clock is used(CLKSOURCE in
	      Systick Control and register bit 2 is set to 0), the
	      SYSTICKCLKDIV must be a non-zero value and 2.5 times
	      faster than the reference clock.
	      When core clock, or system AHB clock, is used(CLKSOURCE
	      in Systick Control and register bit 2 is set to 1), the
	      SYSTICKCLKDIV has no effect to the SYSTICK frequency. See
	      more on Systick clock and status register in Cortex-M3
	  	  technical Reference Manual. */
		  LPC_SYSCON->SYSTICKCLKDIV = 0x08;
	}

		/*Joystick center*/
	GPIOSetDir( PORT2, 0, 0 );
	GPIOSetInterrupt(PORT2, 0, 0, 0, 1);
	GPIOIntEnable(PORT2, 0);
		/*Joystick Down*/
	GPIOSetDir( PORT2, 1, 0 );
	GPIOSetInterrupt(PORT2, 1, 0, 1, 1);
	GPIOIntEnable(PORT2, 1);
		/*Joystick right*/
	GPIOSetDir( PORT2, 2, 0 );
	GPIOSetInterrupt(PORT2, 2, 0, 1, 1);
	GPIOIntEnable(PORT2, 2);
		/*Joystick Up*/
	GPIOSetDir( PORT2, 3, 0 );
	GPIOSetInterrupt(PORT2, 3, 0, 1, 1);
	GPIOIntEnable(PORT2, 3);
		/*Joystick left*/
	GPIOSetDir( PORT2, 4, 0 );
	GPIOSetInterrupt(PORT2, 4, 0, 1, 1);
	GPIOIntEnable(PORT2, 4);

	light_enable();
	light_setWidth(LIGHT_WIDTH_16BITS);
	light_setRange(LIGHT_RANGE_64000);

	min_light_mjer = max_light_mjer = light =  light_read();
	min_temp_mjer = max_temp_mjer = temp = temp_read();

	rotary_init();
	rgb_init();
	rgb_setLeds(rgb);
	acc_read(&xoff, &yoff, &zoff);
		/*sw3*/
	GPIOSetDir(PORT0, 1, 0);
	GPIOSetInterrupt(PORT0, 1, 0, 1, 1);
	GPIOIntEnable(PORT0, 1);

		/*Speaker*/
	GPIOSetDir( PORT1, 9, 1 );
	GPIOSetDir( PORT1, 10, 1 );

	GPIOSetDir( PORT3, 0, 1 );
	GPIOSetDir( PORT3, 1, 1 );
	GPIOSetDir( PORT3, 2, 1 );
	GPIOSetDir( PORT1, 2, 1 );

	LPC_IOCON->JTAG_nTRST_PIO1_2 = (LPC_IOCON->JTAG_nTRST_PIO1_2 & ~0x7) | 0x01;

	GPIOSetValue( PORT3, 0, 0 );  //LM4811-clk
	GPIOSetValue( PORT3, 1, 0 );  //LM4811-up/dn
	GPIOSetValue( PORT3, 2, 0 );  //LM4811-shutdn

	NVIC_SetPriority(TIMER_32_1_IRQn, 0);  //najveci prioritet
	enable_timer32(1);
	trim = ADCRead(0);

	while (1){
		/*=======================Čitanje stanja komponenti============================*/
		extemp = temp;
		exlight = light;
		extrim = trim;
		ex_x = x;
		ex_y = y;
		ex_z = z;
		exMaxTemp = maxTemp;
		exMinTemp = minTemp;
		exMaxLight = maxLight;
		exMinLight = minLight;

		temp = temp_read();
		light = light_read();
		trim = ADCRead(0);
		acc_read(&x, &y, &z);
	/*	x -= xoff;
		y -= yoff;
		z -= zoff;*/

		/*=============================Alarm se ukljucuje=====================================*/
		if((alarm_h == hh) && (alarm_min == mm) && (alarm_sec == ss) && (alarm_flag & 16)){
			oled_clearScreen(OLED_COLOR_WHITE);
			oled_putString(24, 3, (uint8_t *) "ALARM ON" , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			exscreen = 0;
				/*Blink leds*/
			pca9532_setLeds(0, 0xFFFF);
			pca9532_setBlink0Period(148);
			pca9532_setBlink0Duty(60);
			pca9532_setBlink0Leds(0xFFFF);

			while (sw3){
				clockString(hh, mm, ss, buf);
				oled_putString(24, 28, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				dateString(dan, mj, god, buf);
				oled_putString(15, 48, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				playSong((uint8_t*)song);
			}
		}

		/*============================Temperatura veća od maksimale============================*/
		if (temp > (10 * maxTemp)){
			oled_clearScreen(OLED_COLOR_WHITE);
			exscreen = 0;

				/*Temp: _______°C*/
			oled_putString (2, 6, (uint8_t *) "Temp:", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			FloatToString((temp/10.f), buf, 5, 1);
			oled_putString ((6 + 5*6), 6, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			oled_circle(74, 7, 1, OLED_COLOR_BLACK);						// ° na OLED-u
			oled_putChar(76, 6, 'C', OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				/*Max Temp: ______°C*/
			oled_putString (1, 17, (uint8_t *) "Max Temp:", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			FloatToString(maxTemp, buf, 5, 1);
			oled_putString ((9*6), 17, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			oled_circle((1 + 14*6), 18, 1, OLED_COLOR_BLACK);
			oled_putChar((3 + 14*6), 17, 'C', OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				/*WARNING!!!! Temperatura je veca od max!*/
			oled_putString(14, 29, (uint8_t *)"WARNING!!!!", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			oled_putString(5, 40, (uint8_t *)"Temperatura je", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			oled_putString(10, 49, (uint8_t *)"veca od max!", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				/*Blink leds*/
			pca9532_setLeds(0, 0xFFFF);
			pca9532_setBlink0Period(148);
			pca9532_setBlink0Duty(60);
			pca9532_setBlink0Leds(0xFFFF);

			while ((temp = temp_read()) > (10 * maxTemp)){

				if(temp != extemp){
					FloatToString((temp/10.f), buf, 5, 1);
					oled_putString ((6 + 5*6), 6, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				}
				oled_fillRect(14, 29, (14 + 11*6), 35, OLED_COLOR_WHITE);
				playSong((uint8_t*)song);
				oled_putString(14, 29, (uint8_t *)"WARNING!!!!", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			}
		}

		/*===========================Temperatura manja od minimalne============================*/
		else if (temp < (10 * minTemp)){
			oled_clearScreen(OLED_COLOR_WHITE);
			exscreen = 0;

				/*Temp: _____°C*/
			oled_putString (2, 6, (uint8_t *) "Temp:", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			FloatToString((temp/10.f), buf, 5, 1);
			oled_putString ((6 + 5*6), 6, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			oled_circle(72, 7, 1, OLED_COLOR_BLACK);						// ° na OLED-u
			oled_putChar(74, 6, 'C', OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				/*Min Temp: _____°C*/
			oled_putString (1, 17, (uint8_t *) "Min Temp:", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			FloatToString(minTemp, buf, 5, 1);
			oled_putString ((9*6), 17, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			oled_circle((1 + 14*6), 18, 1, OLED_COLOR_BLACK);
			oled_putChar((3 + 14*6), 17, 'C', OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				/*WARNING!!!! Temperatura je manja od min!*/
			oled_putString(14, 29, (uint8_t *)"WARNING!!!!", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			oled_putString(5, 40, (uint8_t *)"Temperatura je", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			oled_putString(10, 49, (uint8_t *)"manja od min!", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				/*Blink leds*/
			pca9532_setLeds(0, 0xFFFF);
			pca9532_setBlink0Period(148);
			pca9532_setBlink0Duty(60);
			pca9532_setBlink0Leds(0xFFFF);

			while ((temp = temp_read()) < (10 * minTemp)){

				if(temp != extemp){
					FloatToString((temp/10.f), buf, 5, 1);
					oled_putString ((6 + 5*6), 6, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				}
				oled_fillRect(14, 29, (14 + 11*6), 35, OLED_COLOR_WHITE);
				playSong((uint8_t*)song);
				oled_putString(14, 29, (uint8_t *)"WARNING!!!!", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			}
		}

		/*==========================Prikaz temperature i svjetlosti na ledicama=======================*/
		ledScale(temp, 10 * maxTemp, 10 * minTemp, 'r', 8);
		ledScale(light, maxLight, minLight, 'y', 8);

		/*==========================Promjena stanja 7 segmentnog pokazivača============================*/
		if(((y - yoff) < -9) && sw3){
			led7seg_cnt--;
			if(led7seg_cnt < '0') led7seg_cnt = '9';
			led7seg_setChar(led7seg_cnt, FALSE);
		}
		else if(((y - yoff) > 9) && sw3){
			led7seg_cnt++;
			if(led7seg_cnt > '9') led7seg_cnt = '0';
			led7seg_setChar(led7seg_cnt, FALSE);
		}

		/*============================Rotary mijenja boju rgb=========================================*/
		if((rot = rotary_read()) != ROTARY_WAIT){
			if (rot == ROTARY_RIGHT){
				if (++rgb > 7)
					rgb = 7;
			}
			else if (--rgb < 0)
				rgb = 0;

			rgb_setLeds(rgb);
		}

		/*=====================Promjena max i min temp i light - mjereno===========================*/
		if (light > max_light_mjer){
			max_light_mjer = light;
			mjer_flag |= 1;
		}
		else if (light < min_light_mjer){
			min_light_mjer = light;
			mjer_flag |= 2;
		}
		if (temp > max_temp_mjer){
			max_temp_mjer = temp;
			mjer_flag |= 4;
		}
		else if (temp < min_temp_mjer){
			min_temp_mjer = temp;
			mjer_flag |= 8;
		}

		/*===============================Promjena ekrana============================================*/
		if(((x - xoff) < -9) && sw3){
			screen++;
			if(screen > maxScreen) screen = 1;
		}
		else if(((x - xoff) > 9) && sw3){
			screen--;
			if(screen == 0) screen = maxScreen;
		}

		/*============================Screen 1 - Temp, light, trim==================================*/
		if(screen == 1){
			if(screen != exscreen){
				oled_clearScreen(OLED_COLOR_WHITE);
				oled_rect(1,1,94,62,OLED_COLOR_BLACK);
				oled_putString(4, 6, (uint8_t *)"Temp  : ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				oled_circle(83, 7, 1, OLED_COLOR_BLACK);
				oled_putChar(85, 6, 'C', OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				oled_putString(4, 22, (uint8_t *)"Light : ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				oled_putString(4, 38, (uint8_t *)"Trim  : ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				FloatToString((temp/10.f), buf, 5, 1);
				oled_putString((2 + 8*6), 6, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				IntToString(light, buf, 5);
				oled_putString((2 + 8*6), 22, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				IntToString(trim, buf, 5);
				oled_putString((2 + 8*6), 38, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				exscreen = screen;
			}
			if(temp != extemp){
				FloatToString((temp/10.f), buf, 5, 1);
				oled_putString((2 + 8*6), 6, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			}
			if(light != exlight){
				IntToString(light, buf, 5);
				oled_putString((2 + 8*6), 22, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			}
			if(trim != extrim){
				IntToString(trim, buf, 5);
				oled_putString((2 + 8*6), 38, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			}
		}

		/*==============================Screen 2 - max Temp, min Temp=================================*/
		else if (screen == 2){

				/*Max ili min temp?*/
			if(((joy & JOYSTICK_UP) || (joy & JOYSTICK_DOWN) || (y - yoff) < -10 || (y - yoff) > 10) && sw3)
				ctrl_temp = !ctrl_temp;

				/*Pomak za 0.1 -> joystick*/
			else if((joy & JOYSTICK_RIGHT) && !ctrl_temp){
				maxTemp += 0.1;
				if (maxTemp > stdMaxTemp) maxTemp = stdMaxTemp;
			}
			else if((joy & JOYSTICK_RIGHT) && ctrl_temp){
				minTemp += 0.1;
				if (minTemp >= maxTemp) minTemp = maxTemp - 0.1;
			}
			else if((joy & JOYSTICK_LEFT) && !ctrl_temp){
				maxTemp -= 0.1;
				if (maxTemp <= minTemp) maxTemp = minTemp + 0.1;
			}
			else if((joy & JOYSTICK_LEFT) && ctrl_temp){
				minTemp -= 0.1;
				if (minTemp < stdMinTemp) minTemp = stdMinTemp;
			}

				/*Pomak za 1 -> acc*/
			else if(!sw3 && ((y - yoff) < -9) && !ctrl_temp){
				maxTemp++;
				if (maxTemp > stdMaxTemp) maxTemp = stdMaxTemp;
			}
			else if(!sw3 && ((y - yoff) < -9) && ctrl_temp){
				minTemp++;
				if (minTemp >= maxTemp) minTemp = maxTemp - 1;
			}
			else if(!sw3 && ((y - yoff) > 9) && !ctrl_temp){
				maxTemp--;
				if (maxTemp <= minTemp) maxTemp = minTemp + 1;
			}
			else if(!sw3 && ((y - yoff) > 9) && ctrl_temp){
				minTemp--;
				if (minTemp < stdMinTemp) minTemp = stdMinTemp;
			}

			if (screen != exscreen){
				oled_clearScreen (OLED_COLOR_WHITE);
				oled_rect(1,1,94,62,OLED_COLOR_BLACK);
				oled_putString(13, 3, (uint8_t *)"Temperatura", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				oled_putString(6, 15, (uint8_t *)"Max: ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				oled_circle(10*6 + 4, 16, 1, OLED_COLOR_BLACK);
				oled_putChar(10*6 + 6, 15, 'C', OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				oled_putString(6, 35, (uint8_t *)"Min: ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				oled_circle(10*6 + 4, 36, 1, OLED_COLOR_BLACK);
				oled_putChar(10*6 + 6, 35, 'C', OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				FloatToString(maxTemp, buf, 5, 1);
				oled_putString((5*6), 15, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				FloatToString(minTemp, buf, 5, 1);
				oled_putString((5*6), 35, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				exscreen = screen;
			}
			if (!ctrl_temp){
				oled_fillRect( 2, 15, 4, 21, OLED_COLOR_BLACK);
				oled_fillRect( 2, 35, 4, 41, OLED_COLOR_WHITE);
			}
			else{
				oled_fillRect( 2, 35, 4, 41, OLED_COLOR_BLACK);
				oled_fillRect( 2, 15, 4, 21, OLED_COLOR_WHITE);
			}

			if(maxTemp != exMaxTemp){
				FloatToString(maxTemp, buf, 5, 1);
				oled_putString((5*6), 15, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			}
			if(minTemp != exMinTemp){
				FloatToString(minTemp, buf, 5, 1);
				oled_putString((5*6), 35, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			}
		}

		/*==============================Screen 3 - max Light, min Light=================================*/
		else if (screen == 3){

				/*Max ili min light?*/
			if(((joy & JOYSTICK_UP) || (joy & JOYSTICK_DOWN) || (y - yoff) < -10 || (y - yoff) > 10) && sw3)
				ctrl_light = !ctrl_light;

				/*Pomak za 10 -> joystick*/
			else if((joy & JOYSTICK_RIGHT) && !ctrl_light){
				maxLight += 10;
				if(maxLight > 64000) maxLight = 64000;
			}
			else if((joy & JOYSTICK_RIGHT) && ctrl_light){
				minLight += 10;
				if (minLight >= maxLight) minLight = maxLight - 10;
			}
			else if((joy & JOYSTICK_LEFT) && !ctrl_light){
				maxLight -= 10;
				if (maxLight <= minLight) maxLight = minLight + 10;
			}
			else if((joy & JOYSTICK_LEFT) && ctrl_light){
				minLight -= 10;
				if(minLight < 0) minLight = 0;
			}

				/*Pomak za 1000 -> acc*/
			else if(!sw3 && ((y - yoff) < -9) && !ctrl_light){
				maxLight += 1000;
				if(maxLight > 64000) maxLight = 64000;
			}
			else if(!sw3 && ((y - yoff) < -9) && ctrl_light){
				minLight += 1000;
				if (minLight >= maxLight) minLight = maxLight - 1000;
			}
			else if(!sw3 && ((y - yoff) > 9) && !ctrl_light){
				maxLight -= 1000;
				if (maxLight <= minLight) maxLight = minLight + 1000;
			}
			else if(!sw3 && ((y - yoff) > 9) && ctrl_light){
				minLight -= 1000;
				if(minLight < 0) minLight = 0;
			}

			if (screen != exscreen){
				oled_clearScreen (OLED_COLOR_WHITE);
				oled_rect(1,1,94,62,OLED_COLOR_BLACK);
				oled_putString(31, 3, (uint8_t *)"Light", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				oled_putString(6, 15, (uint8_t *)"Max:", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				oled_putString(6, 35, (uint8_t *)"Min:", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				IntToString(maxLight, buf, 6);
				oled_putString((6 + 4*6), 15, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				IntToString(minLight, buf, 6);
				oled_putString((6 + 4*6), 35, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				exscreen = screen;
			}
			if (!ctrl_light){
				oled_fillRect( 2, 15, 4, 21, OLED_COLOR_BLACK);
				oled_fillRect( 2, 35, 4, 41, OLED_COLOR_WHITE);
			}
			else{
				oled_fillRect( 2, 35, 4, 41, OLED_COLOR_BLACK);
				oled_fillRect( 2, 15, 4, 21, OLED_COLOR_WHITE);
			}

			if(maxLight != exMaxLight){
				IntToString(maxLight, buf, 6);
				oled_putString((6 + 4*6), 15, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			}
			if(minLight != exMinLight){
				IntToString(minLight, buf, 6);
				oled_putString((6 + 4*6), 35, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			}
		}

		/*=========================Screen 4 - min, max light i temp ->mjereno=============================*/
		else if (screen == 4){
			if (exscreen != screen){
				exscreen = screen;
				oled_clearScreen(OLED_COLOR_WHITE);
				oled_rect(1, 1, 94, 62, OLED_COLOR_BLACK);
				oled_putString(21, 3, (uint8_t *)"Izmjereno", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				oled_putString(3, 11, (uint8_t *)"Temperatura:", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				oled_putString(6, 19, (uint8_t *)"Min:", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				FloatToString((min_temp_mjer / 10.f), buf, 5, 1);
				oled_putString(29, 19, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				oled_circle(10*6 + 2, 20, 1, OLED_COLOR_BLACK);
				oled_putChar(10*6 + 4, 19, 'C', OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				oled_putString(6, 27, (uint8_t *)"Max:", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				FloatToString((max_temp_mjer / 10.f), buf, 5, 1);
				oled_putString(29, 27, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				oled_circle(10*6 + 2, 28, 1, OLED_COLOR_BLACK);
				oled_putChar(10*6 + 4, 27, 'C', OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				oled_putString(3, 35, (uint8_t *)"Light:", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				oled_putString(6, 43, (uint8_t *)"Min:", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				IntToString(min_light_mjer, buf, 6);
				oled_putString(29, 43, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				oled_putString(6, 51, (uint8_t *)"Max:", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				IntToString(max_light_mjer, buf, 6);
				oled_putString(29, 51, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			}
			if (mjer_flag & 8){
				FloatToString((min_temp_mjer / 10.f), buf, 5, 1);
				oled_putString(29, 19, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				mjer_flag &= ~8;
			}
			if (mjer_flag & 4){
				FloatToString((max_temp_mjer / 10.f), buf, 5, 1);
				oled_putString(29, 27, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				mjer_flag &= ~4;
			}
			if (mjer_flag & 2){
				IntToString(min_light_mjer, buf, 6);
				oled_putString(29, 43, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				mjer_flag &= ~2;
			}
			if (mjer_flag & 4){
				IntToString(max_light_mjer, buf, 6);
				oled_putString(29, 51, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				mjer_flag &= ~1;
			}
		}
		/*===================================Screen 5 - Acc===========================================*/
		else if(screen == 5){
			if(screen != exscreen){
				oled_clearScreen(OLED_COLOR_WHITE);
				oled_rect(1,1,94,62,OLED_COLOR_BLACK);
				oled_putString(4, 6, (uint8_t *)"Acc x : ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				oled_putString(4, 22, (uint8_t *)"Acc y : ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				oled_putString(4, 38, (uint8_t *)"Acc z : ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				oled_putString((6 + 13*6), 6, (uint8_t *)"g", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				oled_putString((6 + 13*6), 22, (uint8_t *)"g", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				oled_putString((6 + 13*6), 38, (uint8_t *)"g", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				//IntToString(x, buf, 3);
				FloatToString((x/(float)g_1), buf, 5, 2);
				oled_putString((8*6), 6, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				//IntToString(y, buf, 3);
				FloatToString((y/(float)g_1), buf, 5, 2);
				oled_putString((8*6), 22, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);

				//IntToString(z, buf, 3);
				FloatToString((z/(float)g_1), buf, 5, 2);
				oled_putString((8*6), 38, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				exscreen = screen;
			}
			if(x != ex_x){
				//IntToString(x, buf, 3);
				FloatToString((x/(float)g_1), buf, 5, 2);
				oled_putString((8*6), 6, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			}
			if(y != ex_y){
				//IntToString(y, buf, 3);
				FloatToString((y/(float)g_1), buf, 5, 2);
				oled_putString((8*6), 22, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			}
			if(z != ex_z){
				//IntToString(z, buf, 3);
				FloatToString((z/(float)g_1), buf, 5, 2);
				oled_putString((8*6), 38, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			}
		}

		/*=================================Screen 6 - Sat============================================*/
		else if (screen == 6){
			if(exscreen != screen){
				exscreen = screen;
				oled_clearScreen(OLED_COLOR_WHITE);
				oled_rect(1,1,94,62,OLED_COLOR_BLACK);
				oled_putString(39, 5, (uint8_t *)"SAT" , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
			}
			if (clk_flag & 64){
				clk_flag &= ~64;
				if (joy & JOYSTICK_LEFT){

					if((clk_flag <<= 1) > 32) clk_flag = 1;
				}
				else if (joy & JOYSTICK_RIGHT){

					if((clk_flag >>= 1) < 1) clk_flag = 32;
				}

					/*Promijeni sate*/
				if(clk_flag & 32){
					oled_fillRect( 24, 34, 35, 35, OLED_COLOR_BLACK);
					oled_fillRect( 36, 34, 72, 35, OLED_COLOR_WHITE);
					oled_fillRect(15, 56, 81, 57, OLED_COLOR_WHITE);
					if (joy & JOYSTICK_UP){
						hh++;
						if(hh >= 24) hh = 0;
					}
					else if (joy & JOYSTICK_DOWN){
						hh--;
						if (hh < 0) hh = 23;
					}
				}
					/*Promijeni minute*/
				else if(clk_flag & 16){
					oled_fillRect( 24, 34, 41, 35, OLED_COLOR_WHITE);
					oled_fillRect( 42, 34, 53, 35, OLED_COLOR_BLACK);
					oled_fillRect( 54, 34, 72, 35, OLED_COLOR_WHITE);
					oled_fillRect(15, 56, 81, 57, OLED_COLOR_WHITE);
					if (joy & JOYSTICK_UP){
						mm++;
						if (mm >= 60) mm = 0;
					}
					else if (joy & JOYSTICK_DOWN){
						mm--;
						if (mm < 0) mm = 59;
					}
				}

					/*Promijeni sekunde*/
				else if(clk_flag & 8){
					oled_fillRect( 24, 34, 59, 35, OLED_COLOR_WHITE);
					oled_fillRect( 60, 34, 71, 35, OLED_COLOR_BLACK);
					oled_fillRect(15, 56, 81, 57, OLED_COLOR_WHITE);
					if (joy & JOYSTICK_UP){
						ss++;
						if (ss >= 60) ss = 0;
					}
					else if (joy & JOYSTICK_DOWN){
						ss--;
						if (ss < 0) ss = 59;
					}
				}
					/*Promijeni dane*/
				else if (clk_flag & 4){
					oled_fillRect(24, 34, 71, 35, OLED_COLOR_WHITE);
					oled_fillRect(15, 56, 27, 57, OLED_COLOR_BLACK);
					oled_fillRect(28, 56, 81, 57, OLED_COLOR_WHITE);
					if (joy & JOYSTICK_UP){
						dan++;
						if (dan > 31){
							dan = 1;
						}
						else if (dan > 30){
							switch (mj){
							case 4:
							case 6:
							case 9:
							case 11: dan = 1; break;
							}
						}
						else if(mj == 2){
							if (((dan > 28) && !pgod) || (dan > 29)){
								dan = 1;
							}
						}
					}
					else if (joy & JOYSTICK_DOWN){
						dan--;
						if (dan < 1){
							switch (mj){
								case 4:
								case 6:
								case 9:
								case 11: dan = 30; break;
								case 2: dan = pgod ? 29 : 28; break;
								default: dan = 31; break;
							}
						}
					}
				}
					/*Promijeni mjesec*/
				else if (clk_flag & 2){
					oled_fillRect(24, 34, 71, 35, OLED_COLOR_WHITE);
					oled_fillRect(15, 56, 32, 57, OLED_COLOR_WHITE);
					oled_fillRect(33, 56, 45, 57, OLED_COLOR_BLACK);
					oled_fillRect(46, 56, 81, 57, OLED_COLOR_WHITE);
					if(joy & JOYSTICK_UP){
						mj++;
						if (mj > 12)
							mj = 1;
						else if (dan > 30){
							switch (mj){
								case 4:
								case 6:
								case 9:
								case 11: dan = 30; break;
							}
						}
						if (mj == 2){
							if((dan > 28) && !pgod)
								dan = 28;
							else if ((dan > 29))
								dan = 29;
						}
					}
					else if (joy & JOYSTICK_DOWN){
						mj--;
						if (mj < 1)
							mj = 12;
						else if (dan > 30){
							switch (mj){
								case 4:
								case 6:
								case 9:
								case 11: dan = 30; break;
							}
						}
						if (mj == 2){
							if((dan > 28) && !pgod)
								dan = 28;
							else if ((dan > 29))
								dan = 29;
						}
					}
				}
					/*Promijeni godinu*/
				else if (clk_flag & 1){
					oled_fillRect(24, 34, 71, 35, OLED_COLOR_WHITE);
					oled_fillRect(15, 56, 50, 57, OLED_COLOR_WHITE);
					oled_fillRect(51, 56, 75, 57, OLED_COLOR_BLACK);
					if (joy & JOYSTICK_UP){
						god++;
						if((!(god % 4) && (god % 100)) || !(god % 400))
							pgod = 1;
						else
							pgod = 0;
						if ((mj == 2) && !pgod && (dan > 28))
							dan = 28;
					}
					else if (joy & JOYSTICK_DOWN){
						god--;
						if((!(god % 4) && (god % 100)) || !(god % 400))
							pgod = 1;
						else
							pgod = 0;
						if ((mj == 2) && !pgod && (dan > 28))
							dan = 28;
					}
				}
				clk_flag |= 64;
			}
			else{
				oled_fillRect(24, 34, 71, 35, OLED_COLOR_WHITE);
				oled_fillRect(15, 56, 81, 57, OLED_COLOR_WHITE);
			}

			clockString(hh, mm, ss, buf);
			oled_putString(24, 26, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);

			dateString(dan, mj, god, buf);
			oled_putString(15, 48, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
		}

		/*=================================Screen 7 - Alarm============================================*/
		else if (screen == 7){
			if(exscreen != screen){
				exscreen = screen;
				oled_clearScreen(OLED_COLOR_WHITE);
				oled_rect(1,1,94,62,OLED_COLOR_BLACK);
				oled_putString(33, 5, (uint8_t *)"ALARM" , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				if (alarm_flag & 16){
					oled_fillRect(5, 20, 23, 26, OLED_COLOR_WHITE);
					oled_putString(5, 20, (uint8_t *) "On", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				}
				else{
					oled_fillRect(5, 20, 23, 26, OLED_COLOR_WHITE);
					oled_putString(5, 20, (uint8_t *) "Off", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
				}
			}

			if (joy & JOYSTICK_RIGHT){
				alarm_flag = ((alarm_flag & 7) >> 1) | (alarm_flag & 16);
				if((alarm_flag & 7) < 1) alarm_flag |= 4;
			}
			else if (joy & JOYSTICK_LEFT){
				alarm_flag = ((alarm_flag & 7) << 1) | (alarm_flag & 16);
				if(alarm_flag & 8){
					alarm_flag |= 1;
					alarm_flag &= ~8;
				}
			}

				/*Promijeni sate*/
			if(alarm_flag & 4){
				oled_fillRect( 24, 37, 35, 38, OLED_COLOR_BLACK);
				oled_fillRect( 36, 37, 72, 38, OLED_COLOR_WHITE);
				if (joy & JOYSTICK_UP){
					alarm_h++;
					if(alarm_h >= 24) alarm_h = 0;
				}
				else if (joy & JOYSTICK_DOWN){
					alarm_h--;
					if (alarm_h < 0) alarm_h = 23;
				}
			}
				/*Promijeni minute*/
			else if(alarm_flag & 2){
				oled_fillRect( 24, 37, 41, 38, OLED_COLOR_WHITE);
				oled_fillRect( 42, 37, 53, 38, OLED_COLOR_BLACK);
				oled_fillRect( 54, 37, 72, 38, OLED_COLOR_WHITE);
				if (joy & JOYSTICK_UP){
					alarm_min++;
					if (alarm_min >= 60) alarm_min = 0;
				}
				else if (joy & JOYSTICK_DOWN){
					alarm_min--;
					if (alarm_min < 0) alarm_min = 59;
				}
			}

				/*Promijeni sekunde*/
			else if(alarm_flag & 1){
				oled_fillRect( 24, 37, 59, 38, OLED_COLOR_WHITE);
				oled_fillRect( 60, 37, 71, 38, OLED_COLOR_BLACK);
				if (joy & JOYSTICK_UP){
					alarm_sec++;
					if (alarm_sec >= 60) alarm_sec = 0;
				}
				else if (joy & JOYSTICK_DOWN){
					alarm_sec--;
					if (alarm_sec < 0) alarm_sec = 59;
				}
			}

			clockString(alarm_h, alarm_min, alarm_sec, buf);
			oled_putString(24, 29, buf , OLED_COLOR_BLACK, OLED_COLOR_WHITE);
		}
	}
	return 0 ;
}
