# -*- coding: utf-8 -*-

import contextlib
from PySide6.QtCore import Signal, QObject
from PySide6.QtWidgets import QApplication, QWidget, QFileDialog, QAbstractItemView, QMessageBox
from mainui import Ui_Form
import webbrowser
from concurrent.futures import ThreadPoolExecutor
import subprocess
import sys
from pathlib import Path
import re
import threading
import time
import requests

showMessage = QMessageBox.question


class ffmpegClass():
    def __init__(self, cmd, file):
        self.cmd = cmd
        self.file = file
        self.process = None

    def ffmpegRun(self):
        self.process = subprocess.Popen(self.cmd,
                                        shell=True,
                                        stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT,
                                        encoding="utf-8",
                                        text=True)
        self.infotask(f"开始对{self.file}进行转码\n")
        duration = "N/A"
        for line in self.process.stdout:
            if duration_res := re.search(r'\sDuration: (?P<duration>\S+)', line):
                duration = duration_res.groupdict()['duration']
                duration = re.sub(r',', '', duration)
            if result := re.search(r'\stime=(?P<time>\S+)', line):
                if duration != "N/A":
                    elapsed_time = result.groupdict()['time']
                    if "-" in elapsed_time:
                        elapsed_time = elapsed_time[1:]
                    progress = (self.get_seconds(elapsed_time) /
                                self.get_seconds(duration)) * 100
                    self.infotask(f"正在转码 {self.file}" + " ...... 进度:%3.2f" % progress + "%" + "\n")
                elif re.search(r'frame.*', str(line)):
                    self.infotask("正在转码 " + re.search(r'frame.*', str(line)).group() + "\n")
        self.process.communicate()
        if self.process.poll() == 0:
            self.infotask(f"{self.file}...success! ......转码完成！\n")
            return False
        else:
            self.infotask(f"{self.file}...error! ......转码出错！\n")
            return True

    def get_seconds(self, time):
        h = int(time[:2])
        m = int(time[3:5])
        s = int(time[6:8])
        ms = int(time[9:12])
        return (h * 60 * 60) + (m * 60) + s + (ms / 1000)

    def ffmpegStop(self):
        with contextlib.suppress(Exception):
            self.process.stdin.write('q')
            self.process.stdin.flush()
            self.process.kill()

    def infotask(self, info):
        infoMs.text_print.emit(info)


class MySignals(QObject):
    text_print = Signal(str)


infoMs = MySignals()
verMs = MySignals()


