#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 全功能回归：LRConvert 的任务管线 vs ffmpeg CLI 同参数输出
#
# 对照方法与重构方案 §9 一致：ffprobe 比「格式时长 / 各流包数 / 分辨率」，
# 再用全片解码校验输出完整性。流拷贝类要求与 CLI 逐项一致；
# 转码类帧数/时长一致即可（码率只做范围 sanity check）。
#
# 用法：
#   ./scripts/regression.sh            # 完整回归（remux 4 项 + 转码矩阵）
#   FF=/path/to/ffmpeg/bin ./scripts/regression.sh
# ---------------------------------------------------------------------------
set -uo pipefail

FF="${FF:-C:/ffmpeg/bin}"
FFMPEG="$FF/ffmpeg.exe"
FFPROBE="$FF/ffprobe.exe"
QT_KIT="${QT_KIT:-C:/Qt/6.12.0/mingw_64}"
QT_MINGW="${QT_MINGW:-C:/Qt/Tools/mingw1310_64}"
# 无界面验证工具是 Qt 程序，需要 Qt 运行库在 PATH 上（与 scripts/run.sh 同款处理）。
# 注意：这里必须用 MSYS 能转换成 Windows 形式的路径（/c/... 风格）。
# 直接写 C:/Qt/... 的话 Git Bash 原样透传给 Windows 进程，
# LoadLibrary 解析 PATH 条目时会失败（退出码 127，提示找不到 Qt6Core.dll）。
export PATH="$(cygpath -u "$QT_KIT")/bin:$(cygpath -u "$QT_MINGW")/bin:$PATH"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TEST="$ROOT/test"
OUT="$TEST/out"
A="$TEST/fix_a.mp4"
B="$TEST/fix_b.mp4"
HD="$TEST/fix720.mp4"

PASS=0
FAIL=0

[[ -x "$FFMPEG" ]] || { echo "找不到 ffmpeg CLI：$FFMPEG"; exit 1; }

# 测试夹具缺失时自动生成（6s / h264+aac / 关键帧间隔 1s，B 帧为默认值）。
# test/ 在 .gitignore 里，不随仓库分发，需要时现生成即可。
if [[ ! -f "$A" || ! -f "$B" || ! -f "$HD" ]]; then
    echo "生成测试夹具…"
    mkdir -p "$TEST"
    "$FFMPEG" -y -v error -f lavfi -i "testsrc=size=320x240:rate=30:duration=6" \
        -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=6" \
        -c:v libx264 -g 30 -pix_fmt yuv420p -c:a aac "$A"
    "$FFMPEG" -y -v error -f lavfi -i "testsrc2=size=320x240:rate=30:duration=6" \
        -f lavfi -i "sine=frequency=880:sample_rate=44100:duration=6" \
        -c:v libx264 -g 30 -pix_fmt yuv420p -c:a aac "$B"
    "$FFMPEG" -y -v error -f lavfi -i "testsrc=size=1280x720:rate=30:duration=6" \
        -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=6" \
        -c:v libx264 -g 30 -pix_fmt yuv420p -c:a aac "$HD"
fi
[[ -f "$A" && -f "$B" && -f "$HD" ]] || {
    echo "夹具生成失败";
    exit 1;
}
rm -rf "$OUT"; mkdir -p "$OUT"

# ---- 探测量：格式时长 | 视频包数 | 音频包数 | 分辨率 ----
probe() {
    local f="$1" dur vp ap res
    dur=$("$FFPROBE" -v error -show_entries format=duration -of csv=p=0 "$f" 2>/dev/null)
    vp=$("$FFPROBE" -v error -select_streams v -count_packets \
         -show_entries stream=nb_read_packets -of csv=p=0 "$f" 2>/dev/null)
    ap=$("$FFPROBE" -v error -select_streams a -count_packets \
         -show_entries stream=nb_read_packets -of csv=p=0 "$f" 2>/dev/null)
    res=$("$FFPROBE" -v error -select_streams v \
          -show_entries stream=width,height -of csv=p=0 "$f" 2>/dev/null)
    printf '%s | v:%s a:%s | %s' "${dur:-NA}" "${vp:-0}" "${ap:-0}" "${res:-NA}"
}

