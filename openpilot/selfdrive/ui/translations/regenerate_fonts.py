#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
regenerate_fonts.py
====================
根据各语言翻译(po)重新生成 fallback subset 字体，解决"改 po 后中文/日文/韩文/泰文缺字"。

背景（见 system/ui/lib/application.py）：
  - 主字体 Inter 不加载任何 CJK 字符；zh-CHS/zh-CHT/ja/ko/th 等 fallback 语言
    完全依赖 selfdrive/assets/fonts 下的 Noto subset 字体。
  - 这些 subset 字体必须覆盖对应 po 里出现过的所有字符，否则渲染成空白。
  - 本仓库没有"自动重新生成字体"的构建步骤（字体是提交的静态资源），因此改 po
    后必须手动重生成 subset 字体，否则新加的汉字会缺字。

本脚本做的事：
  1. 检查已提交的 subset 字体是否覆盖该语言 po 的全部"脚本字符"
     （CJK 基本区/扩展A、平假名、片假名、谚文、泰文等 BMP 字符）。
  2. 若缺失（或 --all），从"完整源字体"重新 subset 并覆盖提交字体。

完整源字体来源（按优先级）：
  1. 同目录下的 <Lang>Full.otf（如 NotoSansCJKscFull.otf），需自行放置；
  2. 网络下载并缓存到 --cache 目录（默认 ./_font_cache）；
  3. 回退到仓库内 unifont.otf（完整 Unicode，但位图风、不如 Noto 美观）。

用法：
  python regenerate_fonts.py            # 仅对"缺字"的语言重新生成
  python regenerate_fonts.py --all      # 强制全部重新生成
  python regenerate_fonts.py --check    # 只检查覆盖情况，不写文件
  python regenerate_fonts.py --lang zh-CHS

