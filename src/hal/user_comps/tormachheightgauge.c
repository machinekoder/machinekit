
//
// This is a userspace HAL driver for the Tormach USB Digital Height Gauge
//
// Copyright 2013 Tormach, LLC
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
//


#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/types.h>
#include <linux/hidraw.h>
#include <linux/input.h>

#include "hal.h"

#define USB_HEIGHTGAUGE_V1 1
#define USB_HEIGHTGAUGE_V2 2

#ifndef HIDIOCGRAWNAME
#define HIDIOCGRAWNAME(len)     _IOC(_IOC_READ, 'H', 0x04, len)
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// USB VID:PID
#define VENDOR_ID  0x04d9
#define PRODUCT_ID 0xe002

#define PACKET_LEN 4

// USB VID:PID version 2 with 3 buttons
#define VENDOR_IDv2  0x0e8f
#define PRODUCT_IDv2 0x00fb

char *device_filev2 = "/dev/tormachheightgaugev2";
// this device is created via a udev rule:
// SUBSYSTEM=="input", ATTRS{idVendor}=="0e8f", ATTRS{idProduct}=="00fb", MODE=="0666", SYMLINK+="tormachheightgaugev2"

// the module name and prefix for all HAL pins 
char *modname = "tormachheightgauge";

char *device_file = "/dev/tormachheightgauge";
// this device is created via a udev rule:
// SUBSYSTEM=="hidraw", ATTRS{idVendor}=="04d9", ATTRS{idProduct}=="e002", MODE=="0666", SYMLINK+="tormachheightgauge"


// common to both

// 1 -> original, 2 -> v2 with 3 buttons
int gauge_version = 1;

int hal_comp_id;

// file handle to gauge
int gauge_fd = 0;

// pointer to shared float in HAL space
struct heightgauge_hal {
    hal_float_t *net_height;    // cur_reading - zero_offset, in inch or mm
    hal_s32_t *raw_value;       // 21-bit value from scale
    hal_float_t *cur_reading;   // converted to inch or mm
    hal_float_t *zero_offset;   // zero offset in inch or mm
    hal_bit_t *button_pressed;  // non-zero if button on USB cable is pressed
    hal_bit_t *button_changed;  // non-zero if button on USB cable has changed state
    hal_bit_t *mm_mode;         // if non-zero reported floats are in millimeters
    hal_bit_t *set_zero_offset; // if non-zero sets zero_offset to cur_reading, clears flag
    hal_bit_t *present;         // if non-zero gauge is connected
    hal_bit_t *debug;           // if non-zero print debug messsages
    hal_bit_t *enable;          // set True by UI while tool offsets page is active
    hal_bit_t *has_zero_button; // v2 3-button USB cable has a ZERO
};


struct heightgauge_hal *hal;
    

static void exit_handler(int sig) {
    fprintf(stderr, "%s: exiting\n", modname);
    if (gauge_fd > 0) {
        // release exclusive capture of device
        ioctl(gauge_fd, EVIOCGRAB, 0);
        close(gauge_fd);
        gauge_fd = 0;
    }
    exit(0);
}


static void call_hal_exit(void) {
    hal_exit(hal_comp_id);
}


