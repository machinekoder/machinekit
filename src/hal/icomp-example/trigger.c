
#include "config.h"
#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"
#include "hal_priv.h"

MODULE_AUTHOR("Michael Haberler");
MODULE_DESCRIPTION("component to export trigger usrfuncts, and execute their chains");
MODULE_LICENSE("GPL");

static int comp_id;
static char *compname = "trigger";

struct inst_data {
    hal_u32_t *count; // number of invocations
};

static char *name;
RTAPI_IP_STRING(name, "name of trigger funct");

static int run_chain(const hal_funct_args_t *trigger)
{
    const hal_funct_t *f = trigger->funct;
    struct inst_data *ip = trigger->funct->arg;
    hal_thread_t fakethread; // for now, fake we're a thread
    hal_funct_entry_t *funct_root, *funct_entry;
    long long int end_time;
    int nfuncts = 0;
    fakethread.period = 1000000;

    // fa_period(fa) - formerly 'long period'
    // fa_thread_start_time(fa): _actual_ start time of thread invocation
    // fa_start_time(fa):        _actual_ start time of function invocation
    // fa_thread_name(fa): name of the calling thread (char *)
    // fa_funct_name(fa):  name of the this called function (char *)

    hal_funct_args_t fa = {
	.thread = &fakethread,
	.argc = 0,
	.argv = NULL,
    };

    //if (!hal_data->threads_running)
    //    return -EBUSY;

    funct_root = (hal_funct_entry_t *) & (f->funct_list);
    funct_entry = SHMPTR(funct_root->links.next);

    /* execution time logging */
    fa.start_time = rtapi_get_clocks();
    end_time = fa.start_time;
    fa.thread_start_time = fa.start_time;

    while (funct_entry != funct_root) {

	fa.funct = SHMPTR(funct_entry->funct_ptr);

	HALDBG("calling '%s'", fa_funct_name(&fa));

	switch (funct_entry->type) {

	case FS_LEGACY_THREADFUNC:
	    funct_entry->funct.l(funct_entry->arg, fakethread.period);
	    nfuncts++;
	    break;

	case FS_XTHREADFUNC:
	    funct_entry->funct.x(funct_entry->arg, &fa);
	    nfuncts++;
	    break;

	default:
	    // bad - a mistyped funct
	    ;
	}
	/* capture execution time */
	end_time = rtapi_get_clocks();
	/* update execution time data */
#if 0
	*(fa.funct->runtime) = (hal_s32_t)(end_time - fa.start_time);
	if ( *(fa.funct->runtime) > fa.funct->maxtime) {
	    fa.funct->maxtime = *(fa.funct->runtime);
	    fa.funct->maxtime_increased = 1;
	} else {
	    fa.funct->maxtime_increased = 0;
	}
#endif
	/* point to next next entry in list */
	funct_entry = SHMPTR(funct_entry->links.next);
	/* prepare to measure time for next funct */
	//	fa.start_time = end_time;
    }
    *(ip->count) += 1;
    /* thread->runtime = (hal_s32_t)(end_time - fa.thread_start_time); */
    /* if (thread->runtime > thread->maxtime) { */
    /* 	thread->maxtime = thread->runtime; */
    /* } */
    return 0;
}

static int export_halobjs(struct inst_data *ip, int inst_id, const char *name)
{
    int retval;

    if ((retval = hal_pin_u32_newf(HAL_OUT, &ip->count, inst_id, "%s.count", name)) < 0)
	return retval;

    hal_export_xfunct_args_t trigger = {
	.type = FS_TRIGGER,
	.funct.u = run_chain,
	.arg = (void *) ip,
	.owner_id = inst_id
    };
    if ((retval = hal_export_xfunctf( &trigger, name)) < 0)
	return retval;

    return 0;
}

static int instantiate(const int argc, char* const *argv)
{
    // argv[0]: component name
    const char *name = argv[1]; // instance name
    struct inst_data *ip;
    int inst_id, retval;

    if ((inst_id = hal_inst_create(name, comp_id,
				   sizeof(struct inst_data),
				   (void **)&ip)) < 0)
	return inst_id;

    HALDBG("inst=%s argc=%d\n", name, argc);

    retval = export_halobjs(ip, inst_id, name);
    return retval;
}

int rtapi_app_main(void)
{
    comp_id = hal_xinit(TYPE_RT, 0, 0, instantiate, NULL, compname);
    if (comp_id < 0)
	return -1;

    hal_ready(comp_id);
    return 0;
}

void rtapi_app_exit(void)
{
    hal_exit(comp_id); // calls delete() on all insts
}
