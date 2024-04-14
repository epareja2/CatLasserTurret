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

// PWM output pin for hardware PWM
#define PWM1_PIN GP4
// GPIO pin for software PWM
#define PWM2_PIN GP5
#define LASER_PIN GP2

#define MAX_MOTORES 2
#define PWM_MAX_TIMER 249  //
#define _0_5ms \
    64561  // 0.5ms Calculado a 8MHz de XTAL  en Tmr1 con preescaler 1:1
#define _1ms 63561  // 1ms Calculado a 8MHz de XTAL  en Tmr1 con preescaler 1:1
#define _2ms 61561  // 2ms Calculado a 8MHz de XTAL  en Tmr1 con preescaler 1:1
#define _14ms \
    37535  // 14ms Calculado a 8MHz de XTAL  en Tmr1 con preescaler 1:1

#define TMR2_1ms 249
#define TMR0_10ms 178
#define COUNT_TO_50ms 5
#define COUNT_TO_1000ms 100
#define INIT_MASK 0b00010000   // Mascara inicial para GPIO 4 y 5
#define CLEAR_MASK 0b11001111  // Mascara para borrar los PWM para GPIO 4 y 5

#define MAX_ANGLE 90
#define MIN_ANGLE -90

#define MIN_X_ANGLE -60
#define MAX_X_ANGLE 60
#define MIN_Y_ANGLE -65
#define MAX_Y_ANGLE -5
#define MIN_SPEED 15
#define MAX_SPEED 30

#define TIME_TO_SHUT_OFF 15*60 // number of seconds to turnoff the device

int motorTarget[2];  // has the set position from -180 to 180 degrees.
uint8_t motorRawValueTarget[2];  // Tiene el valor en unidades del timer deseadas
uint8_t pwmActual[2];        // Tiene el valor en unidades del timer
int motorIncrement[2];
int motorAngle[2];        // Tiene el valor actual del angulo en cada eje.
uint8_t speed[2];         // Velocidad de cambio en cada eje
uint8_t mustCheckServos;  // Flag para detectar si ocurrió TMR0 (10ms)
uint8_t mustStopNow ; // Flag para decir que ya termin� el ciclo entonces debe parar los motores

uint8_t activeMotor;  // Index to have the motor that is active
uint8_t EstadoRCServo;
uint8_t contador1Seg;
uint16_t contadorTimeShutOff; // Contador en segundos para apagar el dispositivo.
uint8_t ContadorMotor;  // Tiene el indice al motor que est? activo en un bloque
                        // de tiempo del PWM
uint8_t MaskMotor;      // Tiene el bit que está activo de motor para
volatile unsigned int overflow_count = 0;

void timer1InterruptHandler(void);
void timer1InterruptOld(void);
int calculateSpeedIncrement(uint8_t index);
uint8_t convertAngle(int value);

void __interrupt() myISR(void) {
    if (INTCONbits.TMR0IF) {    // Check if TMR0 overflow interrupt (10ms)
        INTCONbits.TMR0IF = 0;  // Clear the TMR0 interrupt flag
        TMR0 = TMR0_10ms;
        overflow_count++;

        if (overflow_count >= COUNT_TO_50ms) {
            overflow_count = 0;
            mustCheckServos = 1;  // Bandera para chequear posicion de servossdd
        }
        contador1Seg++;
        if(contador1Seg>= COUNT_TO_1000ms){
          contador1Seg = 0;
          contadorTimeShutOff++;
          if(contadorTimeShutOff >= TIME_TO_SHUT_OFF) {
            mustStopNow = 1;
          }
        }
      
    }

    if (PIR1bits.TMR1IF) {
        PIR1bits.TMR1IF = 0;
        timer1InterruptHandler();
    }

    if (PIR1bits.TMR2IF) {
        TMR2ON = 0;  // Apaga el timer 2 porque termino el ciclo
        //        DutyON
        PIR1bits.TMR2IF = 0;

        GPIO = GPIO & CLEAR_MASK;
    }

    // Check and handle other interrupts here
}

