#include "src\PS2E-spu2.h"
#include "AudioRenderer.h"
#include "PCSX2Lib_API.h"


extern void CALLBACK SPU2setDMABaseAddr(uptr baseaddr);

void CALLBACK _SPU2readDMA4Mem(uint16 *pMem, int32 size)
{
    SPU2readDMA4Mem(pMem, (u32)size);
}

void CALLBACK _SPU2writeDMA4Mem(uint16 *pMem, int32 size)
{
    SPU2writeDMA4Mem(pMem, (u32)size);
}


void CALLBACK _SPU2readDMA7Mem(uint16 *pMem, int32 size)
{
    SPU2readDMA7Mem(pMem, (u32)size);
}

void CALLBACK _SPU2writeDMA7Mem(uint16 *pMem, int32 size)
{
    SPU2writeDMA7Mem(pMem, (u32)size);
}


PCSX2Lib::API::SPU2_API g_API = {
	SPU2write,
	SPU2read,
    _SPU2readDMA4Mem,
    _SPU2writeDMA4Mem,
	SPU2interruptDMA4,
    _SPU2readDMA7Mem,
    _SPU2writeDMA7Mem,
    SPU2setDMABaseAddr,
	SPU2interruptDMA7,
	SPU2ReadMemAddr,
	SPU2WriteMemAddr,
	SPU2irqCallback,
	SPU2setClockPtr,
	SPU2async
};