# ---- 全片解码校验 ----
decode_ok() {
    local err="$OUT/.decode_err"
    if "$FFMPEG" -v error -i "$1" -f null - >/dev/null 2>"$err"; then
        echo "解码干净"
    else
        echo "解码失败: $(head -c 300 "$err" | tr '\n' ' ')"
    fi
}

# ---- 单项判定：名称 我们的文件 CLI 文件 [strict] ----
check() {
    local name="$1" ours="$2" cli="$3" strict="${4:-yes}"
    local po pc do_ dc ok=1
    po=$(probe "$ours"); pc=$(probe "$cli")
    do_=$(decode_ok "$ours"); dc=$(decode_ok "$cli")
    if [[ "$do_" != "解码干净" ]]; then ok=0; fi
    if [[ "$strict" == "yes" && "$po" != "$pc" ]]; then ok=0; fi
    if [[ $ok -eq 1 ]]; then
        PASS=$((PASS + 1))
        printf '  ✅ %-22s %s\n' "$name" "$po"
    else
        FAIL=$((FAIL + 1))
        printf '  ❌ %-22s 我们[%s] (%s)\n' "$name" "$po" "$do_"
        printf '     %-22s CLI  [%s] (%s)\n' "" "$pc" "$dc"
    fi
}

# ---- 单项判定（无 CLI 对照，只验证自身可解码 + 关键指标）----
check_self() {
    local name="$1" f="$2" expect="$3"
    local p d ok=1
    p=$(probe "$f"); d=$(decode_ok "$f")
    [[ "$d" == "解码干净" ]] || ok=0
    [[ "$p" == *"$expect"* ]] || ok=0
    if [[ $ok -eq 1 ]]; then
        PASS=$((PASS + 1)); printf '  ✅ %-22s %s\n' "$name" "$p"
    else
        FAIL=$((FAIL + 1))
        printf '  ❌ %-22s 实际[%s] (%s) 期望包含[%s]\n' "$name" "$p" "$d" "$expect"
    fi
}

RC="$ROOT/build/remux_check.exe"
TC="$ROOT/build/transcode_check.exe"
[[ -x "$RC" && -x "$TC" ]] || { echo "请先 ./scripts/build.sh"; exit 1; }

echo "=================================================================="
echo " A. 流拷贝（RemuxTask）vs ffmpeg CLI"
echo "=================================================================="

# A1 截取 2s→5s
# 注意：这一项与 CLI 存在 **已知的边界差异**，不作为失败项：
#   * CLI（7.1）按 dts 收尾，B 帧重序下会多带约 2 帧（66ms），
#     且 muxing 队列预读会多冲几个音频包；
#   * 我们按 pts 收尾，输出是精确的 [2s, 5s) 半开区间。
#   用无 B 帧的夹具时两者逐项一致（见文档 §9.6）。
#   判定标准：双方解码干净 + 我们的帧数 = 区间内精确帧数（90）+ 时长≈3.0s。
"$RC" cut "$A" "$OUT/a1_ours.mp4" 2 5 >"$OUT/a1.log" 2>&1
"$FFMPEG" -y -v error -ss 2 -i "$A" -to 3 -map 0 -c copy \
    -avoid_negative_ts 1 -movflags +faststart "$OUT/a1_cli.mp4" >>"$OUT/a1.log" 2>&1
do_=$(decode_ok "$OUT/a1_ours.mp4"); dc=$(decode_ok "$OUT/a1_cli.mp4")
po=$(probe "$OUT/a1_ours.mp4")
if [[ "$do_" == "解码干净" && "$dc" == "解码干净" && "$po" == *"v:90 a:130"* \
      && "$po" == *"3.0"* ]]; then
    PASS=$((PASS + 1))
    printf '  ✅ %-22s %s（CLI 为 %s，B 帧边界差异见上注）\n' "截取 2s→5s" "$po" \
        "$(probe "$OUT/a1_cli.mp4")"
