
#include <stdio.h>
#include <string.h>

#include "lbp16.h"

extern int sd;
extern socklen_t len;

extern struct sockaddr_in server_addr, client_addr;

u32 lbp16_send_read_u16(u16 cmd, u16 addr) {
    lbp16_cmd_addr packet;
    u16 buff = 0;

    LBP16_INIT_PACKET4(packet, cmd, addr);
    if (LBP16_SENDRECV_DEBUG)
        printf("SEND: %02X %02X %02X %02X\n", packet.cmd_hi, packet.cmd_lo, packet.addr_hi, packet.addr_lo);
    sendto(sd, (char*) &packet, sizeof(packet), 0, (struct sockaddr *) &server_addr, sizeof(server_addr));
    recvfrom(sd, (char*) &buff, sizeof(buff), 0, (struct sockaddr *) &client_addr, &len);
    if (LBP16_SENDRECV_DEBUG)
        printf("RECV: %04X\n", buff);

    return buff;
}

void lbp16_send_write_u16(u16 cmd, u16 addr, u16 val) {
    lbp16_cmd_addr_data16 packet;

    LBP16_INIT_PACKET6(packet, cmd, addr, val);
    if (LBP16_SENDRECV_DEBUG)
        printf("SEND: %02X %02X %02X %02X %02X %02X\n", packet.cmd_hi, packet.cmd_lo, packet.addr_hi, packet.addr_lo, packet.data_hi, packet.data_lo);
    sendto(sd, (char*) &packet, sizeof(packet), 0, (struct sockaddr *) &server_addr, sizeof(server_addr));
}

u32 lbp16_send_read_u32(u16 cmd, u16 addr) {
    lbp16_cmd_addr packet;
    u32 buff = 0;

    LBP16_INIT_PACKET4(packet, cmd, addr);
    if (LBP16_SENDRECV_DEBUG)
        printf("SEND: %02X %02X %02X %02X\n", packet.cmd_hi, packet.cmd_lo, packet.addr_hi, packet.addr_lo);
    sendto(sd, (char*) &packet, sizeof(packet), 0, (struct sockaddr *) &server_addr, sizeof(server_addr));
    recvfrom(sd, (char*) &buff, sizeof(buff), 0, (struct sockaddr *) &client_addr, &len);
    if (LBP16_SENDRECV_DEBUG)
        printf("RECV: %04X\n", (unsigned int) buff);

    return buff;
}

void lbp16_send_sector_erase(u32 addr) {
    lbp16_erase_flash_sector_packets sector_erase_pck;
    lbp16_cmd_addr_data32 write_addr_pck;

    LBP16_INIT_PACKET8(write_addr_pck, CMD_WRITE_FPGA_FLASH_ADDR32(1), FLASH_ADDR_REG, addr);
    if (LBP16_SENDRECV_DEBUG)
        printf("SEND: %02X %02X %02X %02X %02X %02X %02X %02X\n",
          write_addr_pck.cmd_hi,
          write_addr_pck.cmd_lo,
          write_addr_pck.addr_hi,
          write_addr_pck.addr_lo,
          write_addr_pck.data1,
          write_addr_pck.data2,
          write_addr_pck.data3,
          write_addr_pck.data4);
    sendto (sd, (char*) &write_addr_pck, sizeof(write_addr_pck), 0, (struct sockaddr *) &server_addr, sizeof(server_addr));

    LBP16_INIT_PACKET6(sector_erase_pck.write_ena_pck, CMD_WRITE_COMM_CTRL_ADDR16(1), COMM_CTRL_WRITE_ENA_REG, 0x5A03);
    LBP16_INIT_PACKET8(sector_erase_pck.fl_erase_pck, CMD_WRITE_FPGA_FLASH_ADDR32(1), FLASH_SEC_ERASE_REG, 0);
    if (LBP16_SENDRECV_DEBUG)
        printf("SEND: %02X %02X %02X %02X %02X %02X | %02X %02X %02X %02X %02X %02X %02X %02X\n",
          sector_erase_pck.write_ena_pck.cmd_hi,
          sector_erase_pck.write_ena_pck.cmd_lo,
          sector_erase_pck.write_ena_pck.addr_hi,
          sector_erase_pck.write_ena_pck.addr_lo,
          sector_erase_pck.write_ena_pck.data_hi,
          sector_erase_pck.write_ena_pck.data_lo,
          sector_erase_pck.fl_erase_pck.cmd_hi,
          sector_erase_pck.fl_erase_pck.cmd_lo,
          sector_erase_pck.fl_erase_pck.addr_hi,
          sector_erase_pck.fl_erase_pck.addr_lo,
          sector_erase_pck.fl_erase_pck.data1,
          sector_erase_pck.fl_erase_pck.data2,
          sector_erase_pck.fl_erase_pck.data3,
          sector_erase_pck.fl_erase_pck.data4);
    sendto (sd, (char*) &sector_erase_pck, sizeof(sector_erase_pck), 0, (struct sockaddr *) &server_addr, sizeof(server_addr));
    lbp16_send_read_u32(CMD_READ_FPGA_FLASH_ADDR32(1), FLASH_ADDR_REG);
}

void lbp16_send_flash_address(u32 addr) {
    lbp16_cmd_addr_data32 write_addr_pck;

    LBP16_INIT_PACKET8(write_addr_pck, CMD_WRITE_FPGA_FLASH_ADDR32(1), FLASH_ADDR_REG, addr);
    if (LBP16_SENDRECV_DEBUG)
        printf("SEND: %02X %02X %02X %02X %02X %02X %02X %02X\n",
          write_addr_pck.cmd_hi,
          write_addr_pck.cmd_lo,
          write_addr_pck.addr_hi,
          write_addr_pck.addr_lo,
          write_addr_pck.data1,
          write_addr_pck.data2,
          write_addr_pck.data3,
          write_addr_pck.data4);
    sendto (sd, (char*) &write_addr_pck, sizeof(write_addr_pck), 0, (struct sockaddr *) &server_addr, sizeof(server_addr));
}
