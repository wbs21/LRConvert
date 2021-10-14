import tkinter as tk
from tkinter import messagebox, filedialog, OptionMenu, Radiobutton, Scale, ttk
import threading
import subprocess
import webbrowser
import base64
import weixinpng
import sys, os, re, time
import requests

cmds=[]
files=[]
selfName=''

class ffmpegClass():
    def __init__(self, cmd):
        self.cmd = cmd
        self.process = None

    def ffmpegRun(self, app, filename):
        self.process = subprocess.Popen(self.cmd, shell=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding="utf-8", text=True)
        app.sysInfo.config(state='normal')
        app.sysInfo.insert('end', "开始对 "+ filename +" 进行转码 \n")
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
                # 此处可能会出现进度超过100%，未对数值进行纠正
                progress = (self.get_seconds(elapsed_time) / self.get_seconds(duration)) * 100
                app.sysInfo.insert('end', "耗时: " + elapsed_time + "\n")
                app.sysInfo.insert('end', "正在转码 " + filename +" ...... 进度:%3.2f" % progress + "%" + "\n")
                app.sysInfo.see(tk.END)
            elif result and re.search(r'frame.*', str(line)):
                app.sysInfo.insert('end', "正在转码 " + re.search(r'frame.*', str(line)).group() + "\n")
                app.sysInfo.see(tk.END)
        self.process.communicate()
        if self.process.poll() == 0:
            app.sysInfo.insert('end', filename + " success! 转码完成！ " + "\n")
            app.sysInfo.see(tk.END)
        else:
            app.sysInfo.insert('end', filename + " error! 转码出错！ " + "\n")
            app.sysInfo.see(tk.END)
        app.sysInfo.config(state='disable')

    def get_seconds(self, time):
        h = int(time[0:2])
        m = int(time[3:5])
        s = int(time[6:8])
        ms = int(time[9:12])
        ts = (h * 60 * 60) + (m * 60) + s + (ms / 1000)
        return ts

    def ffmpegThread(self, app, filename):
        self.thread = threading.Thread(target=self.ffmpegRun, args=(app, filename), daemon=True)
        self.thread.start()

    def ffmpegStop(self):
        try:
            self.process.stdin.write('q')
            self.process.stdin.flush()
            self.process.kill()
        except:
            pass

def runcode(self):
    global selfName
    selfName = self
    for i in range(len(cmds)):
        cmd = cmds[i]
        file= files[i]
        exec(f'self.n{i}=ffmpegClass(cmd)')
        exec(f'self.n{i}.ffmpegThread(self, file)')

def stopcode(self):
    for i in range(len(cmds)):
        try:
            exec(f'self.n{i}.ffmpegStop()')
        except:
            pass

def on_closing(self):
    for i in range(len(cmds)):
        try:
            exec(f'selfName.n{i}.ffmpegStop()')
        except:
            pass
    self.destroy()
    os.remove('weixin.png')
    sys.exit()

def browsePath(self, i):
    videoFile = [('视频文件', '*.mp4'), ('视频文件', '*.m4v'),  ('视频文件', '*.mkv'), ('视频文件', '*.mts'), ('视频文件', '*.avi'), ('视频文件', '*.mov'),
                        ('视频文件', '*.mpg'), ('视频文件', '*.flv'), ('视频文件', '*.dat'), ('视频文件', '*.wmv'), ('视频文件', '*.rm'), ('视频文件', '*.rmvb'), ('视频文件', '*.mpeg'), ('视频文件', '*.3gp')]
    audioFile = [('音频文件', '*aac'), ('音频文件', 'ac3'), ('音频文件', 'mp3'), ('音频文件', 'wav'), ('音频文件', 'm4a')]
    if i == 42:
        path = filedialog.askopenfilename(filetypes=audioFile)
    else:
        path = filedialog.askopenfilename(filetypes=videoFile)
    exec(f'self.file_path{i}.set(path)')

