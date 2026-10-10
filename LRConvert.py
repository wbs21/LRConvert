# -*- coding: utf-8 -*-
"""Little Rabbit Convert —— FFmpeg 图形界面（GUI 层）。

所有 ffmpeg 相关的纯逻辑都放在 ffmpeg_cmd.py 里，本文件只负责：
  * 收集界面输入
  * 组装任务（FfmpegJob）
  * 把任务丢到后台线程执行，并把进度安全地反馈到界面
"""

from __future__ import annotations

import html
import re
import subprocess
import sys
import threading
import time
import webbrowser
from concurrent.futures import Future, ThreadPoolExecutor
from pathlib import Path

import requests
from PySide6.QtCore import QObject, Qt, Signal
from PySide6.QtGui import QIcon, QTextCursor
from PySide6.QtWidgets import (QAbstractItemView, QApplication, QFileDialog,
                               QMessageBox, QProgressBar, QWidget)

import ffmpeg_cmd as fc
from mainui import Ui_Form

APP_NAME = "Little Rabbit Convert"
VERSION = "3.4"
VERSION_URL = "https://gitee.com/wbs21/lrconvert/raw/main/version.md"
HOME_URL = "https://gitee.com/wbs21/lrconvert/"

MAX_LOG_BLOCKS = 800
START_BUTTONS = ("T1start", "T2start", "T3start", "T4start", "T5start", "T6start")
STOP_BUTTONS = ("T1stop", "T2stop", "T3stop", "T4stop", "T5stop", "T6stop")

ABOUT_TEXT = """\
本工具只是 FFmpeg 的一个图形界面，所有功能都由 FFmpeg 完成。

· 前四项功能（截取 / 连接 / 分离 / 合并）为流拷贝，不重新编码，
  因此速度极快，且不会带来任何画质损失。
· 批量压缩与自定义压缩会重新编码，画质与体积由码率决定。
· 文件名、路径中包含空格或中文都不会有问题。

FFmpeg 的获取方式：
  Windows —— 把 ffmpeg.exe（建议连同 ffprobe.exe）放到本程序同一目录即可；
  macOS   —— brew install ffmpeg；
  Linux   —— 使用系统包管理器安装，或设置环境变量 LRCONVERT_FFMPEG。

编码器选择：
  N 卡用 nvenc，A 卡用 amf，Intel 核显用 qsv，macOS 用 videotoolbox；
  不确定或报错时请选 libx264（通用软件编码器）。
"""


def parse_version(text: str) -> tuple[int, ...]:
    """'3.10' -> (3, 10)，用于版本号比较。"""
    parts = re.findall(r"\d+", text or "")
    return tuple(int(p) for p in parts) or (0,)


class TaskSignals(QObject):
    """工作线程 -> 界面的唯一通道（Qt 信号天然线程安全）。"""

    log = Signal(str, str)        # (文本, 颜色)
    progress = Signal(int, str)   # (百分比, 说明；百分比 < 0 表示未知)
    finished = Signal(bool)       # 整批任务是否全部成功
    version = Signal(str)


