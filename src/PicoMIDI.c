#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
 
// UART defines for MIDI IN
#define MIDI_UART_ID    uart0
#define MIDI_BAUD_RATE  31250
#define MIDI_RX_PIN     13
#define MIDI_TX_PIN     12

// LED
#define LED_PIN         25
 
// MIDI message types
#define MIDI_NOTE_OFF           0x80
#define MIDI_NOTE_ON            0x90
#define MIDI_POLY_PRESSURE      0xA0
#define MIDI_CONTROL_CHANGE     0xB0
#define MIDI_PROGRAM_CHANGE     0xC0
#define MIDI_CHANNEL_PRESSURE   0xD0
#define MIDI_PITCH_BEND         0xE0
 
// MIDI structure
typedef struct {
    uint8_t status;
    uint8_t message_type;
    uint8_t channel;
    uint8_t data1;
    uint8_t data2;
} midi_message_t;
 
// Returns how many data bytes a given message type expects
static uint8_t midi_data_bytes_expected(uint8_t msg_type) {
    switch (msg_type) {
        case MIDI_PROGRAM_CHANGE:
        case MIDI_CHANNEL_PRESSURE:
            return 1;
        default:
            return 2;
    }
}
 
// Convert MIDI note number to note name
static void midi_note_to_name(uint8_t note, char *buf) {
    const char *notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (note / 12) - 1;
    int semitone = note % 12;
    sprintf(buf, "%s%d", notes[semitone], octave);
}

// Display MIDI message info on USB console
static void midi_display_message(const midi_message_t *msg) {
    switch (msg->message_type) {
        case MIDI_NOTE_OFF:
        case MIDI_NOTE_ON: {
            char note_name[8];
            midi_note_to_name(msg->data1, note_name);
            printf("NOTE %-3s | Ch: %2d | %s | Vel: %3d | Raw: 0x%02X 0x%02X 0x%02X\n",
                   (msg->message_type == MIDI_NOTE_ON) ? "ON" : "OFF",
                   msg->channel, note_name, msg->data2,
                   msg->status, msg->data1, msg->data2);
            break;
        }
        case MIDI_POLY_PRESSURE:
            printf("POLY PRES  | Ch: %2d | Note: %3d | Pres: %3d | Raw: 0x%02X 0x%02X 0x%02X\n",
                   msg->channel, msg->data1, msg->data2,
                   msg->status, msg->data1, msg->data2);
            break;
        case MIDI_CONTROL_CHANGE:
            printf("CC         | Ch: %2d | CC:   %3d | Val:  %3d | Raw: 0x%02X 0x%02X 0x%02X\n",
                   msg->channel, msg->data1, msg->data2,
                   msg->status, msg->data1, msg->data2);
            break;
        case MIDI_PROGRAM_CHANGE:
            printf("PROG CHG   | Ch: %2d | Prog: %3d |             Raw: 0x%02X 0x%02X\n",
                   msg->channel, msg->data1,
                   msg->status, msg->data1);
            break;
        case MIDI_CHANNEL_PRESSURE:
            printf("CHAN PRES  | Ch: %2d | Pres: %3d |             Raw: 0x%02X 0x%02X\n",
                   msg->channel, msg->data1,
                   msg->status, msg->data1);
            break;
        case MIDI_PITCH_BEND: {
            int16_t bend = (int16_t)((msg->data2 << 7) | msg->data1) - 8192;
            printf("PITCH BEND | Ch: %2d | Val: %6d |   Raw: 0x%02X 0x%02X 0x%02X\n",
                   msg->channel, bend,
                   msg->status, msg->data1, msg->data2);
            break;
        }
        default:
            printf("UNKNOWN    | Status: 0x%02X\n", msg->status);
            break;
    }
}
 
int main() {

    // Init Business
    stdio_init_all();
    sleep_ms(2000);
 
    // LED Debug
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
 
    // UART Business
    uart_init(MIDI_UART_ID, MIDI_BAUD_RATE);
    gpio_set_function(MIDI_RX_PIN, GPIO_FUNC_UART);
    gpio_set_function(MIDI_TX_PIN, GPIO_FUNC_UART);
    uart_set_format(MIDI_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(MIDI_UART_ID, false, false);
 
    midi_message_t midi_msg = {0};
    uint8_t bytes_needed = 0;
    uint8_t bytes_received = 0;
 
    while (true) {
        
        if (!uart_is_readable(MIDI_UART_ID)) {
            tight_loop_contents();
            continue;
        }

        uint8_t byte = uart_getc(MIDI_UART_ID);
        
        // // Blink LED on RX (for debug)
        // gpio_put(LED_PIN, 1);
        // sleep_us(1000);
        // gpio_put(LED_PIN, 0);

        if (byte & 0x80) {
            // Status byte — start of a new message
            midi_msg.status       = byte;
            midi_msg.message_type = byte & 0xF0;
            midi_msg.channel      = (byte & 0x0F) + 1;  // extract channel BEFORE masking
            midi_msg.data1        = 0;
            midi_msg.data2        = 0;
            bytes_needed   = midi_data_bytes_expected(midi_msg.message_type);
            bytes_received = 0;
        } else {
            // Data byte
            if (bytes_received == 0) {
                midi_msg.data1 = byte;
                bytes_received = 1;
            } else if (bytes_received == 1) {
                midi_msg.data2 = byte;
                bytes_received = 2;
            }
 
            // If all data bytes, display and reset for running status
            if (bytes_received >= bytes_needed && bytes_needed > 0) {
                midi_display_message(&midi_msg);
                bytes_received = 0;  // Ready for running status data bytes
            }
        }
    }
}