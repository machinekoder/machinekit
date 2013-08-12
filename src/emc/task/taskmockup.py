# simulatr task behavior:
# accept commands
# assign ticket number and tack onto reply
# do 'work'
# signal completion via PUB socket


import zmq
import os, time

context = zmq.Context()

identity = 'task-%d' % (os.getpid())

# the command submission socket
command = context.socket(zmq.ROUTER)
command.bind("tcp://*:5556")
command.setsockopt(zmq.IDENTITY, identity )

# the command completion publisher
completion = context.socket(zmq.PUB)
completion.bind ("tcp://127.0.0.1:5557")



def main():
    """ main method """
    ticket= 0

    while True:
        ticket += 1
        [address, contents] = command.recv_multipart()
        print("[%s] %s, assigned ticket %d" % (address, contents, ticket))

        reply = "ack for " + contents + " to " + address

        # simulate reception acknowledgment:
        # route to client (frame 0)
        # ticket = frame 1
        # any other fluff = frame 2..
        command.send_multipart ([address, str(ticket), reply])

        # 'processing'
        time.sleep(0.5)

        # done, push a completion message directed
        # at the command originator
        completion.send_multipart ([address, str(ticket), "done"])


if __name__ == "__main__":
    main()
