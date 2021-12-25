# -*- coding: utf-8 -*-

from PySide6.QtCore import Signal, QObject
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import QApplication, QWidget, QFileDialog, QAbstractItemView, QMessageBox
from mainui import Ui_Form
import webbrowser
from concurrent.futures import ThreadPoolExecutor
import subprocess
import sys
import os
import re
import threading
import time
import requests
import base64
import weixinpng

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
        self.infotask("开始对 " + self.file + " 进行转码 \n")
        duration = "N/A"
        for line in self.process.stdout:
            duration_res = re.search(r'\sDuration: (?P<duration>\S+)', line)
            if duration_res:
                duration = duration_res.groupdict()['duration']
                duration = re.sub(r',', '', duration)
            result = re.search(r'\stime=(?P<time>\S+)', line)
            if result and duration != "N/A":
                elapsed_time = result.groupdict()['time']
                if "-" in elapsed_time:
                    elapsed_time = elapsed_time[1:]
                progress = (self.get_seconds(elapsed_time) /
                            self.get_seconds(duration)) * 100
                # self.app.sysInfo.insert('end', "耗时: " + elapsed_time + "\n")
                self.infotask("正在转码 " + self.file + " ...... 进度:%3.2f" % progress + "%" + "\n")

            elif result and re.search(r'frame.*', str(line)):
                self.infotask("正在转码 " + re.search(r'frame.*', str(line)).group() + "\n")
        self.process.communicate()
        if self.process.poll() == 0:
            self.infotask(self.file + " success! 转码完成！ " + "\n")
            return False
        else:
            self.infotask(self.file + " error! 转码出错！ " + "\n")
            return True

    def get_seconds(self, time):
        h = int(time[0:2])
        m = int(time[3:5])
        s = int(time[6:8])
        ms = int(time[9:12])
        ts = (h * 60 * 60) + (m * 60) + s + (ms / 1000)
        return ts

    def ffmpegStop(self):
        try:
            self.process.stdin.write('q')
            self.process.stdin.flush()
            self.process.kill()
        except:
            pass

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
        self.ver = [1.3]
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
        self.T5big.setChecked(True)
        self.T6start.clicked.connect(self.selfCode)
        self.T6stop.clicked.connect(self.stopcode)
        self.T6add.clicked.connect(lambda: self.browsePath(6))
        self.T6quality.addItems(['high422', 'high', 'main', 'extended', 'baseline'])
        self.T6quality.setCurrentIndex(1)
        self.T6speed.addItems(['veryfast', 'fast', 'medium', 'slow'])
        self.T6speed.setCurrentIndex(1)
        self.T6grade.addItems(['5.1', '4.2', '4.1', '3.1'])
        self.T6grade.setCurrentIndex(2)
        self.T6type.addItems(['film', 'animation', 'stillimage'])
        self.T6type.setCurrentIndex(0)
        self.T6level.addItems(['16', '18', '20', '22', '24', '26', '28'])
        self.T6level.setCurrentIndex(3)
        self.T6audio.addItems(['96', '128', '160', '196'])
        self.T6audio.setCurrentIndex(1)
        self.T7this.setText('当前版本：' + version)
        self.newVer(version_URL)
        self.T7about.append(aboutTxt)
        self.T7check.clicked.connect(lambda: webbrowser.open(home_URL, new=0))

        pix = QPixmap()
        pix.loadFromData(pngData)
        self.T7weixin.setPixmap(pix)

        infoMs.text_print.connect(self.printToGui)
        verMs.text_print.connect(self.printToVer)

    def printToGui(self, text):
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
        newName = "_new_" + str(int(time.time()))[4:]
        filetype = '.' + filename.split('.')[-1]
        cmd = "ffmpeg", "-ss", startTime, "-to", endtime, "-accurate_seek", "-i", filename, "-c", "copy", \
              "-avoid_negative_ts", "1", "-y", filename + newName + filetype
        # cmd = f"ffmpeg -ss {startTime} -to {endtime} -accurate_seek -i {filename} -c copy -avoid_negative_ts 1 -y {filename} {newName} {filetype}"
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
            # task = pool.submit(self.names[str(i)].infotask)
            task_list.append(task)

            def get_result(future):
                try:
                    if future.result():
                        pool.shutdown(wait=False, cancel_futures=True)
                except:
                    pass

            task.add_done_callback(get_result)

    def stopcode(self):
        for i in range(len(self.cmds)):
            try:
                self.names[str(i)].ffmpegStop()
            except:
                pass

    def on_closing(self):
        for i in range(len(self.cmds)):
            try:
                self.names[str(i)].ffmpegStop()
            except:
                pass

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
        path = os.path.split(filename)[0]
        filetype = '.' + filename.split('.')[-1]
        newName = str(int(time.time()))[4:]
        textName = path + "/" + newName + '.txt'
        with open(textName, 'w') as f:
            for line in filelist:
                f.write(r"file '" + line + "'\n")
        cmd = "ffmpeg", "-f", "concat", "-safe", "0", "-i", textName, "-c", "copy", filename + "_new_" + newName + filetype
        cmd = ' '.join(cmd)
        self.cmds.append(cmd)
        self.files.append(filename + newName + filetype)
        self.runcode()

    def splitFile(self):
        self.cmds.clear()
        self.files.clear()
        filename = self.T3file.text()
        newName = "_new_" + str(int(time.time()))[4:]
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
        newName = "_new_" + str(int(time.time()))[4:]
        cmd = "ffmpeg", "-y", "-i", filename, "-i", filename2, "-c", "copy", filename + newName + ".mov"
        cmd = ' '.join(cmd)
        # ffmpeg -y –i input.mp4 –i input.mp3 –vcodec copy –acodec copy output.mp4
        self.cmds.append(cmd)
        self.files.append(filename + newName + ".mov")
        self.runcode()

    def browseDir(self):
        self.files.clear()
        directory = QFileDialog.getExistingDirectory(None, "选取文件夹")
        if directory:
            self.T5file.setText(directory)
            for file in os.listdir(directory):
                if file[-3:].lower() in videoFile2:
                    fullname = directory + "/" + file
                    self.files.append(fullname)
            self.T5info.setText("此文件夹中共有" + str(len(self.files)) + "个视频文件，批量转码耗时较长，请利用空闲时间进行转码!")

    def batCode(self):
        self.cmds.clear()
        newName = "_new_" + str(int(time.time()))[4:]
        for i in range(len(self.files)):
            filename = self.files[i]
            if self.T5big.isChecked():
                cmd = "ffmpeg", "-i", filename, "-c:v", "libx264", "-profile:v", "high", "-preset:v", "slow", "-level", "4.2", "-b:v", "8000k", "-bufsize", "2000k", "-pix_fmt", "yuv420p", "-acodec", "aac", "-ab", "128k", filename + newName + ".mp4"
                cmd = ' '.join(cmd)
            else:
                cmd = "ffmpeg", "-i", filename, "-c:v", "libx264", "-profile:v", "high", "-preset:v", "faster", "-level", "4.1", "-b:v", "2000k", "-bufsize", "2000k", "-pix_fmt", "yuv420p", "-acodec", "aac", "-ab", "128k", filename + newName + ".mp4"
                cmd = ' '.join(cmd)

            self.cmds.append(cmd)
            self.files.append(filename + newName + ".mp4")
        self.runcode()

    def selfCode(self):
        self.cmds.clear()
        self.files.clear()
        path = self.T6file.text()
        filename = path
        newName = "_new_" + str(int(time.time()))[4:]
        profile = self.T6quality.currentText()
        preset = self.T6speed.currentText()
        level = self.T6grade.currentText()
        tune = self.T6type.currentText()
        crf = str(self.T6level.currentText())
        ab = str(self.T6audio.currentText()) + "k"
        if profile == 'high422':
            cmd = "ffmpeg", "-i", path, "-c:v", "libx264", "-profile:v", profile, "-preset:v", preset, "-level", level, "-tune", tune, "-crf", crf, "-pix_fmt", "yuv422p10le", "-acodec", "aac", "-ab", ab, filename + newName + ".mp4"
        else:
            # cmd = "ffmpeg", "-i", file, "-c:v", "libx264", "-profile", "high", "-preset", "veryfast", "-level", "4.1", "-b:v", "2000k", "-bufsize", "2000k", "-pix_fmt", "yuv420p", "-coder", "1", "-refs", "3", "-c:a", "aac", "-ab", "128k", file + "_new.mp4"
            cmd = "ffmpeg", "-i", path, "-c:v", "libx264", "-profile:v", profile, "-preset:v", preset, "-level", level, "-tune", tune, "-crf", crf, "-pix_fmt", "yuv420p", "-acodec", "aac", "-ab", ab, filename + newName + ".mp4"
        cmd = ' '.join(cmd)
        self.cmds.append(cmd)
        self.files.append(filename + newName + ".mp4")
        self.runcode()

    def newVer(self, version_URL):
        self.ver.clear()

        def checkVer(version_URL):
            res = r"version:\s\d\.\d"
            mes = ''
            try:
                ver = requests.get(version_URL)
                html = ver.text
                versionNew = re.search(res, html).group()
                verMs.text_print.emit('最新版本：' + versionNew)
            except:
                pass

        versionCheckThread = threading.Thread(target=checkVer, args=(version_URL,), daemon=True)
        versionCheckThread.start()

    def closeEvent(self, event):
        reply = showMessage(self, '退出', "软件将退出，是否确认?", QMessageBox.Yes |QMessageBox.No, QMessageBox.No)

        if reply == QMessageBox.Yes:
            self.on_closing()
            event.accept()
        else:
            event.ignore()
        
