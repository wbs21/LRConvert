# -*- coding:utf-8 -*-
'''
MIT License

Copyright (c) 2021] [Little Rubbit Software]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
'''
import tkinter as tk
from tkinter import OptionMenu, Radiobutton, ttk
from tkinter import messagebox
from tkinter import filedialog
import subprocess
import sys
import os
import re
import threading
import time
import webbrowser
import requests
import base64
import weixin_png



version = 'version: 1.0.0'
version_URL = 'https://gitee.com/wbs21/lrconvert/blob/master/version.md'
home_URL='https://gitee.com/wbs21/lrconvert/tree/master'
files = []
videoFile = ['mp4', 'm4v', 'mkv', 'mts', 'avi', 'mov',
             'mpg', 'flv', 'dat', 'wmv', '.rm', 'mvb', 'peg', '3gp']
audioFile = ['aac', 'ac3', 'mp3', 'wav', 'm4a']
sysExit = False

root = tk.Tk()
root.geometry('800x600')
root.title("Little Rubbit Convert")


with open('weixin.png', 'wb') as f:
    f.write(base64.b64decode(weixin_png.img))

file_path = tk.StringVar()
file_path.set("")
file_path2 = tk.StringVar()
file_path2.set("")

tab = ttk.Notebook(root)
frame1 = tk.Frame(tab)
tab1 = tab.add(frame1, text="  视频截取  ")
frame2 = tk.Frame(tab)
tab2 = tab.add(frame2, text="  多视频连接  ")
frame3 = tk.Frame(tab)
tab3 = tab.add(frame3, text="  音视频分离  ")
frame4 = tk.Frame(tab)
tab4 = tab.add(frame4, text="  音视频合成  ")
frame5 = tk.Frame(tab)
tab5 = tab.add(frame5, text="  批量压缩  ")
frame6 = tk.Frame(tab)
tab6 = tab.add(frame6, text="  自定义转码  ")
frame7 = tk.Frame(tab)
tab7 = tab.add(frame7, text="  关   于  ")
tab.pack(padx=5, pady=5, expand=True, fill=tk.BOTH)

sysText = tk.Label(root, text="转码进度信息：", font=('Arial'),  height=2)
sysText.place(x=10, y=250)
sysInfo = tk.Text(root)
sysInfo.place(x=140, y=265)


def sysInfoForget(event):
    if event.widget.index("current") == 6:
        sysText.place_forget()
        sysInfo.place_forget()
    else:
       sysText.place(x=10, y=250)
       sysInfo.place(x=140, y=265)


tab.bind('<ButtonRelease-1>', sysInfoForget)


def versionCheck():
    res = r"version:\s\d\.\d\.\d"
    try:
        ver = requests.get(version_URL)
        html = ver.text
        versionNew = re.search(res,html).group()
        if version != versionNew:
            mes = messagebox.askokcancel(title='发现新版本', message='点击查看详细信息')
        if mes:
            webbrowser.open(version_URL, new=0)
    except:
        pass

versionCheckThread = threading.Thread(target=versionCheck, daemon=True)
versionCheckThread.start()


def get_seconds(time):
    h = int(time[0:2])
    m = int(time[3:5])
    s = int(time[6:8])
    ms = int(time[9:12])
    ts = (h * 60 * 60) + (m * 60) + s + (ms / 1000)
    return ts


def browsePath(i):
    path = filedialog.askopenfilename()
    if path[-3:].lower() in videoFile:
        if i == 1:
            file_path1.set(path)
        elif i == 3:
            file_path3.set(path)
        elif i == 4:
            file_path4.set(path)
        elif i == 6:
            file_path6.set(path)
    else:
        messagebox.showinfo(title='error', message="请选择一个视频文件！")


def browsePath2():
    path = filedialog.askopenfilename()
    if path[-3:].lower() in audioFile:
        file_path42.set(path)
    else:
        messagebox.showinfo(title='error', message="请选择一个音频文件！")


def browseDir():
    files.clear()
    path = filedialog.askdirectory()
    file_path5.set(path)
    for file in os.listdir(path):
        if file[-3:] in videoFile:
            fullname = path + "/" + file
            files.append(fullname)
    amount.set("此文件夹中共有"+str(len(files))+"个视频文件，批量转码耗时较长，请在空闲时间使用本功能进行转码")