def browseDir(self):
    videoFile2 = ['mp4', ' m4v', ' mkv', 'mts', 'avi', 'mov', 'mpg', 'flv', 'dat', 'wmv', '.rm', 'mvb', 'peg', '3gp']
    self.files=[]
    path = filedialog.askdirectory()
    if path:
        self.file_path5.set(path)
        for file in os.listdir(path):
            if file[-3:] in videoFile2:
                fullname = path + "/" + file
                self.files.append(fullname)
        self.amount.set("此文件夹中共有"+str(len(self.files))+"个视频文件，批量转码耗时较长，请在空闲时间使用本功能进行转码")

def addList(self):
    videoFile = [('视频文件', '*.mp4'), ('视频文件', '*.m4v'),  ('视频文件', '*.mkv'), ('视频文件', '*.mts'), ('视频文件', '*.avi'), ('视频文件', '*.mov'),
                        ('视频文件', '*.mpg'), ('视频文件', '*.flv'), ('视频文件', '*.dat'), ('视频文件', '*.wmv'), ('视频文件', '*.rm'), ('视频文件', '*.rmvb'), ('视频文件', '*.mpeg'), ('视频文件', '*.3gp')]
    path = filedialog.askopenfilename(filetypes=videoFile)
    self.fileList.insert('end', path)

def delList(self):
    index = self.fileList.curselection()
    if (len(index) == 0):
        return
    self.fileList.delete(index)

def getIndex(self, event):
    self.fileList.index = self.fileList.nearest(event.y)

def dragIndex(self, event):
    newIndex = self.fileList.nearest(event.y)
    if newIndex < self.fileList.index:
        x = self.fileList.get(newIndex)
        self.fileList.delete(newIndex)
        self.fileList.insert(newIndex+1, x)
        self.fileList.index = newIndex
    elif newIndex > self.fileList.index:
        x = self.fileList.get(newIndex)
        self.fileList.delete(newIndex)
        self.fileList.insert(newIndex - 1, x)
        self.fileList.index = newIndex

def cutFile(self):
    cmds.clear()
    files.clear()
    filename = self.filePathEnt1.get()
    startTime = self.time1.get()
    endtime = self.time2.get()
    newName = "_new_" + str(int(time.time()))[4:]+".mp4"
    cmd = "ffmpeg", "-ss", startTime, "-t", endtime, "-i", filename, "-c", "copy", filename+newName
    cmd = ' '.join(cmd)
    cmds.append(cmd)
    files.append(filename + newName + ".mp4")
    runcode(self)

def splitFile(self):
    cmds.clear()
    files.clear()
    filename = self.filePathEnt3.get()
    newName = "_new_" + str(int(time.time()))[4:]
    cmd1 = "ffmpeg", "-i", filename, "-c:v", "copy", "-an", filename + newName + ".m4v"
    cmd1 = ' '.join(cmd1)
    cmds.append(cmd1)
    files.append(filename + newName + ".m4v")
    cmd2 = "ffmpeg", "-i", filename, "-c:a", "copy", "-vn", filename + newName + ".m4a"
    cmd2 = ' '.join(cmd2)
    cmds.append(cmd2)
    files.append(filename + newName + ".m4a")
    runcode(self)

def joinFile(self):
    cmds.clear()
    files.clear()
    filelist = self.fileList.get(0, 'end')
    filename = filelist[0]
    path = os.path.split(filename)[0]
    newName = str(int(time.time()))[4:]
    textName = path + "/" + newName + '.txt'
    with open(textName, 'w') as f:
        for line in filelist:
            f.write(r"file '"+line+"'\n")
    cmd = "ffmpeg", "-f", "concat", "-safe", "0", "-i", textName, "-c", "copy", filename + "_new_" + newName + ".mp4"
    cmd = ' '.join(cmd)
    cmds.append(cmd)
    files.append(filename+ newName + ".mp4")
    runcode(self)

