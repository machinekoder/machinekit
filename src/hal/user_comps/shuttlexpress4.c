
//
// This is a userspace HAL driver for the ShuttleXpress device by Contour
// Design.
//
// Copyright 2011 Sebastian Kuzminsky <seb@highlab.com>
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


//
// Modified by Tormach, LLC to support hot plugging of shuttle(s)
// while LinucCNC is running. HAL data for 4 shuttles are allocated at init.
// HAL pins must all be allocated before calling hal_ready().
// If a shuttle /dev/hidraw* device goes away and reappears it will pick
// up where it was when it departed.
//
// Module named 'shuttlexpress4'to avoid replacing the original 'shuttlexpress4' module by Seb.
// and to reflect support of 4 shuttlexpress maximum.
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

#include "hal.h"



#ifndef HIDIOCGRAWNAME
#define HIDIOCGRAWNAME(len)     _IOC(_IOC_READ, 'H', 0x04, len)
#endif

#define Max(a, b)  ((a) > (b) ? (a) : (b))




// USB Vendor and Product IDs
#define VENDOR_ID  0x0b33  // Contour Design
#define PRODUCT_ID 0x0020  // ShuttleXpress


// each packet from the ShuttleXpress is this many bytes
#define PACKET_LEN 5


// the module name, and prefix for all HAL pins 
char *modname = "shuttlexpress4";


int hal_comp_id;




// each ShuttleXpress presents this interface to HAL
struct shuttlexpress_hal {
    hal_bit_t *button_0;
    hal_bit_t *button_0_not;
    hal_bit_t *button_1;
    hal_bit_t *button_1_not;
    hal_bit_t *button_2;
    hal_bit_t *button_2_not;
    hal_bit_t *button_3;
    hal_bit_t *button_3_not;
    hal_bit_t *button_4;
    hal_bit_t *button_4_not;
    hal_s32_t *counts;        // accumulated counts from the jog wheel
    hal_float_t *spring_wheel_f;  // current position of the springy outer wheel, as a float from -1 to +1 inclusive
    hal_s32_t *spring_wheel_s32;  // current position of the springy outer wheel, as a s32 from -7 to +7 inclusive
};


struct shuttlexpress_t {
    int fd;
    char device_file[100];
    struct shuttlexpress_hal *hal;
    int read_first_event;
    int prev_count;
    int present;
};


#define MAX_SHUTTLES 4

// statically allocate structure array for 4 devices
struct shuttlexpress_t shuttlexpress[MAX_SHUTTLES];
int num_shuttlexpress_entries = MAX_SHUTTLES;


static void exit_handler(int sig) {
    printf("%s: exiting\n", modname);
    exit(0);
}


static void call_hal_exit(void) {
    hal_exit(hal_comp_id);
}


int read_update(struct shuttlexpress_t *s) {
    int r;
    int8_t packet[PACKET_LEN];

    *s->hal->spring_wheel_s32 = 0;
    *s->hal->spring_wheel_f = 0.0;

    r = read(s->fd, packet, PACKET_LEN);
    if (r < 0) {
        // this happens when shuttle gets unplugged
        fprintf(stderr, "%s: error reading %s: %s\n", modname, s->device_file, strerror(errno));
        return -1;
    } else if (r == 0) {
        fprintf(stderr, "%s: EOF on %s\n", modname, s->device_file);
        return -2;
    }

    *s->hal->button_0 = packet[3] & 0x10;
    *s->hal->button_0_not = !*s->hal->button_0;
    *s->hal->button_1 = packet[3] & 0x20;
    *s->hal->button_1_not = !*s->hal->button_1;
    *s->hal->button_2 = packet[3] & 0x40;
    *s->hal->button_2_not = !*s->hal->button_2;
    *s->hal->button_3 = packet[3] & 0x80;
    *s->hal->button_3_not = !*s->hal->button_3;
    *s->hal->button_4 = packet[4] & 0x01;
    *s->hal->button_4_not = !*s->hal->button_4;

    {
        int curr_count = packet[1];

        if (s->read_first_event == 0) {
            // NB: don't clear this count or the shuttle will "rewind" to zero
            //     after an unplug/replug
            //     leave 'counts' alone
            //*s->hal->counts = 0;
            s->prev_count = curr_count;
            s->read_first_event = 1;
        } else {
            int diff_count = curr_count - s->prev_count;
            if (diff_count > 128) diff_count -= 256;
            if (diff_count < -128) diff_count += 256;
            *s->hal->counts += diff_count;
            s->prev_count = curr_count;
        }
    }

    *s->hal->spring_wheel_s32 = packet[0];
    *s->hal->spring_wheel_f = packet[0] / 7.0;

    return 0;
}


