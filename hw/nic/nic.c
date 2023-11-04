#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qemu/units.h"

#include "qemu/bitops.h"

#include "hw/hw.h"
#include "hw/net/mii.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qemu/range.h"
#include "sysemu/dma.h"
#include "sysemu/sysemu.h"

#include "qapi/error.h"
#include "qom/object.h"

#include "hw/nic/nic.h"
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

OBJECT_DECLARE_SIMPLE_TYPE(NICPangoState, NIC_PANGO)

struct NICReg {
  uint32_t tx_bd_ba_low;
  uint32_t tx_bd_ba_high;
  uint32_t tx_bd_size;
  //   uint32_t tx_bd_head;
  uint32_t tx_bd_tail;
  uint32_t tx_bd_tail_last;

  uint32_t rx_bd_ba_low;
  uint32_t rx_bd_ba_high;
  uint32_t rx_bd_size;
  //   uint32_t rx_bd_head;
  uint32_t rx_bd_tail;

  uint32_t intr_tx;
  uint32_t intr_rx;
  //   uint8_t intr_other;
};

struct NICPangoState {
  PCIDevice parent_obj;

  MemoryRegion mmio;

  uint32_t ioaddr;

  uint16_t subsys_ven;
  uint16_t subsys;

  uint16_t subsys_ven_used;
  uint16_t subsys_used;

  bool disable_vnet;

  bool init_vet;
  bool timadj;

  struct NICReg nic_reg[2];
  sem_t sem;
};

static const VMStateDescription nic_vmstate = {
    .name = "nic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields =
        (VMStateField[]){
            VMSTATE_PCI_DEVICE(parent_obj, NICPangoState),
            VMSTATE_END_OF_LIST(),
        },
};

struct TDArg {
  PCIDevice *dev;
  uint16_t dst;
  uint16_t src;
};

static void *nic_copy_data(void *arg) {
  PCIDevice *dev = ((struct TDArg *)arg)->dev;
  NICPangoState *s = NIC_PANGO(dev);
  uint16_t dst = ((struct TDArg *)arg)->dst;
  uint16_t src = ((struct TDArg *)arg)->src;
  struct NICReg *nic_reg_src = &s->nic_reg[src];
  struct NICReg *nic_reg_dst = &s->nic_reg[dst];
  struct NICBD nic_bd_src;
  struct NICBD nic_bd_dst;
  struct NICRxFrame nic_rx_frame;
  dma_addr_t tx_bd_addr;
  dma_addr_t rx_bd_addr;
//   size_t i;

  free(arg);

  if (sem_trywait(&s->sem) != 0) {
    qemu_printf("nic_copy_data: sem_trywait failed\n");
    return NULL;
  }
  while (nic_reg_src->tx_bd_tail_last != nic_reg_src->tx_bd_tail) {
    qemu_printf("nic_copy_data: tx_bd_tail_last = %u, tx_bd_tail = %u\n",
                nic_reg_src->tx_bd_tail_last, nic_reg_src->tx_bd_tail);
    if (nic_reg_src->tx_bd_tail >= nic_reg_src->tx_bd_size) {
      qemu_printf("nic_copy_data: tx_bd_tail >= NIC_TX_RING_QUEUES\n");
      goto err_frame;
    }
    tx_bd_addr = ((uint64_t)nic_reg_src->tx_bd_ba_high << 32) +
                 nic_reg_src->tx_bd_ba_low +
                 sizeof(struct NICBD) * nic_reg_src->tx_bd_tail_last;
    rx_bd_addr = ((uint64_t)nic_reg_dst->rx_bd_ba_high << 32) +
                 nic_reg_dst->rx_bd_ba_low +
                 sizeof(struct NICBD) * nic_reg_dst->rx_bd_tail;

    if (tx_bd_addr == 0 || rx_bd_addr == 0) {
      qemu_printf("nic_copy_data: tx_bd_addr == 0 || rx_bd_addr == 0\n");
      goto err_frame;
    }

    if (nic_reg_src->tx_bd_size == 0 || nic_reg_dst->rx_bd_size == 0) {
      qemu_printf("nic_copy_data: tx_bd_size == 0 || rx_bd_size == 0\n");
      return NULL;
    }

    pci_dma_read(dev, tx_bd_addr, &nic_bd_src, sizeof(struct NICBD));
    pci_dma_read(dev, rx_bd_addr, &nic_bd_dst, sizeof(struct NICBD));

    qemu_printf("src flags = %lx, dst flags = %lx\n", nic_bd_src.flags,
                nic_bd_dst.flags);

    qemu_printf("nic_copy_data: %u->%u %u\n", src, dst, nic_bd_src.len);
    if (nic_bd_src.len > NIC_RX_PKT_SIZE) {
      qemu_printf("nic_copy_data: nic_bd_src.len > NIC_RX_PKT_SIZE\n");
      goto err_frame;
    }

    // copy data
    // read data from src
    pci_dma_read(dev, nic_bd_src.addr, &nic_rx_frame.data, nic_bd_src.len);
    nic_bd_src.flags |= NIC_BD_FLAG_USED;
    pci_dma_write(dev, tx_bd_addr, &nic_bd_src, sizeof(struct NICBD));

    // print data
    // for (i = 0; i < nic_bd_src.len; i++) {
    //   qemu_printf("%02x ", nic_rx_frame.data[i]);
    // }
    // qemu_printf("\n");

    // write data to dst
    nic_bd_dst.len = nic_bd_src.len;
    nic_bd_dst.flags |= NIC_BD_FLAG_VALID;
    nic_bd_dst.flags &= ~NIC_BD_FLAG_USED;

    pci_dma_write(dev, nic_bd_dst.addr, &nic_rx_frame, nic_bd_dst.len);
    pci_dma_write(dev, rx_bd_addr, &nic_bd_dst, sizeof(struct NICBD));

  err_frame:
    nic_reg_src->tx_bd_tail_last =
        (nic_reg_src->tx_bd_tail_last + 1) % nic_reg_src->tx_bd_size;
    nic_reg_dst->rx_bd_tail =
        (nic_reg_dst->rx_bd_tail + 1) % nic_reg_dst->rx_bd_size;

    // notify
    if (msi_enabled(dev)) {
      if (nic_reg_dst->intr_rx) {
        msi_notify(dev, NIC_VEC_IF_SIZE * dst + NIC_VEC_RX);
        qemu_printf("nic_copy_data: msi_notify %u rx\n", dst);
      }
      if (nic_reg_src->intr_tx) {
        msi_notify(dev, NIC_VEC_IF_SIZE * src + NIC_VEC_TX);
        qemu_printf("nic_copy_data: msi_notify %u tx\n", src);
      }
    }
  }
  sem_post(&s->sem);

  return NULL;
}

