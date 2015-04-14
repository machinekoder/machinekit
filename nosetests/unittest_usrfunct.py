#!/usr/bin/env python

from nose import with_setup
from machinekit.nosetests.realtime import setup_module ,teardown_module
from machinekit.nosetests.support import fnear
from unittest import TestCase
import time,os,ConfigParser

from machinekit import rtapi,hal

class TestUserfuncts(TestCase):
    def setUp(self):

        self.cfg = ConfigParser.ConfigParser()
        self.cfg.read(os.getenv("MACHINEKIT_INI"))
        self.uuid = self.cfg.get("MACHINEKIT", "MKUUID")
        self.rt = rtapi.RTAPIcommand(uuid=self.uuid)


    def test_userfuncts(self):
        self.rt.loadrt("ufdemo")

        # this function returns argc:
        assert self.rt.callfunc("ufdemo.demo-funct") == 0
        assert self.rt.callfunc("ufdemo.demo-funct","a","b","c") == 3
        self.rt.unloadrt("ufdemo")

        # define a trigger called 'servo'
        self.rt.loadrt("trigger");
        self.rt.newinst("trigger","servo")
        servo_count = hal.Pin("servo.count")

        # not executed yet:
        assert servo_count.get() == 0

        # load some comps
        self.rt.loadrt("and2")
        self.rt.loadrt("or2")

        # chain threadfuncts onto trigger
        hal.addf("or2.0",  "servo")
        hal.addf("and2.0", "servo")

        or2_in0 = hal.Pin("or2.0.in0")
        or2_out = hal.Pin("or2.0.out")
        or2_in0.set(1)

        assert or2_out.get() == 0 # threadfunc not yet executed

        and2_in0 = hal.Pin("and2.0.in0")
        and2_in1 = hal.Pin("and2.0.in1")
        and2_out = hal.Pin("and2.0.out")
        and2_in0.set(1)
        and2_in1.set(1)

        # threadfunc not yet executed, so input pins had no effect yet
        assert and2_out.get() == 0

        # execute function chain once
        # check return value - all went well?
        assert self.rt.callfunc("servo") == 0

        # number of chain invocations now must be 1
        assert servo_count.get() == 1

        # and the comp output pins must have changed
        # as their thread functs have been executed
        assert or2_out.get() == 1
        assert and2_out.get() == 1


(lambda s=__import__('signal'):
     s.signal(s.SIGTERM, s.SIG_IGN))()