class mainWindow(QWidget, Ui_Form):
    def __init__(self, parent=None):
        super(mainWindow, self).__init__(parent)
        self.setupUi(self)
        self.cmds = []
        self.files = []
        self.T1add.clicked.connect(lambda: self.browsePath(1))
        self.T1time1.setText('00:00:00')
        self.T1time2.setText('00:00:00')
        self.T1start.clicked.connect(self.cutFile)
        self.T1stop.clicked.connect(self.stopcode)
        self.T2add.clicked.connect(self.addList)
        self.T2del.clicked.connect(self.delList)
        self.T2listbox.setDragDropMode(QAbstractItemView.InternalMove)
        self.T2start.clicked.connect(self.joinFile)
        self.T2stop.clicked.connect(self.stopcode)
        self.T3add.clicked.connect(lambda: self.browsePath(3))
        self.T3start.clicked.connect(self.splitFile)
        self.T3stop.clicked.connect(self.stopcode)
        self.T4start.clicked.connect(self.mergeFile)
        self.T4stop.clicked.connect(self.stopcode)
        self.T4addvideo.clicked.connect(lambda: self.browsePath(4))
        self.T4addaudio.clicked.connect(self.browsePath2)
        self.T5start.clicked.connect(self.batCode)
        self.T5stop.clicked.connect(self.stopcode)
        self.T5add.clicked.connect(self.browseDir)
        self.T5bit.addItems(['1', '2', '4', '8', '12', '16', '20', '30'])
        self.T5bit.setCurrentIndex(2)
        self.T5size.addItems(['原始大小', '一半', '四分之一', '1920*1080', '1280*720'])
        self.T5size.setCurrentIndex(0)
        self.T5hwacc.addItems(['libx264', 'nvenc', 'qsv', 'amf', 'videotoolbox'])
        self.T5hwacc.setCurrentIndex(0)
        self.T6start.clicked.connect(self.selfCode)
        self.T6stop.clicked.connect(self.stopcode)
        self.T6add.clicked.connect(lambda: self.browsePath(6))
        self.T6quality.addItems(['libx264', 'nvenc', 'qsv', 'amf', 'videotoolbox'])
        self.T6quality.setCurrentIndex(0)
        self.T6speed.addItems(['fast', 'medium', 'slow'])
        self.T6speed.setCurrentIndex(0)
        self.T6grade.addItems(['5.1', '4.2', '4.1'])
        self.T6grade.setCurrentIndex(2)
        # self.T6type.addItems(['film', 'animation', 'stillimage', 'psnr', 'ssim'])
        self.T6type.addItems(['原始大小', '一半', '四分之一', '1920*1080', '1280*720'])
        self.T6type.setCurrentIndex(0)
        self.T6level.addItems(['1', '2', '4', '8', '12', '16', '20', '30'])
        self.T6level.setCurrentIndex(2)
        self.T6audio.addItems(['96', '128', '160', '196'])
        self.T6audio.setCurrentIndex(1)
        self.T7this.setText(f'当前版本{version}')
        self.newVer(version_URL)
        self.T7about.append(aboutTxt)
        self.T7check.clicked.connect(lambda: webbrowser.open(home_URL, new=0))
        self.info = ''
        infoMs.text_print.connect(self.printToGui)
        verMs.text_print.connect(self.printToVer)

    def printToGui(self, text):
        if 'success' in str(text):
            self.info = text + self.info
        text = self.info + text
        self.infobox.clear()
        self.infobox.append(str(text))
        self.infobox.ensureCursorVisible()

    def printToVer(self, text):
        self.T7new.setText(str(text))

    def browsePath(self, i):
        path, filetype = QFileDialog.getOpenFileName(filter=videoFile)
        exec(f'self.T{i}file.setText(path)')

    def browsePath2(self):
        path, filetype = QFileDialog.getOpenFileName(filter=audioFile)
        self.T4audio.setText(path)

    def cutFile(self):
        self.cmds.clear()
        self.files.clear()
        filename = self.T1file.text()
        startTime = self.T1time1.text()
        endtime = self.T1time2.text()
        newName = f"_new_{str(int(time.time()))[4:]}"
        filetype = '.' + filename.split('.')[-1]
        # cmd = "ffmpeg", "-ss", startTime, "-to", endtime, "-accurate_seek", "-i", filename, "-c", "copy", "-avoid_negative_ts", "1", "-y", filename + newName + filetype
        cmd = "ffmpeg", "-ss", startTime, "-to", endtime, "-i", filename, "-c", "copy", "-keyint_min", "2", "-g", "1", "-y", filename + newName + filetype
        cmd = ' '.join(cmd)
        self.cmds.append(cmd)
        self.files.append(filename + newName + filetype)
        self.runcode()

    def runcode(self):
        self.names = locals()
        pool = ThreadPoolExecutor(max_workers=1)
        task_list = []
        for i in range(len(self.cmds)):
            cmd = self.cmds[i]
            file = self.files[i]
            self.names[str(i)] = ffmpegClass(cmd, file)
            task = pool.submit(self.names[str(i)].ffmpegRun)
            task_list.append(task)

            def get_result(future):
                with contextlib.suppress(Exception):
                    if future.result():
                        pool.shutdown(wait=False, cancel_futures=True)

            task.add_done_callback(get_result)

    def stopcode(self):
        for i in range(len(self.cmds)):
            with contextlib.suppress(Exception):
                self.names[str(i)].ffmpegStop()

    def on_closing(self):
        for i in range(len(self.cmds)):
            with contextlib.suppress(Exception):
                self.names[str(i)].ffmpegStop()

    def addList(self):
        path, filetype = QFileDialog.getOpenFileName(filter=videoFile)
        self.T2listbox.addItem(path)

    def delList(self):
        index = self.T2listbox.currentRow()
        self.T2listbox.takeItem(index)

    def joinFile(self):
        self.cmds.clear()
        self.files.clear()
        filelist = [self.T2listbox.item(i).text() for i in range(self.T2listbox.count())]
        filename = filelist[0]
        path = str(Path(filename).parent)
        filetype = '.' + filename.split('.')[-1]
        newName = str(int(time.time()))[4:]
        textName = f"{path}/{newName}.txt"
        with open(textName, 'w', encoding='utf-8') as f:
            for line in filelist:
                f.write(f"file '{line}" + "'\n")
        cmd = "ffmpeg", "-f", "concat", "-safe", "0", "-i", textName, "-c", "copy", f"{filename}_new_{newName}{filetype}"
        cmd = ' '.join(cmd)
        self.cmds.append(cmd)
        self.files.append(f"{filename}_new_{newName}{filetype}")
        self.runcode()

    def splitFile(self):
        self.cmds.clear()
        self.files.clear()
        filename = self.T3file.text()
        newName = f"_new_{str(int(time.time()))[4:]}"
        cmd1 = "ffmpeg", "-i", filename, "-c:v", "copy", "-an", filename + newName + "-video.mov"
        cmd1 = ' '.join(cmd1)
        self.cmds.append(cmd1)
        self.files.append(filename + newName + "-video.mov")
        cmd2 = "ffmpeg", "-i", filename, "-c:a", "copy", "-vn", filename + newName + "-audio.mov"
        cmd2 = ' '.join(cmd2)
        self.cmds.append(cmd2)
        self.files.append(filename + newName + "-audio.mov")
        self.runcode()

    def mergeFile(self):
        self.cmds.clear()
        self.files.clear()
        filename = self.T4file.text()
        filename2 = self.T4audio.text()
        newName = f"_new_{str(int(time.time()))[4:]}"
        cmd = "ffmpeg", "-y", "-i", filename, "-i", filename2, "-c", "copy", f"{filename}{newName}.mov"
        cmd = ' '.join(cmd)
        self.cmds.append(cmd)
        self.files.append(f"{filename}{newName}.mov")
        self.runcode()

    def browseDir(self):
        self.files.clear()
        if directory := QFileDialog.getExistingDirectory(None, "选取文件夹"):
            self.T5file.setText(directory)
            for file in Path(directory).iterdir():
                if file.suffix.lower() in videoFile2:
                    fullname = f"{directory}/{file.name}"
                    self.files.append(fullname)
            self.T5info.setText(f"此文件夹中共有{len(self.files)}个视频文件，批量转码耗时较长，请利用空闲时间进行转码!")

    def batCode(self):
        self.cmds.clear()
        hwacc = self.T5hwacc.currentText()
        bitrates = f'{self.T5bit.currentText()}000k'
        newName = f"_new_{str(int(time.time()))[4:]}"
        size = self.T5size.currentText()
        if size == '原始大小':
            size = 'scale=iw:ih'
        if size == '一半':
            size = 'scale=iw/2:ih/2'
        if size == '四分之一':
            size = 'scale=iw/4:ih/4'
        if size == '1920*1080':
            size = 'scale=1920:1080'
        if size == '1280*720':
            size = 'scale=1280:720'
        for i in range(len(self.files)):
            filename = self.files[i]
            if hwacc == 'libx264':
                cmd = "ffmpeg", "-i", filename, "-c:v", hwacc, "-profile:v", "high", "-preset:v", "fast", "-level", "4.1", "-b:v", bitrates, \
                    "-vf", size, "-bufsize", "2000k", "-pix_fmt", "yuv420p", "-acodec", "aac", "-ab", "128k", filename + newName + ".mp4"
            else:
                cmd = "ffmpeg", "-i", filename, "-c:v", f'h264_{hwacc}', "-profile:v", "high", "-coder", "cabac", "-preset:v", "fast", "-level", "5.1", "-b:v", bitrates, \
                    "-vf", size, "-bufsize", "2000k", "-pix_fmt", "yuv420p", "-acodec", "aac", "-ab", "128k", filename + newName + ".mp4"
            cmd = ' '.join(cmd)
            self.cmds.append(cmd)
            self.files.append(filename + newName + ".mp4")
        self.runcode()

    def selfCode(self):
        self.cmds.clear()
        self.files.clear()
        path = self.T6file.text()
        filename = path
        newName = f"_new_{str(int(time.time()))[4:]}"
        hwacc = self.T6quality.currentText()
        preset = self.T6speed.currentText()
        level = self.T6grade.currentText()
        size = self.T6type.currentText()
        bitrates = f'{self.T6level.currentText()}000k'
        ab = f"{str(self.T6audio.currentText())}k"
        if size == '原始大小':
            size = 'scale=iw:ih'
        if size == '一半':
            size = 'scale=iw/2:ih/2'
        if size == '四分之一':
            size = 'scale=iw/4:ih/4'
        if size == '1920*1080':
            size = 'scale=1920:1080'
        if size == '1280*720':
            size = 'scale=1280:720'
        if hwacc == 'libx264':
            cmd = "ffmpeg", "-i", path, "-c:v", hwacc, "-profile:v", "high", "-preset:v", preset, "-level", level, "-vf", size, "-b:v", bitrates, \
                "-bufsize", "2000k", "-pix_fmt", "yuv420p", "-acodec", "aac", "-ab", ab, filename + newName + ".mp4"
        else:
            cmd = "ffmpeg", "-i", path, "-c:v", f'h264_{hwacc}', "-profile:v", "high", "-coder", "cabac", "-preset:v", preset, "-level", "5.1", "-b:v", bitrates, \
                "-vf", size, "-bufsize", "2000k", "-pix_fmt", "yuv420p", "-acodec", "aac", "-ab", ab, filename + newName + ".mp4"
        cmd = ' '.join(cmd)
        self.cmds.append(cmd)
        self.files.append(filename + newName + ".mp4")
        self.runcode()

    def newVer(self, version_URL):

        def checkVer(version_URL):
            res = r"version:\s\d\.\d"
            with contextlib.suppress(Exception):
                ver = requests.get(version_URL)
                html = ver.text
                versionNew = re.search(res, html).group()
                verMs.text_print.emit(f'最新版本：{versionNew}')
        versionCheckThread = threading.Thread(target=checkVer, args=(version_URL,), daemon=True)
        versionCheckThread.start()

    def closeEvent(self, event):
        reply = showMessage(self, '退出', "软件将退出，是否确认?", QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
        if reply == QMessageBox.Yes:
            self.on_closing()
            event.accept()
        else:
            event.ignore()


if __name__ == '__main__':
    videoFile2 = ['.mp4', '.m4v', '.mkv', '.mts', '.avi', '.mov', '.mpg', '.flv', '.dat', '.wmv', '.rm', '.rmvb', '.mpeg', '.3gp', '.mxf']
    videoFile = '视频文件(*.mp4 *.m4v *.mkv *.mts *.avi *.mov *.mpg *.flv *.dat *.wmv *.rm *.rmvb *.mpeg *.3gp *.mxf *.m4s)'
    audioFile = '音频文件(*.aac *.wav *.mp3 *.ac3 *.m4a *.mov *.m4s)'
    aboutTxt = '''
        如果转码出错，请先查看文件名或文件夹名称中是否有空格，如果有，请删除其中空格。\n
        查看所选电脑显卡是否支持所选硬件编码器,不确定就选择libx264编码器。 \n
        Little Rabbit Convert的所有功能完全依赖于FFMPEG。如果您使用windows系统，
        并且没有安装过FFMPEG。那么请将本压缩包中的ffmpeg.exe文件，
        拷贝到Little Rabbit Convert的同一文件夹下，本工具才能正常使用。
        MacOs系统则需要使用homebrew安装ffmpeg。Mac安装ffmpeg较为复杂，可参考以下链接教程\n
        https://gitee.com/wbs21/lrconvert\n
        推荐使用显卡硬件编码器，能大幅提升转码速度，
        Windows N卡可使用nvenc编码器，
        A卡可以使用amf编码器，
        intel 核显可使用qsv编码器
        MacOs用户可使用h264_videotoolbox。\n
        '''

    version = 'version: 3.3'
    version_URL = 'https://gitee.com/wbs21/lrconvert/blob/main/version.md'
    home_URL = 'https://gitee.com/wbs21/lrconvert/'
    app = QApplication(sys.argv)
    window = mainWindow()
    window.show()
    app.exec()