static uint64_t nic_mmio_read(void *opaque, hwaddr addr, unsigned size) {
  NICPangoState *s = opaque;
  uint64_t val = 0;
  uint8_t func_id;
  uint8_t if_id;
  uint8_t reg_id;

  func_id = (addr >> 16) & (BIT(4) - 1);
  if_id = (addr >> 9) & (BIT(7) - 1);
  reg_id = NIC_ADDR_TO_REG(addr);

  if (size != 4) {
    qemu_printf("nic_mmio_read: size != 4\n");
    return 0;
  }

  if (func_id != NIC_FUNC_ID_PCIE) {
    qemu_printf("nic_mmio_read: func_id!=NIC_FUNC_ID_PCIE %u\n", func_id);
    return 0;
  }

  if (if_id >= NIC_IF_NUM) {
    qemu_printf("nic_mmio_read: if_id >= NIC_IF_NUM\n");
    return 0;
  }

  switch (reg_id) {
  case NIC_PCIE_REG_TX_BD_BA_LOW:
    val = s->nic_reg[if_id].tx_bd_ba_low;
    break;
  case NIC_PCIE_REG_TX_BD_BA_HIGH:
    val = s->nic_reg[if_id].tx_bd_ba_high;
    break;
  case NIC_PCIE_REG_TX_BD_SIZE:
    val = s->nic_reg[if_id].tx_bd_size;
    break;
  case NIC_PCIE_REG_TX_BD_TAIL:
    val = s->nic_reg[if_id].tx_bd_tail;
    break;
  case NIC_PCIE_REG_RX_BD_BA_LOW:
    val = s->nic_reg[if_id].rx_bd_ba_low;
    break;
  case NIC_PCIE_REG_RX_BD_BA_HIGH:
    val = s->nic_reg[if_id].rx_bd_ba_high;
    break;
  case NIC_PCIE_REG_RX_BD_SIZE:
    val = s->nic_reg[if_id].rx_bd_size;
    break;
  case NIC_PCIE_REG_RX_BD_TAIL:
    val = s->nic_reg[if_id].rx_bd_tail;
    break;
  case NIC_PCIE_REG_INT_OFFSET(0):
    val = s->nic_reg[if_id].intr_tx;
    break;
  case NIC_PCIE_REG_INT_OFFSET(1):
    val = s->nic_reg[if_id].intr_rx;
    break;
  default:
    qemu_printf("nic_mmio_read: reg_id error\n");
    break;
  }

  qemu_printf("nic_mmio_read %lx(%u bytes) = %lx\n", addr, size, val);
  return val;
}

