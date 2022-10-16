<<<<<<< HEAD
Mac安装ffmpeg的主要难点在于第一步，安装brew，因为brew的服务器在国外，所以经常第一步就超时出错。

<<<<<<< HEAD
下面使用国内镜像软件源，一键安装配置脚本。

注：以下命令均在Mac终端窗口中进行：

/bin/zsh -c "$(curl -fsSL https://gitee.com/cunkai/HomebrewCN/raw/master/Homebrew.sh)"


脚本运行后，会要求先选择软件源，个人使用的清华源，所有输入2回车。
然后会要求输入开机密码，输入时没有显示，输入完按回车键开始安装。


brew安装完成后，就可以用brew来安装ffmpeg
brew install ffmpeg

ffmpeg安装完成后，输入以下命令，查看ffmeg版本，如果显示正常，则表示ffmpeg安装成功了！
 
=======
下面介绍的步骤中，把服务器改为国内镜像，应该比较容易安装成功。

注：以下命令均在Mac终端窗口中进行：

一,安装brew 
/usr/bin/ruby -e "$(curl -fsSL https://cdn.jsdelivr.net/gh/ineo6/homebrew-install/install)"

注：最后出现 Installation successful! 或者 Checking out files: 100% (5392/5392), done. 说明安装成功。

brew安装完后，执行以下步骤，把brew的软件源指向清华大学国内仓库。
1，
git -C "$(brew --repo)" remote set-url origin https://mirrors.ustc.edu.cn/brew.git

2，
git -C "$(brew --repo homebrew/core)" remote set-url origin https://mirrors.ustc.edu.cn/homebrew-core.git

3，
git -C "$(brew --repo homebrew/cask)" remote set-url origin https://mirrors.ustc.edu.cn/homebrew-cask.git

4，
echo 'export HOMEBREW_BOTTLE_DOMAIN=https://mirrors.ustc.edu.cn/homebrew-bottles' >> ~/.bash_profile
5，
source ~/.bash_profile

6，
echo 'export HOMEBREW_BOTTLE_DOMAIN=https://mirrors.ustc.edu.cn/homebrew-bottles' >> ~/.bash_profile

7，
source ~/.zshrc


8，
brew update


以上步骤完成后，就可以用brew来安装ffmpeg
brew install ffmpeg

ffmpeg安装完成后，输入以下命令，查看ffmeg版本，如果显示正常，则表示ffmpeg安装成功了！

=======
Mac安装ffmpeg的主要难点在于第一步，安装brew，因为brew的服务器在国外，所以经常第一步就超时出错。

下面介绍的步骤中，把服务器改为国内镜像，应该比较容易安装成功。

注：以下命令均在Mac终端窗口中进行：

一,安装brew 
/usr/bin/ruby -e "$(curl -fsSL https://cdn.jsdelivr.net/gh/ineo6/homebrew-install/install)"

注：最后出现 Installation successful! 或者 Checking out files: 100% (5392/5392), done. 说明安装成功。

brew安装完后，执行以下步骤，把brew的软件源指向清华大学国内仓库。
1，
git -C "$(brew --repo)" remote set-url origin https://mirrors.ustc.edu.cn/brew.git

2，
git -C "$(brew --repo homebrew/core)" remote set-url origin https://mirrors.ustc.edu.cn/homebrew-core.git

3，
git -C "$(brew --repo homebrew/cask)" remote set-url origin https://mirrors.ustc.edu.cn/homebrew-cask.git

4，
echo 'export HOMEBREW_BOTTLE_DOMAIN=https://mirrors.ustc.edu.cn/homebrew-bottles' >> ~/.bash_profile
5，
source ~/.bash_profile

6，
echo 'export HOMEBREW_BOTTLE_DOMAIN=https://mirrors.ustc.edu.cn/homebrew-bottles' >> ~/.bash_profile

7，
source ~/.zshrc


8，
brew update


以上步骤完成后，就可以用brew来安装ffmpeg
brew install ffmpeg

ffmpeg安装完成后，输入以下命令，查看ffmeg版本，如果显示正常，则表示ffmpeg安装成功了！

>>>>>>> 5af6bcec4c7a264b5eed30195adc25e744d28ca9
>>>>>>> 9b8d760e761b339657bcd7d6b07044fd0f43843e
ffmpeg -version