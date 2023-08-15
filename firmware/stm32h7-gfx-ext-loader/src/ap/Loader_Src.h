#ifndef LOADER_SRC_H
#define LOADER_SRC_H

#include "hw.h"


#define KeepInCompilation __attribute__((used))

int Init ();
KeepInCompilation int Write (uint32_t Address, uint32_t Size, uint8_t* buffer);
KeepInCompilation int SectorErase (uint32_t EraseStartAddress ,uint32_t EraseEndAddress);
KeepInCompilation uint64_t Verify (uint32_t MemoryAddr, uint32_t RAMBufferAddr, uint32_t Size, uint32_t missalignement);
KeepInCompilation int MassErase (uint32_t Parallelism );

#endif