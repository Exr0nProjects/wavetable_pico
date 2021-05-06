#include <stdio.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/binary_info.h"
#include "pico/time.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "main.pio.h"

//uint MICROSTEP = 1; // 1 or 4 or 8
//const uint STEPS_PER_ROTATION = 1;
const double BELT_RATIO = 1;
uint MICROSTEP = 32; // 1 or 4 or 8
const uint STEPS_PER_ROTATION = 200;
//const double BELT_RATIO = 100/7;    // TODO diameter hub / diameter stepper

const float RPM_MAX = 300;
const float RPM_MIN = 0.1;

#define BUILTIN_LED_PIN 25
const uint OUTPUT_PIN = 15;

const uint FLASH_PERIOD = 1e2;  // ms
const int CORECOM_FLASH = 1;    // IMPROVE: enums are for nerds
const int CORECOM_SOLID = 2;
const int CORECOM_ERROR = -1;

void print_progbar(const uint len, const double lo, const double hi, const double pos, const double base_pos)
{
    printf("%.2lf |", lo);
    for (uint i=0; i<len; ++i) {
        if ((double)i/len <= pos && pos <= (double)(i+1)/len)
            printf("o");
        else if ((double)i/len <= base_pos)
            printf("-");
        else
            printf(" ");
    }
    printf("| %.2lf ", hi);
}

uint elapsed() {
    return to_ms_since_boot(get_absolute_time());
}

double CLOCK_FREQ = -1;
const double DRIVE_FACTOR = STEPS_PER_ROTATION*BELT_RATIO;
double calc_delay_time(const double rpm)
{
	return 30/rpm / MICROSTEP/DRIVE_FACTOR;
}
double set_rpm(const PIO pio, const uint sm, const double rpm)
{
    if (CLOCK_FREQ < 0) CLOCK_FREQ = pio_clock_freq()/bitbang0_clock_divisor;
	double ans = calc_delay_time(rpm)*CLOCK_FREQ;
    if (ans < bitbang0_upkeep_instruction_count) return -1; // too fast, not possible
    if (ans > 1e6) return -1;                               // too slow, disallow IMPROVE: change condition
    const double reached = (double)30/((unsigned)ans)*CLOCK_FREQ/MICROSTEP/DRIVE_FACTOR;
    printf(" %uipt %.2lftps %.2lfrpm)\r", (unsigned)ans, CLOCK_FREQ/(unsigned)ans/2, reached);
    //printf("set %.2lftps %.2lfrpm\r", CLOCK_FREQ/(unsigned)ans/2, reached);
    pio_sm_put_blocking(pio, sm, (unsigned)ans-bitbang0_upkeep_instruction_count);
    return reached;
}

void core1_entry()
{
    char buf[64];
    float arg;
    int comm_state = CORECOM_SOLID;
// main loop
    while (1) {
    // input
        memset(buf, 0, sizeof buf); arg = 0;
        scanf("%s %f", buf, &arg);
        if (!strcmp(buf, "set")) multicore_fifo_push_blocking(arg);

    // status communication
        if (multicore_fifo_rvalid())
            comm_state = multicore_fifo_pop_blocking();
        if (comm_state == CORECOM_FLASH) {
            gpio_put(BUILTIN_LED_PIN, 0);
            sleep_ms(FLASH_PERIOD);
        } else {
            printf("\n");
        }
        gpio_put(BUILTIN_LED_PIN, 1);
        sleep_ms(FLASH_PERIOD);
    }
}

int main() {
// metadata
    bi_decl(bi_program_description("Sine Blink"));
    bi_decl(bi_1pin_with_name(BUILTIN_LED_PIN, "On-board LED"));

// hardware setup
    stdio_init_all();
    gpio_init(BUILTIN_LED_PIN);
    gpio_set_dir(BUILTIN_LED_PIN, GPIO_OUT);
    gpio_put(BUILTIN_LED_PIN, 1);

// pio init
    const PIO pio = pio0;
    const int sm = 0;
    const uint offset = pio_add_program(pio, &bitbang0_program);
    bitbang0_init(pio, sm, offset, OUTPUT_PIN, 1);

// multicore setup
    multicore_launch_core1(core1_entry);

// state vars
    float sine_amplitude = 0; // rpm
    float sine_frequency = 1; // rpm
    float target_rpm = 5;                                         // INITIAL RPM
    float cur_rpm = target_rpm;

    uint prev_timestamp = elapsed();

    float* to_update = &target_rpm;
// main loop
    while (1) {
	    if (multicore_fifo_rvalid()) {
		    float g = multicore_fifo_pop_blocking();

			if (g < 1) { // control signal
                printf("\n\n\n\nGOT CONTROL SIGNAL...\n\n\n\n");
				switch ((int)g) {
					// TODO: implement sine flags
				case -1: to_update = &target_rpm; break;
				}
				g = multicore_fifo_pop_blocking(); // IMPROVE: this should check if read is valid, and set a flag to remember the read state between iters
			} else {    // default to updating the base RPM
				to_update = &target_rpm;
			}
			*to_update = g;
			multicore_fifo_push_blocking(CORECOM_FLASH);
	    }

	    if (cur_rpm != target_rpm) {
			float delta = log(cur_rpm+1) * (elapsed()-prev_timestamp)/100;
			if (fabs(target_rpm-cur_rpm) < delta) {
				cur_rpm = target_rpm;
				//multicore_fifo_push_blocking(CORECOM_SOLID);
			} else {
				if (target_rpm > cur_rpm) cur_rpm += delta;
				else                      cur_rpm -= delta;
			}
            //printf("enter set rpm                                               \r");
			set_rpm(pio, sm, cur_rpm);
            //printf("                        exit set rpm                        \r");
	    } else {
            printf("target rpm reached\n");
        }

	    // TODO: implement sine handling
        prev_timestamp = elapsed(); // NOTE: could improve using absolute_time_t delayed_by_ms() 
        //sleep_ms(calc_delay_time(cur_rpm)+prev_timestamp-elapsed()-1);
    }
}