注意：emoji（如 🔥，非 BMP）不在 Noto CJK 字体中，本脚本不将其视为"缺字"
（属独立问题，需彩色 emoji 字体才能解决，或在 po 中去掉装饰性 emoji）。
"""
import argparse
import os
import shutil
import sys
import urllib.request

try:
    from fontTools.ttLib import TTFont
    from fontTools.subset import Subsetter, Options
except ImportError:
    sys.exit("需要 fontTools：pip install fonttools")

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS_FONTS = os.path.normpath(os.path.join(HERE, "..", "..", "assets", "fonts"))
TRANSLATIONS = HERE

EXTRA = "–‑✓×°§•X⚙✕◀▶✔⌫⇧␣○●↳çêüñ€£¥"
CJK_PUNCT = "，。、；：？！“”‘’（）《》【】…—·「」『』〈〉"

# 下载地址（完整 Noto CJK 源，~16MB/个）
DOWNLOAD_BASE = "https://cdn.jsdelivr.net/gh/notofonts/noto-cjk@main/Sans/OTF"
DOWNLOAD_URL = {
    "zh-CHS": f"{DOWNLOAD_BASE}/SimplifiedChinese/NotoSansCJKsc-Regular.otf",
    "zh-CHT": f"{DOWNLOAD_BASE}/TraditionalChinese/NotoSansCJKtc-Regular.otf",
    "ja":     f"{DOWNLOAD_BASE}/Japanese/NotoSansCJKjp-Regular.otf",
    "ko":     f"{DOWNLOAD_BASE}/Korean/NotoSansCJKkr-Regular.otf",
}

# 每种 fallback 语言：输出字体 / 完整源文件名 / 覆盖的 Unicode 字符区 / po 文件
LANG_SPECS = {
    "zh-CHS": dict(out="NotoSansCJKsc-Regular.otf", full="NotoSansCJKsc-Regular.otf",
                  blocks=[(0x4E00, 0x9FFF), (0x3400, 0x4DBF)], po="app_zh-CHS.po"),
    "zh-CHT": dict(out="NotoSansCJKtc-Regular.otf", full="NotoSansCJKtc-Regular.otf",
                  blocks=[(0x4E00, 0x9FFF), (0x3400, 0x4DBF)], po="app_zh-CHT.po"),
    "ja":     dict(out="NotoSansCJKjp-Regular.otf", full="NotoSansCJKjp-Regular.otf",
                  blocks=[(0x4E00, 0x9FFF), (0x3400, 0x4DBF),
                          (0x3040, 0x309F), (0x30A0, 0x30FF), (0xFF00, 0xFFEF)], po="app_ja.po"),
    "ko":     dict(out="NotoSansCJKkr-Regular.otf", full="NotoSansCJKkr-Regular.otf",
                  blocks=[(0x4E00, 0x9FFF), (0x3400, 0x4DBF),
                          (0xAC00, 0xD7A3), (0x1100, 0x11FF), (0x3130, 0x318F)], po="app_ko.po"),
    "th":     dict(out="NotoSansThai-Regular.ttf", full=None,
                  blocks=[(0x0E00, 0x0E7F)], po="app_th.po"),
}


def is_script_char(c: str) -> bool:
    o = ord(c)
    # 仅检查 BMP 脚本字符（CJK/假名/谚文/泰文）；emoji(>0xFFFF) 不在 Noto 中，跳过
    return (0x3400 <= o <= 0x9FFF or 0xAC00 <= o <= 0xD7A3 or
            0x3040 <= o <= 0x30FF or 0x0E00 <= o <= 0x0E7F or
            0x1100 <= o <= 0x11FF or 0x3130 <= o <= 0x318F or 0xFF00 <= o <= 0xFFEF)


def collect_chars(po_path: str, blocks):
    chars = set(range(32, 127)) | set(ord(c) for c in EXTRA)
    for a, b in blocks:
        chars |= set(range(a, b + 1))
    chars |= set(ord(c) for c in CJK_PUNCT)
    chars |= set(ord(c) for c in open(po_path, encoding="utf-8").read())
    return chars


def missing_in_font(font_path: str, po_path: str, blocks):
    try:
        cmap = TTFont(font_path).getBestCmap()
    except Exception as e:
        return f"字体读取失败: {e}"
    po = open(po_path, encoding="utf-8").read()
    miss = sorted({c for c in po if is_script_char(c) and ord(c) not in cmap})
    return miss


def get_full_source(lang: str, cache_dir: str):
    """返回完整源字体路径（本地优先 -> 下载缓存 -> unifont 回退）。"""
    spec = LANG_SPECS[lang]
    if lang == "th":
        # 泰文无单独完整源需求，直接用已提交字体（26KB 通常已覆盖全部泰文）
        return os.path.join(ASSETS_FONTS, spec["out"])
    # 1) 本地放置的 Full 文件
    local_full = os.path.join(ASSETS_FONTS, spec["full"].replace(".otf", "Full.otf").replace(".ttf", "Full.ttf"))
    if os.path.exists(local_full):
        return local_full
    # 2) 缓存
    os.makedirs(cache_dir, exist_ok=True)
    cached = os.path.join(cache_dir, spec["full"])
    if os.path.exists(cached):
        return cached
    # 3) 下载
    url = DOWNLOAD_URL[lang]
    print(f"  [下载] {lang} 完整源字体 -> {url}")
    try:
        urllib.request.urlretrieve(url, cached)
        return cached
    except Exception as e:
        print(f"  [警告] 下载失败({e})，回退到 unifont（位图风）")
        return os.path.join(ASSETS_FONTS, "unifont.otf")


def regenerate(lang: str, cache_dir: str, force: bool):
    spec = LANG_SPECS[lang]
    out_path = os.path.join(ASSETS_FONTS, spec["out"])
    po_path = os.path.join(TRANSLATIONS, spec["po"])

    miss = missing_in_font(out_path, po_path, spec["blocks"])
    if isinstance(miss, str):
        print(f"{lang}: {miss}")
        return
    if not miss and not force:
        print(f"{lang}: 已覆盖全部脚本字符，无需重生成")
        return

    if force and not miss:
        print(f"{lang}: 强制重生成（当前无缺字）")
    else:
        preview = "".join(miss[:30])
        print(f"{lang}: 缺字 {len(miss)} 种 -> {preview}，重新生成...")

    src = get_full_source(lang, cache_dir)
    chars = collect_chars(po_path, spec["blocks"])

    if not os.path.exists(out_path + ".bak"):
        shutil.copy2(out_path, out_path + ".bak")

    font = TTFont(src)
    opts = Options()
    opts.glyph_names = False
    opts.recalc_timestamp = False
    ss = Subsetter(options=opts)
    ss.populate(unicodes=sorted(chars))
    ss.subset(font)
    font.save(out_path)

    cmap = TTFont(out_path).getBestCmap()
    po = open(po_path, encoding="utf-8").read()
    remain = sorted({c for c in po if is_script_char(c) and ord(c) not in cmap})
    tail = "无缺字" if not remain else "".join(remain[:20])
    print(f"  -> 导出 {os.path.getsize(out_path) // 1024} KB，剩余缺字 {len(remain)} {tail}")


def main():
    ap = argparse.ArgumentParser(description="根据 po 重新生成 fallback subset 字体")
    ap.add_argument("--all", action="store_true", help="强制全部语言重新生成")
    ap.add_argument("--check", action="store_true", help="只检查覆盖情况，不写文件")
    ap.add_argument("--lang", help="只处理指定语言，如 zh-CHS")
    ap.add_argument("--cache", default=os.path.join(HERE, "_font_cache"), help="下载缓存目录")
    args = ap.parse_args()

    langs = [args.lang] if args.lang else list(LANG_SPECS.keys())
    for lang in langs:
        if lang not in LANG_SPECS:
            print(f"未知语言: {lang}（支持 {list(LANG_SPECS)}）")
            continue
        if args.check:
            spec = LANG_SPECS[lang]
            out_path = os.path.join(ASSETS_FONTS, spec["out"])
            po_path = os.path.join(TRANSLATIONS, spec["po"])
            miss = missing_in_font(out_path, po_path, spec["blocks"])
            if isinstance(miss, str):
                print(f"{lang}: {miss}")
            else:
                preview = "无缺字" if not miss else "".join(miss[:30])
                print(f"{lang}: 脚本字符缺字 {len(miss)} {preview}")
            continue
        regenerate(lang, args.cache, args.all)


if __name__ == "__main__":
    main()
