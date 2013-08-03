#include <stdio.h>
#include <emc/motion/motion.h>
#include <emc/motion/motion_debug.h>

#include <protobuf/generated/test.npb.h>
#include <protobuf/generated/value.npb.h>
#include <protobuf/generated/object.npb.h>
#include <protobuf/generated/message.npb.h>
#include <protobuf/generated/motcmds.npb.h>


int main()
{
    printf("emcmot_joint_t = %d\n", sizeof(emcmot_joint_t));
    printf("emcmot_joint_status_t = %d\n", sizeof(emcmot_joint_status_t));
    printf("emcmot_command_t = %d\n", sizeof(emcmot_command_t));
    printf("spindle_status = %d\n", sizeof(spindle_status));
    printf("emcmot_status_t = %d\n", sizeof(emcmot_status_t));
    printf("emcmot_config_t = %d\n", sizeof(emcmot_config_t));
    printf("emcmot_debug_t = %d\n", sizeof(emcmot_debug_t));

    printf("npb Container = %d\n", sizeof(Container));
    printf("npb Test1 = %d\n", sizeof(Test1));
    printf("npb Value = %d\n", sizeof(Value));
    printf("npb Object = %d\n", sizeof(Object));
    printf("npb Telegram = %d\n", sizeof(Telegram));


    printf("npb MotionCommand = %d\n", sizeof(MotionCommand));


    return 0;
}