def mergeFile(self):
    cmds.clear()
    files.clear()
    filename = self.filePathEnt4.get()
    filename2 = self.filePathEnt42.get()
    newName = "_new_" + str(int(time.time()))[4:]
    cmd = "ffmpeg", "-y", "-i", filename, "-i", filename2, "-c", "copy", filename + newName + ".mp4"
    cmd = ' '.join(cmd)
    # ffmpeg -y –i input.mp4 –i input.mp3 –vcodec copy –acodec copy output.mp4
    cmds.append(cmd)
    files.append(filename + newName + ".mp4")
    runcode(self)

def batCode(self):
    cmds.clear()
    files.clear()
    newName = "_new_" + str(int(time.time()))[4:]
    for i in range(len(self.files)):
        filename = self.files[i]
        if self.option.get():
            cmd = "ffmpeg", "-i", filename, "-c:v", "libx264", "-profile:v", "high", "-preset:v", "fast", "-level", "4.2", "-b:v", "8000k", "-bufsize", "2000k", "-pix_fmt", "yuv420p", "-coder", "1", "-refs", "3", "-acodec", "aac", "-ab", "128k", filename + newName + ".mp4"
            cmd = ' '.join(cmd)
        else:
            cmd = "ffmpeg", "-i", filename, "-c:v", "libx264", "-profile:v", "high", "-preset:v", "veryfast", "-level", "4.1", "-b:v", "2000k", "-bufsize", "2000k", "-pix_fmt", "yuv420p", "-coder", "1", "-refs", "3", "-acodec", "aac", "-ab", "128k", filename + newName + ".mp4"
            cmd = ' '.join(cmd)
        # ffmpeg -i 123.mp4 -c:v libx264 -profile:v high -preset:v fast -level 4.2 -b:v 8000k -bufsize 2000k -pix_fmt yuv420p -coder 1 -refs 3 -acodec aac -ab 128k output.mp4
        cmds.append(cmd)
        files.append(filename + newName + ".mp4")
    runcode(self)

def selfCode(self):
    cmds.clear()
    files.clear()
    path = self.filePathEnt6.get()
    filename = path
    newName = "_new_" + str(int(time.time()))[4:]
    profile = self.opt1.get()
    preset = self.opt2.get()
    level = self.opt3.get()
    tune = self.opt4.get()
    crf = str(self.opt5.get())
    ab = str(self.opt6.get())+"k"
    # cmd = "ffmpeg", "-i", file, "-c:v", "libx264", "-profile", "high", "-preset", "veryfast", "-level", "4.1", "-b:v", "2000k", "-bufsize", "2000k", "-pix_fmt", "yuv420p", "-coder", "1", "-refs", "3", "-c:a", "aac", "-ab", "128k", file + "_new.mp4"
    cmd = "ffmpeg", "-i", path, "-c:v", "libx264", "-profile:v", profile, "-preset:v", preset, "-level", level, "-tune", tune, "-crf", crf, "-pix_fmt", "yuv420p", "-coder", "1", "-refs", "3", "-acodec", "aac", "-ab", ab, filename + newName + ".mp4"
    cmd = ' '.join(cmd)
    cmds.append(cmd)
    files.append(filename + newName + ".mp4")
    runcode(self)

def checkVer(version, version_URL, home_URL):
    res = r"version:\s\d\.\d\.\d"
    mes = ''
    try:
        ver = requests.get(version_URL)
        html = ver.text
        versionNew = re.search(res, html).group()
        if version < versionNew:
            mes = messagebox.askokcancel(title='发现新版本', message='发现新版本,点击查看详细信息')
        if mes:
            webbrowser.open(home_URL, new=0)
    except:
        pass