int read_updatev2(int fd) {
    int bytes_read;
    unsigned int event_type;
    unsigned int key_code;
    unsigned int key_value;
    struct input_event input;
    static char input_buffer[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    static int input_index = 0;

    // read input
    bytes_read = read(fd, &input, sizeof(input));
    if (bytes_read < 0) {
        fprintf(stderr, "%s: error reading %s: %s\n", modname, device_file, strerror(errno));
        return -1;
    }
    else if (bytes_read != sizeof(input)) {
        fprintf(stderr, "%s: expected to read %d bytes %s only read %d\n", modname, sizeof(input), device_file, bytes_read);
        return -1;
    }
    else if (bytes_read == 0) {
        fprintf(stderr, "%s: EOF %s\n", modname, device_file);
        return -1;
    }

    // offset 2 holds key value '0-9', '.', '-', and 'Enter'
    event_type = (unsigned int)input.type;
    key_code = (unsigned int)input.code;
    key_value = input.value;
    //fprintf(stderr, "%d %d %d\n", (unsigned int)input.type, (unsigned int)input.code, input.value);
    //if (event_type == EV_KEY) {
    //    fprintf(stderr, "got key: %d, val: %d\n", key_code, key_value);
    //}
    if (event_type != EV_KEY || key_value == 0) {
        return 0;
    }

    switch (key_code) {
        case KEY_ENTER:
            // Enter - end of stream
            input_buffer[input_index] = '\0';
            input_index = 0;
            // 4 digits past the decimal point is inches
            // 2 digits past the decimal point is mm
            int digit_count = 0;
            int counting = 0;
            for (int i = 0; i < strlen(input_buffer); i++) {
                if (input_buffer[i] == '.') {
                    // start counting digits
                    counting = 1;
                }
                else if (counting) {
                    digit_count++;
                }
            }
            // ASCII to float
            *hal->cur_reading = strtof(input_buffer, NULL);
            //fprintf(stderr, "input_buffer: '%s'", input_buffer);
            if (digit_count < 3 && !(*hal->mm_mode)) {
                *hal->cur_reading = *hal->cur_reading / 25.4;
            }
            //fprintf(stderr, "-> %0.4f\n", *hal->cur_reading);

            if (*hal->set_zero_offset) {
                // set new zero offset to current reading, clear flag
                *hal->zero_offset = *hal->cur_reading;
                *hal->set_zero_offset = FALSE;
            }
            // apply offset
            *hal->net_height = *hal->cur_reading - *hal->zero_offset;

            *hal->button_pressed = TRUE;
            *hal->button_changed = TRUE;
            break;

        case KEY_DOT:
            input_buffer[input_index++] = '.';
            break;

        case KEY_MINUS:
            input_buffer[input_index++] = '-';
            break;

        case KEY_0:
            input_buffer[input_index++] = '0';
            break;

        case KEY_1:
        case KEY_2:
        case KEY_3:
        case KEY_4:
        case KEY_5:
        case KEY_6:
        case KEY_7:
        case KEY_8:
        case KEY_9:
            input_buffer[input_index++] = '1' + (key_code - KEY_1);
            break;

        default:
            break;
    }
    return 0;

    return bytes_read;
}

int read_updatev1(int fd) {
    int bytes_read;
    unsigned char buf[PACKET_LEN];

    // read ASCII readout
    bytes_read = read(fd, buf, PACKET_LEN);
    if (bytes_read < 0) {
        //fprintf(stderr, "%s: error reading %s: %s\n", modname, device_file, strerror(errno));
        return -1;
    }
    else if (bytes_read != PACKET_LEN) {
        fprintf(stderr, "%s: expected to read %d bytes %s only read %d\n", modname, PACKET_LEN, device_file, bytes_read);
        return -1;
    }
    else if (bytes_read == 0) {
        fprintf(stderr, "%s: EOF %s\n", modname, device_file);
        return -1;
    }


    //fprintf(stderr, "0x%02x 0x%02x 0x%02x 0x%02x\n", buf[0], buf[1], buf[2], buf[3]);
    // raw 24 bit value
    *hal->raw_value = buf[0] + (buf[1] << 8) + (buf[2] << 16);

    // 2s complement, 21 bits
    if (*hal->raw_value >= 0x100000) {
        *hal->raw_value -= 0x200000;
    }
    // 21-bit scale, 2560 counts per inch

    *hal->cur_reading = *hal->raw_value / 2560.0;
    if (*hal->mm_mode) {
        *hal->cur_reading = *hal->cur_reading * 25.4;
    }

    if (*hal->set_zero_offset) {
        // set new zero offset to current reading, clear flag
        *hal->zero_offset = *hal->cur_reading;
        *hal->set_zero_offset = FALSE;
    }

    // apply offset
    *hal->net_height = *hal->cur_reading - *hal->zero_offset;

    if (buf[3] && *hal->button_pressed == FALSE) {
        // set flag on make
        *hal->button_pressed = TRUE;
        *hal->button_changed = TRUE;

        if (*hal->debug) {
            fprintf(stderr, "raw_value: %d\n", *hal->raw_value);
            fprintf(stderr, "cur_reading: %f\n", *hal->cur_reading);
            fprintf(stderr, "net_height: %f\n", *hal->net_height);
        }

    }
    else if (buf[3] == 0 && *hal->button_pressed == TRUE) {
        // clear flag on break
        *hal->button_pressed = FALSE;
        *hal->button_changed = TRUE;
    }

    return bytes_read;
}


int read_update(int fd) {
    if (gauge_version == USB_HEIGHTGAUGE_V1) {
        return read_updatev1(fd);
    }
    else if (gauge_version == USB_HEIGHTGAUGE_V2) {
        return read_updatev2(fd);
    }
    return 0;
}


int check_for_height_gauge(char *dev_filename) {
    struct hidraw_devinfo devinfo;
    char name[100];
    int code;

    gauge_fd = open(device_file, O_RDONLY);
    if (gauge_fd < 0) {
        //fprintf(stderr, "%s: error opening %s: %s\n", modname, device_file, strerror(errno));
        if (errno == EACCES) {
            fprintf(stderr, "%s: is %s readable by any user?\n", modname, device_file);
        }
        return 0;
    }

    code = ioctl(gauge_fd, HIDIOCGRAWINFO, &devinfo);
    if (code < 0) {
        fprintf(stderr, "%s: error with ioctl HIDIOCGRAWINFO on %s: %s\n", modname, device_file, strerror(errno));
        close(gauge_fd);
        return 0;
    }

    if (devinfo.vendor != VENDOR_ID) {
        fprintf(stderr, "%s: dev %s has unexpected Vendor ID 0x%04x expected: 0x%04x\n", modname, device_file, devinfo.vendor, VENDOR_ID);
        close(gauge_fd);
        return 0;
    }

    if ((devinfo.product & 0xffff) != PRODUCT_ID) {
        fprintf(stderr, "%s: dev %s has unexpected Product ID 0x%04x expected: 0x%04x\n", modname, device_file, devinfo.product, PRODUCT_ID);
        close(gauge_fd);
        return 0;
    }

    code = ioctl(gauge_fd, HIDIOCGRAWNAME(99), name);
    if (code < 0) {
        fprintf(stderr, "%s: error with ioctl HIDIOCGRAWNAME on %s: %s\n", modname, device_file, strerror(errno));
        close(gauge_fd);
        return 0;
    }
    //if (*hal->debug) {
        fprintf(stderr, "%s: found '%s' on %s\n", modname, name, device_file);
    //}
    // success
    
    gauge_version = USB_HEIGHTGAUGE_V1;
    *hal->has_zero_button = FALSE;
    return 1;
}


int check_for_height_gaugev2(char *dev_filename) {
    int code;

    gauge_fd = open(device_filev2, O_RDONLY);
    if (gauge_fd < 0) {
        //fprintf(stderr, "%s: error opening %s: %s\n", modname, device_file, strerror(errno));
        if (errno == EACCES) {
            fprintf(stderr, "%s: is %s readable by any user?\n", modname, device_filev2);
        }
        return 0;
    }

    // exclusive capture of device
    code = ioctl(gauge_fd, EVIOCGRAB, 1);
    if (code < 0) {
        fprintf(stderr, "%s: error with ioctl EVIOCGRAB(1) on %s: %s\n", modname, device_filev2, strerror(errno));
        close(gauge_fd);
        gauge_fd = 0;
        return 0;
    }

    //if (*hal->debug) {
        fprintf(stderr, "%s: found '%s' on %s\n", modname, "USB height gauge v2", device_file);
    //}
    // success

    gauge_version = USB_HEIGHTGAUGE_V2;
    *hal->has_zero_button = TRUE;
    return 1;
}


int alloc_hal() {
    int code;

    hal = (struct heightgauge_hal *)hal_malloc(sizeof(struct heightgauge_hal));
    if (hal == NULL) {
        fprintf(stderr, "%s: ERROR: unable to allocate HAL shared memory\n", modname);
        return 0;
    }

    code = hal_pin_float_newf(HAL_OUT, &(hal->net_height), hal_comp_id, "%s.net-height", modname);
    if (code != 0) return 0;
    code = hal_pin_s32_newf(HAL_OUT, &(hal->raw_value), hal_comp_id, "%s.raw-value", modname);
    if (code != 0) return 0;
    code = hal_pin_float_newf(HAL_OUT, &(hal->cur_reading ), hal_comp_id, "%s.cur-reading ", modname);
    if (code != 0) return 0;
    code = hal_pin_float_newf(HAL_IO, &(hal->zero_offset), hal_comp_id, "%s.zero-offset", modname);
    if (code != 0) return 0;
    code = hal_pin_bit_newf(HAL_OUT, &(hal->button_pressed), hal_comp_id, "%s.button-pressed", modname);
    if (code != 0) return 0;
    code = hal_pin_bit_newf(HAL_IO, &(hal->button_changed), hal_comp_id, "%s.button-changed", modname);
    if (code != 0) return 0;
    code = hal_pin_bit_newf(HAL_IO, &(hal->mm_mode), hal_comp_id, "%s.mm-mode", modname);
    if (code != 0) return 0;
    code = hal_pin_bit_newf(HAL_IN, &(hal->set_zero_offset), hal_comp_id, "%s.set-zero-offset", modname);
    if (code != 0) return 0;
    code = hal_pin_bit_newf(HAL_OUT, &(hal->present), hal_comp_id, "%s.present", modname);
    if (code != 0) return 0;
    code = hal_pin_bit_newf(HAL_IN, &(hal->debug), hal_comp_id, "%s.debug", modname);
    if (code != 0) return 0;
    code = hal_pin_bit_newf(HAL_IN, &(hal->enable), hal_comp_id, "%s.enable", modname);
    if (code != 0) return 0;
    code = hal_pin_bit_newf(HAL_OUT, &(hal->has_zero_button), hal_comp_id, "%s.has-zero-button", modname);
    if (code != 0) return 0;

    *hal->net_height = 0.0;
    *hal->raw_value = 0;
    *hal->cur_reading = 0.0;
    *hal->zero_offset = 0.0;
    *hal->button_pressed = FALSE;
    *hal->button_changed = FALSE;
    *hal->mm_mode = FALSE;
    *hal->set_zero_offset = FALSE;
    *hal->present = FALSE;
    *hal->debug = FALSE;
    *hal->enable = FALSE;
    *hal->has_zero_button = TRUE;

    return 1;
}


int main(int argc, char *argv[]) {

    fprintf(stderr, "%s: starting\n", modname);

    hal_comp_id = hal_init(modname);
    if (hal_comp_id < 1) {
        fprintf(stderr, "%s: hal_init failed\n", modname);
        exit(1);
    }

    signal(SIGINT, exit_handler);
    signal(SIGTERM, exit_handler);
    atexit(call_hal_exit);

    if (alloc_hal() == 0) {
        fprintf(stderr, "%s: height gauge failed to allocate HAL data\n", modname);
        exit(1);
    }

    hal_ready(hal_comp_id);

    // super loop
    while (1) {
        if (*hal->enable == FALSE) {
            // stay asleep until woken by UI
            sleep(1);
            continue;
        }

        if (*hal->present == FALSE) {
            // attempt to open device
            if (check_for_height_gauge(device_file)) {
                // found it!
                //fprintf(stderr, "%s: found device on %s\n", modname, device_file);
                *hal->present = TRUE;
            }
            else if (check_for_height_gaugev2(device_filev2)) {
                // found it!
                //fprintf(stderr, "%s: found device on %s\n", modname, device_filev2);
                *hal->present = TRUE;
            }
            else {
                // sleep
                sleep(2);
            }
        }
        else {
            // read gauge, update data
            while (1) {
                fd_set readers;
                int count;

                FD_ZERO(&readers);
                FD_SET(gauge_fd, &readers);

                // block for data ready or device unplugged
                count = select(gauge_fd + 1, &readers, NULL, NULL, NULL);
                if (count < 0) {
                    if ((errno == EAGAIN) || (errno == EINTR)) {
                        continue;
                    }
                    fprintf(stderr, "%s: select() returned < 0\n", modname);
                    close(gauge_fd);
                    gauge_fd = -1;
                    *hal->present = FALSE;
                    break; // look for gauge to reappear
                }

                if (FD_ISSET(gauge_fd, &readers)) {
                    count = read_update(gauge_fd);
                    if (count < 0) {
                        // gauge unplugged
                        fprintf(stderr, "%s: unplugged?\n", modname);
                        if (gauge_version == USB_HEIGHTGAUGE_V2) {
                            // release exclusive capture of device (v2 specific)
                            ioctl(gauge_fd, EVIOCGRAB, 0);
                        }
                        close(gauge_fd);
                        gauge_fd = 0;
                        *hal->present = FALSE;
                        break; // look for gauge to reappear
                    }
                    if (*hal->set_zero_offset) {
                        *hal->set_zero_offset = FALSE;
                        *hal->zero_offset = *hal->cur_reading;
                    }
                }
            }
        }
    }

    // should never get here
    exit(0);
}