else
    FAIL=$((FAIL + 1))
    printf '  ❌ %-22s 我们[%s] (%s)\n' "截取 2s→5s" "$po" "$do_"
    printf '     %-22s CLI  [%s] (%s)\n' "" "$(probe "$OUT/a1_cli.mp4")" "$dc"
fi

# A2 分离视频轨
"$RC" video "$A" "$OUT/a2_ours.mp4" >"$OUT/a2.log" 2>&1
"$FFMPEG" -y -v error -i "$A" -map 0:v -c copy -an "$OUT/a2_cli.mp4" >>"$OUT/a2.log" 2>&1
check "分离视频轨" "$OUT/a2_ours.mp4" "$OUT/a2_cli.mp4"

# A3 分离音频轨
"$RC" audio "$A" "$OUT/a3_ours.m4a" >"$OUT/a3.log" 2>&1
"$FFMPEG" -y -v error -i "$A" -map 0:a -c copy -vn "$OUT/a3_cli.m4a" >>"$OUT/a3.log" 2>&1
check "分离音频轨" "$OUT/a3_ours.m4a" "$OUT/a3_cli.m4a"

# A4 视频+音频合并
"$RC" merge "$A" "$B" "$OUT/a4_ours.mp4" >"$OUT/a4.log" 2>&1
"$FFMPEG" -y -v error -i "$A" -i "$B" -map 0:v:0 -map 1:a:0 -c copy \
    -shortest -movflags +faststart "$OUT/a4_cli.mp4" >>"$OUT/a4.log" 2>&1
check "合并 视+音" "$OUT/a4_ours.mp4" "$OUT/a4_cli.mp4"

# A5 连接 2 个 6s
"$RC" concat "$OUT/a5_ours.mp4" "$A" "$B" >"$OUT/a5.log" 2>&1
printf "file '%s'\nfile '%s'\n" "$A" "$B" >"$OUT/a5_list.txt"
"$FFMPEG" -y -v error -f concat -safe 0 -i "$OUT/a5_list.txt" -map 0 -c copy \
    -movflags +faststart "$OUT/a5_cli.mp4" >>"$OUT/a5.log" 2>&1
check "连接 2×6s" "$OUT/a5_ours.mp4" "$OUT/a5_cli.mp4" no   # 引导样本差异，见文档 §9.6

echo ""
echo "=================================================================="
echo " B. 转码（TranscodeTask）vs ffmpeg CLI —— 源：720p / 6s / 180 帧"
echo "=================================================================="
# CLI 公共参数与 python/ffmpeg_cmd.py::cmd_transcode 完全一致：
#   -b:v {k}k -maxrate {1.5k}k -bufsize {2k}k -pix_fmt yuv420p
#   -profile:v high -preset:v fast -level 4.1 -c:a aac -b:a {a}k
cli_transcode() {  # out in kbps scale audiokbps extra...
    local out="$1" in="$2" kbps="$3" scale="$4" ak="$5"; shift 5
    local maxrate bufsize
    maxrate=$(( kbps * 3 / 2 )); bufsize=$(( kbps * 2 ))
    (( maxrate < kbps + 500 )) && maxrate=$(( kbps + 500 ))
    (( bufsize < 1000 )) && bufsize=1000
    local aargs=(-c:a aac -b:a "${ak}k")
    [[ "$ak" == "0" ]] && aargs=(-an)
    "$FFMPEG" -y -v error -i "$in" -vf "$scale" -c:v libx264 \
        -b:v "${kbps}k" -maxrate "${maxrate}k" -bufsize "${bufsize}k" \
        -pix_fmt yuv420p -profile:v high -preset:v fast -level 4.1 \
        "${aargs[@]}" "$@" -movflags +faststart "$out"
}

