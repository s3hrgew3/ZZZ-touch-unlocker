/*
 * uinput-tap -- synthesize one touch contact on a temporary uinput device.
 *
 * Used by scripts/check-wine-touch.sh to drive touch-probe.exe without a human
 * finger. The device is created for the duration of the tap and destroyed
 * afterwards, so nothing is left behind in the input stack.
 *
 * Coordinates are normalized 0..32767 over the whole screen, like a real
 * touchscreen reports them.
 *
 *   uinput-tap [--x N] [--y N] [--hold MS] [--name NAME]
 *
 * Requires write access to /dev/uinput (root, or the `input` group).
 *
 * Build: gcc -O2 -o uinput-tap wine/uinput-tap.c
 */
#include <linux/uinput.h>
#include <linux/input.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int emit(int fd, int type, int code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    return (int)write(fd, &ev, sizeof ev);
}

static int abs_setup(int fd, int code, int min, int max)
{
    struct uinput_abs_setup setup;
    memset(&setup, 0, sizeof setup);
    setup.code = code;
    setup.absinfo.minimum = min;
    setup.absinfo.maximum = max;
    return ioctl(fd, UI_ABS_SETUP, &setup);
}

int main(int argc, char **argv)
{
    int x = 16383, y = 16383, hold_ms = 300, fd, i;
    const char *name = "zzz-touch-tap";
    struct uinput_setup setup;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--x") && i + 1 < argc) x = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--y") && i + 1 < argc) y = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--hold") && i + 1 < argc) hold_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else {
            printf("usage: %s [--x N] [--y N] [--hold MS] [--name NAME]\n"
                   "  coordinates are 0..32767 over the screen (default: centre)\n", argv[0]);
            return 2;
        }
    }
    if (hold_ms < 10) hold_ms = 10;

    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "uinput-tap: open /dev/uinput: %s\n", strerror(errno));
        fprintf(stderr, "uinput-tap: needs root or membership of the 'input' group\n");
        return 1;
    }
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    abs_setup(fd, ABS_MT_SLOT, 0, 9);
    abs_setup(fd, ABS_MT_TRACKING_ID, 0, 65535);
    abs_setup(fd, ABS_MT_POSITION_X, 0, 32767);
    abs_setup(fd, ABS_MT_POSITION_Y, 0, 32767);

    memset(&setup, 0, sizeof setup);
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1234;
    setup.id.product = 0x5678;
    snprintf(setup.name, sizeof setup.name, "%s", name);
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "uinput-tap: create device: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    usleep(300000); /* let the compositor pick the device up */

    emit(fd, EV_ABS, ABS_MT_SLOT, 0);
    emit(fd, EV_ABS, ABS_MT_TRACKING_ID, 42);
    emit(fd, EV_ABS, ABS_MT_POSITION_X, x);
    emit(fd, EV_ABS, ABS_MT_POSITION_Y, y);
    emit(fd, EV_KEY, BTN_TOUCH, 1);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    usleep((useconds_t)hold_ms * 1000);
    emit(fd, EV_ABS, ABS_MT_POSITION_X, x + 2);
    emit(fd, EV_ABS, ABS_MT_POSITION_Y, y + 2);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    usleep(60000);
    emit(fd, EV_ABS, ABS_MT_SLOT, 0);
    emit(fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
    emit(fd, EV_KEY, BTN_TOUCH, 0);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    usleep(100000);

    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    printf("uinput-tap: contact at %d,%d held %d ms\n", x, y, hold_ms);
    return 0;
}
