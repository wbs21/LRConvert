# -*- coding: utf-8 -*-
"""LRConvert 的纯逻辑层：可执行程序定位、ffmpeg 参数构造、进度解析。

本模块刻意不 import 任何 Qt 模块，因此可以被单元测试直接引用。
GUI 层（LRConvert.py）只负责收集用户输入 + 调用这里的函数。
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

__all__ = [
    "VIDEO_EXTS",
    "VIDEO_FILE_FILTER",
    "AUDIO_FILE_FILTER",
    "SCALE_FILTERS",
    "HW_ENCODERS",
    "ffmpeg_path",
    "ffprobe_path",
    "probe_duration",
    "parse_timestamp",
    "format_timestamp",
    "parse_progress_line",
    "output_path",
    "copy_safe_suffix",
    "with_progress",
    "cmd_cut",
    "cmd_concat",
    "cmd_split_video",
    "cmd_split_audio",
    "cmd_merge",
    "cmd_transcode",
    "write_concat_list",
]

# --------------------------------------------------------------------------- #
# 常量
# --------------------------------------------------------------------------- #

VIDEO_EXTS = frozenset({
    ".mp4", ".m4v", ".mkv", ".mts", ".avi", ".mov", ".mpg", ".flv",
    ".dat", ".wmv", ".rm", ".rmvb", ".mpeg", ".3gp", ".mxf", ".m4s",
})

VIDEO_FILE_FILTER = (
    "视频文件(*.mp4 *.m4v *.mkv *.mts *.avi *.mov *.mpg *.flv *.dat *.wmv "
    "*.rm *.rmvb *.mpeg *.3gp *.mxf *.m4s)"
)
AUDIO_FILE_FILTER = "音频文件(*.aac *.wav *.mp3 *.ac3 *.m4a *.mov *.m4s)"

#: 分辨率预设 -> scale 滤镜表达式。
#: 注意：除以 2 / 4 之后必须再对齐到偶数，否则 yuv420p 编码会直接失败
#: （"width not divisible by 2"），这是原实现里最隐蔽的一个坑。
SCALE_FILTERS = {
    "原始大小": "scale=trunc(iw/2)*2:trunc(ih/2)*2",
    "一半": "scale=trunc(iw/4)*2:trunc(ih/4)*2",
    "四分之一": "scale=trunc(iw/8)*2:trunc(ih/8)*2",
    "1920*1080": "scale=1920:1080",
    "1280*720": "scale=1280:720",
}

#: 界面下拉框里的编码器名称 -> ffmpeg 实际的 -c:v 值
HW_ENCODERS = {
    "libx264": "libx264",
    "nvenc": "h264_nvenc",
    "qsv": "h264_qsv",
    "amf": "h264_amf",
    "videotoolbox": "h264_videotoolbox",
}

FFMPEG_ENV_VAR = "LRCONVERT_FFMPEG"
FFPROBE_ENV_VAR = "LRCONVERT_FFPROBE"

# --------------------------------------------------------------------------- #
# 工具函数
# --------------------------------------------------------------------------- #


def subprocess_kwargs() -> dict:
    """Windows 下隐藏 ffmpeg 弹出的黑色控制台窗口。"""
    if sys.platform == "win32":
        return {"creationflags": getattr(subprocess, "CREATE_NO_WINDOW", 0)}
    return {}


def _is_executable(path: Path) -> bool:
    if not path.is_file():
        return False
    if os.name == "nt":
        return True
    return os.access(path, os.X_OK)


def _search_dirs() -> list[Path]:
    """按优先级返回需要搜索 ffmpeg / ffprobe 的目录。"""
    candidates: list[Path] = []

    for env_var in (FFMPEG_ENV_VAR, FFPROBE_ENV_VAR):
        raw = os.environ.get(env_var)
        if raw:
            p = Path(raw)
            # 允许环境变量直接指向可执行文件本身
            candidates.append(p.parent if p.is_file() else p)

    # PyInstaller onefile 解压目录
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        candidates.append(Path(meipass))

    # 冻结后的可执行文件目录（macOS 下为 xxx.app/Contents/MacOS）
    if getattr(sys, "frozen", False):
        candidates.append(Path(sys.executable).resolve().parent)

    # 源码目录 / 当前工作目录
    candidates.append(Path(__file__).resolve().parent)
    candidates.append(Path.cwd())

    result: list[Path] = []
    seen: set[Path] = set()
    for item in candidates:
        try:
            item = item.resolve()
        except OSError:
            continue
        if item not in seen:
            seen.add(item)
            result.append(item)
    return result


def _find_tool(base: str, env_var: str) -> str | None:
    raw = os.environ.get(env_var)
    if raw and _is_executable(Path(raw)):
        return str(Path(raw))

    exe = f"{base}.exe" if sys.platform == "win32" else base
    for directory in _search_dirs():
        candidate = directory / exe
        if _is_executable(candidate):
            return str(candidate)

    return shutil.which(base)


def ffmpeg_path() -> str | None:
    """返回可用的 ffmpeg 路径；找不到返回 None。"""
    return _find_tool("ffmpeg", FFMPEG_ENV_VAR)


def ffprobe_path() -> str | None:
    """返回可用的 ffprobe 路径；找不到返回 None。"""
    return _find_tool("ffprobe", FFPROBE_ENV_VAR)


def probe_duration(path: str | Path, ffprobe: str | None = None) -> float | None:
    """用 ffprobe 读取媒体总时长（秒）。失败返回 None。"""
    ffprobe = ffprobe or ffprobe_path()
    if not ffprobe:
        return None
    try:
        completed = subprocess.run(
            [ffprobe, "-v", "error",
             "-show_entries", "format=duration",
             "-of", "default=noprint_wrappers=1:nokey=1",
             str(path)],
            capture_output=True, text=True, timeout=20,
            **subprocess_kwargs(),
        )
    except (OSError, subprocess.SubprocessError):
        return None

    for token in reversed(completed.stdout.split()):
        try:
            value = float(token)
        except ValueError:
            continue
        if value > 0:
            return value
    return None


# --------------------------------------------------------------------------- #
# 时间 / 进度解析
# --------------------------------------------------------------------------- #

_TIMESTAMP_RE = re.compile(
    r"^(?:(\d+):)?([0-5]?\d):([0-5]?\d)(?:[.,](\d{1,6}))?$"
)
_PROGRESS_RE = re.compile(r"^\s*([a-z_][a-z0-9_]*)=(.*?)\s*$", re.IGNORECASE)


def parse_timestamp(text: str) -> float | None:
    """把 ``HH:MM:SS.mmm`` / ``MM:SS`` / ``SS`` / 纯秒数解析为秒。

    原实现用固定下标切片（``time[3:5]``），遇到 ``0:05.50`` 或 ``N/A``
    就会抛异常，这里改用正则，兼容各种写法。
    """
    if text is None:
        return None
    token = str(text).strip().lstrip("-").strip()
    if not token:
        return None
    try:
        value = float(token)          # 纯秒数
    except ValueError:
        pass
    else:
        return value if value >= 0 else None

    match = _TIMESTAMP_RE.match(token)
    if not match:
        return None
    hours = int(match.group(1) or 0)
    minutes = int(match.group(2))
    seconds = int(match.group(3))
    fraction = float("0." + match.group(4)) if match.group(4) else 0.0
    return hours * 3600 + minutes * 60 + seconds + fraction


def format_timestamp(value: float | None) -> str:
    """秒 -> ``HH:MM:SS``。"""
    if value is None or value < 0:
        return "--:--:--"
    value = int(value)
    return f"{value // 3600:02d}:{value % 3600 // 60:02d}:{value % 60:02d}"


def parse_progress_line(line: str) -> tuple[str, str] | None:
    """解析 ``-progress pipe:1`` 输出的一行 ``key=value``。"""
    match = _PROGRESS_RE.match(line or "")
    if not match:
        return None
    return match.group(1), match.group(2)


def with_progress(args: list[str]) -> list[str]:
    """给命令追加 GUI 需要的开关。

    * ``-hide_banner`` / ``-loglevel warning``：让 stderr 只留下真正有用的
      告警和错误，不再刷屏；
    * ``-progress pipe:1``：把进度以 ``key=value`` 形式写到 stdout，比解析
      stderr 里被 ``\\r`` 反复覆盖的进度条可靠得多；
    * ``-nostats``：关掉 stderr 上的进度条。
    """
    program, *rest = args
    return [program, "-hide_banner", "-loglevel", "warning", *rest,
            "-progress", "pipe:1", "-nostats"]


# --------------------------------------------------------------------------- #
# 输出路径
# --------------------------------------------------------------------------- #


def _unique(path: Path) -> Path:
    """若文件已存在，追加 ``(1)``、``(2)``... 避免静默覆盖。"""
    if not path.exists():
        return path
    for index in range(1, 1000):
        candidate = path.with_name(f"{path.stem}({index}){path.suffix}")
        if not candidate.exists():
            return candidate
    return path


def output_path(
    source: str | Path,
    suffix: str,
    tag: str = "",
    stamp: str | None = None,
) -> Path:
    """根据源文件生成输出路径：``原名[tag]_new_时间戳.后缀``。

    原实现是 ``a.mp4 + _new_123 + .mp4`` => ``a.mp4_new_123.mp4``，
    这里改成基于 stem，得到 ``a_new_123.mp4``。
    """
    source = Path(source)
    stamp = stamp or time.strftime("%m%d%H%M%S")
    if suffix and not suffix.startswith("."):
        suffix = "." + suffix
    name = f"{source.stem}{tag}_new_{stamp}{suffix}"
    return _unique(source.with_name(name))


def copy_safe_suffix(source: str | Path) -> str:
    """流拷贝类操作时使用的后缀：沿用原封装，B 站 m4s 分片改成 mp4。"""
    suffix = Path(source).suffix
    if suffix.lower() in {".m4s", ""}:
        return ".mp4"
    return suffix


# --------------------------------------------------------------------------- #
# 命令构造（全部返回 list[str]，配合 shell=False 使用）
# --------------------------------------------------------------------------- #


def cmd_cut(ffmpeg: str, source: str | Path, start: str, end: str,
            output: str | Path) -> list[str]:
    """截取片段（流拷贝，无损）。"""
    args = [ffmpeg, "-y"]
    start_seconds = parse_timestamp(start or "0")
    end_seconds = parse_timestamp(end)

    # -ss 放输入侧（快），-to 放输出侧（兼容老版本 ffmpeg）
    if start_seconds:
        args += ["-ss", f"{start_seconds:.3f}"]
    args += ["-i", str(source)]
    if end_seconds is not None and end_seconds > (start_seconds or 0):
        args += ["-to", f"{end_seconds - (start_seconds or 0):.3f}"]

    # -map 0 保留全部轨道（字幕等）；-avoid_negative_ts 修正拷贝后的负时间戳
    args += ["-map", "0", "-c", "copy", "-avoid_negative_ts", "1",
             "-movflags", "+faststart", str(output)]
    return args


def write_concat_list(files: list[str], list_path: str | Path) -> Path:
    """写 ffmpeg concat 清单文件（对单引号做转义）。"""
    list_path = Path(list_path)
    lines = []
    for item in files:
        escaped = str(item).replace("'", "'\\''")
        lines.append(f"file '{escaped}'\n")
    list_path.write_text("".join(lines), encoding="utf-8")
    return list_path


def cmd_concat(ffmpeg: str, list_path: str | Path, output: str | Path) -> list[str]:
    """按清单拼接多个视频（流拷贝）。"""
    return [ffmpeg, "-y", "-f", "concat", "-safe", "0",
            "-i", str(list_path), "-map", "0", "-c", "copy",
            "-movflags", "+faststart", str(output)]


def cmd_split_video(ffmpeg: str, source: str | Path, output: str | Path) -> list[str]:
    """抽取视频轨（去掉音频）。"""
    return [ffmpeg, "-y", "-i", str(source),
            "-map", "0:v", "-c", "copy", "-an", str(output)]


def cmd_split_audio(ffmpeg: str, source: str | Path, output: str | Path) -> list[str]:
    """抽取音频轨（去掉视频）。"""
    return [ffmpeg, "-y", "-i", str(source),
            "-map", "0:a", "-c", "copy", "-vn", str(output)]


def cmd_merge(ffmpeg: str, video: str | Path, audio: str | Path,
              output: str | Path) -> list[str]:
    """视频 + 音频合并（流拷贝）。"""
    return [ffmpeg, "-y", "-i", str(video), "-i", str(audio),
            "-map", "0:v:0", "-map", "1:a:0", "-c", "copy", "-shortest",
            "-movflags", "+faststart", str(output)]


def cmd_transcode(
    ffmpeg: str,
    source: str | Path,
    output: str | Path,
    *,
    encoder: str = "libx264",
    bitrate_kbps: int = 4000,
    audio_kbps: int = 128,
    scale: str = SCALE_FILTERS["原始大小"],
    preset: str = "fast",
    level: str = "4.1",
) -> list[str]:
    """重新编码（批量压缩 / 自定义压缩共用）。"""
    codec = HW_ENCODERS.get(encoder, encoder)
    # bufsize 原来写死 2000k，高码率下会把码率压死、画质很差，
    # 这里改成随目标码率联动，同时补上 maxrate。
    bufsize = max(bitrate_kbps * 2, 1000)
    maxrate = max(int(bitrate_kbps * 1.5), bitrate_kbps + 500)

    args = [
        ffmpeg, "-y", "-i", str(source),
        "-vf", scale,
        "-c:v", codec,
        "-b:v", f"{bitrate_kbps}k",
        "-maxrate", f"{maxrate}k",
        "-bufsize", f"{bufsize}k",
        "-pix_fmt", "yuv420p",
    ]
    # 硬件编码器不认 -coder 之类的 x264 私有选项，这里只保留通用项；
    # videotoolbox 连 -level 都不接受，直接跳过。
    if codec != "h264_videotoolbox":
        args += ["-profile:v", "high", "-preset:v", preset, "-level", level]

    args += ["-c:a", "aac", "-b:a", f"{audio_kbps}k",
             "-movflags", "+faststart", str(output)]
    return args
