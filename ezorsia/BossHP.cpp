#include "stdafx.h"
#include "BossHP.h"
#include "Client.h"
#ifndef BEIDOU_MINIMAL_PLUGIN
#include "compat/ModRegistry.h"
#include "pendant2/Pendant2Api.h"
#endif

#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "compat/hook.h"
#include "compat/wvs/field.h"
#include "compat/wvs/util.h"
#include "compat/ztl/zcom.h"
#include "worldmap/getwzinfo.h"

// ---------------------------------------------------------------------------
// Boss info HUD under MobGage:
// - Static text on HP-tag canvas (BOSS信息 / Lv / HP%)
// - Scroll on a SEPARATE layer: RemoveCanvas+InsertCanvas each tick
// - 「伤害统计」button removed (glyph boxes + unreliable hit-test)
// ---------------------------------------------------------------------------

namespace {

constexpr DWORD kShowMobHpTag = 0x005336CA;
constexpr DWORD kShowMobHpTagGetHeightCall = 0x00533AFD;
constexpr DWORD kCFieldDispose = 0x00529035;
	constexpr DWORD kOnFieldEffect = 0x005330F7;
constexpr DWORD kWzFontCreate = 0x0046341A;

constexpr unsigned long kColLabel = 0xFFFFCC00;
constexpr unsigned long kColValue = 0xFFCCFF00;
constexpr unsigned long kColOutline = 0xFF000000;

constexpr unsigned int kBossInfoCanvasHeight = 38;
constexpr int kPortraitW = 40;
constexpr int kInfoBarTop = 19;
constexpr int kInfoBarH = 17;
constexpr int kInfoTextY = 21;
constexpr int kInfoPadL = 0;
constexpr int kInfoPadR = 4;
constexpr int kScrollGap = 8;
constexpr int kScrollViewIdeal = 240;
constexpr int kScrollSpeedPxPerSec = 36;
constexpr int kScrollLoopPad = 48;
constexpr int kScrollLayerZ = 50;

bool g_visible = false;
IWzGr2DLayerPtr g_hpLayer;
IWzGr2DLayerPtr g_scrollLayer;
int g_canvasW = 0;
int g_canvasH = 0;

int g_staticW = 0;
int g_scrollTextW = 0;
int g_scrollViewX = 0;
int g_scrollViewW = 0;
float g_scrollOffset = 0.f;
DWORD g_lastTick = 0;

int g_level = 0;
int g_pa = 0;
int g_pd = 0;
int g_ma = 0;
int g_md = 0;
int g_acc = 0;
int g_eva = 0;
char g_hpVal[64] = {};
char g_weak[32] = {};
char g_resist[32] = {};