class Frame1(ttk.Frame):
    def __init__(self, master, i, *args, **kwargs):
        super().__init__(master, *args, **kwargs)
        self.master=master
        # 第一个标签页，截取视频----------------------------------------------------------------------------------------------
        tk.Label(master, text="请选择视频文件", font=('Arial'), background=bgcolor, height=2).place(x=10, y=5)
        tk.Button(master, text='浏览', width=6, height=1, command=lambda: browsePath(self,1)).place(x=140, y=12)
        self.file_path1 = tk.StringVar()
        self.file_path1.set("")
        self.filePathEnt1 = tk.Entry(master, textvariable=self.file_path1, width=40, show=None)
        self.filePathEnt1.place(x=220, y=15)
        tk.Label(master, text="开始时间（格式：hh:mm:ss）", font=('Arial'), background=bgcolor,height=2).place(x=10, y=55)
        self.time1 = tk.Entry(master, show=None, width=12)
        self.time1.place(x=220, y=55)
        self.time1.insert(0, "00:00:00")
        tk.Label(master, text="结束时间：", font=('Arial'), background=bgcolor,height=2).place(x=390, y=55)
        self.time2 = tk.Entry(master, show=None, width=12)
        self.time2.place(x=460, y=55)
        self.time2.insert(0, "00:00:00")
        tk.Button(master, text='开始截取', width=8, height=1, command=lambda: cutFile(self)).place(x=620, y=35)
        tk.Button(master, text='停止编码', width=8, height=1, command=lambda: stopcode(self)).place(x=620, y=75)
        tk.Label(master, text='时间格式既支持hh:mm:ss 时：分：秒 的格式，也支持 00：00：00.000 小数点后3位为毫秒的格式。', font=(
            'Arial',14), bg='#eeeeee', height=2).place(x=10, y=150)
        self.sysText = tk.Label(master, text="转码进度信息：", font=('Arial'), background=bgcolor, height=2)
        self.sysInfo = tk.Text(master, background='#fafafa', height=22)
        self.sysText.place(x=30, y=225)
        self.sysInfo.place(x=140, y=225)
        self.sysInfo.config(state='disable')
        self.pack()

class Frame2(ttk.Frame):
    def __init__(self, master, i, *args, **kwargs):
        super().__init__(master, *args, **kwargs)
        self.master=master
        # 第二个标签页，连接视频----------------------------------------------------------------------------------------------
        tk.Label(master, text="请添加视频文件", font=('Arial'), background=bgcolor, height=2).place(x=10, y=5)
        self.fileList = tk.Listbox(master, width=50, show=None)
        self.fileList.bind("<Button-1>", lambda event:getIndex(self, event))
        self.fileList.bind("<B1-Motion>", lambda event:dragIndex(self, event))
        self.fileList.place(x=140, y=15)
        tk.Button(master, text='添加', width=8, height=1, command=lambda: addList(self)).place(x=630, y=12)
        tk.Button(master, text='删除', width=8, height=1, command=lambda: delList(self)).place(x=630, y=55)
        tk.Button(master, text='开始连接', width=8, height=1, command=lambda: joinFile(self)).place(x=630, y=115)
        tk.Button(master, text='停止编码', width=8, height=1, command=lambda: stopcode(self)).place(x=630, y=155)
        tk.Label(master, text='用于连接的视频必须使用相同编码，否则会出现播放错误。不同编码视频必须先批量转码为相同编码格式。', font=(
            'Arial',12), background=bgcolor,  height=2).place(x=10, y=190)
        self.sysText = tk.Label(master, text="转码进度信息：", font=('Arial'), background=bgcolor,  height=2)
        self.sysInfo = tk.Text(master, background='#fafafa', height=22)
        self.sysText.place(x=30, y=225)
        self.sysInfo.place(x=140, y=225)
        self.sysInfo.config(state='disable')
        self.pack()

class Frame3(ttk.Frame):
    def __init__(self, master, i, *args, **kwargs):
        super().__init__(master, *args, **kwargs)
        self.master=master
        # 第三个标签页，分割提取音视频----------------------------------------------------------------------------------------------
        tk.Label(master, text="请选择视频文件", font=('Arial'), background=bgcolor, height=2).place(x=10, y=50)
        tk.Button(master, text='浏览', width=6, height=1, command=lambda: browsePath(self, 3)).place(x=140, y=55)
        self.file_path3 = tk.StringVar()
        self.file_path3.set("")
        self.filePathEnt3 = tk.Entry(master, textvariable=self.file_path3, width=40, show=None)
        self.filePathEnt3.place(x=220, y=55)
        tk.Button(master, text='开始提取', width=8, height=1, command=lambda:splitFile(self)).place(x=630, y=35)
        tk.Button(master, text='停止编码', width=8, height=1, command=lambda:stopcode(self)).place(x=630, y=75)
        self.sysText = tk.Label(master, text="转码进度信息：", font=('Arial'), background=bgcolor, height=2)
        self.sysInfo = tk.Text(master, background='#fafafa', height=22)
        self.sysText.place(x=30, y=225)
        self.sysInfo.place(x=140, y=225)
        self.sysInfo.config(state='disable')
        self.pack()

