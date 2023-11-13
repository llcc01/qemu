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


// mmio

#define NIC_CTL_ADDR(func, ch, reg)                                            \
  (((func) << 16) + (((ch) & (BIT(7) - 1)) << 9) +                             \
   (((reg) & (BIT(7) - 1)) << 2))

#define NIC_IF_REG_SIZE BIT(9)

#define NIC_FUNC_ID_PCIE 0xf

#define NIC_REG_TO_ADDR(reg) ((reg) << 2)

#define NIC_ADDR_TO_REG(addr) (((addr) >> 2) & (BIT(7) - 1))

#define NIC_PCIE_REG(s, nr) (((s) << 3) + (nr))

// rx

#define NIC_PCIE_REG_RX_BD_BA_LOW NIC_PCIE_REG(0x0, 0x0)

#define NIC_PCIE_REG_RX_BD_BA_HIGH NIC_PCIE_REG(0x0, 0x1)

// #define NIC_PCIE_REG_RX_BD_SIZE NIC_PCIE_REG(0x0, 0x2)

// #define NIC_PCIE_REG_RX_BD_HEAD NIC_PCIE_REG(0x0, 0x3)

#define NIC_PCIE_REG_RX_BD_TAIL NIC_PCIE_REG(0x0, 0x2)

// tx

#define NIC_PCIE_REG_TX_BD_BA_LOW NIC_PCIE_REG(0x1, 0x0)

#define NIC_PCIE_REG_TX_BD_BA_HIGH NIC_PCIE_REG(0x1, 0x1)

// #define NIC_PCIE_REG_TX_BD_SIZE NIC_PCIE_REG(0x1, 0x2)

// #define NIC_PCIE_REG_TX_BD_HEAD NIC_PCIE_REG(0x1, 0x3)

#define NIC_PCIE_REG_TX_BD_TAIL NIC_PCIE_REG(0x1, 0x2)

// interrupt

#define NIC_PCIE_REG_INT_OFFSET(tx_rx) NIC_PCIE_REG(0x2, (tx_rx))

// vector

#define NIC_VEC_TX 0

#define NIC_VEC_RX 1

#define NIC_VEC_IF_SIZE 2

// flags

#define NIC_BD_FLAG_VALID BIT(63)

#define NIC_BD_FLAG_USED BIT(62)

struct NICBD {
  union {
    uint64_t flags;
    uint16_t len;
  };
  uint64_t addr;
};

struct NICRxFrame {
  uint8_t data[NIC_RX_PKT_SIZE];
};

#endif
