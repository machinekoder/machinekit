// inject an EMC_TASK_ABORT NML message into task via zmq
// based on zguide Hello World client

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>

#include <czmq.h>

#include "config.h"
#include "rcs.hh"
#include "emc.hh"
#include "emc_nml.hh"

#include <google/protobuf/text_format.h>
#include <google/protobuf/message_lite.h>

#include <protobuf/generated/types.pb.h>
#include <protobuf/generated/message.pb.h>

using namespace std;
using namespace google::protobuf;

double timeout = 2.0;

static zctx_t *z_context;
static void *z_task, *z_update;
char z_ident[20];

#define REPLY_TIMEOUT 3000 //ms
#define UPDATE_TIMEOUT 3000 //ms


static int z_init(void)
{
    int rc;

    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    snprintf(z_ident, sizeof(z_ident), "zmqcmd%d", getpid());

    z_context = zctx_new ();
    z_task = zsocket_new (z_context, ZMQ_DEALER);
    zsocket_set_linger (z_task, 0);
    zsocket_set_identity (z_task, z_ident);
    zsocket_set_rcvtimeo (z_task, REPLY_TIMEOUT);
    rc = zsocket_connect(z_task, "tcp://127.0.0.1:5556");
    assert (rc == 0);

    z_update = zsocket_new (z_context, ZMQ_SUB);
    zsocket_set_subscribe (z_update, z_ident);
    zsocket_set_rcvtimeo (z_update, UPDATE_TIMEOUT);

    rc = zsocket_connect (z_update, "tcp://127.0.0.1:5557");
    assert (rc == 0);

    return 0;
}

static void z_finish()
{
    zctx_destroy (&z_context);
}

static int waitTicketCompleted(int ticket, double timeout)
{
    static int last_ticket = -1;
    static int last_status = -1;

    fprintf(stderr, "wait_for_ticket: %d\n", ticket);

    // emcwaitdone equivalent: listen on update channel until our
    // ticket shows up with RCS_DONE or RCS_ERROR
    do {
	zmsg_t *m_update = zmsg_recv (z_update);

	Container update;
	int status, cticket;

	if (m_update == NULL) {
	    fprintf(stderr, "no ticket update after %g secs - task running?\n", timeout);
	    return -ETIMEDOUT;
	}

        char *dest = zmsg_popstr (m_update);
	free(dest);
	zframe_t *pb_update  = zmsg_pop (m_update);

	assert(update.ParseFromArray(zframe_data(pb_update),zframe_size(pb_update)));
	zmsg_destroy(&m_update);

	assert(update.type() == MT_TICKET_UPDATE);
	assert(update.has_ticket_update());

	status = update.ticket_update().status();
	cticket = update.ticket_update().cticket();

	fprintf(stderr, "update.ticket_update.cticket = %d status=%d\n",
		cticket, status);

	if (ticket == cticket) {
	    // the ticket we got handed back changed state:
	    last_ticket = cticket;
	    last_status = status;

	    switch (status) {
	    case RCS_DONE:
		fprintf(stderr, "ticket %d OK\n", ticket);
		return status;
		break;

	    case RCS_ERROR:
		fprintf(stderr, "ticket %d ERROR\n", ticket);
		return status;
		break;

	    default:
		;
	    }
	}
    } while (1);
}

int main (int argc, const char **argv)
{
    int rc, ticket;
    Container request, response;

    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    snprintf(z_ident, sizeof(z_ident), "cmd%d", getpid());
    printf ("Connecting to task via zmq, socket identity=%s\n", z_ident);

    z_init();

    EMC_TASK_ABORT m;
    m.serial_number = 4711;

    // construct protobuf message
    // container type
    request.set_type(MT_LEGACY_NML);
    // submessage: opaque byte blob
    request.set_legacy_nml(&m, sizeof(m));

    // determine the size of the encoded message (cached)
    size_t pb_request_size = request.ByteSize();
    // allocate a frame of that size
    zframe_t *pb_frame = zframe_new (NULL, pb_request_size);

    // pack it into the frame
    assert(request.SerializeToArray(zframe_data (pb_frame), zframe_size (pb_frame)));
    // send it off
    assert(zframe_send (&pb_frame, z_task, 0) == 0);

   zmsg_t *reply = zmsg_recv (z_task);
    if (!reply) {
	fprintf(stderr, "%s:  z_submi: reply timeout\n", z_ident);
	z_finish();
	exit(1);
    }

    // obtain response (with timeout)
    zframe_t *pb_response  = zmsg_pop (reply);
    if (reply == NULL) {
	fprintf(stderr, "no reply - task running?\n");
	z_finish();
	exit(1);
    }
    if (!response.ParseFromArray(zframe_data(pb_response),
				 zframe_size(pb_response))) {
	fprintf(stderr, "%s: response not a valid protobuf frame: %s\n",
		z_ident, zframe_strhex (pb_response));
	abort();
    }
    assert(response.has_task_reply());
    ticket = response.task_reply().ticket();

    fprintf(stderr, "%s: got ticket = %d\n", z_ident, ticket);


    rc = waitTicketCompleted(ticket, timeout);

    if (rc == -ETIMEDOUT) {
	fprintf(stderr, "no update for ticket %d after %g secs - task running?\n",
		ticket, timeout);
	z_finish();
	exit(1);
    }
    z_finish();
    exit(0);
}
