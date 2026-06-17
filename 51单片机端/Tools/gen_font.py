"""生成 16x16 中文汉字点阵数据 (C51 code 数组格式)
输出格式: 逐列式, 高位(bit7)在下, 亮点为1
"""
from PIL import Image, ImageDraw, ImageFont

# ===== 配置 =====
CHARS = "智 能 门 锁 按 任 意 键 开 请 输 入 密 码 确 认 退 格 取 消 已 欢 迎 回 家 修 改 重 新 上 再 次 成 功 错 误 还 剩 机 会 时 秒 0 1 2 3 4 5 6 7 8 9 死 配 对 蓝 牙 模 式 等 待 中".replace(" ", "")
FONT_PATH = "C:\\Windows\\Fonts\\simhei.ttf"
OUTPUT_FILE = "C:\\Users\\ChenShuang\\Desktop\\51单片机端\\Hardware\\Font_16x16.h"
CHAR_SIZE = 16

# 导出索引宏定义
FONT_INDEX = {
    "ZHI": 0, "NENG": 1, "MEN": 2, "SUO": 3,
    "AN": 4, "REN": 5, "YI": 6, "JIAN": 7,
    "KAI": 8,
    "QING": 9, "SHU": 10, "RU": 11, "MI": 12, "MA": 13,
    "QUE": 14, "REN2": 15,
    "TUI": 16, "GE": 17, "QU": 18, "XIAO": 19,
    "YI2": 20,
    "HUAN": 21, "YING2": 22, "HUI": 23, "JIA": 24,
    "XIU": 25, "GAI": 26, "CHONG": 27, "XIN": 28, "SHANG": 29,
    "ZAI": 30, "CI": 31,
    "CHENG": 32, "GONG": 33,
    "CUO": 34, "WU": 35, "HAI": 36, "SHENG": 37,
    "JI": 38, "HUI2": 39,
    "SHI": 40, "MIAO": 41,
    "D0": 42, "D1": 43, "D2": 44, "D3": 45, "D4": 46,
    "D5": 47, "D6": 48, "D7": 49, "D8": 50, "D9": 51,
    "SI": 52,
    "PEI": 53,
    "DUI": 54,
    "LAN": 55,
    "YA": 56,
    "MO": 57,
    "SHI4": 58,
    "DENG": 59,
    "DAI": 60,
    "ZHONG": 61,
}


def render_char_to_bytes(char, font):
    img = Image.new("1", (CHAR_SIZE, CHAR_SIZE), 0)
    draw = ImageDraw.Draw(img)
    bbox = draw.textbbox((0, 0), char, font=font)
    cw = bbox[2] - bbox[0]
    ch = bbox[3] - bbox[1]
    ox = (CHAR_SIZE - cw) // 2 - bbox[0]
    oy = (CHAR_SIZE - ch) // 2 - bbox[1]
    draw.text((ox, oy), char, font=font, fill=1)

    pixels = img.load()
    data = []
    for x in range(CHAR_SIZE):
        upper = 0
        lower = 0
        for y in range(8):
            if pixels[x, y] > 0:
                upper |= (1 << y)
        for y in range(8):
            if pixels[x, y + 8] > 0:
                lower |= (1 << y)
        data.append(upper)
        data.append(lower)
    return data


def main():
    font = ImageFont.truetype(FONT_PATH, CHAR_SIZE)

    lines = []
    lines.append("#ifndef __FONT_16X16_H__")
    lines.append("#define __FONT_16X16_H__")
    lines.append("")

    # 索引宏
    lines.append("// ============ 汉字索引宏 ============")
    for name, idx in sorted(FONT_INDEX.items(), key=lambda x: x[1]):
        ch = CHARS[idx]
        lines.append(f"#define FONT_IDX_{name:8s} {idx}   // '{ch}'")
    lines.append("")
    lines.append(f"#define FONT_CHAR_COUNT  {len(CHARS)}")
    lines.append("")

    # 数据数组
    lines.append("// 汉字 16x16 点阵 (逐列式, 高位在下, 亮为1)")
    lines.append("// 每个汉字 32 字节, 存放于 CODE 区")
    lines.append(f"static unsigned char code font_data[FONT_CHAR_COUNT][32] = {{")
    for idx, ch in enumerate(CHARS):
        data = render_char_to_bytes(ch, font)
        hex_bytes = ", ".join(f"0x{b:02X}" for b in data)
        lines.append(f"    {{ {hex_bytes} }},  // [{idx:2d}] '{ch}'")
    lines.append("};")
    lines.append("")
    lines.append("#endif")

    content = "\n".join(lines)
    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"已生成: {OUTPUT_FILE}")
    print(f"字符数: {len(CHARS)}")
    print(f"总字节: {len(CHARS) * 32} (CODE 区)")


if __name__ == "__main__":
    main()
