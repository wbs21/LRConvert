<<<<<<< HEAD
Mac安装ffmpeg的主要难点在于第一步，安装brew，因为brew的服务器在国外，所以经常第一步就超时出错。

下面使用国内镜像软件源，一键安装配置脚本。

注：以下命令均在Mac终端窗口中进行：

/bin/zsh -c "$(curl -fsSL https://gitee.com/cunkai/HomebrewCN/raw/master/Homebrew.sh)"


脚本运行后，会要求先选择软件源，个人使用的清华源，所有输入2回车。
然后会要求输入开机密码，输入时没有显示，输入完按回车键开始安装。


brew安装完成后，就可以用brew来安装ffmpeg
brew install ffmpeg

ffmpeg安装完成后，输入以下命令，查看ffmeg版本，如果显示正常，则表示ffmpeg安装成功了！
 
ffmpeg -version