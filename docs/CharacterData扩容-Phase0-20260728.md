# CharacterData 扩容 Phase0 干跑报告

- 生成时间: 2026-07-28T00:21:05+08:00
- EXE: `E:\mxd_soft\2.客户端\083\BeiDou-Client\beidou.exe`
- IDA: {"status": "ok", "uptime_sec": 88451.682, "idb_path": "E:\\资料\\xiaoye\\mxd学习\\ida\\BeiDou_GMS_083.exe.i64", "module": "BeiDou.exe", "input_path": "E:\\mxd_soft\\2.客户端\\083\\BeiDou-Client\\BeiDou.exe",

## 方案

- `aEquipped` / `aEquipped2`: **52 → 64**
- 每表插入: **96** 字节 (`0x60`)
- 尾部字段总位移: **192** 字节 (`0xc0`)
- Get/Set 上界: **-51 → -63**

### 偏移规则

- off < 0x287: unchanged
- 0x287 <= off < 0x427: +0x60 (aEquipped2 body)
- off >= 0x427: +0xc0 (tail after both arrays)

### 关键字段映射 (ctor anchors)

| old | new | delta |
|-----|-----|-------|
| `0xbd` | `0xbd` | `+0x0` |
| `0xe7` | `0xe7` | `+0x0` |
| `0x287` | `0x2e7` | `+0x60` |
| `0x427` | `0x4e7` | `+0xc0` |
| `0x447` | `0x507` | `+0xc0` |
| `0x467` | `0x527` | `+0xc0` |
| `0x47f` | `0x53f` | `+0xc0` |
| `0x497` | `0x557` | `+0xc0` |
| `0x4af` | `0x56f` | `+0xc0` |
| `0x4c7` | `0x587` | `+0xc0` |
| `0x4df` | `0x59f` | `+0xc0` |
| `0x4f7` | `0x5b7` | `+0xc0` |
| `0x513` | `0x5d3` | `+0xc0` |
| `0x527` | `0x5e7` | `+0xc0` |
| `0x53b` | `0x5fb` | `+0xc0` |
| `0x54f` | `0x60f` | `+0xc0` |
| `0x5bf` | `0x67f` | `+0xc0` |
| `0x5d7` | `0x697` | `+0xc0` |
| `0x5db` | `0x69b` | `+0xc0` |
| `0x5ff` | `0x6bf` | `+0xc0` |
| `0x617` | `0x6d7` | `+0xc0` |

### 槽位抽样 (扩容后)

| BP | pos | zref | 说明 |
|----|-----|------|------|
| 20 | -20 | `0x187` | 经典原生 |
| 51 | -51 | `0x27f` | 经典原生 |
| 52 | -52 | `0x287` | 原与现金碰撞 |
| 53 | -53 | `0x28f` | 原与现金碰撞 |
| 54 | -54 | `0x297` | 原与现金碰撞 |
| 55 | -55 | `0x29f` | 原与现金碰撞 |
| 56 | -56 | `0x2a7` | 原与现金碰撞 |
| 59 | -59 | `0x2bf` | 原与现金碰撞 |
| 63 | -63 | `0x2df` | 原与现金碰撞 |

## 补丁候选量 (PE .text 原始字节命中，上限)

- 需移位 ctor 锚点 raw 命中合计: **525**
- 按 delta 汇总: `{'0x60': 47, '0xc0': 478}`
- bound/ctor 模式: `{'cmp_eax_m51': 3, 'cmp_ebx_m51': 1, 'cmp_esi_m51': 0, 'cmp_edi_m51': 1, 'cmp_ecx_m51': 0, 'push_52_6A34': 125}`
- apply 模式: `{'cmp_esi_51_83FE33': 3, 'cmp_ebx_m51_walk_83FBCD': 1}`

> raw_hits = byte-pattern count in .text (upper bound; Phase1 must use IDA find_imm / insn decode to filter false positives before patching)

## IDA 核对

- verified: **True**
- checks: `{'getitem_mentions_m51': True, 'ctor_mentions_52': True, 'ctor_mentions_231_or_0xe7': True, 'ctor_mentions_647_or_0x287': True}`

## Phase1 门禁 (未满足勿改 PE)

1. IDA `find_imm` / 指令解码后，移位命中清单导出为 `phase1_imm_sites.json`
2. 副本客户端备份 + 仅改分配大小/ctor count 的烟雾测试可进游戏
3. 经典装备穿戴/现金装/进图/重登全绿后，再抬 bound 与扩展槽
4. 扩展槽生效后删除 ijl15 `g_ring52/53` `g_badge54` `g_totem55` 影子

## 下一步

```bat
python phase1_build_imm_site_list.py --ida-port 13337
```