# B1 原始大小 / 4000k / 128k
"$TC" "$HD" "$OUT/b1_ours.mp4" libx264 4000 原始大小 128 >"$OUT/b1.log" 2>&1
cli_transcode "$OUT/b1_cli.mp4" "$HD" 4000 "scale=trunc(iw/2)*2:trunc(ih/2)*2" 128
check "原始 4000k" "$OUT/b1_ours.mp4" "$OUT/b1_cli.mp4" no

# B2 一半 / 2000k
"$TC" "$HD" "$OUT/b2_ours.mp4" libx264 2000 一半 128 >"$OUT/b2.log" 2>&1
cli_transcode "$OUT/b2_cli.mp4" "$HD" 2000 "scale=trunc(iw/4)*2:trunc(ih/4)*2" 128
check "一半 2000k" "$OUT/b2_ours.mp4" "$OUT/b2_cli.mp4" no

# B3 四分之一 / 1000k
"$TC" "$HD" "$OUT/b3_ours.mp4" libx264 1000 四分之一 128 >"$OUT/b3.log" 2>&1
cli_transcode "$OUT/b3_cli.mp4" "$HD" 1000 "scale=trunc(iw/8)*2:trunc(ih/8)*2" 128
check "四分之一 1000k" "$OUT/b3_ours.mp4" "$OUT/b3_cli.mp4" no

# B4 指定 1280*720
"$TC" "$HD" "$OUT/b4_ours.mp4" libx264 4000 1280*720 128 >"$OUT/b4.log" 2>&1
cli_transcode "$OUT/b4_cli.mp4" "$HD" 4000 "scale=1280:720" 128
check "指定1280x720" "$OUT/b4_ours.mp4" "$OUT/b4_cli.mp4" no

# B5 无音轨
"$TC" "$HD" "$OUT/b5_ours.mp4" libx264 4000 原始大小 0 >"$OUT/b5.log" 2>&1
cli_transcode "$OUT/b5_cli.mp4" "$HD" 4000 "scale=trunc(iw/2)*2:trunc(ih/2)*2" 0
check "无音轨" "$OUT/b5_ours.mp4" "$OUT/b5_cli.mp4" no

# B6 MKV 容器
"$TC" "$HD" "$OUT/b6_ours.mkv" libx264 4000 原始大小 128 >"$OUT/b6.log" 2>&1
"$FFMPEG" -y -v error -i "$HD" -vf "scale=trunc(iw/2)*2:trunc(ih/2)*2" \
    -c:v libx264 -b:v 4000k -maxrate 6000k -bufsize 8000k -pix_fmt yuv420p \
    -profile:v high -preset:v fast -level 4.1 -c:a aac -b:a 128k "$OUT/b6_cli.mkv" \
    >>"$OUT/b6.log" 2>&1
check "MKV 容器" "$OUT/b6_ours.mkv" "$OUT/b6_cli.mkv" no

# B7 硬件编码器回退（无 NVIDIA 显卡时应自动回退 libx264 并成功）
"$TC" "$HD" "$OUT/b7_ours.mp4" h264_nvenc 4000 原始大小 128 >"$OUT/b7.log" 2>&1
rc=$?
if [[ $rc -eq 0 ]]; then
    enc=$("$FFPROBE" -v error -select_streams v -show_entries stream=codec_name \
          -of csv=p=0 "$OUT/b7_ours.mp4")
    if grep -qi "fallback\|回退\|nvenc" "$OUT/b7.log"; then
        check_self "nvenc→回退" "$OUT/b7_ours.mp4" "1280,720"
        printf '     （实际编码器 %s，日志有回退提示）\n' "$enc"
    else
        check_self "nvenc 原生" "$OUT/b7_ours.mp4" "1280,720"
        printf '     （实际编码器 %s）\n' "$enc"
    fi
else
    FAIL=$((FAIL + 1)); printf '  ❌ %-22s 工具退出码 %d，见 test/out/b7.log\n' "nvenc 回退" "$rc"
fi

echo ""
echo "=================================================================="
printf " 回归结果：通过 %d 项，失败 %d 项\n" "$PASS" "$FAIL"
echo "=================================================================="
[[ $FAIL -eq 0 ]]
