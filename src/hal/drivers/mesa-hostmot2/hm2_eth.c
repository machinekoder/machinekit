
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "config_module.h"
#include RTAPI_INC_SLAB_H
#include RTAPI_INC_CTYPE_H
#include RTAPI_INC_STRING_H

#include "rtapi.h"
#include "rtapi_app.h"
#include "rtapi_string.h"

#include "hal.h"

#include "hostmot2-lowlevel.h"
#include "hostmot2.h"
#include "hm2_eth.h"
#include "lbp16.h"

#include "/usr/rtnet/include/rtnet.h"
#include <native/task.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Geszkiewicz");
MODULE_DESCRIPTION("Driver for HostMot2 on the 7i80 Anything I/O board from Mesa Electronics");
MODULE_SUPPORTED_DEVICE("Mesa-AnythingIO-7i80");

static char *ip;
RTAPI_MP_STRING(ip, "ip address of ethernet board(s)");

static char *config[2];
RTAPI_MP_ARRAY_STRING(config, 2, "config string for the AnyIO boards (see hostmot2(9) manpage)")

static struct list_head eth_boards;
static int num_boards = 0;
static int num_7i80 = 0;
hm2_eth_t *board;
int probe_fail = 0;
int comm_active = 0;

RT_TASK rt_probe_task;

static int comp_id;

#define UDP_PORT 27181
#define RCV_TIMEOUT 200000

static int sockfd = -1;
static struct sockaddr_in local_addr;
static struct sockaddr_in server_addr;

static lbp16_cmd_addr read_packet;

void lbp16_dump_packet4(lbp16_cmd_addr *packet) {
    if (packet == NULL) return;
    rtapi_print("LBP16 PACKET: %02X | %02X | %02X | %02X\n", packet->cmd_lo, packet->cmd_hi, packet->addr_lo, packet->addr_hi);
}

void lbp16_dump_packet8(lbp16_cmd_addr_data32 *packet) {
    if (packet == NULL) return;
    rtapi_print("LBP16 PACKET: %02X | %02X | %02X | %02X | %02X | %02X | %02X | %02X\n", packet->cmd_lo, packet->cmd_hi, packet->addr_lo, packet->addr_hi,
    packet->data1, packet->data2, packet->data3, packet->data4);
}

static int hm2_eth_read(hm2_lowlevel_io_t *this, u32 addr, void *buffer, int size) {
    int ret, ret2;
    u8 tmp_buffer[size];

    if (comm_active == 0) return 1;

    if (size != 4) {
        //rtapi_print_msg(RTAPI_MSG_ERR, "hm2_eth: unusual size to read: %d\n", size);
        if (size == 0) return 1;
    }
    LBP16_INIT_PACKET4(read_packet, CMD_READ_HOSTMOT2_ADDR32_INC(size/4), addr);

    //lbp16_dump_packet4(&read_packet);
    ret2 = rt_dev_send(sockfd, (void*) &read_packet, sizeof(read_packet), 0);

    ret = rt_dev_recv(sockfd, (void*) &tmp_buffer, size, 0);
    //rtapi_print_msg(RTAPI_MSG_ERR, "hm2_eth: read: addr=%X size=%d | send=%d recv=%d\n", addr, size, ret2, ret);
    if (ret == -EAGAIN) {
        rtapi_print_msg(RTAPI_MSG_ERR, "hm2_eth: Receive timeout!\n");
        return -1;
    }

    memcpy(buffer, tmp_buffer, size);
    return 1;  // success
}

typedef struct {
    void *buffer;
    int size;
    int from;
} read_queue_entry;

#define MAX_ETH_READS 64
read_queue_entry queue_reads[MAX_ETH_READS];
lbp16_cmd_addr queue_packets[MAX_ETH_READS];
int queue_reads_count = 0;
int queue_buff_size = 0;

static int hm2_eth_enqueue_read(hm2_lowlevel_io_t *this, u32 addr, void *buffer, int size) {
    if (size == -1) {
        int ret, ret2, i;
        u8 tmp_buffer[queue_buff_size];

        ret = rt_dev_send(sockfd, (void*) &queue_packets, sizeof(lbp16_cmd_addr)*queue_reads_count, 0);
        ret2 = rt_dev_recv(sockfd, (void*) &tmp_buffer, queue_buff_size, 0);

        for (i = 0; i < queue_reads_count; i++) {
            memcpy(queue_reads[i].buffer, &tmp_buffer[queue_reads[i].from], queue_reads[i].size);
        }

        queue_reads_count = 0;
        queue_buff_size = 0;
    } else {
        if (comm_active == 0) return 1;
        if (size == 0) return 1;

        LBP16_INIT_PACKET4(queue_packets[queue_reads_count], CMD_READ_HOSTMOT2_ADDR32_INC(size/4), addr);
        queue_reads[queue_reads_count].buffer = buffer;
        queue_reads[queue_reads_count].size = size;
        queue_reads[queue_reads_count].from = queue_buff_size;
        queue_reads_count++;
        queue_buff_size += size;
    }
    return 1;
}

static int hm2_eth_write(hm2_lowlevel_io_t *this, u32 addr, void *buffer, int size) {
    int ret;

    struct {
        lbp16_cmd_addr write_packet;
        u8 tmp_buffer[size];
    } packet;

    if (comm_active == 0) return 1;

    memcpy(packet.tmp_buffer, buffer, size);
    if (size != 4) {
        //rtapi_print("hm2_eth: unusual size to read: %d\n", size);
        if (size == 0) return 1;
    }
    LBP16_INIT_PACKET4(packet.write_packet, CMD_WRITE_HOSTMOT2_ADDR32_INC(size/4), addr);

    //lbp16_dump_packet8(&write_packet);
    ret = rt_dev_send(sockfd, (void*) &packet, sizeof(packet), 0);

    //rtapi_print_msg(RTAPI_MSG_ERR, "hm2_eth: write: addr=%X size=%d | send=%d | buff=0x%08X\n", addr, size, ret2, tmp_buffer[0]);
    if (ret == -EAGAIN) {
        rtapi_print_msg(RTAPI_MSG_ERR, "hm2_eth: Receive timeout!\n");
        return -1;
    }

    return 1;  // success
}