void timer1InterruptHandler(void) {
    switch (EstadoRCServo) {
        case 0:
            TMR1 = _0_5ms;
            TMR2ON = 0;  //
            TMR2 = 0;
            EstadoRCServo = 1;
            if (ContadorMotor >= MAX_MOTORES) {
                ContadorMotor = 0;
                MaskMotor = INIT_MASK;  // Son GP4 y GP5 entonces arranca en el
                GPIO = GPIO & CLEAR_MASK;
            }
            GPIO = (GPIO & CLEAR_MASK) | MaskMotor;

            break;
        case 1:
            TMR1 = _2ms;  // Tiempo máximo del PWM del servos
            PR2 = motorRawValueTarget[ContadorMotor];
            TMR2ON = 1;
            EstadoRCServo = 2;
            if (PR2 <= 1) {
                TMR2ON = 0;  // Si el valor de referencia es 0 entonces apaga el
                             // puerto y no activa el PWM
                GPIO = (GPIO & CLEAR_MASK);
            }
            break;

        case 2:
            TMR2IF = 0;  // Si llego aqui y no habia terminado el ciclo del TMR2
                         // entonces lo inhibe para que no resetee
            TMR2ON = 0;
            EstadoRCServo = 0;
            ContadorMotor++;
            GPIO = GPIO & CLEAR_MASK;
            MaskMotor = (uint8_t)(MaskMotor << 1);
            if (ContadorMotor >= MAX_MOTORES) {
                ContadorMotor = 0;
                MaskMotor = INIT_MASK;
                TMR1 = _14ms;  // 20ms - (3ms*NMotores)
            } else {
                TMR1 = 65535;  // Hace que se dispare nuevamente para arrancar
                               // con el siguiente motor inmediatamente
            }
            break;

        default:
            EstadoRCServo = 0;
            GPIO = GPIO & CLEAR_MASK;
            break;
    }
}

void setupTimer0() {
    OPTION_REG = 0;          // Reset OPTION_REG
    OPTION_REGbits.PSA = 0;  // Assign prescaler to Timer0
    OPTION_REGbits.PS = 7;   // Prescaler 1:256
    TMR0 = 176;              // Reset Timer0 to 176 (10ms)
    INTCONbits.TMR0IE = 1;   // Enable Timer0 interrupt
}

void setupTimer1() {
    T1CON = 0;             // Clear the Timer 1 control register
    T1CONbits.T1CKPS = 0;  // Set the prescaler to 1:1
    TMR1 = _2ms;           // (TMR1 = 61536 for 2ms)
    T1CONbits.TMR1ON = 1;  // Turn on Timer 1
}

void initPWM() {
    CCP1CON = 0b00000000;  // Configure CCP1 module for PWM mode
    T2CON = 0b00000011;    // Enable Timer2 with prescaler=1 and
    // preescaler=16
    // T2CON = 0b00111000;  // Enable Timer2 with prescaler = 1 and postcaler =
    // 8
    //    TMR2ON = 1;
    PR2 = TMR2_1ms;  // Set the PWM MAX period to 2ms (with 8MHz clock and
                     // prescaler = 1)
    MaskMotor = INIT_MASK;
    EstadoRCServo = 0;  // El estado inicial es para que arranque los 0.5ms
    ContadorMotor = 0;
}

void setup(void) {
    srand(TMR1);    // Inicia el motor de n�meros aleatorios
    OSCCON = 0x71;  // Set the internal oscillator to 8MHz and internal clock
    ANSEL = 0;      // Make all analog pins digital
    TRISIO = 0b00000011;  // Set PWM pins as output and laser control
    initPWM();
    setupTimer1();
    setupTimer0();
    ContadorMotor = 0;
    contador1Seg =0;
    contadorTimeShutOff =0;
    mustStopNow = 0;
    GPIObits.GP2 = 0;
    GPIObits.GP4 = 0;
    GPIObits.GP5 = 0;

    // Interrupt setup
    INTCONbits.GIE = 1;   // Enable global interrupts
    INTCONbits.PEIE = 1;  // Enable peripheral interrupts

    INTCONbits.TMR0IF = 0;
    PIR1bits.TMR1IF = 0;
    PIR1bits.TMR2IF = 0;
    INTCONbits.TMR0IE = 1;
    PIE1bits.TMR2IE = 1;  // Enable TMR2 interrupt
    PIE1bits.TMR1IE = 1;
    
}

int generateRandom(int min, int max) {
    int randomNumber = 0;

    randomNumber = (rand() % (max - min + 1)) +
                   min;  // Generate a random number between min and max
    return randomNumber;
}

uint8_t convertAngle(int value) {
    int temp;
    if (value < MIN_ANGLE) {
        value = MIN_ANGLE;
    } else if (value > MAX_ANGLE) {
        value = MAX_ANGLE;
    }
    temp = (value * 255) / (MAX_ANGLE - MIN_ANGLE) + (127);
    return temp;
}

int calculateSpeedIncrement(uint8_t index) {
    int increment;
    int speed;

    if (index > (MAX_MOTORES - 1)) {
        return 0;  // No es un indice válido
    }
    speed = (uint8_t)generateRandom(MIN_SPEED, MAX_SPEED);
    increment = (motorTarget[index] - motorAngle[index]) / speed;
    if (increment == 0) {
        if (motorTarget[index] > motorAngle[index]) {
            increment = 1;
        } else {
            increment = -1;
        }
    }
    return increment;
}

