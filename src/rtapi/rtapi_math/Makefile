CC = gcc
CFLAGS = -g -O2 -Wall -fno-builtin -I ../ -I ../../ -D RTAPI -D BUILD_SYS_USER_DSO -D_IEEE_LIBM \
	-D_ISOC99_SOURCE \
	-D_SVID_SOURCE
AR = ar
RANLIB = ranlib
AS = as

INCS = ../

xeno_math-objs := s_floor.o
xeno_math-objs += e_pow.o
xeno_math-objs += w_pow.o
xeno_math-objs += s_fabs.o
xeno_math-objs += e_sqrt.o
xeno_math-objs += w_sqrt.o
xeno_math-objs += s_ceil.o
xeno_math-objs += e_acos.o
xeno_math-objs += w_acos.o
xeno_math-objs += e_asin.o
xeno_math-objs += w_asin.o
xeno_math-objs += s_tan.o
xeno_math-objs += k_tan.o
xeno_math-objs += s_atan.o
xeno_math-objs += w_atan2.o
xeno_math-objs += e_atan2.o
xeno_math-objs += s_sin.o
xeno_math-objs += k_sin.o
xeno_math-objs += s_cos.o
xeno_math-objs += k_cos.o
xeno_math-objs += e_rem_pio2.o
xeno_math-objs += k_rem_pio2.o
xeno_math-objs += s_scalbn.o
xeno_math-objs += k_standard.o
xeno_math-objs += s_cbrt.o
xeno_math-objs += s_copysign.o
xeno_math-objs += s_finite.o
xeno_math-objs += s_rint.o
xeno_math-objs += powidf.o


libmd.a: $(xeno_math-objs) $(INCS)
	# for real Unix:
	$(AR) rv libmd.a $(xeno_math-objs)