	// int64 HP from OnFieldEffect packet tail
	static bool g_haveRealHp = false;
	static unsigned int g_realHpMobId = 0;
	static int64_t g_realHp = 0;
	static int64_t g_realMaxHp = 0;

IWzFontPtr g_fontLabel;
IWzFontPtr g_fontValue;
IWzFontPtr g_fontOutline;

Ztl_bstr_t GbkToBstr(const char* gbk) {
	wchar_t wbuf[256];
	if (!gbk || MultiByteToWideChar(CP_ACP, 0, gbk, -1, wbuf, _countof(wbuf)) <= 0) {
		return Ztl_bstr_t(L"");
	}
	return Ztl_bstr_t(wbuf);
}

void FormatIntWithCommas(int value, char* out, size_t outSize) {
	if (!out || outSize < 2) {
		return;
	}
	if (value < 0) {
		value = 0;
	}
	char digits[16];
	sprintf_s(digits, "%d", value);
	const size_t len = strlen(digits);
	size_t o = 0;
	for (size_t i = 0; i < len && o + 1 < outSize; ++i) {
		const size_t remain = len - i;
		if (i > 0 && remain % 3 == 0) {
			out[o++] = ',';
			if (o + 1 >= outSize) {
				break;
			}
		}
		out[o++] = digits[i];
	}
	out[o < outSize ? o : outSize - 1] = 0;
}

void FormatInt64WithCommas(int64_t value, char* out, size_t outSize) {
	if (!out || outSize < 2) return;
	if (value < 0) value = 0;
	char digits[32];
	sprintf_s(digits, "%lld", static_cast<long long>(value));
	size_t len = strlen(digits), o = 0;
	for (size_t i = 0; i < len && o + 1 < outSize; ++i) {
		if (i > 0 && (len - i) % 3 == 0) { out[o++] = ','; if (o + 1 >= outSize) break; }
		out[o++] = digits[i];
	}
	out[o < outSize ? o : outSize - 1] = 0;
}

bool CreateFontFace(IWzFontPtr& font, const wchar_t* face, unsigned long color, unsigned long size, const wchar_t* style) {
	if (font) {
		return true;
	}
	PcCreateObject<IWzFontPtr>(L"Canvas#Font", font, nullptr);
	if (!font) {
		return false;
	}
	Ztl_variant_t vStyle = style;
	auto fnCreate = reinterpret_cast<HRESULT(__thiscall*)(
			IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&)>(kWzFontCreate);
	if (SUCCEEDED(fnCreate(font, Ztl_bstr_t(face), size, color, vStyle))) {
		return true;
	}
	font = nullptr;
	return false;
}

bool EnsureFonts() {
	if (g_fontLabel && g_fontValue && g_fontOutline) {
		return true;
	}
	auto makeBold = [&](IWzFontPtr& f, unsigned long color, unsigned long sz) -> bool {
		return CreateFontFace(f, L"\x5B8B\x4F53", color, sz, L"B")
				|| CreateFontFace(f, L"SimSun", color, sz, L"B")
				|| CreateFontFace(f, L"Arial", color, sz, L"B");
	};
	return makeBold(g_fontLabel, kColLabel, 12)
			&& makeBold(g_fontValue, kColValue, 12)
			&& makeBold(g_fontOutline, kColOutline, 12);
}

int MeasureGbkWidth(IWzFontPtr font, const char* gbk) {
	if (!font || !gbk || !gbk[0]) {
		return 0;
	}
	try {
		return static_cast<int>(font->CalcTextWidth(GbkToBstr(gbk), Ztl_variant_t()));
	} catch (...) {
		return 0;
	}
}

void DrawOutlinedPart(IWzCanvasPtr canvas, int x, int y, const char* gbk, IWzFontPtr fillFont) {
	if (!canvas || !fillFont || !gbk || !gbk[0]) {
		return;
	}
	Ztl_bstr_t text = GbkToBstr(gbk);
	try {
		if (g_fontOutline) {
			Ztl_variant_t aOutline((long)255);
			static const int kOff[8][2] = {
					{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
			for (const auto& o : kOff) {
				canvas->DrawTextA(x + o[0], y + o[1], text, g_fontOutline, aOutline, Ztl_variant_t());
			}
		}
		canvas->DrawTextA(x, y, text, fillFont, Ztl_variant_t(), Ztl_variant_t());
	} catch (...) {
	}
}

int DrawPair(IWzCanvasPtr canvas, int x, int y, const char* label, const char* value) {
	DrawOutlinedPart(canvas, x, y, label, g_fontLabel);
	x += MeasureGbkWidth(g_fontLabel, label);
	DrawOutlinedPart(canvas, x, y, value, g_fontValue);
	x += MeasureGbkWidth(g_fontValue, value);
	return x;
}

int MeasurePair(const char* label, const char* value) {
	return MeasureGbkWidth(g_fontLabel, label) + MeasureGbkWidth(g_fontValue, value);
}

const char* ElemNameGbk(char c) {
	switch (static_cast<char>(::toupper(static_cast<unsigned char>(c)))) {
	case 'F':
		return "\xBB\xF0";
	case 'I':
		return "\xB1\xF9";
	case 'L':
		return "\xB5\xE7";
	case 'S':
		return "\xB6\xBE";
	case 'H':
		return "\xCA\xA5";
	case 'D':
		return "\xB0\xB5";
	case 'P':
		return "\xCE\xEF";
	default:
		return "?";
	}
}

void ParseElemAttr(const std::string& attr, std::string& weakOut, std::string& resistOut) {
	weakOut.clear();
	resistOut.clear();
	for (size_t i = 0; i + 1 < attr.size(); i += 2) {
		const char elem = attr[i];
		const char digit = attr[i + 1];
		if (digit < '0' || digit > '9') {
			continue;
		}
		const int eff = digit - '0';
		const char* name = ElemNameGbk(elem);
		if (eff == 3) {
			if (!weakOut.empty()) {
				weakOut += "/";
			}
			weakOut += name;
		} else if (eff == 1 || eff == 2) {
			if (!resistOut.empty()) {
				resistOut += "/";
			}
			resistOut += name;
		}
	}
	if (weakOut.empty()) {
		weakOut = "\xCE\xDE";
	}
	if (resistOut.empty()) {
		resistOut = "\xCE\xDE";
	}
}

void DrawStaticParts(IWzCanvasPtr canvas, int x, int y) {
	char lvBuf[16];
	sprintf_s(lvBuf, "%d ", g_level);
	x = DrawPair(canvas, x, y, "BOSS\xD0\xC5\xCF\xA2 ", "");
	x = DrawPair(canvas, x, y, "Lv.", lvBuf);
	DrawPair(canvas, x, y, "HP:", g_hpVal);
}

int MeasureStaticWidth() {
	char lvBuf[16];
	sprintf_s(lvBuf, "%d ", g_level);
	return MeasurePair("BOSS\xD0\xC5\xCF\xA2 ", "")
			+ MeasurePair("Lv.", lvBuf)
			+ MeasurePair("HP:", g_hpVal);
}

void DrawScrollParts(IWzCanvasPtr canvas, int x, int y) {
	char pdBuf[24];
	sprintf_s(pdBuf, "%d ", g_pd);
	char mdBuf[24];
	sprintf_s(mdBuf, "%d ", g_md);
	char weakBuf[40];
	sprintf_s(weakBuf, "%s ", g_weak);
	char paBuf[24];
	sprintf_s(paBuf, "%d ", g_pa);
	char maBuf[24];
	sprintf_s(maBuf, "%d ", g_ma);
	char accBuf[24];
	sprintf_s(accBuf, "%d ", g_acc);
	char evaBuf[24];
	sprintf_s(evaBuf, "%d ", g_eva);
	// 物防 / 魔防 / 弱点 / 抗性 / 物攻 / 魔攻 / 命中 / 回避
	x = DrawPair(canvas, x, y, "\xCE\xEF\xB7\xC0:", pdBuf);
	x = DrawPair(canvas, x, y, "\xC4\xA7\xB7\xC0:", mdBuf);
	x = DrawPair(canvas, x, y, "\xC8\xF5\xB5\xE3:", weakBuf);
	x = DrawPair(canvas, x, y, "\xBF\xB9\xD0\xD4:", g_resist);
	x = DrawPair(canvas, x, y, "  ", "");
	x = DrawPair(canvas, x, y, "\xCE\xEF\xB9\xA5:", paBuf);
	x = DrawPair(canvas, x, y, "\xC4\xA7\xB9\xA5:", maBuf);
	x = DrawPair(canvas, x, y, "\xC3\xFC\xD6\xD0:", accBuf);
	DrawPair(canvas, x, y, "\xBB\xD8\xB1\xDC:", evaBuf);
}

int MeasureScrollWidth() {
	char pdBuf[24];
	sprintf_s(pdBuf, "%d ", g_pd);
	char mdBuf[24];
	sprintf_s(mdBuf, "%d ", g_md);
	char weakBuf[40];
	sprintf_s(weakBuf, "%s ", g_weak);
	char paBuf[24];
	sprintf_s(paBuf, "%d ", g_pa);
	char maBuf[24];
	sprintf_s(maBuf, "%d ", g_ma);
	char accBuf[24];
	sprintf_s(accBuf, "%d ", g_acc);
	char evaBuf[24];
	sprintf_s(evaBuf, "%d ", g_eva);
	return MeasurePair("\xCE\xEF\xB7\xC0:", pdBuf)
			+ MeasurePair("\xC4\xA7\xB7\xC0:", mdBuf)
			+ MeasurePair("\xC8\xF5\xB5\xE3:", weakBuf)
			+ MeasurePair("\xBF\xB9\xD0\xD4:", g_resist)
			+ MeasurePair("  ", "")
			+ MeasurePair("\xCE\xEF\xB9\xA5:", paBuf)
			+ MeasurePair("\xC4\xA7\xB9\xA5:", maBuf)
			+ MeasurePair("\xC3\xFC\xD6\xD0:", accBuf)
			+ MeasurePair("\xBB\xD8\xB1\xDC:", evaBuf);
}

void HideScrollLayer() {
	if (g_scrollLayer) {
		try {
			g_scrollLayer->visible = 0;
		} catch (...) {
		}
	}
}

void DestroyScrollLayer() {
	HideScrollLayer();
	g_scrollLayer = nullptr;
}

IWzCanvasPtr BuildScrollCanvas(int viewW, int viewH, int scrollOffset, int textW) {
	IWzCanvasPtr canvas;
	PcCreateObject<IWzCanvasPtr>(L"Canvas", canvas, nullptr);
	if (!canvas || viewW <= 0 || viewH <= 0) {
		return IWzCanvasPtr();
	}
	try {
		canvas->Create(static_cast<unsigned>(viewW), static_cast<unsigned>(viewH), vtMissing, vtMissing);
	} catch (...) {
		return IWzCanvasPtr();
	}

	const int loopW = textW + kScrollLoopPad;
	const int drawX = -scrollOffset;
	const int textY = kInfoTextY - kInfoBarTop;
	DrawScrollParts(canvas, drawX, textY);
	if (loopW > 0) {
		DrawScrollParts(canvas, drawX + loopW, textY);
	}
	return canvas;
}

void ReplaceLayerCanvas(IWzGr2DLayerPtr layer, IWzCanvasPtr canvas) {
	if (!layer || !canvas) {
		return;
	}
	try {
		layer->RemoveCanvas(0);
	} catch (...) {
	}
	try {
		layer->InsertCanvas(canvas, 0, 255, 255, 100, 100);
	} catch (...) {
	}
}

bool EnsureScrollLayer(int viewW, int viewH) {
	if (viewW <= 0 || viewH <= 0 || !g_hpLayer) {
		return false;
	}
	auto gr = get_gr();
	if (!gr) {
		return false;
	}

	bool needCreate = !g_scrollLayer;
	if (!needCreate) {
		try {
			if (g_scrollLayer->width != viewW || g_scrollLayer->height != viewH) {
				needCreate = true;
			}
		} catch (...) {
			needCreate = true;
		}
	}

	if (needCreate) {
		g_scrollLayer = nullptr;
		IWzCanvasPtr blank;
		PcCreateObject<IWzCanvasPtr>(L"Canvas", blank, nullptr);
		if (!blank) {
			return false;
		}
		try {
			blank->Create(static_cast<unsigned>(viewW), static_cast<unsigned>(viewH), vtMissing, vtMissing);
			g_scrollLayer = gr->CreateLayer(
					0,
					0,
					static_cast<unsigned>(viewW),
					static_cast<unsigned>(viewH),
					kScrollLayerZ,
					static_cast<IUnknown*>(blank),
					vtMissing);
		} catch (...) {
			g_scrollLayer = nullptr;
			return false;
		}
		if (!g_scrollLayer) {
			return false;
		}
		try {
			g_scrollLayer->origin = static_cast<IUnknown*>(g_hpLayer.GetInterfacePtr());
			g_scrollLayer->color = 0xFFFFFFFF;
		} catch (...) {
		}
	}

	try {
		g_scrollLayer->width = viewW;
		g_scrollLayer->height = viewH;
		g_scrollLayer->visible = 1;
		g_scrollLayer->RelMove(g_scrollViewX, kInfoBarTop);
	} catch (...) {
		return false;
	}
	return true;
}

void UpdateLayout() {
	const int barX = kPortraitW;
	const int availW = g_canvasW - kPortraitW;
	g_staticW = MeasureStaticWidth();
	g_scrollTextW = MeasureScrollWidth();

	g_scrollViewX = barX + kInfoPadL + g_staticW + kScrollGap;
	// Use remaining width after static text (button removed — scroll gets full remainder)
	g_scrollViewW = availW - kInfoPadL - g_staticW - kScrollGap - kInfoPadR;
	if (g_scrollViewW > kScrollViewIdeal * 2) {
		g_scrollViewW = kScrollViewIdeal * 2; // cap so it doesn't span entire bar oddly
	}
	if (g_scrollViewW < 64) {
		g_scrollViewW = (availW > 64) ? 64 : availW;
	}
}

void DrawStaticOnHpTag() {
	if (!g_hpLayer) {
		return;
	}
	IWzCanvasPtr canvas;
	try {
		canvas = g_hpLayer->canvas[0];
	} catch (...) {
		return;
	}
	if (!canvas) {
		return;
	}
	try {
		const int h = (g_canvasH > kInfoBarTop) ? (g_canvasH - kInfoBarTop) : kInfoBarH;
		canvas->DrawRectangle(0, kInfoBarTop, g_canvasW, h, 0x00000000);
	} catch (...) {
	}
	DrawStaticParts(canvas, kPortraitW + kInfoPadL, kInfoTextY);
}

void RefreshScrollLayer() {
	if (!g_visible || g_scrollViewW <= 0 || g_scrollTextW <= 0) {
		HideScrollLayer();
		return;
	}
	if (!EnsureScrollLayer(g_scrollViewW, kInfoBarH)) {
		return;
	}
	IWzCanvasPtr canvas = BuildScrollCanvas(
			g_scrollViewW,
			kInfoBarH,
			static_cast<int>(g_scrollOffset),
			g_scrollTextW);
	ReplaceLayerCanvas(g_scrollLayer, canvas);
}

void RedrawAll() {
	if (!g_visible || !g_hpLayer || !EnsureFonts()) {
		HideScrollLayer();
		return;
	}
	UpdateLayout();
	DrawStaticOnHpTag();
	RefreshScrollLayer();
}

void HideAll() {
	g_visible = false;
	g_hpLayer = nullptr;
	DestroyScrollLayer();
}

struct FieldPacketView {
	unsigned char* base;
	explicit FieldPacketView(void* p) : base(reinterpret_cast<unsigned char*>(p)) {}
	unsigned char* data() const { return *reinterpret_cast<unsigned char**>(base + 0x8); }
	unsigned int& offset() const { return *reinterpret_cast<unsigned int*>(base + 0x14); }
	unsigned short length() const { return *reinterpret_cast<unsigned short*>(base + 0xC); }
	bool CanRead(size_t n) const { return offset() + n <= length(); }
	unsigned int Decode4() { if (!CanRead(4)) return 0; unsigned int v = 0; memcpy(&v, data() + offset(), 4); offset() += 4; return v; }
	int64_t Decode8() { if (!CanRead(8)) return 0; int64_t v = 0; memcpy(&v, data() + offset(), 8); offset() += 8; return v; }
};

void PeekBossHpReals(void* iPacket) {
	g_haveRealHp = false;
	if (!iPacket) return;
	FieldPacketView pkt(iPacket);
	unsigned int saved = pkt.offset();
	if (!pkt.CanRead(1)) return;
	if (pkt.data()[pkt.offset()] != 5) return;
	pkt.offset() += 1;
	if (!pkt.CanRead(14)) return;
	unsigned int mobId = pkt.Decode4();
	pkt.Decode4(); pkt.Decode4();
	pkt.offset() += 2;
	if (pkt.CanRead(16)) {
		int64_t cur = pkt.Decode8();
		int64_t max = pkt.Decode8();
		if (max > 0 && cur >= 0) {
			g_haveRealHp = true; g_realHpMobId = mobId;
			g_realHp = cur; g_realMaxHp = max;
		}
	}
	pkt.offset() = saved;
}

void CacheAndShow(void* pField, unsigned int dwMobId, int nHP, int nMaxHP) {
	if (!pField || nHP <= 0 || nMaxHP <= 0) {
		HideAll();
		return;
	}
	if (!EnsureFonts()) {
		HideAll();
		return;
	}

	auto* field = reinterpret_cast<CField*>(pField);
	IWzGr2DLayerPtr layer = field->m_pLayerHPTag;
	if (!layer) {
		HideAll();
		return;
	}

	IWzCanvasPtr canvas;
	try {
		canvas = layer->canvas[0];
		g_canvasW = canvas ? canvas->width : 0;
		g_canvasH = canvas ? canvas->height : 0;
	} catch (...) {
		HideAll();
		return;
	}
	if (g_canvasW <= kPortraitW + 80) {
		HideAll();
		return;
	}

	const MobCombatInfo combat = GetMobCombatInfoById(static_cast<int>(dwMobId));
	std::string weak;
	std::string resist;
	ParseElemAttr(combat.elemAttr, weak, resist);

	g_level = combat.level;
	g_pa = combat.paDamage;
	g_pd = combat.pdDamage;
	g_ma = combat.maDamage;
	g_md = combat.mdDamage;
	g_acc = combat.acc;
	g_eva = combat.eva;
	strncpy_s(g_weak, weak.c_str(), _TRUNCATE);
	strncpy_s(g_resist, resist.c_str(), _TRUNCATE);

	char hpBuf[32];
	if (g_haveRealHp && dwMobId == g_realHpMobId) {
		FormatInt64WithCommas(g_realHp, hpBuf, sizeof(hpBuf));
		const double pct = static_cast<double>(g_realHp) / static_cast<double>(g_realMaxHp) * 100.0;
		sprintf_s(g_hpVal, "%s(%.1f%%) ", hpBuf, pct);
	} else {
		FormatIntWithCommas(nHP, hpBuf, sizeof(hpBuf));
		const double pct = static_cast<double>(nHP) / static_cast<double>(nMaxHP) * 100.0;
		sprintf_s(g_hpVal, "%s(%.1f%%) ", hpBuf, pct);
	}

	if (g_hpLayer != layer) {
		DestroyScrollLayer();
	}
	g_hpLayer = layer;
	g_visible = true;
	g_lastTick = GetTickCount();
	RedrawAll();
}

void TickScroll() {
	if (!g_visible) {
		return;
	}

	const DWORD now = GetTickCount();
	const DWORD dt = (g_lastTick == 0) ? 16 : (now - g_lastTick);
	g_lastTick = now;

	const int loopW = g_scrollTextW + kScrollLoopPad;
	if (loopW > 0 && dt > 0 && dt <= 200) {
		g_scrollOffset += (kScrollSpeedPxPerSec * static_cast<float>(dt)) / 1000.f;
		while (g_scrollOffset >= static_cast<float>(loopW)) {
			g_scrollOffset -= static_cast<float>(loopW);
		}
	}

	UpdateLayout();
	DrawStaticOnHpTag();
	RefreshScrollLayer();
}

unsigned int __fastcall BossHpTag_GetHeight_hook(IWzCanvas* /*pThis*/, void* /*edx*/) {
	return kBossInfoCanvasHeight;
}

} // namespace

void BossHP::OnClientTick() {
	TickScroll();
}

void BossHP::OnFieldEnter() {
	HideAll();
}

void BossHP::OnFieldDispose() {
	HideAll();
}

void BossHP::Hook() {
	static bool s_hooked = false;
	if (s_hooked) {
		return;
	}
	s_hooked = true;

	// CreateLayer @0x533B03: width imm / nLeft from [ebp-44]. RS nLeft hook sets
	// CT-adjusted nLeft (after minimap) and final barW, and NOPs the width sub.
	// Keep NOP here as belt-and-suspenders if ShowMobHPTag runs before first RS calc.
	{
		DWORD oldProt = 0;
		VirtualProtect(reinterpret_cast<void*>(0x00533B08), 3, PAGE_EXECUTE_READWRITE, &oldProt);
		*reinterpret_cast<unsigned char*>(0x00533B08) = 0x90;
		*reinterpret_cast<unsigned char*>(0x00533B09) = 0x90;
		*reinterpret_cast<unsigned char*>(0x00533B0A) = 0x90;
		VirtualProtect(reinterpret_cast<void*>(0x00533B08), 3, oldProt, &oldProt);
	}
	std::cout << "[BossHP] Hook TIP_BOSSHP_MINIMAP_20260801 (info HUD + RS minimap layout)" << std::endl;

	PatchCall(kShowMobHpTagGetHeightCall, &BossHpTag_GetHeight_hook);

	typedef void(__fastcall* ShowMobHpTag_t)(
			void* pThis, void* edx, unsigned int dwMobID, int nColor, int nBgColor, int nHP, int nMaxHP);
	static auto _ShowMobHpTag = reinterpret_cast<ShowMobHpTag_t>(kShowMobHpTag);

	ShowMobHpTag_t showHook = [](void* pThis, void* edx, unsigned int dwMobID, int nColor, int nBgColor, int nHP, int nMaxHP) -> void {
		_ShowMobHpTag(pThis, edx, dwMobID, nColor, nBgColor, nHP, nMaxHP);
		CacheAndShow(pThis, dwMobID, nHP, nMaxHP);
	};
	Memory::SetHook(true, reinterpret_cast<void**>(&_ShowMobHpTag), showHook);

	typedef void(__fastcall* FieldDispose_t)(void* pThis, void* edx);
	static auto _FieldDispose = reinterpret_cast<FieldDispose_t>(kCFieldDispose);
	FieldDispose_t disposeHook = [](void* pThis, void* edx) -> void {
		BossHP::OnFieldDispose();
		_FieldDispose(pThis, edx);
	};
	Memory::SetHook(true, reinterpret_cast<void**>(&_FieldDispose), disposeHook);

	typedef void(__fastcall* OnFieldEffect_t)(void* pThis, void* edx, void* iPacket);
	static auto _OnFieldEffect = reinterpret_cast<OnFieldEffect_t>(kOnFieldEffect);
	OnFieldEffect_t fieldHook = [](void* pThis, void* edx, void* iPacket) -> void {
		PeekBossHpReals(iPacket);
		_OnFieldEffect(pThis, edx, iPacket);
	};
	Memory::SetHook(true, reinterpret_cast<void**>(&_OnFieldEffect), fieldHook);

	// Own UserLocal::Update so scroll + ModRegistry tick even if LazyCompat 2nd SetHook fails.
	typedef void(__fastcall* UserLocalUpdate_t)(void* pThis, void* edx);
	static auto _UserLocalUpdate = reinterpret_cast<UserLocalUpdate_t>(0x0094A144);
	UserLocalUpdate_t updateHook = [](void* pThis, void* edx) -> void {
		_UserLocalUpdate(pThis, edx);
		BossHP::OnClientTick();
#ifndef BEIDOU_MINIMAL_PLUGIN
		ModRegistry::OnClientTick();
		Pendant2OnClientTick();
#endif
	};
	Memory::SetHook(true, reinterpret_cast<void**>(&_UserLocalUpdate), updateHook);
}
