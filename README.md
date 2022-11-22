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

#### 安装说明

1. 本软件为绿色免安装版。
2. 将压缩包解压到本地，打开LRConvert.exe文件即可使用。
3. Little Rabbit Convert的前四项功能，因为不重新编码，所以速度极快，而且不会带来质量损失。
