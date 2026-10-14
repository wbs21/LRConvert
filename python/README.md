### 软件下载地址

链接：[https://pan.baidu.com/s/1dGx04xZ-LoiZwMHB3KheOQ?pwd=sxmf](https://pan.baidu.com/s/1dGx04xZ-LoiZwMHB3KheOQ?pwd=sxmf)

### 安装ffmpeg帮助说明

LRConvert只是一个ffmpeg的用户界面，必须配合ffmpeg使用。
windows只需将LRConvert和ffmpeg放到一个文件夹中即可。

### 一,Mac直接使用编译好的ffmpeg

1. 打开链接[static FFmpeg binaries for macOS 64-bit (evermeet.cx)](https://evermeet.cx/ffmpeg/)下载编译好的文件，解压缩后，拷贝到应用程序中，并运行一次。
2. 配置环境变量，打开终端，依次运行以下命令:

   ```
   touch .zshrc
   open -e .zshrc
   ```

3.在打开的文件中添加下面一行

```
export PATH=$PATH:/Applications
```

4.关闭文件，然后在终端中输入以下并运行，让环境变量生效：

```
source .zshrc
```

5.在终端中输入ffmpeg并回车，查看ffmpeg是否能运行。

#### 二，Mac使用homebrew安装ffmpeg

Mac安装homebrew，可参考以下步骤。

#### 1，intel版 Mac安装brew

下面的简化方法使用国内镜像软件源，一键安装配置脚本。

注：以下命令均在Mac终端窗口中进行：

```
/bin/zsh -c "$(curl -fsSL https://gitee.com/cunkai/HomebrewCN/raw/master/Homebrew.sh)"
```

脚本运行后，会要求先选择软件源，个人使用的清华源，输入2回车。
然后会要求输入开机密码，输入时没有显示，输入完按回车键开始安装。
如果系统没有安装过git，安装自动会跳出git安装程序，安装完成后请重新从头开始安装brew。

brew安装完成后，就可以用brew来安装ffmpeg

```
brew install ffmpeg
```

ffmpeg安装完成后，输入ffmpeg回车，可以看到ffmeg可以正常运行的多行提示，则表示ffmpeg安装成功了！

#### 2，M1版 Mac安装brew

可先通过app Store安装xcode,以备后续安装git需要。
在终端中输入以下命令并回车运行

```
/bin/bash -c "$(curl -fsSL https://gitee.com/ineo6/homebrew-install/raw/master/install.sh)"
```

然后根据提示输入国内源，开机密码等。直到安装结束。

安装结束后，需要设置环境变量，brew才能正常运行。在终端中运行下面两行代码，每行输入完后回车：

```
echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
eval "$(/opt/homebrew/bin/brew shellenv)"
```

在终端中运行以下语句,可以查看brew是否成功运行：

```
brew -v
```

最后，在终端中运行以下代码，安装ffmpeg:

```
brew install ffmpeg
```

安装完成后，在终端中运行ffmpeg，可以看到ffmpeg已经安装成功。

```
ffmpeg
```

#### 更新说明

v 3.4
     1. 命令行改为参数列表方式传递（不再使用 shell），文件名/路径中的空格、中文、引号都不再导致失败；
     2. 修复批量压缩时输出文件被写回输入列表、重复点击会对输出文件再转码的问题；
     3. 修复缺少 `-y` 时 ffmpeg 等待确认导致程序卡死的问题；
     4. 修复 `scale=iw/2:ih/2` 产生奇数分辨率导致 yuv420p 编码失败的问题；
     5. 修复 `-bufsize` 写死 2000k 导致高码率下画质被压死的问题；
     6. 停止按钮改为先发 `q` 让 ffmpeg 正常收尾，避免输出文件损坏；
     7. 进度改用 `ffprobe` + `-progress pipe:1` 计算，并新增底部进度条；
     8. 输出文件名改为 `原名_new_时间戳.后缀`，并自动避让已存在的文件；
     9. 移除 `exec()`、`locals()` 等动态写法，ffmpeg 相关逻辑抽到 `ffmpeg_cmd.py` 并补充单元测试。

v 3.3
     添加分辨率修改

v 3.2
     增加Intel核显、A卡硬件编码器

v 3.1
     增加B站缓存文件m4s类型的音视频合并

v 3.0
     增加显卡加速编码

v 2.1
     Bug调整

v 2.0

    1，使用了pyside6进行了重构，操作界面更美观。
    2，调整了部分编码细节。

v 1.3.1

    1，调整了一下批量压缩和自定义压缩的编码细节。
    2，改变了视频截取、视频连接、视频分离、视频合成的文件封装格式（文件名类型），以适应除mp4外的更多文件类型。

v 1.3.0

    增加线程控制，避免批量压缩时，因文件数量过多引起的进程崩溃。

v 1.2.0:
    1，修正了一些软件BUG;
    2, 增加了MAC版本。

v 1.0.0:

    第一个测试版本

#### LRConvert 软件说明**

Little Rabbit Convert 小兔子转换器 (Little Rabbit Convert) 是为了方便使用FFMPEG而设计的一个简易的FFMPEG图形界面（GUI）。

#### 软件说明

Little Rabbit Convert的所有功能完全依赖于FFMPEG。因此您的系统中如果没有安装过FFMPEG,
那么请将本压缩包中的ffmpeg.exe文件，拷贝到Little Rabbit Convert的同一文件夹下，本工具才能正常使用。

#### 开发说明

```bash
pip install -r requirements.txt
python LRConvert.py          # 运行

python -m unittest discover -s tests -v   # 运行测试（需要 ffmpeg / ffprobe）
```

代码分为两层：

- `ffmpeg_cmd.py`：ffmpeg 定位、命令构造、进度解析（不依赖 Qt，可单独测试）；
- `LRConvert.py`：界面与任务调度。

如果不想把 ffmpeg 放进 PATH，可以用环境变量指定：

```bash
export LRCONVERT_FFMPEG=/path/to/ffmpeg
export LRCONVERT_FFPROBE=/path/to/ffprobe
```

#### 安装说明

1. 本软件为绿色免安装版。
2. 将压缩包解压到本地，打开LRConvert.exe文件即可使用。
3. Little Rabbit Convert的前四项功能，因为不重新编码，所以速度极快，而且不会带来质量损失。
