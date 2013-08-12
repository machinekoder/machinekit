# a client simulating:
#
# command injection
# acknowledgment and ticket number extraction
# waitforcommandcomplete:
#  receive completion messages until the ticket
#  matches up

import zmq
import os

context = zmq.Context()
identity = 'client-%d' % (os.getpid())

command = context.socket(zmq.DEALER)
command.setsockopt(zmq.IDENTITY, identity )
command.connect ("tcp://localhost:5556")

# the command completion publisher
completion = context.socket(zmq.SUB)

# receive only completions directed at us:
completion.setsockopt(zmq.SUBSCRIBE, identity)

# connect to task completion publisher
completion.connect ("tcp://localhost:5557")


for request in range (10):

    # send a command
    command.send("command %d" % request)

    # the reply will contain the ticker number assigned
    [t, other] = command.recv_multipart()
    ticket = int(t)
    print("command ack: [%d] %s" % (ticket, other))

    # simulate 'waitforcommandcomplete'
    # read from the completion socket until tickets match up
    # FIXME: this needs a timeout
    while True:
        [address, cticket, rest] = completion.recv_multipart()
        completed = int(cticket)
        print "command completion: [%d] %s" % (completed,rest)
        if ticket == completed:
            print "matching ticket found: [%d]" % (completed)
            break
