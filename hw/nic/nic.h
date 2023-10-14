#ifndef HW_NIC_NIC_H_
#define HW_NIC_NIC_H_

#include <stdint.h>
#define TYPE_NIC_PANGO "nic_pango"
#define NIC_RX_PKT_SIZE 2048
#define NIC_TX_RING_QUEUES 16
#define NIC_RX_RING_QUEUES 16

#define NIC_MMIO_TX_BD_HEAD 0x00
#define NIC_MMIO_TX_BD_TAIL 0x04
// #define NIC_MMIO_TX_BD_HEAD_PA 0x08 // reserved
// #define NIC_MMIO_TX_BD_TAIL_PA 0x10 // reserved
#define NIC_MMIO_TX_BD_PA 0x18

// #define NIC_MMIO_RX_BD_HEAD 0x20 // reserved
// #define NIC_MMIO_RX_BD_TAIL 0x24 // reserved
#define NIC_MMIO_RX_BD_HEAD_PA 0x28
#define NIC_MMIO_RX_BD_TAIL_PA 0x30
#define NIC_MMIO_RX_BD_PA 0x38

#define NIC_MMIO_CSR_INT 0x40

#define NIC_MMIO_IF_REG_SIZE 0x50



struct NICBD {
  uint64_t addr;
  uint16_t len;
  uint16_t flags;
};

struct NICRxFrame {
  uint16_t data_len;
  uint8_t data[NIC_RX_PKT_SIZE - 2];
};

#endif