class Frame4(ttk.Frame):
    def __init__(self, master, i, *args, **kwargs):
        super().__init__(master, *args, **kwargs)
        self.master=master
        # 第四个标签页，音视频合成----------------------------------------------------------------------------------------------
        tk.Label(master, text="请选择视频文件", font=('Arial'), background=bgcolor, height=2).place(x=10, y=5)
        tk.Button(master, text='浏览', width=6, height=1,
                  command=lambda: browsePath(self,4)).place(x=140, y=12)
        self.file_path4 = tk.StringVar()
        self.file_path4.set("")
        self.filePathEnt4 = tk.Entry(master, textvariable=self.file_path4, width=40, show=None)
        self.filePathEnt4.place(x=220, y=15)
        tk.Label(master, text="请选择音频文件", font=('Arial'), background=bgcolor, height=2).place(x=10, y=55)
        tk.Button(master, text='浏览', width=6, height=1, command=lambda: browsePath(self,42)).place(x=140, y=62)
        self.file_path42 = tk.StringVar()
        self.file_path42.set("")
        self.filePathEnt42 = tk.Entry(master, textvariable=self.file_path42, width=40, show=None)
        self.filePathEnt42.place(x=220, y=65)
        tk.Button(master, text='开始合并', width=8, height=1, command= lambda: mergeFile(self)).place(x=630, y=30)
        tk.Button(master, text='停止编码', width=8, height=1, command= lambda: mergeFile(self)).place(x=630, y=65)

        self.sysText = tk.Label(master, text="转码进度信息：", font=('Arial'), background=bgcolor, height=2)
        self.sysInfo = tk.Text(master, background='#fafafa', height=22)
        self.sysText.place(x=30, y=225)
        self.sysInfo.place(x=140, y=225)
        self.sysInfo.config(state='disable')
        self.pack()

class Frame5(ttk.Frame):
    def __init__(self, master, i, *args, **kwargs):
        super().__init__(master, *args, **kwargs)
        self.master=master
        # 第五个标签页，批量压缩视频----------------------------------------------------------------------------------------------
        tk.Label(master, text="请选择包含视频的文件夹", font=(
            'Arial'), background=bgcolor, height=2).place(x=10, y=5)
        tk.Button(master, text='浏览', width=6, height=1, command=lambda: browseDir(self)).place(x=200, y=12)
        self.file_path5 = tk.StringVar()
        self.file_path5.set("")
        self.filePathEnt5 = tk.Entry(master, textvariable=self.file_path5, width=30, show=None)
        self.filePathEnt5.place(x=280, y=15)
        tk.Button(master, text='开始压缩', width=8, height=1, command=lambda: batCode(self)).place(x=630, y=50)
        tk.Button(master, text='停止编码', width=8, height=1, command=lambda: stopcode(self)).place(x=630, y=90)
        self.amount = tk.StringVar()
        self.amount.set("待编码文件数量")
        tk.Label(master, textvariable=self.amount, font=('Arial'), background=bgcolor, height=2).place(x=10, y=120)
        tk.Label(master, text="请选择压缩选项： ", font=('Arial'), background=bgcolor, height=2).place(x=10, y=60)
        self.option = tk.IntVar()
        self.option.set(1)
        self.option1 = Radiobutton(master, text="更高质量", background=bgcolor,variable=self.option, value=1)
        self.option1.place(x=150, y=70)
        self.option2 = Radiobutton(master, text="更小体积", background=bgcolor,variable=self.option, value=0)
        self.option2.place(x=300, y=70)
        self.sysText = tk.Label(master, text="转码进度信息：", font=('Arial'), background=bgcolor, height=2)
        self.sysInfo = tk.Text(master, background='#fafafa', height=22)
        self.sysText.place(x=30, y=225)
        self.sysInfo.place(x=140, y=225)
        self.sysInfo.config(state='disable')
        self.pack()