int alloc_hal_shuttlexpress(int index) {
    struct shuttlexpress_t *s;
    int r;

    s = &shuttlexpress[index];

    s->hal = (struct shuttlexpress_hal *)hal_malloc(sizeof(struct shuttlexpress_hal));
    if (s->hal == NULL) {
        fprintf(stderr, "%s: ERROR: unable to allocate HAL shared memory\n", modname);
        return 1;
    }

    r = hal_pin_bit_newf(HAL_OUT, &(s->hal->button_0), hal_comp_id, "%s.%d.button-0", modname, index);
    if (r != 0) return 1;

    r = hal_pin_bit_newf(HAL_OUT, &(s->hal->button_0_not), hal_comp_id, "%s.%d.button-0-not", modname, index);
    if (r != 0) return 1;

    r = hal_pin_bit_newf(HAL_OUT, &(s->hal->button_1), hal_comp_id, "%s.%d.button-1", modname, index);
    if (r != 0) return 1;

    r = hal_pin_bit_newf(HAL_OUT, &(s->hal->button_1_not), hal_comp_id, "%s.%d.button-1-not", modname, index);
    if (r != 0) return 1;

    r = hal_pin_bit_newf(HAL_OUT, &(s->hal->button_2), hal_comp_id, "%s.%d.button-2", modname, index);
    if (r != 0) return 1;

    r = hal_pin_bit_newf(HAL_OUT, &(s->hal->button_2_not), hal_comp_id, "%s.%d.button-2-not", modname, index);
    if (r != 0) return 1;

    r = hal_pin_bit_newf(HAL_OUT, &(s->hal->button_3), hal_comp_id, "%s.%d.button-3", modname, index);
    if (r != 0) return 1;

    r = hal_pin_bit_newf(HAL_OUT, &(s->hal->button_3_not), hal_comp_id, "%s.%d.button-3-not", modname, index);
    if (r != 0) return 1;

    r = hal_pin_bit_newf(HAL_OUT, &(s->hal->button_4), hal_comp_id, "%s.%d.button-4", modname, index);
    if (r != 0) return 1;

    r = hal_pin_bit_newf(HAL_OUT, &(s->hal->button_4_not), hal_comp_id, "%s.%d.button-4-not", modname, index);
    if (r != 0) return 1;

    r = hal_pin_s32_newf(HAL_OUT, &(s->hal->counts), hal_comp_id, "%s.%d.counts", modname, index);
    if (r != 0) return 1;

    r = hal_pin_float_newf(HAL_OUT, &(s->hal->spring_wheel_f), hal_comp_id, "%s.%d.spring-wheel-f", modname, index);
    if (r != 0) return 1;

    r = hal_pin_s32_newf(HAL_OUT, &(s->hal->spring_wheel_s32), hal_comp_id, "%s.%d.spring-wheel-s32", modname, index);
    if (r != 0) return 1;

    *s->hal->button_0 = 0;
    *s->hal->button_0_not = 1;
    *s->hal->button_1 = 0;
    *s->hal->button_1_not = 1;
    *s->hal->button_2 = 0;
    *s->hal->button_2_not = 1;
    *s->hal->button_3 = 0;
    *s->hal->button_3_not = 1;
    *s->hal->button_4 = 0;
    *s->hal->button_4_not = 1;
    *s->hal->counts = 0;
    *s->hal->spring_wheel_f = 0.0;
    *s->hal->spring_wheel_s32 = 0;

    return 0;
}


