
#ifndef __INCLUDE_HM2_ETH_H
#define __INCLUDE_HM2_ETH_H

#include RTAPI_INC_SLAB_H

#define HM2_ETH_VERSION "0.1"
#define HM2_LLIO_NAME "hm2_eth"

typedef struct {
    hm2_lowlevel_io_t llio;

    struct list_head list;
} hm2_eth_t;

#endif