def addList():
    path = filedialog.askopenfilename()
    if path[-3:].lower() in videoFile:
        fileList.insert('end', path)
    else:
        messagebox.showinfo(title='error', message="请选择一个视频文件！")


def delList():
    index = fileList.curselection()
    if (len(index) == 0):
        return
    fileList.delete(index)


def getIndex(event):
    fileList.index = fileList.nearest(event.y)


def dragIndex(event):
    newIndex = fileList.nearest(event.y)
    if newIndex < fileList.index:
        x = fileList.get(newIndex)
        fileList.delete(newIndex)
        fileList.insert(newIndex+1, x)
        fileList.index = newIndex
    elif newIndex > fileList.index:
        x = fileList.get(newIndex)
        fileList.delete(newIndex)
        fileList.insert(newIndex - 1, x)
        fileList.index = newIndex


def ffmpegCmd(cmd, filename):
    global process
    # process = subprocess.Popen(cmd, shell=False, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,encoding="utf-8",text=True)
    process = subprocess.Popen(cmd, shell=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding="utf-8",
                           text = True)
    # root.protocol("WM_DELETE_WINDOW", on_closing)
    sysInfo.insert('end', "开始对 "+filename+" 进行转码 \n")
    for line in process.stdout:
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
            progress = (get_seconds(elapsed_time) / get_seconds(duration)) * 100
            sysInfo.insert('end', "耗时: " + elapsed_time +"\n" )
            sysInfo.insert('end', "正在转码 " + filename+" ...... 进度:%3.2f" % progress + "%" +"\n")
            sysInfo.see(tk.END)
        elif result and re.search(r'frame.*', str(line)):
            sysInfo.insert('end', "正在转码 " + re.search(r'frame.*', str(line)).group() +"\n")
            sysInfo.see(tk.END)
    
    process.wait()
    if process.poll() == 0:
        sysInfo.insert('end', filename+" success! 转码完成！ "+"\n")
    else:
        sysInfo.insert('end', filename+" error! 转码出错！ "+"\n")
    sysInfo.see(tk.END)


def cutFile():
    path = filePathEnt1.get()
    startTime = time1.get()
    endtime = time2.get()
    filename = path
    newName = "_new_" + str(int(time.time()))[4:]+".mp4"
    cmd = "ffmpeg", "-ss", startTime, "-t", endtime, "-i", path, "-c", "copy", path+newName
    timer = threading.Thread(target=ffmpegCmd, args=(cmd, filename), daemon=True)
    timer.start()


def splitFile():
    path = filePathEnt3.get()
    filename = path
    newName = "_new_" + str(int(time.time()))[4:]
    cmd1 = "ffmpeg", "-i", path, "-c:v", "copy", "-an", path + newName + ".m4v"
    timer1 = threading.Thread(target=ffmpegCmd, args=(cmd1, filename), daemon=True)
    timer1.start()
    cmd2 = "ffmpeg", "-i", path, "-c:a", "copy", "-vn", path + newName + ".m4a"
    timer2 = threading.Thread(target=ffmpegCmd, args=(cmd2, filename), daemon=True)
    timer2.start()


def joinFile():
    filelist = fileList.get(0, 'end')
    filename = filelist[0]
    path = os.path.split(filename)[0]
    newName = str(int(time.time()))[4:]
    textName = path + "/" + newName + '.txt'
    with open(textName, 'w') as f:
        for line in filelist:
            f.write(r"file '"+line+"'\n")
    cmd = "ffmpeg", "-f", "concat", "-safe", "0", "-i", textName, "-c", "copy", filename + "_new_" + newName + ".mp4"
    timer = threading.Thread(target=ffmpegCmd, args=(cmd, filename), daemon=True)
    timer.start()


def mergeFile():
    path = filePathEnt4.get()
    path2 = filePathEnt42.get()
    filename = path+path2
    newName = "_new_" + str(int(time.time()))[4:]
    cmd = "ffmpeg", "-y", "-i", path, "-i", path2, "-c", "copy", path + newName + ".mp4"
    # ffmpeg -y –i input.mp4 –i input.mp3 –vcodec copy –acodec copy output.mp4
    timer = threading.Thread(target=ffmpegCmd, args=(cmd, filename), daemon=True)
    timer.start()


