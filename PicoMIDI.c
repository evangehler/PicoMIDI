#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
 
// UART defines for MIDI IN (DIN)
// uart0 is reserved for stdio (USB console output)
// uart1 is used for MIDI input at 31250 baud (MIDI standard)
#define MIDI_UART_ID    uart0
#define MIDI_BAUD_RATE  31250
#define MIDI_RX_PIN     13
#define MIDI_TX_PIN     12

// LED
#define LED_PIN         25
 
// MIDI message types (upper nibble of status byte)
#define MIDI_NOTE_OFF           0x80
#define MIDI_NOTE_ON            0x90
#define MIDI_POLY_PRESSURE      0xA0
#define MIDI_CONTROL_CHANGE     0xB0
#define MIDI_PROGRAM_CHANGE     0xC0
#define MIDI_CHANNEL_PRESSURE   0xD0
#define MIDI_PITCH_BEND         0xE0
 
// MIDI message structure
typedef struct {
    uint8_t status;         // Raw status byte
    uint8_t message_type;   // Upper nibble (message type)
    uint8_t channel;        // Lower nibble + 1 (1-16)
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
 
// Convert MIDI note number to note name (e.g., 60 = "C4")
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
            const char *type = (msg->message_type == MIDI_NOTE_ON) ? "ON " : "OFF";
            printf("Packet: 0x%02X 0x%02X 0x%02X | %s | %s | Vel: %3d\n",
                   msg->status, msg->data1, msg->data2,
                   note_name, type, msg->data2);
            break;
        }
        case MIDI_POLY_PRESSURE:
            printf("POLY PRES - Ch: %2d | Note: %3d | Pres: %3d\n",
                   msg->channel, msg->data1, msg->data2);
            break;
        case MIDI_CONTROL_CHANGE:
            printf("CC        - Ch: %2d | CC:   %3d | Val: %3d\n",
                   msg->channel, msg->data1, msg->data2);
            break;
        case MIDI_PROGRAM_CHANGE:
            printf("PROG CHG  - Ch: %2d | Prog: %3d\n",
                   msg->channel, msg->data1);
            break;
        case MIDI_CHANNEL_PRESSURE:
            printf("CHAN PRES  - Ch: %2d | Pres: %3d\n",
                   msg->channel, msg->data1);
            break;
        case MIDI_PITCH_BEND: {
            int16_t bend = (int16_t)((msg->data2 << 7) | msg->data1) - 8192;
            printf("PITCH BEND - Ch: %2d | Val: %6d\n",
                   msg->channel, bend);
            break;
        }
        default:
            printf("UNKNOWN   - Status: 0x%02X\n", msg->status);
            break;
    }
}
 
int main() {
    stdio_init_all();
    sleep_ms(2000);
 
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
 
    uart_init(MIDI_UART_ID, MIDI_BAUD_RATE);
    gpio_set_function(MIDI_RX_PIN, GPIO_FUNC_UART);
    gpio_set_function(MIDI_TX_PIN, GPIO_FUNC_UART);
    gpio_pull_down(MIDI_RX_PIN);
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
        
        // Blink LED on RX
        gpio_put(LED_PIN, 1);
        sleep_us(100);
        gpio_put(LED_PIN, 0);

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
 
            // If we have all the data bytes, display and reset for running status
            if (bytes_received >= bytes_needed && bytes_needed > 0) {
                midi_display_message(&midi_msg);
                bytes_received = 0;  // Ready for running status data bytes
            }
        }
    }
}