#include "stdafx.h"
#include "HpMpAlert.h"

#ifdef BEIDOU_MINIMAL_PLUGIN
namespace {
constexpr unsigned long kSaveGlobalAddr = 0x0049C8E7;
constexpr unsigned long kUIStatusBarPtr = 0x00BEBF9C;
constexpr unsigned long kHpAlertOffset = 0x80;
constexpr unsigned long kMpAlertOffset = 0x84;
constexpr unsigned long kClientSocketPtr = 0x00BE7914;
constexpr unsigned long kProcessPacketAddr = 0x004965F1;
constexpr unsigned short kOpcodeSetHpMpAlert = 0x1000;

struct COutPacket {
    int Loopback;
    union {
        unsigned char* Data;
        void* Unk;
        unsigned short* Header;
    };
    unsigned long Size;
    unsigned int Offset;
    int EncryptedByShanda;
};

struct CInPacket {
    bool Loopback;
    int State;
    void* Data;
    unsigned long Size;
    unsigned short RawSeq;
    unsigned short DataLen;
    unsigned short Unknown;
    unsigned int Offset;
    void* Unk;
};

using SendPacket_t = void(__fastcall*)(void* pThis, void* edx, COutPacket* packet);
static SendPacket_t g_SendPacket = reinterpret_cast<SendPacket_t>(0x0049637B);

static bool TryReadDword(unsigned long address, unsigned long& out) {
    __try {
        out = *reinterpret_cast<unsigned long*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 0;
        return false;
    }
}

static unsigned char ClampAlert(int value) {
    if (value < 0) return 0;
    if (value > 20) return 20;
    return static_cast<unsigned char>(value);
}

static void SendHpMpAlertFromStatusBar() {
    unsigned long statusBar = 0;
    if (!TryReadDword(kUIStatusBarPtr, statusBar) || statusBar == 0) return;
    unsigned long hpRaw = 0, mpRaw = 0;
    if (!TryReadDword(statusBar + kHpAlertOffset, hpRaw)) return;
    if (!TryReadDword(statusBar + kMpAlertOffset, mpRaw)) return;
    const unsigned char hpAlert = ClampAlert(static_cast<int>(hpRaw));
    const unsigned char mpAlert = ClampAlert(static_cast<int>(mpRaw));
    unsigned long socketPtr = 0;
    if (!TryReadDword(kClientSocketPtr, socketPtr) || socketPtr == 0) return;
    unsigned char payload[4] = {
        static_cast<unsigned char>(kOpcodeSetHpMpAlert & 0xFF),
        static_cast<unsigned char>((kOpcodeSetHpMpAlert >> 8) & 0xFF),
        hpAlert, mpAlert
    };
    COutPacket packet{};
    packet.Loopback = 0;
    packet.Data = payload;
    packet.Size = sizeof(payload);
    packet.Offset = 0;
    packet.EncryptedByShanda = 0;
    g_SendPacket(reinterpret_cast<void*>(socketPtr), nullptr, &packet);
}

static void ApplyHpMpAlertToStatusBar(unsigned char hpAlert, unsigned char mpAlert) {
    unsigned long statusBar = 0;
    if (!TryReadDword(kUIStatusBarPtr, statusBar) || statusBar == 0) return;
    Memory::WriteInt(statusBar + kHpAlertOffset, hpAlert);
    Memory::WriteInt(statusBar + kMpAlertOffset, mpAlert);
}

static void HandleHpMpAlertPacket(CInPacket* packet) {
    if (packet == nullptr) return;
    __try {
        if (packet->Data == nullptr || packet->Size < 8) return;
        const unsigned char* data = reinterpret_cast<const unsigned char*>(packet->Data);
        const unsigned short opcode = *reinterpret_cast<const unsigned short*>(data + 4);
        if (opcode != kOpcodeSetHpMpAlert) return;
        ApplyHpMpAlertToStatusBar(
            ClampAlert(static_cast<int>(data[6])),
            ClampAlert(static_cast<int>(data[7])));
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

using SaveGlobal_t = void(__fastcall*)(void* pThis, void* edx);
static SaveGlobal_t s_SaveGlobal = reinterpret_cast<SaveGlobal_t>(kSaveGlobalAddr);

static void __fastcall SaveGlobal_Hook(void* pThis, void* edx) {
    s_SaveGlobal(pThis, edx);
    SendHpMpAlertFromStatusBar();
}

using ProcessPacket_t = void(__fastcall*)(void* pThis, void* edx, CInPacket* packet);
static ProcessPacket_t s_ProcessPacket = reinterpret_cast<ProcessPacket_t>(kProcessPacketAddr);

static void __fastcall ProcessPacket_Hook(void* pThis, void* edx, CInPacket* packet) {
    HandleHpMpAlertPacket(packet);
    s_ProcessPacket(pThis, edx, packet);
}
} // namespace

void RegisterHpMpAlertPacketHandler() {}

void HookSaveGlobal(bool enable) {
    Memory::SetHook(enable, reinterpret_cast<void**>(&s_SaveGlobal), SaveGlobal_Hook);
}

void HookHpMpAlertRecv(bool enable) {
    Memory::SetHook(enable, reinterpret_cast<void**>(&s_ProcessPacket), ProcessPacket_Hook);
}

#else
#include "compat/ClientAddresses.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"

namespace {
constexpr unsigned long kUIStatusBarPtr = ClientAddresses::kUIStatusBarPtr;
constexpr unsigned long kHpAlertOffset = 0x80;
constexpr unsigned long kMpAlertOffset = 0x84;
constexpr unsigned long kClientSocketPtr = ClientAddresses::kClientSocketPtr;
constexpr unsigned short kOpcodeSetHpMpAlert = CustomRecvOpcode::kHpMpAlert;

using SendPacket_t = void(__fastcall*)(void* pThis, void* edx, CompatOutPacket* packet);
static SendPacket_t g_SendPacket = reinterpret_cast<SendPacket_t>(ClientAddresses::kSendPacket);

static bool TryReadDword(unsigned long address, unsigned long& out) {
    __try {
        out = *reinterpret_cast<unsigned long*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 0;
        return false;
    }
}

static unsigned char ClampAlert(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 20) {
        return 20;
    }
    return static_cast<unsigned char>(value);
}

static void SendHpMpAlertFromStatusBar() {
    unsigned long statusBar = 0;
    if (!TryReadDword(kUIStatusBarPtr, statusBar) || statusBar == 0) {
        return;
    }

    unsigned long hpRaw = 0;
    unsigned long mpRaw = 0;
    if (!TryReadDword(statusBar + kHpAlertOffset, hpRaw)) {
        return;
    }
    if (!TryReadDword(statusBar + kMpAlertOffset, mpRaw)) {
        return;
    }

    const unsigned char hpAlert = ClampAlert(static_cast<int>(hpRaw));
    const unsigned char mpAlert = ClampAlert(static_cast<int>(mpRaw));

    unsigned long socketPtr = 0;
    if (!TryReadDword(kClientSocketPtr, socketPtr) || socketPtr == 0) {
        return;
    }

    unsigned char payload[4] = {
        static_cast<unsigned char>(kOpcodeSetHpMpAlert & 0xFF),
        static_cast<unsigned char>((kOpcodeSetHpMpAlert >> 8) & 0xFF),
        hpAlert,
        mpAlert
    };

    CompatOutPacket packet{};
    packet.Loopback = 0;
    packet.Data = payload;
    packet.Size = sizeof(payload);
    packet.Offset = 0;
    packet.EncryptedByShanda = 0;
    g_SendPacket(reinterpret_cast<void*>(socketPtr), nullptr, &packet);
}

static void ApplyHpMpAlertToStatusBar(unsigned char hpAlert, unsigned char mpAlert) {
    unsigned long statusBar = 0;
    if (!TryReadDword(kUIStatusBarPtr, statusBar) || statusBar == 0) {
        return;
    }
    Memory::WriteInt(statusBar + kHpAlertOffset, hpAlert);
    Memory::WriteInt(statusBar + kMpAlertOffset, mpAlert);
}

static bool HandleHpMpAlertPacket(CompatInPacket* packet, unsigned short /*opcode*/) {
    if (packet == nullptr) {
        return false;
    }

    __try {
        const unsigned char* data = packet->Data();
        if (data == nullptr || packet->Size() < 8) {
            return false;
        }

        const unsigned short recvOpcode = *reinterpret_cast<const unsigned short*>(data + 4);
        if (recvOpcode != kOpcodeSetHpMpAlert) {
            return false;
        }

        const unsigned char hpAlert = ClampAlert(static_cast<int>(data[6]));
        const unsigned char mpAlert = ClampAlert(static_cast<int>(data[7]));
        ApplyHpMpAlertToStatusBar(hpAlert, mpAlert);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

using SaveGlobal_t = void(__thiscall*)(void* pThis);
static SaveGlobal_t s_SaveGlobal = reinterpret_cast<SaveGlobal_t>(ClientAddresses::kSaveGlobal);

static void __fastcall SaveGlobal_Hook(void* pThis, void* /*edx*/) {
    s_SaveGlobal(pThis);
    SendHpMpAlertFromStatusBar();
}
} // namespace

void RegisterHpMpAlertPacketHandler() {
    PacketDispatcher::RegisterLegacyHandler(
        kOpcodeSetHpMpAlert,
        [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
            return HandleHpMpAlertPacket(packet, opcode);
        });
}

void HookSaveGlobal(bool enable) {
    Memory::SetHook(enable, reinterpret_cast<void**>(&s_SaveGlobal), SaveGlobal_Hook);
}

void HookHpMpAlertRecv(bool enable) {
    (void)enable;
    // ProcessPacket hook is owned by compat::PacketDispatcher via ModRegistry::Initialize().
}

#endif // BEIDOU_MINIMAL_PLUGIN