def batCode():
    newName = "_new_" + str(int(time.time()))[4:]
    for file in files:
        filename = file
        if option.get():
            cmd = "ffmpeg", "-i", file, "-c:v", "libx264", "-profile:v", "high", "-preset:v", "fast", "-level", "4.2", "-b:v", "8000k", "-bufsize", "2000k", "-pix_fmt", "yuv420p", "-coder", "1", "-refs", "3", "-acodec", "aac", "-ab", "128k", file + newName+ ".mp4"
        else:
            cmd = "ffmpeg", "-i", file, "-c:v", "libx264", "-profile:v", "high", "-preset:v", "veryfast", "-level", "4.1", "-b:v", "2000k", "-bufsize", "2000k", "-pix_fmt", "yuv420p", "-coder", "1", "-refs", "3", "-acodec", "aac", "-ab", "128k", file + newName+ ".mp4"
        # ffmpeg -i 123.mp4 -c:v libx264 -profile:v high -preset:v fast -level 4.2 -b:v 8000k -bufsize 2000k -pix_fmt yuv420p -coder 1 -refs 3 -acodec aac -ab 128k output.mp4
        timer = threading.Thread(target=ffmpegCmd, args=(cmd, filename), daemon=True)
        timer.start()


def selfCode():
    path = filePathEnt6.get()
    filename = path
    newName = "_new_" + str(int(time.time()))[4:]
    profile = opt1.get()
    preset = opt2.get()
    level = opt3.get()
    tune = opt4.get()
    crf = opt5.get()
    ab = opt6.get()
    # cmd = "ffmpeg", "-i", file, "-c:v", "libx264", "-profile", "high", "-preset", "veryfast", "-level", "4.1", "-b:v", "2000k", "-bufsize", "2000k", "-pix_fmt", "yuv420p", "-coder", "1", "-refs", "3", "-c:a", "aac", "-ab", "128k", file + "_new.mp4"
    cmd = "ffmpeg", "-i", path, "-c:v", "libx264", "-profile:v", profile, "-preset:v", preset, "-level", level, "-tune", tune, "-crf", crf, "-pix_fmt", "yuv420p", "-coder", "1", "-refs", "3", "-acodec", "aac", "-ab", ab, filename + newName + ".mp4"
    timer = threading.Thread(target=ffmpegCmd, args=(cmd, filename), daemon=True)
    timer.start()


# 第一个标签页，截取视频----------------------------------------------------------------------------------------------

tk.Label(frame1, text="请选择视频文件", font=('Arial'),  height=2).place(x=10, y=5)
tk.Button(frame1, text='浏览', width=6, height=1,
          command=lambda: browsePath(1)).place(x=140, y=12)
file_path1 = tk.StringVar()
file_path1.set("")
filePathEnt1 = tk.Entry(frame1, textvariable=file_path1, width=50, show=None)
filePathEnt1.place(x=220, y=15)
tk.Label(frame1, text="开始时间（格式：hh:mm:ss）", font=(
    'Arial'), height=2).place(x=10, y=50)
time1 = tk.Entry(frame1, show=None)
time1.place(x=230, y=60)
time1.insert(0, "00:00:00")
tk.Label(frame1, text="结束时间：", font=('Arial'), height=2).place(x=390, y=50)
time2 = tk.Entry(frame1, show=None)
time2.place(x=480, y=60)
time2.insert(0, "00:00:00")


tk.Button(frame1, text='开始截取', width=8, height=1,
          command=cutFile).place(x=680, y=55)

tk.Label(frame1, text='时间格式既支持hh:mm:ss 时：分：秒 的格式，也支持 00：00：00：000最后3位为毫秒的格式。', font=(
    'Arial', 9),  height=2).place(x=10, y=150)

# 第二个标签页，连接视频----------------------------------------------------------------------------------------------

tk.Label(frame2, text="请添加视频文件", font=('Arial'),  height=2).place(x=10, y=5)

fileList = tk.Listbox(frame2, width=65, show=None)
fileList.bind("<Button-1>", getIndex)
fileList.bind("<B1-Motion>", dragIndex)
fileList.place(x=140, y=15)

