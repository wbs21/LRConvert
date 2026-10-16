#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 损坏文件模糊测试（稳定性改进方案 P1-7）
#
# 目标不是"解码结果是否正确"（那是 regression.sh 的事），而是三件更硬的事：
#   1. 不崩 —— 退出码不能是 STATUS_ACCESS_VIOLATION 一类，也不能写出新的 .dmp；
#   2. 不挂 —— 每个用例都有硬超时（本工程的 I/O 超时是 30 秒，这里给 90 秒，
#              真卡住(超过 OS 和网络容忍)的都应该在前 30 秒自己报错退出）；
#   3. 不乱来 —— 失败要体面地报错返回非零（或 0 表示"尽力救回来了"），
#              并且不能留下半个输出文件（P2-9 原子落盘的验收点之一）。
#
# 语料：从一份健康的测试片做 9 种变异（截断 / 位翻转 / 头部改错 / 中段填 FF /
#      插入垃圾 / 空文件 / moov 清零 / 只留头部 / 随机打乱），覆盖
#      "解封装读不到东西"、"能读头但读不动数据"、"数据结构自相矛盾"三类。
#
# 用法：
#   ./scripts/fuzz_corpus.sh                 # 跑全语料（每处的 4 个入口）
#   SEED=1234 ./scripts/fuzz_corpus.sh       # 换随机种子
#   KEEP=1 ./scripts/fuzz_corpus.sh          # 保留生成的语料（默认跑完清理）
#   ASAN_BUILD=build-asan ./scripts/fuzz_corpus.sh   # 用 ASan 构建跑（更严格）
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT_KIT="${QT_KIT:-C:/Qt/6.12.0/mingw_64}"
QT_MINGW="${QT_MINGW:-C:/Qt/Tools/mingw1310_64}"
FF="${FF:-C:/ffmpeg/bin}"
BUILD="${ASAN_BUILD:-build}"
SEED="${SEED:-20260816}"
KEEP="${KEEP:-0}"

MEDIA="$ROOT/$BUILD/media_check.exe"
REMUX="$ROOT/$BUILD/remux_check.exe"
TRANS="$ROOT/$BUILD/transcode_check.exe"
GUI="$ROOT/$BUILD/LRConvert.exe"

CORPUS="$ROOT/test/fuzz"
OUT="$CORPUS/out"
LOGS="$CORPUS/logs"
DUMPDIR="$ROOT/$BUILD/logs/crashdumps"
SRC="$ROOT/test/fix_a.mp4"

PASS=0
FAIL=0
CRASHED=()
HUNG=()

export PATH="$(cygpath -u "$QT_KIT")/bin:$(cygpath -u "$QT_MINGW")/bin:$PATH"

log() { printf '%s\n' "$*"; }

fail() { printf '  ❌ %s\n' "$*"; FAIL=$((FAIL + 1)); }
pass() { printf '  ✅ %s\n' "$*"; PASS=$((PASS + 1)); }

for exe in "$MEDIA" "$REMUX" "$TRANS" "$GUI"; do
    [[ -f "$exe" ]] || { echo "缺构造产物：$exe（先跑 ./scripts/build.sh）"; exit 1; }
done

# ---------------------------------------------------------------------------
# 语料生成（Python：比 dd / head 好控制，也跨平台）
# ---------------------------------------------------------------------------
if [[ ! -f "$SRC" ]]; then
    echo "生成健康源片：$SRC"
    mkdir -p "$(dirname "$SRC")"
    "$FF/ffmpeg.exe" -y -v error -f lavfi -i "testsrc=size=320x240:rate=30:duration=6" \
        -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=6" \
        -c:v libx264 -g 30 -pix_fmt yuv420p -c:a aac "$SRC" || {
        echo "源片生成失败（需要 ffmpeg CLI，FF=/path/to/bin）"; exit 1; }
fi

rm -rf "$CORPUS"; mkdir -p "$CORPUS" "$OUT" "$LOGS"

