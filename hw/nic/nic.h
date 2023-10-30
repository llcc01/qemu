#ifndef HW_NIC_NIC_H_
#define HW_NIC_NIC_H_

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qemu/units.h"

#include "qemu/bitops.h"

#define NIC_PCI_VENDOR_ID 0x0755

#define NIC_PCI_DEVICE_ID 0x0755

#define TYPE_NIC_PANGO "nic_pango"

#define NIC_IF_NUM 2

#define NIC_RX_PKT_SIZE 2048

#define NIC_TX_RING_QUEUES 128

#define NIC_RX_RING_QUEUES 128

// mmio
#define NIC_IF_REG_SIZE BIT(9)

#define NIC_FUNC_ID_DMA 0

#define NIC_PCIE_CTL_CALC_OFFSET(s, offset) (((s) << 5) + ((offset) << 2))

#define NIC_DMA_CTL_TX_BD_BA_LOW NIC_PCIE_CTL_CALC_OFFSET(0x0, 0x0)

#define NIC_DMA_CTL_TX_BD_BA_HIGH NIC_PCIE_CTL_CALC_OFFSET(0x0, 0x1)

#define NIC_DMA_CTL_TX_BD_SIZE NIC_PCIE_CTL_CALC_OFFSET(0x0, 0x2)

// #define NIC_DMA_CTL_TX_BD_HEAD NIC_PCIE_CTL_CALC_OFFSET(0x0, 0x3)

#define NIC_DMA_CTL_TX_BD_TAIL NIC_PCIE_CTL_CALC_OFFSET(0x0, 0x4)

#define NIC_DMA_CTL_RX_BD_BA_LOW NIC_PCIE_CTL_CALC_OFFSET(0x1, 0x0)

#define NIC_DMA_CTL_RX_BD_BA_HIGH NIC_PCIE_CTL_CALC_OFFSET(0x1, 0x1)

#define NIC_DMA_CTL_RX_BD_SIZE NIC_PCIE_CTL_CALC_OFFSET(0x1, 0x2)

// #define NIC_DMA_CTL_RX_BD_HEAD NIC_PCIE_CTL_CALC_OFFSET(0x1, 0x3)

#define NIC_DMA_CTL_RX_BD_TAIL NIC_PCIE_CTL_CALC_OFFSET(0x1, 0x4)

#define NIC_CSR_CTL_INT_OFFSET(tx_rx) NIC_PCIE_CTL_CALC_OFFSET(0x2, (tx_rx))

#define NIC_BD_FLAG_VALID BIT(0)
#define NIC_BD_FLAG_USED BIT(1)

#define NIC_VEC_TX 0

#define NIC_VEC_RX 1

#define NIC_VEC_OTHER 2

#define NIC_VEC_IF_SIZE 4

#define REG_ADDR_TO_ID(addr) ((addr) >> 2)

struct NICBD {
  union {
    uint32_t flags;
    uint16_t len;
  };
  uint64_t addr;
};

struct NICRxFrame {
  uint8_t data[NIC_RX_PKT_SIZE];
};

#endif
