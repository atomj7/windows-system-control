#include "catalog.hpp"

#include "util.hpp"

namespace {

// USB identities are matched exactly. Channel counts for NZXT follow the public
// Hue 2 device table: lighting channels, then fan channels.
constexpr KnownHardware kKnown[] = {
    {0x1E71, 0x2001, "NZXT", "Hue 2", "fan_controller", "nzxt", 4, 0, false},
    {0x1E71, 0x2002, "NZXT", "Hue 2 Ambient", "fan_controller", "nzxt", 4, 0, false},
    {0x1E71, 0x2005, "NZXT", "Hue 2 Motherboard", "motherboard", "nzxt", 2, 0, false},
    {0x1E71, 0x200B, "NZXT", "Hue 2 Motherboard", "motherboard", "nzxt", 2, 0, false},
    {0x1E71, 0x2006, "NZXT", "Smart Device V2", "fan_controller", "nzxt", 2, 3, false},
    {0x1E71, 0x200D, "NZXT", "Smart Device V2", "fan_controller", "nzxt", 2, 3, false},
    {0x1E71, 0x200F, "NZXT", "Smart Device V2", "fan_controller", "nzxt", 2, 3, false},
    {0x1E71, 0x2007, "NZXT", "Kraken X3", "cooler", "nzxt", 3, 0, false},
    {0x1E71, 0x2014, "NZXT", "Kraken X3 RGB", "cooler", "nzxt", 3, 0, false},
    {0x1E71, 0x3012, "NZXT", "Kraken Elite", "cooler", "nzxt", 2, 2, true},
    {0x1E71, 0x2009, "NZXT", "RGB & Fan Controller", "fan_controller", "nzxt", 2, 3, false},
    {0x1E71, 0x2010, "NZXT", "RGB & Fan Controller", "fan_controller", "nzxt", 2, 3, false},
    {0x1E71, 0x200E, "NZXT", "RGB & Fan Controller", "fan_controller", "nzxt", 2, 3, false},
    {0x1E71, 0x2011, "NZXT", "RGB & Fan Controller", "fan_controller", "nzxt", 6, 3, false},
    {0x1E71, 0x2019, "NZXT", "RGB & Fan Controller", "fan_controller", "nzxt", 6, 3, false},
    {0x1E71, 0x2020, "NZXT", "RGB & Fan Controller", "fan_controller", "nzxt", 6, 3, false},
    {0x1E71, 0x201F, "NZXT", "RGB & Fan Controller", "fan_controller", "nzxt", 6, 3, false},
    {0x1E71, 0x2022, "NZXT", "RGB & Fan Controller", "fan_controller", "nzxt", 6, 3, true},
    {0x1E71, 0x201B, "NZXT", "B650E", "motherboard", "nzxt", 6, 3, false},
    {0x1E71, 0x2012, "NZXT", "RGB Controller", "fan_controller", "nzxt", 3, 0, false},
    {0x1E71, 0x2021, "NZXT", "RGB Controller", "fan_controller", "nzxt", 3, 0, false},

    {0x1B1C, 0x0C0B, "Corsair", "Lighting Node Pro", "fan_controller", "identified", 2, 0, false},
    {0x1B1C, 0x0C1A, "Corsair", "Lighting Node Core", "fan_controller", "identified", 1, 0, false},
    {0x1B1C, 0x0C10, "Corsair", "Commander Pro", "fan_controller", "identified", 2, 6, false},
    {0x1B1C, 0x0C22, "Corsair", "H150i RGB PRO XT", "cooler", "identified", 1, 1, false},

    {0x048D, 0x8297, "GIGABYTE", "RGB Fusion 2.0", "motherboard", "fusion", 3, 0, false},
    {0x048D, 0x5702, "GIGABYTE", "RGB Fusion 2.0", "motherboard", "fusion", 3, 0, false},
    {0x048D, 0x5711, "GIGABYTE", "RGB Fusion 2.0", "motherboard", "fusion", 3, 0, false},

    {0x1462, 0x1562, "MSI", "Mystic Light", "motherboard", "identified", 1, 0, false},
    {0x1462, 0x1563, "MSI", "Mystic Light", "motherboard", "identified", 1, 0, false},
    {0x1462, 0x1720, "MSI", "Mystic Light", "motherboard", "identified", 1, 0, false},

    {0x0416, 0x7372, "Lian Li", "UNI FAN TL Hub", "fan_controller", "identified", 4, 4, false},
    {0x0416, 0x7750, "Lian Li", "UNI FAN Hub", "fan_controller", "identified", 4, 4, false},
    {0x0416, 0xA100, "Lian Li", "UNI FAN Hub", "fan_controller", "identified", 4, 4, false},
    {0x0416, 0xA101, "Lian Li", "UNI FAN Hub", "fan_controller", "identified", 4, 4, false},
    {0x0416, 0xA102, "Lian Li", "UNI FAN Hub", "fan_controller", "identified", 4, 4, false},
};

struct VendorEntry {
  uint16_t vid;
  const char* name;
};

constexpr VendorEntry kVendors[] = {
    {0x1B1C, "Corsair"}, {0x1E71, "NZXT"},      {0x048D, "ITE"},         {0x0B05, "ASUS"},
    {0x1462, "MSI"},     {0x26CE, "ASRock"},    {0x2516, "Cooler Master"}, {0x0416, "Lian Li"},
    {0x1532, "Razer"},   {0x1038, "SteelSeries"}, {0x046D, "Logitech"},  {0x0DB0, "MSI"},
};

}  // namespace

const KnownHardware* find_known_device(uint16_t vid, uint16_t pid) {
  for (const auto& device : kKnown) {
    if (device.vid == vid && device.pid == pid) return &device;
  }
  return nullptr;
}

const char* vendor_from_vid(uint16_t vid) {
  for (const auto& vendor : kVendors) {
    if (vendor.vid == vid) return vendor.name;
  }
  return nullptr;
}

bool is_lighting_vendor(uint16_t vid) { return vendor_from_vid(vid) != nullptr; }

bool looks_like_lighting(std::string_view name) {
  static constexpr const char* kWords[] = {"rgb",     "led",      "aura",     "fusion", "mystic", "polychrome",
                                           "kraken",  "hue",      "fan",      "cooler", "aio",    "lighting",
                                           "argb",    "trident",  "vengeance", "ll120", "uni fan", "keyboard",
                                           "mouse",   "headset",  "strip",    "pump"};
  for (const char* word : kWords) {
    if (contains_ci(name, word)) return true;
  }
  return false;
}

std::string guess_kind_from_name(std::string_view name) {
  const std::string text = lower_copy(name);
  auto has = [&](const char* word) { return text.find(word) != std::string::npos; };
  if (has("keyboard") || has("mouse") || has("headset")) return "peripheral";
  if (has("kraken") || has("aio") || has("cooler") || has("h150i") || has("liquid")) return "cooler";
  if (has("gpu") || has("rtx") || has("radeon") || has("geforce")) return "gpu";
  if (has("dram") || has("dimm") || has("memory") || has("trident") || has("vengeance")) return "memory";
  if (has("motherboard") || has("mainboard") || has("fusion") || has("mystic") || has("aura")) return "motherboard";
  if (has("ssd") || has("nvme")) return "storage";
  if (has("fan") || has("hue") || has("node") || has("commander")) return "fan_controller";
  return "unknown";
}
