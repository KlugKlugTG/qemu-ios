/* Startup boot-args API preserves Apple byte length and bounds portable escaped format.
 *
 * SLICE hw/arm/ipod_touch_2g.c define BOOT_ARGS_CMDLINE_LEN
 * SLICE hw/arm/ipod_touch_2g.c fn ipod_touch_requested_boot_args boot_args_as_format ipod_touch_set_boot_args
 * SLICE hw/arm/ipod_touch_2g.c fn ipod_touch_get_boot_args_delay_ms ipod_touch_set_boot_args_delay_ms ipod_touch_get_boot_args_repeat ipod_touch_set_boot_args_repeat ipod_touch_get_boot_args_interval_ms ipod_touch_set_boot_args_interval_ms
 * VARIANT
 * VARIANT -D__APPLE__=1
 * PKG glib-2.0
 */
#include <glib.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {void*cpu;void*boot_args_timer;unsigned boot_args_writes;uint32_t boot_args_delay_ms,boot_args_repeat,boot_args_interval_ms;uint64_t boot_args_scan_deadline,boot_args_scan_started_ms,boot_args_scan_attempts;bool boot_args_scan_failed,boot_args_explicit,boot_args_delay_ms_explicit,boot_args_repeat_explicit,boot_args_interval_ms_explicit;char boot_args[512];} IPodTouchMachineState;
typedef IPodTouchMachineState Object;
typedef int Error;
#define IPOD_TOUCH_MACHINE(o) (o)
#define error_setg(errp,...) (**(errp)=1)
typedef struct {bool input;uint32_t value;} Visitor;
static bool visit_type_uint32(Visitor *v,const char *name,uint32_t *value,Error **errp){if(v->input)*value=v->value;else v->value=*value;return true;}
#include "slice.h"

int main(void){
 IPodTouchMachineState machine={.boot_args_delay_ms=2000,.boot_args_repeat=24,.boot_args_interval_ms=500};
 setenv("IT_BOOT_ARGS","-v",1);
 assert(!ipod_touch_requested_boot_args(&machine));
 char oversized[512];memset(oversized,'x',511);oversized[511]=0;
 int error=0;Error*ep=&error;
 ipod_touch_set_boot_args(&machine,"",&ep);
 assert(!error&&machine.boot_args_explicit&&!ipod_touch_requested_boot_args(&machine));
 char exact[256];memset(exact,'x',255);exact[255]=0;
 ipod_touch_set_boot_args(&machine,exact,&ep);assert(!error&&strlen(machine.boot_args)==255);
 ipod_touch_set_boot_args(&machine,oversized,&ep);assert(error&&strlen(machine.boot_args)==255);error=0;
 machine.cpu=&machine;ipod_touch_set_boot_args(&machine,"changed",&ep);
 assert(error&&strlen(machine.boot_args)==255);
 error=0;machine.cpu=NULL;exact[254]='%';
 ipod_touch_set_boot_args(&machine,exact,&ep);
#ifdef __APPLE__
 assert(!error && !strcmp(machine.boot_args,exact));
#else
 assert(error);error=0;exact[254]='x';exact[253]='%';exact[254]=0;
 ipod_touch_set_boot_args(&machine,exact,&ep);assert(!error && !strcmp(machine.boot_args,exact));
 char format[256];assert(boot_args_as_format(exact,format,sizeof(format)) && strlen(format)==255);
#endif
 Visitor v={.input=true,.value=123};error=0;
 ipod_touch_set_boot_args_delay_ms(&machine,&v,"",NULL,&ep);
 assert(!error && machine.boot_args_delay_ms==123 && machine.boot_args_delay_ms_explicit);
 v.value=7;ipod_touch_set_boot_args_repeat(&machine,&v,"",NULL,&ep);assert(!error && machine.boot_args_repeat==7 && machine.boot_args_repeat_explicit);
 v.value=456;ipod_touch_set_boot_args_interval_ms(&machine,&v,"",NULL,&ep);assert(!error && machine.boot_args_interval_ms==456 && machine.boot_args_interval_ms_explicit);
 v.input=false;ipod_touch_get_boot_args_delay_ms(&machine,&v,"",NULL,&ep);assert(v.value==123);
 ipod_touch_get_boot_args_repeat(&machine,&v,"",NULL,&ep);assert(v.value==7);
 ipod_touch_get_boot_args_interval_ms(&machine,&v,"",NULL,&ep);assert(v.value==456);
 v.input=true;v.value=3600001;ipod_touch_set_boot_args_delay_ms(&machine,&v,"",NULL,&ep);assert(error && machine.boot_args_delay_ms==123);error=0;
 v.value=1000001;ipod_touch_set_boot_args_repeat(&machine,&v,"",NULL,&ep);assert(error && machine.boot_args_repeat==7);error=0;
 v.value=0;ipod_touch_set_boot_args_interval_ms(&machine,&v,"",NULL,&ep);assert(error && machine.boot_args_interval_ms==456);error=0;
 machine.cpu=&machine;v.value=3;ipod_touch_set_boot_args_repeat(&machine,&v,"",NULL,&ep);assert(error && machine.boot_args_repeat==7);
 puts("PASS startup API: original Apple length policy and portable escaped-format bounds; empty immutable arguments");
}
