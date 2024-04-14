/*
 * File:   main.c
 * Author: epare
 *
 * Created on January 28, 2024, 11:05 PM
 */

#include <stdint.h>
#include <stdlib.h>
#include <xc.h>

// Configuration bits: Internal oscillator, No watchdog timer, MCLR pin is
// digital input, Power-up Timer enabled
#pragma config FOSC = INTOSCIO  // Oscillator Selection bits
#pragma config WDTE = OFF       // Watchdog Timer Enable bit
#pragma config PWRTE = ON       // Power-up Timer Enable bit
#pragma config MCLRE = OFF      // MCLR Pin Function Select bit
#pragma config CP = OFF         // Code Protection bit
// #pragma config IOC = 8MHZ   // Internal Oscillator Frequency Select
#pragma config BOREN = ON  // Brown-out Reset Selection bits

#define _XTAL_FREQ 8000000  // Define the clock frequency as 8 MHz

void main(void) {
  
    while (1) {
    
    }

    return;
}
