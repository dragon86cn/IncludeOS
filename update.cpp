#include "update.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <unistd.h>
#include "elf.h"
#include "storage.hpp"

static const int SECT_SIZE   = 512;
static const int SERV_OFFSET = 6; // seems like an unintended offset

static const uintptr_t UPDATE_STORAGE = 0x6000000; // at 96mb
static const uint64_t  LIVEUPD_MAGIC  = 0xbaadb33fdeadc0de;

static void* HOTSWAP_AREA = (void*) 0x8000;

void LiveUpdate::resume(resume_func func)
{
  // check if an update has occurred
  if (*(uint64_t*) UPDATE_STORAGE == LIVEUPD_MAGIC)
  {
    printf("* Restoring data...\n");
    // restore connections etc.
    extern void resume_begin(storage_header&, LiveUpdate::resume_func);
    resume_begin(*(storage_header*) UPDATE_STORAGE, func);
  } else {
    printf("* Not restoring data, because no update has happened\n");
  }
}

struct SymTab {
  Elf32_Sym* base;
  uint32_t   entries;
};
struct StrTab {
  char*    base;
  uint32_t size;
};
static void update_store_data(LiveUpdate::storage_func);

extern "C" void hotswap(const char*, int, char*, uintptr_t);
extern "C" char __hotswap_length;

#include <hw/devices.hpp>

void LiveUpdate::begin(buffer_len blob, storage_func func)
{
  // use area just below the update storage to
  // prevent it getting overwritten during the update
  char* update_area = (char*) (UPDATE_STORAGE - blob.length);
  memcpy(update_area, blob.buffer, blob.length);

  // validate ELF header
  const char* binary  = &update_area[SECT_SIZE];
  const int   bin_len = blob.length - SECT_SIZE;
  Elf32_Ehdr& hdr = *(Elf32_Ehdr*) binary;

  if (hdr.e_ident[0] != 0x7F ||
      hdr.e_ident[1] != 'E' ||
      hdr.e_ident[2] != 'L' ||
      hdr.e_ident[3] != 'F') return;
  printf("* Validated ELF header\n");

  // discover _start() entry point
  const uintptr_t serv_offset = *(uintptr_t*) &update_area[SERV_OFFSET];
  printf("* _start is located at %#x\n", serv_offset);

  // save ourselves
  update_store_data(func);

  // try to guess base address for the new service based on entry point
  /// FIXME
  char* phys_base = (char*) 0x100000; //(serv_offset & 0xffff0000);
  printf("* Estimate physical base to be %p...\n", phys_base);

  /// prepare for the end
  // 1. turn off interrupts
  asm volatile("cli");
  // 2. deactivate all PCI devices and mask all MSI-X vectors
  hw::Devices::deactivate_all();

  // replace ourselves and reset by jumping to _start
  printf("* Replacing self with %d bytes and jumping to %#x\n", bin_len, serv_offset);

  // copy hotswapping function to sweet spot
  memcpy(HOTSWAP_AREA, (void*) &hotswap, &__hotswap_length - (char*) &hotswap);

  /// the end
  ((decltype(&hotswap)) HOTSWAP_AREA)(binary, bin_len, phys_base, serv_offset);
}

void update_store_data(LiveUpdate::storage_func func)
{
  // create storage header in the fixed location
  auto* storage = (storage_header*) UPDATE_STORAGE;
  new (storage) storage_header(LIVEUPD_MAGIC);
  
  /// callback for storing stuff
  func({storage});
}

/// struct Storage

void Storage::add_string(uint16_t id, const std::string& string)
{
  hdr->add_string(id, string);
}
void Storage::add_buffer(uint16_t id, buffer_len blob)
{
  hdr->add_buffer(id, blob.buffer, blob.length);
}