void setTorretPosition(void) {
    //     BOTTOM SERVO -- 0 is right 180 is left
    //     this may be reversed for some setups
    _nop();
    if ((motorIncrement[0] >= 0 && (motorAngle[0] >= motorTarget[0])) ||
        ((motorIncrement[0] < 0 && (motorAngle[0] <= motorTarget[0])))) {
        motorTarget[0] = (int)generateRandom(MIN_X_ANGLE, MAX_X_ANGLE);
        motorIncrement[0] = calculateSpeedIncrement(0);
    } else if (motorAngle[0] < MIN_X_ANGLE) {
        motorTarget[0] = (int)generateRandom(MIN_X_ANGLE, MAX_X_ANGLE);
        motorAngle[0] = MIN_X_ANGLE;
        motorRawValueTarget[0] = convertAngle(motorAngle[0]);
        motorIncrement[0] = calculateSpeedIncrement(0);
    } else if (motorAngle[0] > MAX_X_ANGLE) {
        motorTarget[0] = (int)generateRandom(MIN_X_ANGLE, MAX_X_ANGLE);
        motorAngle[0] = MAX_X_ANGLE;
        motorRawValueTarget[0] = convertAngle(motorAngle[0]);
        motorIncrement[0] = calculateSpeedIncrement(0);
    } else {
        motorAngle[0] = motorAngle[0] + motorIncrement[0];
        motorRawValueTarget[0] = convertAngle(motorAngle[0]);
    }

    //    // TOP SERVO -- 0 is up 180 is down
    //    // this may be reversed for some setups
    //     BOTTOM SERVO -- 0 is right 180 is left
    //     this may be reversed for some setups
    if ((motorIncrement[1] >= 0 && (motorAngle[1] >= motorTarget[1])) ||
        ((motorIncrement[1] < 0 && (motorAngle[1] <= motorTarget[1])))) {
        motorTarget[1] = (int)generateRandom(MIN_Y_ANGLE, MAX_Y_ANGLE);
        motorIncrement[1] = calculateSpeedIncrement(1);
    } else if (motorAngle[1] < MIN_Y_ANGLE) {
        motorTarget[1] = (int)generateRandom(MIN_Y_ANGLE, MAX_Y_ANGLE);
        motorAngle[1] = MIN_Y_ANGLE;
        motorRawValueTarget[1] = convertAngle(motorAngle[1]);
        motorIncrement[1] = calculateSpeedIncrement(1);
    } else if (motorAngle[1] > MAX_Y_ANGLE) {
        motorTarget[1] = (int)generateRandom(MIN_Y_ANGLE, MAX_Y_ANGLE);
        motorAngle[1] = MAX_Y_ANGLE;
        motorIncrement[1] = calculateSpeedIncrement(1);
    } else {
        motorAngle[1] = motorAngle[1] + motorIncrement[1];
        motorRawValueTarget[1] = convertAngle(motorAngle[1]);
    }
}

void main(void) {
    setup();
    motorAngle[0] = 0;
    motorAngle[1] = 0;
    motorRawValueTarget[0] = convertAngle(motorAngle[0]);
    motorRawValueTarget[1] = convertAngle(motorAngle[1]);
    LASER_PIN = 0;

    // __delay_ms(5000);
    // motorAngle[0] = MIN_ANGLE;
    // motorAngle[1] = MIN_ANGLE;
    // motorRawValueTarget[0] = convertAngle(motorAngle[0]);
    // motorRawValueTarget[1] = convertAngle(motorAngle[1]);
    // LASER_PIN ^= 1;

    // __delay_ms(5000);
    // motorAngle[0] = MAX_ANGLE;
    // motorAngle[1] = MAX_ANGLE;
    // motorRawValueTarget[0] = convertAngle(motorAngle[0]);
    // motorRawValueTarget[1] = convertAngle(motorAngle[1]);
    // LASER_PIN ^= 1;

    __delay_ms(1000);

    motorAngle[0] = MIN_X_ANGLE;
    motorAngle[1] = MIN_Y_ANGLE;
    motorRawValueTarget[0] = convertAngle(motorAngle[0]);
    motorRawValueTarget[1] = convertAngle(motorAngle[1]);

    __delay_ms(1000);
    motorAngle[0] = MAX_X_ANGLE;
    motorAngle[1] = MAX_Y_ANGLE;
    motorRawValueTarget[0] = convertAngle(motorAngle[0]);
    motorRawValueTarget[1] = convertAngle(motorAngle[1]);

    __delay_ms(1000);
    motorAngle[0] = 0;
    motorAngle[1] = 0;
    motorRawValueTarget[0] = convertAngle(motorAngle[0]);
    motorRawValueTarget[1] = convertAngle(motorAngle[1]);

    __delay_ms(1000);
    LASER_PIN = 1;

    while (1) {
        if (mustCheckServos == 1 && !mustStopNow) {
            mustCheckServos = 0;
            setTorretPosition();
        }
        if (mustStopNow == 1) {
          LASER_PIN = 0;
          motorAngle[0] = 0;
          motorAngle[1] = 0;
          motorRawValueTarget[0] = convertAngle(motorAngle[0]);
          motorRawValueTarget[1] = convertAngle(motorAngle[1]);
          
        }
        
    }

    return;
}
