#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qemu/units.h"

#include "hw/hw.h"
#include "hw/net/mii.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qemu/range.h"
#include "sysemu/sysemu.h"

#include "qapi/error.h"
#include "qom/object.h"

#include "hw/nic/nic.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define NIC_PCI_VENDOR_ID 0x11cc
#define NIC_PCI_DEVICE_ID 0x1234

#define NIC_MMIO_SIZE 0x00000100

OBJECT_DECLARE_SIMPLE_TYPE(NICPangoState, NIC_PANGO)

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

struct NICReg {
  uint32_t tx_bd_head;
  uint32_t tx_bd_tail;
  dma_addr_t tx_bd_head_pa; // reserved
  dma_addr_t tx_bd_tail_pa; // reserved
  dma_addr_t tx_bd_pa;

  uint32_t rx_bd_head; // reserved
  uint32_t rx_bd_tail; // reserved
  dma_addr_t rx_bd_head_pa;
  dma_addr_t rx_bd_tail_pa;
  dma_addr_t rx_bd_pa;

  uint8_t intr;
};

struct TDArg {
  PCIDevice *dev;
  uint16_t dst;
  uint16_t src;
};

static uint8_t nic_reg[NIC_MMIO_SIZE] = {0};

static void *nic_copy_data(void *arg) {
  PCIDevice *dev = ((struct TDArg *)arg)->dev;
  uint16_t dst = ((struct TDArg *)arg)->dst;
  uint16_t src = ((struct TDArg *)arg)->src;
  struct NICReg *nic_reg_src =
      (struct NICReg *)(nic_reg + NIC_MMIO_IF_REG_SIZE * src);
  struct NICReg *nic_reg_dst =
      (struct NICReg *)(nic_reg + NIC_MMIO_IF_REG_SIZE * dst);
  struct NICBD nic_bd_src;
  struct NICBD nic_bd_dst;
  struct NICRxFrame nic_rx_frame;

  free(arg);
  while (nic_reg_src->tx_bd_tail != nic_reg_src->tx_bd_head) {
    qemu_printf("nic_copy_data: tx_bd_tail = %u, tx_bd_head = %u\n",
                nic_reg_src->tx_bd_tail, nic_reg_src->tx_bd_head);
    if (nic_reg_src->tx_bd_head >= NIC_TX_RING_QUEUES) {
      qemu_printf("nic_copy_data: tx_bd_head >= NIC_TX_RING_QUEUES\n");
      goto err_frame;
    }
    pci_dma_read(dev,
                 nic_reg_src->tx_bd_pa +
                     sizeof(struct NICBD) * nic_reg_src->tx_bd_tail,
                 &nic_bd_src, sizeof(struct NICBD));
    pci_dma_read(dev,
                 nic_reg_dst->rx_bd_pa +
                     sizeof(struct NICBD) * nic_reg_dst->rx_bd_head,
                 &nic_bd_dst, sizeof(struct NICBD));

    qemu_printf("nic_copy_data: %u->%u %u\n", src, dst, nic_bd_src.len);
    if (nic_bd_src.len > NIC_RX_PKT_SIZE) {
      qemu_printf("nic_copy_data: nic_bd_src.len > NIC_RX_PKT_SIZE\n");
      goto err_frame;
    }

    // copy data
    nic_rx_frame.data_len = nic_bd_src.len;
    pci_dma_read(dev, nic_bd_src.addr, &nic_rx_frame.data, nic_bd_src.len);

    pci_dma_write(dev, nic_bd_dst.addr, &nic_rx_frame,
                  nic_bd_src.len + sizeof(nic_rx_frame.data_len));

  err_frame:
    nic_reg_src->tx_bd_tail =
        (nic_reg_src->tx_bd_tail + 1) % NIC_TX_RING_QUEUES;
    nic_reg_dst->rx_bd_head =
        (nic_reg_dst->rx_bd_head + 1) % NIC_RX_RING_QUEUES;
    pci_dma_write(dev, nic_reg_dst->rx_bd_head_pa, &nic_reg_dst->rx_bd_head,
                  sizeof(uint32_t));

    // notify
    if (msi_enabled(dev) && nic_reg_dst->intr) {
      msi_notify(dev, 0);
      qemu_printf("nic_copy_data: msi_notify\n");
    }
  }

  return NULL;
}

static uint64_t nic_mmio_read(void *opaque, hwaddr addr, unsigned size) {
  //   NICPangoState *s = opaque;
  qemu_printf("nic_mmio_read %lx(%u bytes)\n", addr, size);
  if (addr + size > NIC_MMIO_SIZE) {
    qemu_printf("nic_mmio_read: addr + size > NIC_MMIO_SIZE\n");
    return 0;
  }

  switch (size) {
  case 1:
    return *(uint8_t *)(nic_reg + addr);
  case 2:
    return *(uint16_t *)(nic_reg + addr);
  case 4:
    return *(uint32_t *)(nic_reg + addr);
  case 8:
    return *(uint64_t *)(nic_reg + addr);
  default:
    break;
  }
  return 0;
}

static void nic_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size) {
  //   NICPangoState *s = opaque;
  int res;
  pthread_t thread;
  struct TDArg *arg = malloc(sizeof(struct TDArg));
  if (addr + size > NIC_MMIO_SIZE) {
    qemu_printf("nic_mmio_write: addr + size > NIC_MMIO_SIZE\n");
    return;
  }

  switch (size) {
  case 1:
    *(uint8_t *)(nic_reg + addr) = val;
    break;
  case 2:
    *(uint16_t *)(nic_reg + addr) = val;
    break;
  case 4:
    *(uint32_t *)(nic_reg + addr) = val;
    if (addr == NIC_MMIO_TX_BD_HEAD) {
      qemu_printf("nic_mmio_write: if0 tx_bd_head = %u\n",
                  ((struct NICReg *)nic_reg)->tx_bd_head);
      arg->dev = opaque;
      arg->dst = 1;
      arg->src = 0;
      res = pthread_create(&thread, NULL, nic_copy_data, arg);
      if (res != 0) {
        qemu_printf("nic_mmio_write: pthread_create failed\n");
      }
      return;
    } else if (addr == NIC_MMIO_IF_REG_SIZE + NIC_MMIO_TX_BD_HEAD) {
      qemu_printf(
          "nic_mmio_write: if1 tx_bd_head = %u\n",
          ((struct NICReg *)(nic_reg + NIC_MMIO_IF_REG_SIZE))->tx_bd_head);
      arg->dev = opaque;
      arg->dst = 0;
      arg->src = 1;
      res = pthread_create(&thread, NULL, nic_copy_data, arg);
      if (res != 0) {
        qemu_printf("nic_mmio_write: pthread_create failed\n");
      }
      return;
    }
    break;
  case 8:
    *(uint64_t *)(nic_reg + addr) = val;
    break;
  default:
    break;
  }
  qemu_printf("nic_mmio_write %lx(%u bytes) = %lx\n", addr, size, val);
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
                        NIC_MMIO_SIZE);
  pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

  msi_init(pci_dev, 0, 1, true, true, errp);

  if (pcie_endpoint_cap_init(pci_dev, nic_pcie_offset) < 0) {
    hw_error("Failed to initialize PCIe capability");
  }
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