void check_for_shuttlexpress(char *dev_filename) {
    struct shuttlexpress_t *s;
    struct hidraw_devinfo devinfo;
    char name[100];
    int r;
    int sx_index;

    //fprintf(stderr, "%s: checking %s\n", modname, dev_filename);

    for (sx_index  = 0; sx_index  < num_shuttlexpress_entries; sx_index ++) {
        if (shuttlexpress[sx_index].device_file[0] == '\0') {
            // found the first available entry
            //fprintf(stderr, "sx_index: %d\n", sx_index);
            break;
        }
    }
    s = &shuttlexpress[sx_index];

    s->fd = open(dev_filename, O_RDONLY);
    if (s->fd < 0) {
        // probably just no read permission on the mouse or keyboard, quietly continue
        //fprintf(stderr, "%s: error opening %s: %s\n", modname, dev_filename, strerror(errno));
        //if (errno == EACCES) {
        //    fprintf(stderr, "%s: make sure you have read permission on %s, read the shuttlexpress(1) manpage for more info\n", modname, dev_filename);
        //}
        return;
    }

    r = ioctl(s->fd, HIDIOCGRAWINFO, &devinfo);
    if (r < 0) {
        fprintf(stderr, "%s: error with ioctl HIDIOCGRAWINFO on %s: %s\n", modname, dev_filename, strerror(errno));
        close(s->fd);
        return;
    }

    if (devinfo.vendor != VENDOR_ID) {
        // commented out to reduce useless noise about keyboards and mice not being shuttles
        //fprintf(stderr, "%s: dev %s has unexpected Vendor ID 0x%04x (expected Contour Design, 0x%04x)\n", modname, dev_filename, devinfo.vendor, VENDOR_ID);
        close(s->fd);
        return;
    }

    if (devinfo.product != PRODUCT_ID) {
        fprintf(stderr, "%s: dev %s has unexpected Product ID 0x%04x (expected ShuttleXpress, 0x%04x)\n", modname, dev_filename, devinfo.product, PRODUCT_ID);
        close(s->fd);
        return;
    }

    r = ioctl(s->fd, HIDIOCGRAWNAME(99), name);
    if (r < 0) {
        fprintf(stderr, "%s: error with ioctl HIDIOCGRAWNAME on %s: %s\n", modname, dev_filename, strerror(errno));
        close(s->fd);
        return;
    }
    fprintf(stderr, "%s: found '%s' on device '%s'\n", modname, name, dev_filename);

    // clear first event flag
    s->read_first_event = 0;

    // mark as present
    s->present = 1;

    // copy device filename to shuttlexpress structure
    strncpy(s->device_file, dev_filename, sizeof(s->device_file) - 1);
    s->device_file[sizeof(s->device_file) - 1] = '\0';
}