tk.Button(frame2, text='添加', width=8, height=1,
          command=addList).place(x=680, y=12)
tk.Button(frame2, text='删除', width=8, height=1,
          command=delList).place(x=680, y=55)

tk.Button(frame2, text='开始连接', width=8, height=1,
          command=joinFile).place(x=680, y=155)


# 第三个标签页，分割提取音视频----------------------------------------------------------------------------------------------

tk.Label(frame3, text="请选择视频文件", font=('Arial'),  height=2).place(x=10, y=5)
tk.Button(frame3, text='浏览', width=6, height=1,
          command=lambda: browsePath(3)).place(x=140, y=12)
file_path3 = tk.StringVar()
file_path3.set("")
filePathEnt3 = tk.Entry(frame3, textvariable=file_path3, width=50, show=None)
filePathEnt3.place(x=220, y=15)
tk.Button(frame3, text='开始提取', width=8, height=1,
          command=splitFile).place(x=680, y=55)


# 第四个标签页，音视频合成----------------------------------------------------------------------------------------------

tk.Label(frame4, text="请选择视频文件", font=('Arial'),  height=2).place(x=10, y=5)
tk.Button(frame4, text='浏览', width=6, height=1,
          command=lambda: browsePath(4)).place(x=140, y=12)
file_path4 = tk.StringVar()
file_path4.set("")
filePathEnt4 = tk.Entry(frame4, textvariable=file_path4, width=50, show=None)
filePathEnt4.place(x=220, y=15)
tk.Label(frame4, text="请选择音频文件", font=('Arial'),  height=2).place(x=10, y=55)
tk.Button(frame4, text='浏览', width=6, height=1,
          command=browsePath2).place(x=140, y=62)
file_path42 = tk.StringVar()
file_path42.set("")
filePathEnt42 = tk.Entry(frame4, textvariable=file_path42, width=50, show=None)
filePathEnt42.place(x=220, y=65)
tk.Button(frame4, text='开始合并', width=8, height=1,
          command=mergeFile).place(x=680, y=55)

# 第五个标签页，批量压缩视频----------------------------------------------------------------------------------------------

tk.Label(frame5, text="请选择包含视频的文件夹", font=(
    'Arial'),  height=2).place(x=10, y=5)
tk.Button(frame5, text='浏览', width=6, height=1,
          command=browseDir).place(x=200, y=12)
file_path5 = tk.StringVar()
file_path5.set("")
filePathEnt5 = tk.Entry(frame5, textvariable=file_path5, width=50, show=None)
filePathEnt5.place(x=280, y=15)
tk.Button(frame5, text='开始压缩', width=8, height=1,
          command=batCode).place(x=680, y=55)
amount = tk.StringVar()
amount.set("待编码文件数量")
tk.Label(frame5, textvariable=amount, font=(
    'Arial'),  height=2).place(x=10, y=120)
tk.Label(frame5, text="请选择压缩选项： ", font=(
    'Arial'),  height=2).place(x=10, y=60)
option = tk.IntVar()
option.set(1)
option1 = Radiobutton(frame5, text="更高质量", variable=option,
                      value=1)
option1.place(x=150, y=70)
option2 = Radiobutton(frame5, text="更小体积", variable=option,
                      value=0)
option2.place(x=300, y=70)


# 第六个标签页，高级视频转码----------------------------------------------------------------------------------------------
# "-profile:v high", "-preset:v fast", "-level 4.2", "-tune film", "-crf 18","-ab 128k",
tk.Label(frame6, text="请选择视频文件", font=('Arial'),  height=2).place(x=10, y=5)
tk.Button(frame6, text='浏览', width=6, height=1,
          command=lambda: browsePath(6)).place(x=140, y=12)
file_path6 = tk.StringVar()
file_path6.set("")
filePathEnt6 = tk.Entry(frame6, textvariable=file_path6, width=50, show=None)
filePathEnt6.place(x=220, y=15)
tk.Button(frame6, text='开始转码', width=8, height=1,
          command=selfCode).place(x=700, y=135)

tk.Label(frame6, text="请选择压缩选项： ", font=(
    'Arial'),  height=2).place(x=10, y=60)