class Frame6(ttk.Frame):
    def __init__(self, master, i, *args, **kwargs):
        super().__init__(master, *args, **kwargs)
        self.master=master
        # 第六个标签页，高级视频转码----------------------------------------------------------------------------------------------
        # "-profile:v high", "-preset:v fast", "-level 4.2", "-tune film", "-crf 18","-ab 128k",
        tk.Label(master, text="请选择视频文件", font=('Arial'), background=bgcolor, height=2).place(x=10, y=5)
        tk.Button(master, text='浏览', width=6, height=1, command=lambda: browsePath(self, 6)).place(x=140, y=12)
        self.file_path6 = tk.StringVar()
        self.file_path6.set("")
        self.filePathEnt6 = tk.Entry(master, textvariable=self.file_path6, width=50, show=None)
        self.filePathEnt6.place(x=220, y=15)
        tk.Button(master, text='开始转码', width=8, height=1, command=lambda: selfCode(self)).place(x=640, y=80)
        tk.Button(master, text='停止编码', width=8, height=1, command=lambda: stopcode(self)).place(x=640, y=120)
        tk.Label(master, text="请选择压缩选项： ", font=('Arial'), background=bgcolor, height=2).place(x=10, y=60)
        tk.Label(master, text="画面质量： ", font=('Arial', 9),background=bgcolor,).place(x=160, y=55)
        self.opt1s = ("high", "main", "extended", "baseline")
        self.opt1 = tk.StringVar(master)
        self.opt1.set("high")
        self.optionMenu1 = OptionMenu(master, self.opt1, *self.opt1s)
        self.optionMenu1.place(x=150, y=80)
        tk.Label(master, text="编码速度： ", font=('Arial', 9),background=bgcolor,).place(x=360, y=55)
        self.opt2s = ("veryfast", "fast", "medium", "slow")
        self.opt2 = tk.StringVar(master)
        self.opt2.set("veryfast")
        self.optionMenu2 = OptionMenu(master, self.opt2, *self.opt2s)
        self.optionMenu2.place(x=350, y=80)
        tk.Label(master, text="编码等级： ", font=('Arial', 9),background=bgcolor,).place(x=560, y=55)
        self.opt3s = ("4.2", "4.1", "3.1")
        self.opt3 = tk.StringVar(master)
        self.opt3.set("4.2")
        self.optionMenu3 = OptionMenu(master, self.opt3, *self.opt3s)
        self.optionMenu3.place(x=550, y=80)
        tk.Label(master, text="视频类别： ", font=('Arial', 9),background=bgcolor,).place(x=160, y=135)
        self.opt4s = ("film", "animation", "stillimage")
        self.opt4 = tk.StringVar(master)
        self.opt4.set("film")
        self.optionMenu4 = OptionMenu(master, self.opt4, *self.opt4s)
        self.optionMenu4.place(x=150, y=180)
        tk.Label(master, text="压缩等级（数字越大，体积越小)", font=('Arial', 9),background=bgcolor,).place(x=290, y=135)
        self.opt5 = Scale(master, from_=16, to=28, resolution=2, tickinterval=2, orient="horizontal", background=bgcolor, length=200,)
        self.opt5.set(22)
        self.opt5.place(x=270, y=160)
        tk.Label(master, text="音频码率k： ", font=('Arial', 9),background=bgcolor,).place(x=560, y=135)
        self.opt6 = tk.StringVar(master)
        self.opt6 = Scale(master, from_=96, to=196, resolution=32, tickinterval=32, orient="horizontal", background=bgcolor,length=150,)
        self.opt6.set("128")
        self.opt6.place(x=540, y=160)
        self.sysText = tk.Label(master, text="转码进度信息：", font=('Arial'), background=bgcolor, height=2)
        self.sysInfo = tk.Text(master, background='#fafafa', height=22)
        self.sysText.place(x=30, y=225)
        self.sysInfo.place(x=140, y=225)
        self.sysInfo.config(state='disable')
        self.pack()