// returns file handle or -1 if error
int shuttle_present(char *dev_filename)
{
    int fd;
    struct hidraw_devinfo devinfo;
    char name[100];
    int r;

    fd = open(dev_filename, O_RDONLY);
    if (fd < 0) {
        // probably just no read permission on the mouse or keyboard, quietly continue
        //fprintf(stderr, "%s: error opening %s: %s\n", modname, dev_filename, strerror(errno));
        //if (errno == EACCES) {
        //    fprintf(stderr, "%s: make sure you have read permission on %s, read the shuttlexpress(1) manpage for more info\n", modname, dev_filename);
        //}
        return -1;
    }

    r = ioctl(fd, HIDIOCGRAWINFO, &devinfo);
    if (r < 0) {
        fprintf(stderr, "%s: error with ioctl HIDIOCGRAWINFO on %s: %s\n", modname, dev_filename, strerror(errno));
        close(fd);
        return -1;
    }

    if (devinfo.vendor != VENDOR_ID) {
        fprintf(stderr, "%s: dev %s has unexpected Vendor ID 0x%04x (expected Contour Design, 0x%04x)\n", modname, dev_filename, devinfo.vendor, VENDOR_ID);
        close(fd);
        return -1;
    }

    if (devinfo.product != PRODUCT_ID) {
        fprintf(stderr, "%s: dev %s has unexpected Product ID 0x%04x (expected ShuttleXpress, 0x%04x)\n", modname, dev_filename, devinfo.product, PRODUCT_ID);
        close(fd);
        return -1;
    }

    r = ioctl(fd, HIDIOCGRAWNAME(99), name);
    if (r < 0) {
        fprintf(stderr, "%s: error with ioctl HIDIOCGRAWNAME on %s: %s\n", modname, dev_filename, strerror(errno));
        close(fd);
        return -1;
    }
    printf("%s: found '%s' on device '%s'\n", modname, name, dev_filename);
    return fd;
}


void glob_dev_names(glob_t *gb) {
    int r;

    r = glob("/dev/hidraw*", 0, NULL, gb);
    if (r == GLOB_NOMATCH) {
        //fprintf(stderr, "%s: no /dev/hidraw* device entries found, is device plugged in?\n", modname);
    } else if (r != 0) {
        fprintf(stderr, "%s: error with glob!\n", modname);
        exit(1);
   }

}


// returns != 0 if this device name is not already in the shuttle_express array
int dev_not_known(char *dev_filename) {
    int i;

    for (i = 0; i < num_shuttlexpress_entries; i++) {
        if (!strcmp(dev_filename, shuttlexpress[i].device_file)) {
            // found it
            return 0;
        }
    }
    // didn't find this device
    return 1;
}


// returns != 0 if this device name is already in the shuttle_express array
int dev_known_and_not_present(char *dev_filename) {
    int i;

    for (i = 0; i < num_shuttlexpress_entries; i++) {
        if (!strcmp(dev_filename, shuttlexpress[i].device_file)) {
            // found it
            // is it also not present?
            if (!shuttlexpress[i].present) {
                // not present
                return 1;
            }
        }
    }
    // didn't find this device as not present
    return 0;
}


// returns -1 if not found
int get_shuttle_array_index(char *dev_filename) {
    int i;

    for (i = 0; i < num_shuttlexpress_entries; i++) {
        if (!strcmp(dev_filename, shuttlexpress[i].device_file)) {
            // found it
            return i;
        }
    }
    // didn't find this device
    return -1;
}