class FfmpegJob:
    """一个 ffmpeg 子进程任务。run() 必须工作线程中调用。"""

    def __init__(self, args, title, signals, *,
                 source=None, ffprobe=None, cleanup=()):
        self.args = fc.with_progress(args)
        self.title = title
        self.signals = signals
        self.source = source
        self.ffprobe = ffprobe
        self.cleanup_paths = list(cleanup)

        self.process: subprocess.Popen | None = None
        self.stop_event = threading.Event()
        self.total: float | None = None
        self.current = 0.0
        self.speed = ""
        self.frame = 0

        self._last_emit = 0.0
        self._last_percent = -1.0
        self._stderr_lines = 0

    # ---------------- 对外接口 ---------------- #

    def run(self) -> bool:
        """执行任务；成功返回 True。"""
        if self.stop_event.is_set():
            return False

        if self.ffprobe and self.source:
            self.total = fc.probe_duration(self.source, self.ffprobe)

        try:
            self.process = subprocess.Popen(
                self.args,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
                **fc.subprocess_kwargs(),
            )
        except OSError as exc:
            self._log(f"✗ 无法启动 ffmpeg：{exc}", "#c0392b")
            return False

        drain = threading.Thread(target=self._drain_stderr, daemon=True)
        drain.start()

        self._log(f"▶ 开始处理：{self.title}", "#1a6fc4")
        self.signals.progress.emit(0, f"{self.title}")

        try:
            for line in self.process.stdout:
                if self.stop_event.is_set():
                    break
                self._handle_progress_line(line)
        finally:
            returncode = self.process.wait()
            drain.join(timeout=3)
            for stream in (self.process.stdout, self.process.stdin,
                           self.process.stderr):
                try:
                    stream.close()
                except (OSError, AttributeError):
                    pass

        if self.stop_event.is_set():
            self._log(f"■ 已停止：{self.title}", "#a06000")
            return False

        if returncode == 0:
            if self.total:
                self.current = self.total
                self._emit_throttled(force=True)
            self._log(f"✓ 完成：{self.title}", "#237804")
            return True

        self._log(f"✗ 失败（退出码 {returncode}）：{self.title}", "#c0392b")
        return False

    def stop(self) -> None:
        """优雅停止：先发 q 让 ffmpeg 正常收尾，超时才强杀。"""
        self.stop_event.set()
        process = self.process
        if process is None or process.poll() is not None:
            return
        try:
            if process.stdin and not process.stdin.closed:
                process.stdin.write("q\n")
                process.stdin.flush()
        except (OSError, ValueError):
            pass
        try:
            process.wait(timeout=5)
            return
        except subprocess.TimeoutExpired:
            pass
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()

    def cleanup(self) -> None:
        """删除临时文件（如 concat 清单）。"""
        for path in self.cleanup_paths:
            try:
                Path(path).unlink()
            except OSError:
                pass

    # ---------------- 内部实现 ---------------- #

    def _log(self, text: str, color: str = "#333333") -> None:
        self.signals.log.emit(text, color)

    def _handle_progress_line(self, line: str) -> None:
        parsed = fc.parse_progress_line(line)
        if not parsed:
            return
        key, value = parsed
        if key == "out_time":
            self.current = fc.parse_timestamp(value) or self.current
        elif key == "speed":
            self.speed = value.strip()
        elif key == "frame":
            try:
                self.frame = int(value)
            except ValueError:
                pass
        elif key == "progress" and value.strip() == "end":
            if self.total:
                self.current = self.total
        self._emit_throttled()

    def _emit_throttled(self, force: bool = False) -> None:
        now = time.monotonic()
        percent = -1.0
        if self.total and self.total > 0:
            percent = min(100.0, max(0.0, self.current / self.total * 100.0))
        if not force and now - self._last_emit < 0.25:
            return
        if not force and abs(percent - self._last_percent) < 0.4:
            return
        self._last_emit = now
        self._last_percent = percent
        detail = f"{self.title} · {fc.format_timestamp(self.current)}"
        if self.total:
            detail += f" / {fc.format_timestamp(self.total)}"
        if self.speed:
            detail += f" · {self.speed}x"
        self.signals.progress.emit(int(percent) if percent >= 0 else -1, detail)

    def _drain_stderr(self) -> None:
        """读取 stderr，避免管道写满导致 ffmpeg 卡死，同时把告警显示出来。"""
        try:
            for line in self.process.stderr:
                text = line.rstrip()
                if not text:
                    continue
                self._stderr_lines += 1
                if self._stderr_lines <= 200:
                    self._log(f"   {text}", "#a06000")
        except (OSError, ValueError):
            pass