python - "$SRC" "$CORPUS" "$SEED" <<'PY'
import os, random, struct, sys

src, outdir, seed = sys.argv[1], sys.argv[2], int(sys.argv[3])
data = open(src, 'rb').read()
rnd = random.Random(seed)
n = len(data)

def write(name, blob):
    with open(os.path.join(outdir, name), 'wb') as f:
        f.write(blob)
    return name

cases = []

# 1) 截断：常见的"下载到一半"
for frac, tag in ((0.05, 'trunc05'), (0.4, 'trunc40'), (0.9, 'trunc90')):
    cases.append(write('truncate_%s.mp4' % tag, data[:int(n * frac)]))

# 2) 顶部/bitr 位置翻转：头部结构还在，字段值荒谬
def flip(blob, count, lo, hi):
    b = bytearray(blob)
    for _ in range(count):
        i = rnd.randrange(lo, min(hi, len(b)))
        b[i] ^= 1 << rnd.randrange(8)
    return bytes(b)
cases.append(write('flip_header.mp4', flip(data, 200, 0, 4096)))
cases.append(write('flip_body.mp4', flip(data, 2000, 4096, max(4097, n - 4096))))

# 3) 中段填成 0xFF：真实素材里"这一段媒体数据全坏"的样子
m = len(data) // 2
cases.append(write('ff_mid.mp4', data[:m] + b'\xff' * min(n // 4, 3 << 20) + data[m + min(n // 4, 3 << 20):]))

# 4) 中段插一串垃圾：容器长度与实际内容不符
cases.append(write('junk_insert.mp4', data[:m] + os.urandom(1 << 20) + data[m:]))

# 5) 空 / 极小文件：read 一下就 EOF 的极限
cases.append(write('empty.mp4', b''))
cases.append(write('tiny.mp4', data[:16]))
cases.append(write('all_zero.mp4', b'\x00' * min(n, 1 << 20)))

# 6) moov 清零：MP4 的索引表没了（ users最常遇到的"只有声音没有画面"的反面 ）
moov = data.find(b'moov')
if moov > 0:
    b = bytearray(data)
    end = min(len(b), moov + 4096)
    for i in range(moov, end):
        b[i] = 0xFF
    cases.append(write('moov_scrambled.mp4', bytes(b)))

# 7) 随机打乱后半段：解码器要面对一堆不像包的包
tail = bytearray(data[m:])
rnd.shuffle(tail)
cases.append(write('shuffled_tail.mp4', data[:m] + bytes(tail)))

print('\n'.join(cases))
PY
[[ ${PIPESTATUS[0]} -eq 0 ]] || { echo "语料生成失败"; exit 1; }

mapfile -t CASES < <(ls "$CORPUS" | grep '\.mp4$')
((${#CASES[@]} > 0)) || { echo "没有生成任何用例"; exit 1; }

# ---------------------------------------------------------------------------
# 带超时的执行器：超时 / 崩溃一律记为失败，并留下日志
#
# 退出码约定（Windows 上 Git Bash 会把 STATUS_* 映射为 128+n 或直接 255，
# 所以这里以"是否产生新 dump + 是否非零且非 1"综合判断）；
# 工具自己 return 1 表示"任务判定失败（读取失败等）"，属于**正常**，
# 崩溃/超时才算问题。
# ---------------------------------------------------------------------------
run_case() {
    local tag="$1" timeout_s="$2"; shift 2
    local rc=0 status="ok"

    "$@" >"$LOGS/$tag.log" 2>&1 &
    local pid=$!
    local waited=0
    while kill -0 "$pid" 2>/dev/null; do
        sleep 1
        waited=$((waited + 1))
        if ((waited >= timeout_s)); then
            kill -9 "$pid" 2>/dev/null
            wait "$pid" 2>/dev/null
            HUNG+=("$tag")
            fail "$tag 超时（> ${timeout_s}s）—— 挂住比崩更难受"
            return
        fi
    done
    wait "$pid"; rc=$?

    # 崩溃信号：Windows 上常见 139(SIGSEGV)/134(SIGABRT)/136(SIGFPE)，
    # 也可能是 255（STATUS_* 未被映射）
    case $rc in
        0|1) status="ok" ;;
        134|136|138|139|255) status="crash" ;;
        *) status="rc$rc" ;;
    esac

    if [[ $status == "crash" ]]; then
        CRASHED+=("$tag")
        fail "$tag 崩溃（退出码 $rc，见 $LOGS/$tag.log）"
        return
    fi
    if [[ $status != "ok" ]]; then
        fail "$tag 异常退出码 $rc（见 $LOGS/$tag.log）"
        return
    fi
    pass "$tag"
}

# 跑之前先记下已有 dump 的数量：跑完多出来的就是本次造成的
dump_count() { [[ -d "$DUMPDIR" ]] && find "$DUMPDIR" -maxdepth 1 -name '*.dmp' 2>/dev/null | wc -l || echo 0; }
DUMP_BEFORE=$(dump_count)

echo ""
echo "=================================================================="
echo " 模糊测试：${#CASES[@]} 份损坏语料 × 4 个入口（P1-7）"
echo " 语料目录：$CORPUS"
echo " 构建目录：$BUILD  随机种子：$SEED"
echo "=================================================================="

for f in "${CASES[@]}"; do
    case="$CORPUS/$f"
    name="${f%.*}"
    echo ""
    echo "── $f ──────────────────────────────────────────"

    # 1) 探测 + 取帧 + 波形（Tab1 导入的完整路径）
    run_case "media_$name" 90 "$MEDIA" "$case"

    # 2) 流拷贝截取：0~2 秒（headless 版 RemuxTask）
    run_case "remux_${name}" 90 "$REMUX" cut "$case" "$OUT/${name}_cut.mp4" 0 2

    # 3) 转码：损坏处的解码容差压力最大
    run_case "trans_${name}" 180 "$TRANS" "$case" "$OUT/${name}_enc.mp4" libx264 800 quarter 64

    # 4) GUI 冒烟：真跑一遍界面（含播放），验证"界面层不会因为坏文件失去响应"
    run_case "gui_${name}" 120 env LRCONVERT_SMOKE_TEST=1 \
        LRCONVERT_CUT_FILE="$case" "$GUI"

    # 5) 原子落盘（P2-9）：失败的任务不该把临时文件留在原地
    #    （临时文件名形如 xxx.LRConvert-tmp.mp4 —— 后缀要留给 muxer 认，
    #     见 core/PathUtil::atomicTempPath）
    leftover=$(find "$OUT" -maxdepth 1 -name '*LRConvert-tmp*' 2>/dev/null | wc -l)
    if ((leftover > 0)); then
        fail "$name 残留了 $leftover 个临时文件 .LRConvert-tmp（原子落盘没收干净）"
    else
        pass "$name 无临时文件残留"
    fi
done

DUMP_AFTER=$(dump_count)
if ((DUMP_AFTER > DUMP_BEFORE)); then
    echo ""
    fail "本轮产生了 $((DUMP_AFTER - DUMP_BEFORE)) 份新的崩溃转储（$DUMPDIR）"
fi

echo ""
echo "=================================================================="
printf " 模糊测试结果：通过 %d 项，失败 %d 项\n" "$PASS" "$FAIL"
if ((${#CRASHED[@]} > 0)); then
    printf " 崩溃用例：%s\n" "${CRASHED[*]}"
fi
if ((${#HUNG[@]} > 0)); then
    printf " 超时用例：%s\n" "${HUNG[*]}"
fi
echo "=================================================================="

if [[ "$KEEP" != "1" && $FAIL -eq 0 ]]; then
    rm -rf "$CORPUS"
    echo "（全部通过，语料已清理；想保留请 KEEP=1）"
else
    echo "语料与日志保留在：$CORPUS"
fi

[[ $FAIL -eq 0 ]]