tk.Label(frame6, text="画面质量： ", font=(
    'Arial',9)).place(x=160, y=65)
opt1s=("high","main","extended","baseline")
opt1=tk.StringVar(root)
opt1.set("high")
optionMenu1=OptionMenu(frame6, opt1, *opt1s)
optionMenu1.place(x=150,y=90)
tk.Label(frame6, text="编码速度： ", font=(
    'Arial',9)).place(x=360, y=65)
opt2s=("veryfast","fast","medium","slow")
opt2=tk.StringVar(root)
opt2.set("veryfast")
optionMenu2=OptionMenu(frame6, opt2, *opt2s)
optionMenu2.place(x=350,y=90)
tk.Label(frame6, text="编码等级： ", font=(
    'Arial',9)).place(x=560, y=65)
opt3s=("4.2","4.1","3.1")
opt3=tk.StringVar(root)
opt3.set("4.2")
optionMenu3=OptionMenu(frame6, opt3, *opt3s)
optionMenu3.place(x=550,y=90)
tk.Label(frame6, text="视频类别： ", font=(
    'Arial',9)).place(x=160, y=155)
opt4s=("film","animation","stillimage")
opt4=tk.StringVar(root)
opt4.set("film")
optionMenu4=OptionMenu(frame6, opt4, *opt4s)
optionMenu4.place(x=150,y=180)
tk.Label(frame6, text="压缩等级（数字越大，体积越小)", font=(
    'Arial',9)).place(x=290, y=155)
opt5s=("18","20","22","24","26","28")
opt5=tk.StringVar(root)
opt5.set("22")
optionMenu5=OptionMenu(frame6, opt5, *opt5s)
optionMenu5.place(x=350,y=180)
tk.Label(frame6, text="音频码率： ", font=(
    'Arial',9)).place(x=560, y=155)
opt6s=("192k","128k","96k")
opt6=tk.StringVar(root)
opt6.set("128k")
optionMenu6=OptionMenu(frame6, opt6, *opt6s)
optionMenu6.place(x=550,y=180)


# 第七个标签页，分割提取音视频----------------------------------------------------------------------------------------------

tk.Label(frame7, text="当前版本： "+ version, font=('Arial'),  height=2).place(x=10, y=5)
tk.Button(frame7, text='点击查看版本信息', width=16, height=1,
          command=lambda: webbrowser.open(home_URL, new=0)).place(x=250, y=12)
aboutTxt='''小兔子转换器 (Little Rubbit Convert) 是为了方便使用FFMPEG而设计的一个简易的FFMPEG图形界面（GUI）。\n
Little Rubbit Convert的所有功能完全依赖于FFMPEG。因此您的系统中如果没有安装过FFMPEG,
那么请将本压缩包中的ffmpeg.exe文件，拷贝到Little Rubbit Convert的同一文件夹下，本工具才能正常使用。\n
Little Rubbit Convert的前四项功能，因为不重新编码，所以速度极快，而且不会带来质量损失。\n
Little Rubbit Convert还处于最初开发版本，无可避免有很多的bug和功能缺陷，
如果您有任何问题及建议，非常期待您的反馈。\n
您可以点击上方的版本信息链接，在页面下方进行留言；
也可以扫描下方的二维码，添加开发者的企业微信，进行交流。\n

'''
weixin = tk.PhotoImage(file='weixin.png')
about = tk.Text(frame7, width=112, height=38)
about.insert('end', aboutTxt)
about.image_create("end", image=weixin)
about.tag_configure("center", justify='center')
about.tag_add("center", 1.0, "end")
about.config(state='disable')
about.place(x=0, y=60)

# ----------------------------------------------------------------------------------------------


def on_closing():
    root.destroy()
    process.stdin.write('q')
    process.stdin.flush()
    #process.communicate()
    process.kill()
    os.remove('weixin.png')
    sys.exit()
    

def ffmpegClose():
    process.stdin.write('q'.encode("GBK"))
    process.stdin.flush()
    process.communicate()
    process.kill()

#root.protocol("WM_DELETE_WINDOW", lambda: process.stdin.write('q'.encode("GBK")))
root.protocol("WM_DELETE_WINDOW", on_closing)

root.mainloop()