int main(int argc, char *argv[]) {
    int i;
    glob_t glob_buffer;

    char **names = NULL;
    int num_hidraw_names = 0;

    struct timeval timeout;

    hal_comp_id = hal_init(modname);
    if (hal_comp_id < 1) {
        fprintf(stderr, "%s: ERROR: hal_init failed\n", modname);
        exit(1);
    }

    signal(SIGINT, exit_handler);
    signal(SIGTERM, exit_handler);
    atexit(call_hal_exit);

    // must alloc all the HAL pins you might ever want before calling hal_ready()
    for (i = 0; i < num_shuttlexpress_entries; i++) {
        alloc_hal_shuttlexpress(i);
    }

    // get the list of device filenames to check for ShuttleXpress devices
    if (argc > 1) {
        // list of devices provided on the command line
        names = &argv[1];
        num_hidraw_names = argc - 1;
    } else {
        // probe for /dev/hidraw*
        glob_dev_names(&glob_buffer);
        names = glob_buffer.gl_pathv;
        num_hidraw_names = glob_buffer.gl_pathc;
    }

    // probe for ShuttleXpress devices on all those device file names
    for (i = 0; i < num_hidraw_names; i ++) {
        check_for_shuttlexpress(names[i]);
    }
    // free glob_buffer
    globfree(&glob_buffer);

    if (num_shuttlexpress_entries == 0) {
        fprintf(stderr, "%s: no ShuttleXpress devices found\n", modname);
    }

    // tell HAL we're ready
    hal_ready(hal_comp_id);

    // select on all the hidraw devices, process events from the active ones
    while (1) {
        fd_set readers;
        int max_fd;
        int i;
        int r;
        int tmp_fd;
        int shuttle_index;

        FD_ZERO(&readers);
        max_fd = -1;

        for (i = 0; i < num_shuttlexpress_entries; i ++) {
            // only select() on shuttles we think are present
            if (shuttlexpress[i].present) {
                FD_SET(shuttlexpress[i].fd, &readers);
                max_fd = Max(max_fd, shuttlexpress[i].fd);
            }
        }

	// 3 second timeout
        timeout.tv_sec  = 3;
        timeout.tv_usec = 0;
        r = select(max_fd + 1, &readers, NULL, NULL, &timeout);

        if (r == 0) {
            // select() timed out
            // look for new or reappearing shuttles
            // probe for /dev/hidraw*
            glob_dev_names(&glob_buffer);
            names = glob_buffer.gl_pathv;
            num_hidraw_names = glob_buffer.gl_pathc;

            // probe for ShuttleXpress devices on all these device file names
            // but only for new devices or existing devices that are no longer present
            for (i = 0; i < num_hidraw_names; i++) {
                //fprintf(stderr, "glob %d: %s\n", i, names[i]);
                // 
                if (dev_not_known(names[i])) {
                    //fprintf(stderr, "not previously known as a shuttlexpress: %s\n", names[i]);
                    // haven't seen this device as a shuttle yet
                    check_for_shuttlexpress(names[i]);
                }
                else if (dev_known_and_not_present(names[i])) {
                    //fprintf(stderr, "previously known as a shuttlexpress but currently not present: %s\n", names[i]);
                    // we've seen this device but it's no longer present
                    // attempt to open same device name and verify it's a shuttle
                    shuttle_index = get_shuttle_array_index(names[i]);
                    //fprintf(stderr, "shuttle_index: %d\n", shuttle_index);
                    if (shuttle_index != -1) {
                        tmp_fd = shuttle_present(shuttlexpress[shuttle_index].device_file);
                        if (tmp_fd >= 0) {
                            // it's back
                            //fprintf(stderr, "The shuttle is baaaaaack . . . %s\n", shuttlexpress[shuttle_index].device_file);
                            // clear first event flag
                            shuttlexpress[shuttle_index].read_first_event = 0;
                            shuttlexpress[shuttle_index].present = 1;
                            shuttlexpress[shuttle_index].fd = tmp_fd;
                        }
                    }
                }
            }

            // free glob_buffer
            globfree(&glob_buffer);
        }

        if (r < 0) {
            if ((errno == EAGAIN) || (errno == EINTR)) continue;
            fprintf(stderr, "%s: error with select!\n", modname);
            exit(1);
        }

        for (i = 0; i < num_shuttlexpress_entries; i ++) {
            if (shuttlexpress[i].present && FD_ISSET(shuttlexpress[i].fd, &readers)) {
                r = read_update(&shuttlexpress[i]);
                if (r == -1) {
                    // read_update() returns -1 when shuttle is unplugged
                    // input/output error
                    // mark this shuttle as currently not present
                    shuttlexpress[i].present = 0;
                    // close file handle to prevent leak
                    close(shuttlexpress[i].fd);
                    shuttlexpress[i].fd = -1;
                }
                else if (r <= -2) {
                    // EOF error
                    fprintf(stderr, "error %d read on fd %d: EOF\n", r, shuttlexpress[i].fd);
                    exit(1);
                }
            }
        }
    }

    exit(0);
}

