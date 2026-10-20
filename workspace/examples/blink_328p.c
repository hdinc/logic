// firmware of blink_328p: blinks the led on PB5 (Arduino D13), the button on
// PD2 switches between slow and fast, and the serial port (9600 baud) says
// what happens and answers typed keys.
//
// avr-gcc -mmcu=atmega328p -Os -o blink_328p.elf blink_328p.c

#define F_CPU 16000000UL

#include <avr/io.h>
#include <stdio.h>
#include <util/delay.h>

static int put(char c, FILE* f)
{
    while (!(UCSR0A & (1 << UDRE0)))
        ;
    UDR0 = c;
    return 0;
}

static FILE out = FDEV_SETUP_STREAM(put, NULL, _FDEV_SETUP_WRITE);

int main(void)
{
    UBRR0 = F_CPU / 16 / 9600 - 1;
    UCSR0B = (1 << TXEN0) | (1 << RXEN0);
    stdout = &out;

    DDRB |= 1 << PB5;
    // the button pulls PD2 to 0 when pressed
    PORTD |= 1 << PD2;

    printf("hello from the atmega328p\n");

    unsigned count = 0;
    uint8_t fast = 0, was_pressed = 0;
    for (;;) {
        PINB = 1 << PB5;
        if (PINB & (1 << PB5)) printf("blink %u\n", ++count);

        for (uint8_t i = 0; i < (fast ? 5 : 25); i++) {
            _delay_ms(10);
            uint8_t pressed = !(PIND & (1 << PD2));
            if (pressed && !was_pressed) {
                fast = !fast;
                printf(fast ? "fast\n" : "slow\n");
            }
            was_pressed = pressed;
            if (UCSR0A & (1 << RXC0)) printf("you typed '%c'\n", UDR0);
        }
    }
}