static void nic_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size) {
  NICPangoState *s = opaque;
  int res;
  pthread_t thread;
  struct TDArg *arg = malloc(sizeof(struct TDArg));
  uint8_t func_id;
  uint8_t if_id;
  uint8_t reg_id;

  func_id = (addr >> 16) & (BIT(4) - 1);
  if_id = (addr >> 9) & (BIT(7) - 1);
  reg_id = NIC_ADDR_TO_REG(addr);

  if (size != 4) {
    qemu_printf("nic_mmio_write: size != 4\n");
    return;
  }

  if (func_id != NIC_FUNC_ID_PCIE) {
    qemu_printf("nic_mmio_write: func_id!=NIC_FUNC_ID_PCIE\n");
    return;
  }

  if (if_id >= NIC_IF_NUM) {
    qemu_printf("nic_mmio_write: if_id >= NIC_IF_NUM\n");
    return;
  }

  switch (reg_id) {
  case NIC_PCIE_REG_TX_BD_BA_LOW:
    s->nic_reg[if_id].tx_bd_ba_low = val;
    break;
  case NIC_PCIE_REG_TX_BD_BA_HIGH:
    s->nic_reg[if_id].tx_bd_ba_high = val;
    break;
  case NIC_PCIE_REG_TX_BD_SIZE:
    s->nic_reg[if_id].tx_bd_size = val;
    break;
  case NIC_PCIE_REG_TX_BD_TAIL:
    s->nic_reg[if_id].tx_bd_tail = val;
    qemu_printf("nic_mmio_write: if%u tx_bd_tail = %lu\n", if_id, val);
    arg->dev = opaque;
    arg->dst = if_id ^ 1;
    arg->src = if_id;
    res = pthread_create(&thread, NULL, nic_copy_data, arg);
    if (res != 0) {
      qemu_printf("nic_mmio_write: pthread_create failed\n");
    }
    return;
    break;
  case NIC_PCIE_REG_RX_BD_BA_LOW:
    s->nic_reg[if_id].rx_bd_ba_low = val;
    break;
  case NIC_PCIE_REG_RX_BD_BA_HIGH:
    s->nic_reg[if_id].rx_bd_ba_high = val;
    break;
  case NIC_PCIE_REG_RX_BD_SIZE:
    s->nic_reg[if_id].rx_bd_size = val;
    break;
  case NIC_PCIE_REG_RX_BD_TAIL:
    s->nic_reg[if_id].rx_bd_tail = val;
    break;
  case NIC_PCIE_REG_INT_OFFSET(0):
    s->nic_reg[if_id].intr_tx = val;
    break;
  case NIC_PCIE_REG_INT_OFFSET(1):
    s->nic_reg[if_id].intr_rx = val;
    break;
  default:
    qemu_printf("nic_mmio_write: reg_id error\n");
    qemu_printf("nic_mmio_write %lx(%u bytes) = %lx\n", addr, size, val);
    return;
    break;
  }

  qemu_printf("nic_mmio_write: func = %u if = %u reg = %u val = %lx\n", func_id,
              if_id, reg_id, val);
}

static const MemoryRegionOps nic_mmio_ops = {
    .read = nic_mmio_read,
    .write = nic_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl =
        {
            .min_access_size = 1,
            .max_access_size = 8,
        },
};

static void nic_pci_realize(PCIDevice *pci_dev, Error **errp) {
  NICPangoState *s = NIC_PANGO(pci_dev);
  static const uint16_t nic_pcie_offset = 0x0E0;

  qemu_printf("nic_pci_realize\n");

  memory_region_init_io(&s->mmio, OBJECT(s), &nic_mmio_ops, s, "nic-mmio",
                        BIT(20));
  pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

  msi_init(pci_dev, 0, NIC_VEC_IF_SIZE * 2, true, false, errp);

  if (pcie_endpoint_cap_init(pci_dev, nic_pcie_offset) < 0) {
    hw_error("Failed to initialize PCIe capability");
  }

  sem_init(&s->sem, 0, 1);
}

static void nic_pci_uninit(PCIDevice *pci_dev) {
  qemu_printf("nic_pci_uninit\n");
  msi_uninit(pci_dev);
}

static void nic_class_init(ObjectClass *klass, void *data) {
  DeviceClass *dc = DEVICE_CLASS(klass);
  PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

  k->realize = nic_pci_realize;
  k->exit = nic_pci_uninit;
  k->vendor_id = NIC_PCI_VENDOR_ID;
  k->device_id = NIC_PCI_DEVICE_ID;
  k->revision = 0x10;
  k->class_id = PCI_CLASS_NETWORK_ETHERNET;

  dc->desc = "Pango NIC";
  dc->vmsd = &nic_vmstate;
}

static const TypeInfo nic_info = {
    .name = TYPE_NIC_PANGO,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(NICPangoState),
    .class_init = nic_class_init,
    .interfaces = (InterfaceInfo[]){{INTERFACE_PCIE_DEVICE}, {}}};

static void nic_register_types(void) { type_register_static(&nic_info); }

type_init(nic_register_types)
