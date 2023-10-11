#include "qemu/osdep.h"
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
#include <stdint.h>

#define NIC_PCI_VENDOR_ID 0x11cc
#define NIC_PCI_DEVICE_ID 0x1234

#define NIC_MMIO_SIZE 0x00000010

#define TYPE_NIC_PANGO "nic_pango"
OBJECT_DECLARE_SIMPLE_TYPE(NICPangoState, NIC_PANGO)

struct NICPangoState {
  PCIDevice parent_obj;

  MemoryRegion mmio;
  MemoryRegion msix;

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

static uint64_t nic_mmio_read(void *opaque, hwaddr addr, unsigned size) {
  //   NICPangoState *s = opaque;
  return 0;
}

static void nic_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size) {
  //   NICPangoState *s = opaque;
}

static const MemoryRegionOps nic_mmio_ops = {
    .read = nic_mmio_read,
    .write = nic_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl =
        {
            .min_access_size = 4,
            .max_access_size = 4,
        },
};

static void nic_pci_realize(PCIDevice *pci_dev, Error **errp) {
  NICPangoState *s = NIC_PANGO(pci_dev);
  static const uint16_t nic_pcie_offset = 0x0E0;

  memory_region_init_io(&s->mmio, OBJECT(s), &nic_mmio_ops, s, "nic-mmio",
                        NIC_MMIO_SIZE);
  pci_register_bar(pci_dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

  if (pcie_endpoint_cap_v1_init(pci_dev, nic_pcie_offset) < 0) {
    hw_error("Failed to initialize PCIe capability");
  }
}

static void nic_class_init(ObjectClass *klass, void *data) {
  DeviceClass *dc = DEVICE_CLASS(klass);
  PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

  k->realize = nic_pci_realize;
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
