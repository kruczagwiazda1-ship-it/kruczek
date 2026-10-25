#!/usr/bin/env bash
# Assemble the input folder for NUSPacker:  build/wup_input/{code,content,meta}
#   usage: tools/make_wup.sh <template_dir> [TITLE_ID_16_HEX]
# <template_dir> = the code/ + meta/ (+content/) folders of ANY existing installable Wii U homebrew channel
# (it supplies app.xml / cos.xml / meta.xml, which differ between firmware/SDK versions and which I could
# not verify). We swap in kruczek.rpx, the images and the names/title id.
set -euo pipefail
T="${1:?template dir (with code/ meta/) required}"
TID="${2:-0005000012345678}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; OUT="$ROOT/build/wup_input"
[ -f "$ROOT/kruczek.rpx" ] || { echo "build kruczek.rpx first (make)"; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT"; cp -r "$T/code" "$T/meta" "$OUT/"; mkdir -p "$OUT/content"
[ -d "$T/content" ] && cp -r "$T/content/." "$OUT/content/" || true
python3 "$ROOT/tools/make_tga.py" "$OUT/meta"
# replace the template's main rpx (keep its file name) with ours
RPX="$(ls "$OUT"/code/*.rpx | head -n1)"; cp "$ROOT/kruczek.rpx" "$RPX"
# title id + names
for f in "$OUT/code/app.xml" "$OUT/meta/meta.xml"; do
  [ -f "$f" ] || continue
  sed -i -E "s#(<title_id[^>]*>)[0-9A-Fa-f]{16}(</title_id>)#\1${TID}\2#g" "$f"
  sed -i -E "s#(<(longname|shortname)_[a-z]+[^>]*>)[^<]*(</)#\1Kruczek Survival\3#g" "$f"
  sed -i -E "s#(<publisher_[a-z]+[^>]*>)[^<]*(</)#\1Luk\2#g" "$f"
done
echo "Input ready: $OUT"
echo "Next:  java -jar NUSPacker.jar -in $OUT -out $ROOT/build/wup_out -encryptKeyWith <WII_U_COMMON_KEY>"
echo "Then copy build/wup_out/<titleid> to SD:/install/ and use WUP Installer GX2 (install to USB, not NAND)."
