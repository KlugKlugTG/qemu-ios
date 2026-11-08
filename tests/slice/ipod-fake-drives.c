/* The fake drives the portable front end offers an app's menu (battery, cellular).
 *
 * SLICE contrib/ios-app/qemu-ios-ui.c range struct portable_prop_write { | static void portable_prop_write_bh
 * SLICE contrib/ios-app/qemu-ios-ui.c fn portable_prop_write_bh portable_prop_write qemu_ios_ui_battery_set qemu_ios_ui_modem_write
 *
 * Battery: level 0..100 only, charging 0 auto / 1 on / 2 off, the level and the
 * charging word both queued where the machine has "battery-charging" and the level
 * alone where it has not (the iPad's charging is its USB port's). Cellular: one
 * property of the machine's "baseband-modem" child from its string form, and only
 * the ones that object owns -- an unknown name is refused before anything is
 * queued, so an app can hide a command the machine cannot honour.
 *
 * Mutation (named, must fail this test): in portable_prop_write, drop the
 *     !object_property_find(object, name)
 * guard so a name the object does not own is queued anyway, and the modem half of
 * this test then reads a write the model never sees.
 */
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct { int id; } Object;
typedef int Error;
typedef void *ObjectProperty;
#define OBJECT(x) (x)
#define g_new(type, n) ((type *)calloc((n), sizeof(type)))
#define g_free free

static char *g_strdup(const char *s) { char *c = malloc(strlen(s) + 1); strcpy(c, s); return c; }
static char *g_strdup_printf(const char *format, ...)
{
    char buffer[64];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    return g_strdup(buffer);
}

static bool ready;
static Object machine = { 1 }, modem = { 2 };
static void (*queued_fn[8])(void *);
static void *queued_arg[8];
static int queued;

static bool qemu_ios_ui_ready(void) { return ready; }
static Object *qdev_get_machine(void) { return &machine; }
static void *qemu_get_aio_context(void) { return (void *)3; }
static void aio_bh_schedule_oneshot(void *ctx, void (*fn)(void *), void *arg)
{ assert(ctx == (void *)3 && queued < (int)(sizeof queued_fn / sizeof queued_fn[0])); queued_fn[queued] = fn; queued_arg[queued] = arg; queued++; }

/* What each object's property table holds: the machine's battery level always and
 * its charging word only where the board models one; the modem's own set behind
 * the "baseband-modem" child. */
static bool has_charging = true;
static ObjectProperty *object_property_find(Object *object, const char *name)
{
    if (object == &machine)
        return (!strcmp(name, "battery-level") || (has_charging && !strcmp(name, "battery-charging"))) ? (ObjectProperty)1 : NULL;
    if (object == &modem)
        return (!strcmp(name, "incoming-call") || !strcmp(name, "incoming-sms") ||
                !strcmp(name, "remote-hangup") || !strcmp(name, "signal-dbm")) ? (ObjectProperty)1 : NULL;
    return NULL;
}

static Object *object_resolve_path_component(Object *parent, const char *part)
{
    assert(parent == &machine);
    return !strcmp(part, "baseband-modem") ? &modem : NULL;
}

/* Every parse the bottom half performs, in order. The names and values are copied:
 * the production bottom half frees its own copies as soon as it returns. */
static struct { char object[16], name[64], value[256]; } writes[8];
static int write_count;
static void object_property_parse(Object *object, const char *name, const char *value, Error **err)
{
    assert(write_count < (int)(sizeof writes / sizeof writes[0]));
    snprintf(writes[write_count].object, sizeof writes[write_count].object, "%s",
             object == &machine ? "machine" : "modem");
    snprintf(writes[write_count].name, sizeof writes[write_count].name, "%s", name);
    snprintf(writes[write_count].value, sizeof writes[write_count].value, "%s", value);
    write_count++;
    (void)err;
}
static const char *error_get_pretty(Error *error) { (void)error; return "refused"; }
static void error_free(Error *error) { (void)error; }

#include "slice.h"

/* One property write is one bottom half, so a battery update with charging queues
 * two: run them in the order they were queued. */
static void run_pending(void)
{
    int n = queued;
    queued = 0;
    for (int i = 0; i < n; i++) queued_fn[i](queued_arg[i]);
}

int main(void) {
    /* Nothing before the machine is up, and nothing for a value out of range. */
    assert(!qemu_ios_ui_battery_set(60, 0) && !queued);
    ready = true;
    assert(!qemu_ios_ui_battery_set(-1, 0));
    assert(!qemu_ios_ui_battery_set(101, 0));
    assert(!qemu_ios_ui_battery_set(60, -1));
    assert(!qemu_ios_ui_battery_set(60, 3));
    assert(!queued && !write_count);

    /* A level and a charging word, both queued, in that order. */
    const char *modes[] = { "auto", "on", "off" };
    for (int mode = 0; mode < 3; mode++) {
        char level[8];
        snprintf(level, sizeof level, "%d", mode * 50);
        write_count = 0;
        assert(qemu_ios_ui_battery_set(mode * 50, mode));
        assert(!write_count && queued == 2);        /* queued, not written yet */
        run_pending();
        assert(write_count == 2 && !queued);
        assert(!strcmp(writes[0].object, "machine") && !strcmp(writes[0].name, "battery-level") &&
               !strcmp(writes[0].value, level));
        assert(!strcmp(writes[1].name, "battery-charging") && !strcmp(writes[1].value, modes[mode]));
    }

    /* The iPad has no battery-charging: the level alone. */
    has_charging = false;
    write_count = 0;
    assert(qemu_ios_ui_battery_set(70, 2));
    assert(queued == 1);
    run_pending();
    assert(write_count == 1 && !strcmp(writes[0].name, "battery-level") && !strcmp(writes[0].value, "70"));
    has_charging = true;

    /* Cellular: the machine's child, and only the properties it owns. */
    write_count = 0;
    assert(qemu_ios_ui_modem_write("incoming-sms", "5551234|hello"));
    assert(!write_count && queued == 1);
    run_pending();
    assert(write_count == 1 && !strcmp(writes[0].object, "modem") &&
           !strcmp(writes[0].name, "incoming-sms") && !strcmp(writes[0].value, "5551234|hello"));
    assert(qemu_ios_ui_modem_write("incoming-call", "5551234"));
    run_pending();
    assert(write_count == 2 && !strcmp(writes[1].name, "incoming-call") && !strcmp(writes[1].value, "5551234"));
    assert(!qemu_ios_ui_modem_write("incoming-mms", "x") && !queued);
    assert(!qemu_ios_ui_modem_write("", "x") && !queued);
    assert(!qemu_ios_ui_modem_write(NULL, "x") && !queued);
    assert(!qemu_ios_ui_modem_write("incoming-call", NULL) && !queued);
    assert(write_count == 2);

    puts("PASS: battery bounds and charging where the board has it, modem properties only where the modem owns them");
}
