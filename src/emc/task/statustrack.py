# track task status updates, subscribing to all channels

import zmq

from types_pb2 import *
from message_pb2 import *

import google.protobuf.text_format

context = zmq.Context()

# the command completion publisher
completion = context.socket(zmq.SUB)

identity = ""  # all channels
completion.setsockopt(zmq.SUBSCRIBE, identity)

# connect to task completion publisher
completion.connect ("tcp://localhost:5557")


while True:
    [address, update] = completion.recv_multipart()
    container = Container()
    container.ParseFromString(update)
    print str(container)
