#ifndef HW_NIC_NIC_H_
#define HW_NIC_NIC_H_

#include <stdint.h>
#define TYPE_NIC_PANGO "nic_pango"
#define NIC_RX_PKT_SIZE 2048

struct nic_bd {
  uint64_t addr;
  uint16_t len;
  uint16_t flags;
};

struct nic_rx_frame {
  uint16_t data_len;
  uint16_t data[NIC_RX_PKT_SIZE - 2];
};

#endif