if __name__ == '__main__':
    videoFile2 = ['mp4', ' m4v', ' mkv', 'mts', 'avi', 'mov', 'mpg', 'flv', 'dat', 'wmv', '.rm', 'mvb', 'peg', '3gp',
                  'mxf']
    videoFile = '视频文件(*.mp4 *.m4v *.mkv *.mts *.avi *.mov *.mpg *.flv *.dat *.wmv *.rm *.rmvb *.mpeg *.3gp *.mxf)'
    audioFile = '音频文件(*.aac *.wav *.mp3 *.ac3 *.m4a *.mov)'
    aboutTxt = '''
        小兔子转换器 (Little Rabbit Convert) 是为了方便使用FFMPEG而设计的一个简易的FFMPEG图形界面（GUI）。\n
        Little Rabbit Convert的所有功能完全依赖于FFMPEG。因此您的系统中如果没有安装过FFMPEG,
        那么请将本压缩包中的ffmpeg.exe文件，拷贝到Little Rabbit Convert的同一文件夹下，本工具才能正常使用。\n
        Little Rabbit Convert的前四项功能，因为不重新编码，所以速度极快，而且不会带来质量损失。\n
        Little Rabbit Convert还处于最初开发版本，无可避免有很多的bug和功能缺陷，
        如果您有任何问题及建议，非常期待您的反馈。\n
        您可以点击上方的版本信息链接，在页面下方进行留言；
        也可以扫描右方的二维码，关注开发者的微信公众号“小兔子爱编程”，进行交流。
        '''

    version = 'version: 2.0'
    version_URL = 'https://gitee.com/wbs21/lrconvert/blob/master/version.md'
    home_URL = 'https://gitee.com/wbs21/lrconvert/tree/master'
    app = QApplication(sys.argv)
    pngData = base64.b64decode(weixinpng.img)
    window = mainWindow()
    window.show()
    app.exec()