static int init_rtnet(void) {
    int ret;
    int64_t timeout = RCV_TIMEOUT;

    memset(&local_addr, 0, sizeof(struct sockaddr_in));
    memset(&server_addr, 0, sizeof(struct sockaddr_in));

    // Set address information structures
    local_addr.sin_family      = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;

    server_addr.sin_family      = AF_INET;
    inet_aton(ip, &server_addr.sin_addr);
    server_addr.sin_port        = htons(UDP_PORT);

   // Create new socket.
    sockfd = rt_dev_socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "Error opening socket: %d\n", sockfd);
        rt_dev_close(sockfd);
        return sockfd;
    }
    ret = rt_dev_ioctl(sockfd, RTNET_RTIOC_TIMEOUT, &timeout);
    if (ret < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "Setting socket option failed with error %d", ret);
        return ret;
    }

    ret = rt_dev_bind(sockfd, (struct sockaddr *) &local_addr, sizeof(struct sockaddr_in));
    if (ret < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "Binding to socket %d failed!\n", 10000);
        return ret;
    }

    ret = rt_dev_connect(sockfd, (struct sockaddr *) &server_addr, sizeof(struct sockaddr_in));
    if (ret < 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, "Connect to socket failed with error %d\n", ret);
        return ret;
    }

    return 0;
}

static int close_rtnet(void) {
    int ret;

    ret = rt_dev_close(sockfd);
    rtapi_print("Close RTNET %d\n", ret);
    return 0;
}

static void hm2_eth_probe() {
    int r, ret, ret2;
    char board_name[BOARD_NAME_LEN];
    hm2_lowlevel_io_t *this;

    LBP16_INIT_PACKET4(read_packet, CMD_READ_BOARD_INFO_ADDR16_INC(BOARD_NAME_LEN/2), 0);
    ret = rt_dev_send(sockfd, (void*) &read_packet, sizeof(read_packet), 0);
    ret2 = rt_dev_recv(sockfd, (void*) &board_name, BOARD_NAME_LEN, 0);

    board = (hm2_eth_t *) kmalloc(sizeof(hm2_eth_t), GFP_KERNEL);
    memset(board, 0, sizeof(hm2_eth_t));
    list_add_tail(&board->list, &eth_boards);
    this = &board->llio;

    if (strncmp(board_name, "7I80DB", 6) == 0) {
        board->llio.num_ioport_connectors = 4;
        board->llio.pins_per_connector = 17;
        board->llio.ioport_connector_name[0] = "J2";
        board->llio.ioport_connector_name[1] = "J3";
        board->llio.ioport_connector_name[2] = "J4";
        board->llio.ioport_connector_name[3] = "J5";
        board->llio.fpga_part_number = "2s200pq208";
        board->llio.num_leds = 4;
    } else if (strncmp(board_name, "7I80HD", 6) == 0) {
        board->llio.num_ioport_connectors = 3;
        board->llio.pins_per_connector = 24;
        board->llio.ioport_connector_name[0] = "P1";
        board->llio.ioport_connector_name[1] = "P2";
        board->llio.ioport_connector_name[2] = "P3";
        board->llio.fpga_part_number = "2s200pq208";
        board->llio.num_leds = 4;
    } else {
        probe_fail = 1;
        LL_PRINT("No ethernet board found\n");
        return;
    }

    LL_PRINT("discovered %.*s\n", 16, board_name);

    rtapi_snprintf(board->llio.name, sizeof(board->llio.name), "hm2_7i80.%d", num_7i80);
    num_7i80++;

    board->llio.comp_id = comp_id;
    board->llio.private = board;

    board->llio.read = hm2_eth_read;
    board->llio.write = hm2_eth_write;
    board->llio.queue_read = hm2_eth_enqueue_read;

    num_boards++;

    r = hm2_register(&board->llio, config[num_boards]);
    if (r != 0) {
        rtapi_print("board fails HM2 registration\n");
        return;
    }
    rtapi_print("board 7i80 registred succesfully\n");
}

int rtapi_app_main(void) {
    int ret;

    LL_PRINT("loading Mesa AnyIO HostMot2 ethernet driver version " HM2_ETH_VERSION "\n");

    ret = hal_init(HM2_LLIO_NAME);
    if (ret < 0) goto error0;
    comp_id = ret;

    rtapi_print("ip = %s\n", ip);

    ret = init_rtnet();
    if (ret < 0) {
        rtapi_print("RTNET layer not ready\n");
        goto error1;
    }

    INIT_LIST_HEAD(&eth_boards);

    comm_active = 1;

    //rest of init must be done in rt context
    // start rt task, run hm2_eth_probe() in it, and wait for finish here
    ret = rt_task_create(&rt_probe_task, "probe", 0, 10, T_JOINABLE);
    rt_task_start(&rt_probe_task, hm2_eth_probe, NULL);
    rt_task_join(&rt_probe_task);

    if (probe_fail == 1) goto error1;

    hal_ready(comp_id);

    return 0;

error1:
    close_rtnet();
error0:
    hal_exit(comp_id);
    return ret;
}

void rtapi_app_exit(void) {
    LL_PRINT("HostMot2 ethernet driver unloaded\n");
    comm_active = 0;
    close_rtnet();
    hal_exit(comp_id);
}