class Frame7(ttk.Frame):
    def __init__(self, master, i, *args, **kwargs):
        super().__init__(master, *args, **kwargs)
        self.master=master
        # 第七个标签页，关于软件----------------------------------------------------------------------------------------------
        tk.Label(master, text="当前版本： " + version, font=('Arial'), background=bgcolor, height=2).place(x=10, y=5)
        tk.Button(master, text='点击查看版本信息', width=16, height=1,
                  command=lambda: webbrowser.open(home_URL, new=0)).place(x=250, y=12)
        self.aboutTxt = '''
        小兔子转换器 (Little Rabbit Convert) 是为了方便使用FFMPEG而设计的一个简易的FFMPEG图形界面（GUI）。\n
        Little Rabbit Convert的所有功能完全依赖于FFMPEG。因此您的系统中如果没有安装过FFMPEG,
        那么请将本压缩包中的ffmpeg.exe文件，拷贝到Little Rabbit Convert的同一文件夹下，本工具才能正常使用。\n
        Little Rabbit Convert的前四项功能，因为不重新编码，所以速度极快，而且不会带来质量损失。\n
        Little Rabbit Convert还处于最初开发版本，无可避免有很多的bug和功能缺陷，
        如果您有任何问题及建议，非常期待您的反馈。\n
        您可以点击上方的版本信息链接，在页面下方进行留言；
        也可以扫描下方的二维码，关注开发者的微信公众号“小兔子爱编程”，进行交流。
        '''
        self.weixin = tk.PhotoImage(file='weixin.png')
        self.about = tk.Text(master, width=112, height=38)
        self.about.insert('end', self.aboutTxt)
        self.about.image_create("end", image=self.weixin)
        self.about.tag_configure("center", justify='center')
        self.about.tag_add("center", 14.0, "end")
        self.about.config(state='disable')
        self.about.place(x=0, y=60)

class tabClass(ttk.Frame):
    def __init__(self, master, i, *args, **kwargs):
        super().__init__(master, *args, **kwargs)
        self.master=master
        self.pack()
        exec(f'frame{i} = Frame{i}(self, master)')

class frameClass(ttk.Frame):
    def __init__(self, master, *args, **kwargs):
        super().__init__(master, *args, **kwargs)
        self.tab = ttk.Notebook(self)
        tabName = ["tabname", "  视频截取  ", "  多视频连接  ", "  音视频分离  ", "  音视频合成  ", "  批量压缩  ", "  自定义转码  ", "  关   于  "]
        for i in range(1, 8):
            tab = tabClass(self, i)
            self.tab.add(tab, text=tabName[i])
        self.tab.pack(expand=1, fill='both')
        self.pack(expand=1, fill='both', padx=2, pady=2)

def main():
    with open('weixin.png', 'wb') as f:
        f.write(base64.b64decode(weixinpng.img))
    root = tk.Tk()
    root.geometry('800x600')
    root.title("Little Rabbit Convert")
    versionCheckThread = threading.Thread(target=checkVer, args=(version, version_URL, home_URL), daemon=True)
    versionCheckThread.start()
    frame = frameClass(root)
    root.protocol("WM_DELETE_WINDOW", lambda: on_closing(root))
    root.mainloop()

if __name__ == '__main__':
    bgcolor='#eeeeee'
    version = 'version: 1.2.0'
    version_URL = 'https://gitee.com/wbs21/lrconvert/blob/master/version.md'
    home_URL = 'https://gitee.com/wbs21/lrconvert/tree/master'
    main()