class MainWindow(QWidget, Ui_Form):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setupUi(self)
        self.setWindowTitle(f"{APP_NAME}  version {VERSION}")

        self.ffmpeg = fc.ffmpeg_path()
        self.ffprobe = fc.ffprobe_path()
        self.executor = ThreadPoolExecutor(max_workers=1,
                                           thread_name_prefix="ffmpeg")
        self.signals = TaskSignals()

        self.jobs: list[FfmpegJob] = []
        self.future: Future | None = None
        self.stop_all = threading.Event()
        self.busy = False
        self.batch_inputs: list[Path] = []

        self._build_extra_ui()
        self._init_widgets()
        self._connect_actions()
        self._connect_signals()

        self.infobox.document().setMaximumBlockCount(MAX_LOG_BLOCKS)
        self._log_ffmpeg_status()
        self.check_version()

    # ---------------- 初始化 ---------------- #

    def _build_extra_ui(self) -> None:
        """界面底部补一个进度条（不需要改动 mainui.ui）。"""
        self.progressBar = QProgressBar()
        self.progressBar.setRange(0, 100)
        self.progressBar.setValue(0)
        self.progressBar.setFormat("空闲")
        self.progressBar.setVisible(False)
        self.progressBar.setFixedHeight(20)
        self.verticalLayout.addWidget(self.progressBar)

    def _init_widgets(self) -> None:
        self.T1time1.setText("00:00:00")
        self.T1time2.setText("00:00:00")

        self.T2listbox.setDragDropMode(QAbstractItemView.InternalMove)
        self.T2listbox.setDefaultDropAction(Qt.MoveAction)
        self.T2listbox.setDragEnabled(True)
        self.T2listbox.setAcceptDrops(True)
        self.T2listbox.setDropIndicatorShown(True)
        self.T2listbox.setSelectionMode(QAbstractItemView.ExtendedSelection)

        self.T5bit.addItems(["1", "2", "4", "8", "12", "16", "20", "30"])
        self.T5bit.setCurrentIndex(2)
        self.T5size.addItems(list(fc.SCALE_FILTERS))
        self.T5size.setCurrentIndex(0)
        self.T5hwacc.addItems(list(fc.HW_ENCODERS))
        self.T5hwacc.setCurrentIndex(0)

        self.T6quality.addItems(list(fc.HW_ENCODERS))
        self.T6quality.setCurrentIndex(0)
        self.T6speed.addItems(["fast", "medium", "slow"])
        self.T6speed.setCurrentIndex(0)
        self.T6grade.addItems(["5.1", "4.2", "4.1"])
        self.T6grade.setCurrentIndex(2)
        self.T6type.addItems(list(fc.SCALE_FILTERS))
        self.T6type.setCurrentIndex(0)
        self.T6level.addItems(["1", "2", "4", "8", "12", "16", "20", "30"])
        self.T6level.setCurrentIndex(2)
        self.T6audio.addItems(["96", "128", "160", "196"])
        self.T6audio.setCurrentIndex(1)

        self.T7this.setText(f"当前版本 {VERSION}")
        self.T7about.setPlainText(ABOUT_TEXT)
        self._set_busy(False)

    def _connect_actions(self) -> None:
        self.T1add.clicked.connect(lambda: self._pick_file(self.T1file))
        self.T1start.clicked.connect(self.cut_file)
        self.T1stop.clicked.connect(self.stop_code)

        self.T2add.clicked.connect(self.add_list)
        self.T2del.clicked.connect(self.del_list)
        self.T2start.clicked.connect(self.join_file)
        self.T2stop.clicked.connect(self.stop_code)

        self.T3add.clicked.connect(lambda: self._pick_file(self.T3file))
        self.T3start.clicked.connect(self.split_file)
        self.T3stop.clicked.connect(self.stop_code)

        self.T4addvideo.clicked.connect(lambda: self._pick_file(self.T4file))
        self.T4addaudio.clicked.connect(self._pick_audio)
        self.T4start.clicked.connect(self.merge_file)
        self.T4stop.clicked.connect(self.stop_code)

        self.T5add.clicked.connect(self.browse_dir)
        self.T5start.clicked.connect(self.batch_code)
        self.T5stop.clicked.connect(self.stop_code)

        self.T6add.clicked.connect(lambda: self._pick_file(self.T6file))
        self.T6start.clicked.connect(self.custom_code)
        self.T6stop.clicked.connect(self.stop_code)

        self.T7check.clicked.connect(lambda: webbrowser.open(HOME_URL, new=0))

    def _connect_signals(self) -> None:
        self.signals.log.connect(self.append_log)
        self.signals.progress.connect(self.set_progress)
        self.signals.finished.connect(self.on_batch_finished)
        self.signals.version.connect(self.T7new.setText)

    def _log_ffmpeg_status(self) -> None:
        if self.ffmpeg:
            self.append_log(f"已找到 ffmpeg：{self.ffmpeg}", "#237804")
        else:
            self.append_log(
                "未找到 ffmpeg！请把 ffmpeg 放到本程序目录，或用环境变量 "
                "LRCONVERT_FFMPEG 指定路径。", "#c0392b")
        if not self.ffprobe:
            self.append_log("未找到 ffprobe，进度条将无法显示百分比（不影响转码）。",
                            "#a06000")

    # ---------------- 通用辅助 ---------------- #

    def _pick_file(self, line_edit) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "选择视频文件", "",
                                              fc.VIDEO_FILE_FILTER)
        if path:
            line_edit.setText(path)

    def _pick_audio(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "选择音频文件", "",
                                              fc.AUDIO_FILE_FILTER)
        if path:
            self.T4audio.setText(path)

    @staticmethod
    def _stamp() -> str:
        return time.strftime("%m%d%H%M%S")

    def _check_source(self, path: str) -> bool:
        if not path:
            QMessageBox.warning(self, "缺少输入", "请先选择一个输入文件。")
            return False
        if not Path(path).is_file():
            QMessageBox.warning(self, "文件不存在", f"找不到文件：\n{path}")
            return False
        return True

    def _make_job(self, args, source, title, **kwargs) -> FfmpegJob:
        return FfmpegJob(args, title, self.signals, source=source,
                         ffprobe=self.ffprobe, **kwargs)

    def _set_busy(self, busy: bool) -> None:
        for name in START_BUTTONS:
            getattr(self, name).setEnabled(not busy)
        for name in STOP_BUTTONS:
            getattr(self, name).setEnabled(busy)

    def _run_jobs(self, jobs) -> None:
        if not jobs:
            return
        if self.busy:
            self.append_log("已有任务在执行，请先等待完成或点击停止。", "#a06000")
            return
        if not self.ffmpeg:
            QMessageBox.critical(
                self, "缺少 ffmpeg",
                "没有找到 ffmpeg，无法执行。\n"
                "请把 ffmpeg 放到本程序目录，或设置环境变量 LRCONVERT_FFMPEG。")
            return

        self.jobs = list(jobs)
        self.stop_all.clear()
        self.busy = True
        self._set_busy(True)
        self.progressBar.setVisible(True)
        self.progressBar.setValue(0)
        self.future = self.executor.submit(self._run_all, self.jobs)

    def _run_all(self, jobs) -> bool:
        """在工作线程里串行执行所有任务。"""
        ok = True
        total = len(jobs)
        for index, job in enumerate(jobs, 1):
            if self.stop_all.is_set():
                break
            if total > 1:
                self.signals.log.emit(f"[{index}/{total}] {job.title}", "#666666")
            try:
                succeeded = job.run()
            finally:
                job.cleanup()
            if not succeeded:
                ok = False
                break
        self.signals.finished.emit(ok)
        return ok

    # ---------------- 槽函数 ---------------- #

    def append_log(self, text: str, color: str = "#333333") -> None:
        self.infobox.moveCursor(QTextCursor.End)
        self.infobox.insertHtml(
            f'<span style="color:{color}">{html.escape(text)}</span><br>')
        self.infobox.ensureCursorVisible()

    def set_progress(self, percent: int, detail: str) -> None:
        if percent < 0:
            self.progressBar.setRange(0, 0)
            self.progressBar.setFormat(detail or "处理中…")
        else:
            self.progressBar.setRange(0, 100)
            self.progressBar.setValue(percent)
            self.progressBar.setFormat(f"{detail} · %p%")

    def on_batch_finished(self, ok: bool) -> None:
        self.busy = False
        self._set_busy(False)
        self.progressBar.setVisible(False)
        self.progressBar.setFormat("空闲")
        if ok:
            self.append_log("全部任务已完成。", "#237804")
        else:
            self.append_log("任务结束（失败或已被停止）。", "#a06000")

    def stop_code(self) -> None:
        if not self.busy:
            return
        self.stop_all.set()
        self.append_log("正在停止，等待 ffmpeg 正常收尾…", "#a06000")
        for job in self.jobs:
            job.stop()

    def check_version(self) -> None:
        def worker():
            try:
                response = requests.get(VERSION_URL, timeout=6)
                response.raise_for_status()
                match = re.search(r"version:\s*([0-9]+(?:\.[0-9]+)*)",
                                  response.text)
                if not match:
                    raise ValueError("version.md 格式无法识别")
                latest = match.group(1)
                text = f"最新版本：{latest}"
                if parse_version(latest) > parse_version(VERSION):
                    text += "（有更新）"
            except Exception:
                text = "无法获取最新版本信息"
            self.signals.version.emit(text)

        threading.Thread(target=worker, daemon=True).start()

    # ---------------- Tab 1：截取 ---------------- #

    def cut_file(self) -> None:
        source = self.T1file.text().strip()
        if not self._check_source(source):
            return
        start_text = self.T1time1.text().strip()
        end_text = self.T1time2.text().strip()
        start = fc.parse_timestamp(start_text or "0")
        end = fc.parse_timestamp(end_text) if end_text else None
        if start is None or (end_text and end is None):
            QMessageBox.warning(self, "时间格式错误",
                                "时间请写成 00:00:00 或 00:10 或 90（秒）。")
            return
        if end is not None and end <= start:
            QMessageBox.warning(self, "时间范围错误", "结束时间必须晚于开始时间。")
            return

        output = fc.output_path(source, fc.copy_safe_suffix(source))
        args = fc.cmd_cut(self.ffmpeg, source, start_text, end_text, output)
        job = self._make_job(args, source, f"截取 {Path(source).name}")
        self._run_jobs([job])

    # ---------------- Tab 2：连接 ---------------- #

    def add_list(self) -> None:
        paths, _ = QFileDialog.getOpenFileNames(self, "添加视频（可多选）", "",
                                                fc.VIDEO_FILE_FILTER)
        for path in paths:
            self.T2listbox.addItem(path)

    def del_list(self) -> None:
        rows = sorted({index.row() for index in self.T2listbox.selectedIndexes()},
                      reverse=True)
        if not rows:
            row = self.T2listbox.currentRow()
            rows = [row] if row >= 0 else []
        for row in rows:
            self.T2listbox.takeItem(row)

    def join_file(self) -> None:
        files = [self.T2listbox.item(i).text()
                 for i in range(self.T2listbox.count())]
        if len(files) < 2:
            QMessageBox.warning(self, "文件不足", "请至少添加 2 个视频再连接。")
            return
        missing = [f for f in files if not Path(f).is_file()]
        if missing:
            QMessageBox.warning(self, "文件不存在", "以下文件不存在：\n"
                                + "\n".join(missing[:5]))
            return

        first = Path(files[0])
        stamp = self._stamp()
        list_path = first.with_name(f"concat_{stamp}.txt")
        fc.write_concat_list(files, list_path)
        output = fc.output_path(first, fc.copy_safe_suffix(first), stamp=stamp)
        args = fc.cmd_concat(self.ffmpeg, list_path, output)
        job = self._make_job(args, files[0], f"连接 {len(files)} 个视频",
                             cleanup=[list_path])
        self._run_jobs([job])

    # ---------------- Tab 3：分离 ---------------- #

    def split_file(self) -> None:
        source = self.T3file.text().strip()
        if not self._check_source(source):
            return
        stamp = self._stamp()
        suffix = fc.copy_safe_suffix(source)
        video_out = fc.output_path(source, suffix, tag="-video", stamp=stamp)
        audio_out = fc.output_path(source, suffix, tag="-audio", stamp=stamp)
        jobs = [
            self._make_job(fc.cmd_split_video(self.ffmpeg, source, video_out),
                           source, f"抽取视频轨 {Path(source).name}"),
            self._make_job(fc.cmd_split_audio(self.ffmpeg, source, audio_out),
                           source, f"抽取音频轨 {Path(source).name}"),
        ]
        self._run_jobs(jobs)

    # ---------------- Tab 4：合并 ---------------- #

    def merge_file(self) -> None:
        video = self.T4file.text().strip()
        audio = self.T4audio.text().strip()
        if not self._check_source(video) or not self._check_source(audio):
            return
        output = fc.output_path(video, fc.copy_safe_suffix(video))
        args = fc.cmd_merge(self.ffmpeg, video, audio, output)
        job = self._make_job(args, video, f"合并 {Path(video).name}")
        self._run_jobs([job])

    # ---------------- Tab 5：批量压缩 ---------------- #

    def browse_dir(self) -> None:
        directory = QFileDialog.getExistingDirectory(self, "选取文件夹")
        if not directory:
            return
        self.T5file.setText(directory)
        self.batch_inputs = sorted(
            path for path in Path(directory).iterdir()
            if path.is_file() and path.suffix.lower() in fc.VIDEO_EXTS
        )
        self.T5info.setText(
            f"此文件夹中共有 {len(self.batch_inputs)} 个视频文件，"
            f"批量转码耗时较长，请利用空闲时间进行转码！")

    def batch_code(self) -> None:
        if not self.batch_inputs:
            QMessageBox.warning(self, "未选择文件夹",
                                "请先点击“添加文件夹”选择视频所在目录。")
            return
        stamp = self._stamp()
        encoder = self.T5hwacc.currentText()
        bitrate = int(self.T5bit.currentText()) * 1000
        scale = fc.SCALE_FILTERS.get(self.T5size.currentText(),
                                     fc.SCALE_FILTERS["原始大小"])
        jobs = []
        for source in self.batch_inputs:
            output = fc.output_path(source, ".mp4", stamp=stamp)
            args = fc.cmd_transcode(self.ffmpeg, source, output,
                                    encoder=encoder, bitrate_kbps=bitrate,
                                    audio_kbps=128, scale=scale,
                                    preset="fast", level="4.1")
            jobs.append(self._make_job(args, source, f"压缩 {source.name}"))
        self._run_jobs(jobs)

    # ---------------- Tab 6：自定义压缩 ---------------- #

    def custom_code(self) -> None:
        source = self.T6file.text().strip()
        if not self._check_source(source):
            return
        encoder = self.T6quality.currentText()
        preset = self.T6speed.currentText()
        level = self.T6grade.currentText()
        scale = fc.SCALE_FILTERS.get(self.T6type.currentText(),
                                     fc.SCALE_FILTERS["原始大小"])
        bitrate = int(self.T6level.currentText()) * 1000
        audio_kbps = int(self.T6audio.currentText())

        output = fc.output_path(source, ".mp4")
        args = fc.cmd_transcode(self.ffmpeg, source, output,
                                encoder=encoder, bitrate_kbps=bitrate,
                                audio_kbps=audio_kbps, scale=scale,
                                preset=preset, level=level)
        job = self._make_job(args, source, f"压缩 {Path(source).name}")
        self._run_jobs([job])

    # ---------------- 退出 ---------------- #

    def closeEvent(self, event) -> None:
        if self.busy:
            reply = QMessageBox.question(
                self, "退出", "任务正在执行，确定要退出吗？",
                QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
            if reply != QMessageBox.Yes:
                event.ignore()
                return
            self.stop_all.set()
            for job in self.jobs:
                job.stop()
        self.executor.shutdown(wait=False, cancel_futures=True)
        event.accept()


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)

    icon_path = Path(__file__).resolve().parent / "favicon.ico"
    if icon_path.is_file():
        app.setWindowIcon(QIcon(str(icon_path)))

    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
