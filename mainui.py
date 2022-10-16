# -*- coding: utf-8 -*-

################################################################################
## Form generated from reading UI file 'mainui.ui'
##
## Created by: Qt User Interface Compiler version 6.3.1
##
## WARNING! All changes made in this file will be lost when recompiling UI file!
################################################################################

from PySide6.QtCore import (QCoreApplication, QDate, QDateTime, QLocale,
    QMetaObject, QObject, QPoint, QRect,
    QSize, QTime, QUrl, Qt)
from PySide6.QtGui import (QBrush, QColor, QConicalGradient, QCursor,
    QFont, QFontDatabase, QGradient, QIcon,
    QImage, QKeySequence, QLinearGradient, QPainter,
    QPalette, QPixmap, QRadialGradient, QTransform)
from PySide6.QtWidgets import (QApplication, QComboBox, QGridLayout, QGroupBox,
    QHBoxLayout, QLabel, QLineEdit, QListWidget,
    QListWidgetItem, QPushButton, QRadioButton, QSizePolicy,
    QSpacerItem, QTabWidget, QTextBrowser, QVBoxLayout,
    QWidget)

class Ui_Form(object):
    def setupUi(self, Form):
        if not Form.objectName():
            Form.setObjectName(u"Form")
        Form.resize(800, 600)
        self.verticalLayout = QVBoxLayout(Form)
        self.verticalLayout.setObjectName(u"verticalLayout")
        self.tabWidget = QTabWidget(Form)
        self.tabWidget.setObjectName(u"tabWidget")
        sizePolicy = QSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        sizePolicy.setHorizontalStretch(0)
        sizePolicy.setVerticalStretch(0)
        sizePolicy.setHeightForWidth(self.tabWidget.sizePolicy().hasHeightForWidth())
        self.tabWidget.setSizePolicy(sizePolicy)
        self.T1 = QWidget()
        self.T1.setObjectName(u"T1")
        self.verticalLayout_2 = QVBoxLayout(self.T1)
        self.verticalLayout_2.setObjectName(u"verticalLayout_2")
        self.verticalSpacer_3 = QSpacerItem(20, 40, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_2.addItem(self.verticalSpacer_3)

        self.horizontalLayout = QHBoxLayout()
        self.horizontalLayout.setObjectName(u"horizontalLayout")
        self.label = QLabel(self.T1)
        self.label.setObjectName(u"label")

        self.horizontalLayout.addWidget(self.label)

        self.T1add = QPushButton(self.T1)
        self.T1add.setObjectName(u"T1add")

        self.horizontalLayout.addWidget(self.T1add)

        self.T1file = QLineEdit(self.T1)
        self.T1file.setObjectName(u"T1file")

        self.horizontalLayout.addWidget(self.T1file)

        self.horizontalSpacer_2 = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.horizontalLayout.addItem(self.horizontalSpacer_2)

        self.T1start = QPushButton(self.T1)
        self.T1start.setObjectName(u"T1start")

        self.horizontalLayout.addWidget(self.T1start)

        self.horizontalLayout.setStretch(0, 1)
        self.horizontalLayout.setStretch(1, 1)
        self.horizontalLayout.setStretch(2, 5)
        self.horizontalLayout.setStretch(3, 1)
        self.horizontalLayout.setStretch(4, 1)

        self.verticalLayout_2.addLayout(self.horizontalLayout)

        self.verticalSpacer_2 = QSpacerItem(20, 40, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_2.addItem(self.verticalSpacer_2)

        self.horizontalLayout_3 = QHBoxLayout()
        self.horizontalLayout_3.setObjectName(u"horizontalLayout_3")
        self.label_2 = QLabel(self.T1)
        self.label_2.setObjectName(u"label_2")

        self.horizontalLayout_3.addWidget(self.label_2)

        self.T1time1 = QLineEdit(self.T1)
        self.T1time1.setObjectName(u"T1time1")

        self.horizontalLayout_3.addWidget(self.T1time1)

        self.horizontalSpacer_4 = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.horizontalLayout_3.addItem(self.horizontalSpacer_4)

        self.label_3 = QLabel(self.T1)
        self.label_3.setObjectName(u"label_3")

        self.horizontalLayout_3.addWidget(self.label_3)

        self.T1time2 = QLineEdit(self.T1)
        self.T1time2.setObjectName(u"T1time2")

        self.horizontalLayout_3.addWidget(self.T1time2)

        self.horizontalSpacer_5 = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.horizontalLayout_3.addItem(self.horizontalSpacer_5)

        self.T1stop = QPushButton(self.T1)
        self.T1stop.setObjectName(u"T1stop")

        self.horizontalLayout_3.addWidget(self.T1stop)


        self.verticalLayout_2.addLayout(self.horizontalLayout_3)

        self.verticalSpacer = QSpacerItem(20, 40, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_2.addItem(self.verticalSpacer)

        self.label_4 = QLabel(self.T1)
        self.label_4.setObjectName(u"label_4")

        self.verticalLayout_2.addWidget(self.label_4)

        self.label_9 = QLabel(self.T1)
        self.label_9.setObjectName(u"label_9")

        self.verticalLayout_2.addWidget(self.label_9)

        self.verticalLayout_2.setStretch(1, 1)
        self.verticalLayout_2.setStretch(3, 1)
        self.tabWidget.addTab(self.T1, "")
        self.T2 = QWidget()
        self.T2.setObjectName(u"T2")
        self.horizontalLayout_5 = QHBoxLayout(self.T2)
        self.horizontalLayout_5.setObjectName(u"horizontalLayout_5")
        self.groupBox_2 = QGroupBox(self.T2)
        self.groupBox_2.setObjectName(u"groupBox_2")
        self.verticalLayout_4 = QVBoxLayout(self.groupBox_2)
        self.verticalLayout_4.setObjectName(u"verticalLayout_4")
        self.T2listbox = QListWidget(self.groupBox_2)
        self.T2listbox.setObjectName(u"T2listbox")

        self.verticalLayout_4.addWidget(self.T2listbox)

        self.horizontalLayout_14 = QHBoxLayout()
        self.horizontalLayout_14.setObjectName(u"horizontalLayout_14")
        self.label_5 = QLabel(self.groupBox_2)
        self.label_5.setObjectName(u"label_5")

        self.horizontalLayout_14.addWidget(self.label_5)

        self.horizontalSpacer_11 = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.horizontalLayout_14.addItem(self.horizontalSpacer_11)

        self.T2add = QPushButton(self.groupBox_2)
        self.T2add.setObjectName(u"T2add")

        self.horizontalLayout_14.addWidget(self.T2add)

        self.T2del = QPushButton(self.groupBox_2)
        self.T2del.setObjectName(u"T2del")

        self.horizontalLayout_14.addWidget(self.T2del)


        self.verticalLayout_4.addLayout(self.horizontalLayout_14)


        self.horizontalLayout_5.addWidget(self.groupBox_2)

        self.verticalLayout_3 = QVBoxLayout()
        self.verticalLayout_3.setObjectName(u"verticalLayout_3")
        self.T2start = QPushButton(self.T2)
        self.T2start.setObjectName(u"T2start")

        self.verticalLayout_3.addWidget(self.T2start)

        self.T2stop = QPushButton(self.T2)
        self.T2stop.setObjectName(u"T2stop")

        self.verticalLayout_3.addWidget(self.T2stop)


        self.horizontalLayout_5.addLayout(self.verticalLayout_3)

        self.tabWidget.addTab(self.T2, "")
        self.T3 = QWidget()
        self.T3.setObjectName(u"T3")
        self.verticalLayout_6 = QVBoxLayout(self.T3)
        self.verticalLayout_6.setObjectName(u"verticalLayout_6")
        self.verticalSpacer_4 = QSpacerItem(20, 60, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_6.addItem(self.verticalSpacer_4)

        self.horizontalLayout_4 = QHBoxLayout()
        self.horizontalLayout_4.setObjectName(u"horizontalLayout_4")
        self.label_6 = QLabel(self.T3)
        self.label_6.setObjectName(u"label_6")

        self.horizontalLayout_4.addWidget(self.label_6)

        self.T3add = QPushButton(self.T3)
        self.T3add.setObjectName(u"T3add")

        self.horizontalLayout_4.addWidget(self.T3add)

        self.T3file = QLineEdit(self.T3)
        self.T3file.setObjectName(u"T3file")

        self.horizontalLayout_4.addWidget(self.T3file)

        self.horizontalSpacer = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.horizontalLayout_4.addItem(self.horizontalSpacer)

        self.verticalLayout_5 = QVBoxLayout()
        self.verticalLayout_5.setObjectName(u"verticalLayout_5")
        self.T3start = QPushButton(self.T3)
        self.T3start.setObjectName(u"T3start")

        self.verticalLayout_5.addWidget(self.T3start)

        self.verticalSpacer_6 = QSpacerItem(20, 40, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_5.addItem(self.verticalSpacer_6)

        self.T3stop = QPushButton(self.T3)
        self.T3stop.setObjectName(u"T3stop")

        self.verticalLayout_5.addWidget(self.T3stop)


        self.horizontalLayout_4.addLayout(self.verticalLayout_5)

        self.horizontalLayout_4.setStretch(0, 1)
        self.horizontalLayout_4.setStretch(2, 5)
        self.horizontalLayout_4.setStretch(3, 1)
        self.horizontalLayout_4.setStretch(4, 1)

        self.verticalLayout_6.addLayout(self.horizontalLayout_4)

        self.verticalSpacer_5 = QSpacerItem(20, 60, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_6.addItem(self.verticalSpacer_5)

        self.tabWidget.addTab(self.T3, "")
        self.T4 = QWidget()
        self.T4.setObjectName(u"T4")
        self.verticalLayout_12 = QVBoxLayout(self.T4)
        self.verticalLayout_12.setObjectName(u"verticalLayout_12")
        self.verticalSpacer_13 = QSpacerItem(20, 52, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_12.addItem(self.verticalSpacer_13)

        self.horizontalLayout_10 = QHBoxLayout()
        self.horizontalLayout_10.setObjectName(u"horizontalLayout_10")
        self.label_7 = QLabel(self.T4)
        self.label_7.setObjectName(u"label_7")

        self.horizontalLayout_10.addWidget(self.label_7)

        self.T4addvideo = QPushButton(self.T4)
        self.T4addvideo.setObjectName(u"T4addvideo")

        self.horizontalLayout_10.addWidget(self.T4addvideo)

        self.T4file = QLineEdit(self.T4)
        self.T4file.setObjectName(u"T4file")

        self.horizontalLayout_10.addWidget(self.T4file)

        self.horizontalSpacer_9 = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.horizontalLayout_10.addItem(self.horizontalSpacer_9)

        self.T4start = QPushButton(self.T4)
        self.T4start.setObjectName(u"T4start")

        self.horizontalLayout_10.addWidget(self.T4start)

        self.horizontalLayout_10.setStretch(0, 1)
        self.horizontalLayout_10.setStretch(1, 1)
        self.horizontalLayout_10.setStretch(2, 5)
        self.horizontalLayout_10.setStretch(3, 1)
        self.horizontalLayout_10.setStretch(4, 1)

        self.verticalLayout_12.addLayout(self.horizontalLayout_10)

        self.verticalSpacer_15 = QSpacerItem(20, 53, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_12.addItem(self.verticalSpacer_15)

        self.horizontalLayout_11 = QHBoxLayout()
        self.horizontalLayout_11.setObjectName(u"horizontalLayout_11")
        self.label_15 = QLabel(self.T4)
        self.label_15.setObjectName(u"label_15")

        self.horizontalLayout_11.addWidget(self.label_15)

        self.T4addaudio = QPushButton(self.T4)
        self.T4addaudio.setObjectName(u"T4addaudio")

        self.horizontalLayout_11.addWidget(self.T4addaudio)

        self.T4audio = QLineEdit(self.T4)
        self.T4audio.setObjectName(u"T4audio")

        self.horizontalLayout_11.addWidget(self.T4audio)

        self.horizontalSpacer_10 = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.horizontalLayout_11.addItem(self.horizontalSpacer_10)

        self.T4stop = QPushButton(self.T4)
        self.T4stop.setObjectName(u"T4stop")

        self.horizontalLayout_11.addWidget(self.T4stop)

        self.horizontalLayout_11.setStretch(0, 1)
        self.horizontalLayout_11.setStretch(1, 1)
        self.horizontalLayout_11.setStretch(2, 5)
        self.horizontalLayout_11.setStretch(3, 1)
        self.horizontalLayout_11.setStretch(4, 1)

        self.verticalLayout_12.addLayout(self.horizontalLayout_11)

        self.verticalSpacer_14 = QSpacerItem(20, 52, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_12.addItem(self.verticalSpacer_14)

        self.tabWidget.addTab(self.T4, "")
        self.T5 = QWidget()
        self.T5.setObjectName(u"T5")
        self.verticalLayout_14 = QVBoxLayout(self.T5)
        self.verticalLayout_14.setObjectName(u"verticalLayout_14")
        self.verticalSpacer_16 = QSpacerItem(20, 40, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_14.addItem(self.verticalSpacer_16)

        self.horizontalLayout_12 = QHBoxLayout()
        self.horizontalLayout_12.setObjectName(u"horizontalLayout_12")
        self.label_16 = QLabel(self.T5)
        self.label_16.setObjectName(u"label_16")

        self.horizontalLayout_12.addWidget(self.label_16)

        self.T5add = QPushButton(self.T5)
        self.T5add.setObjectName(u"T5add")

        self.horizontalLayout_12.addWidget(self.T5add)

        self.T5file = QLineEdit(self.T5)
        self.T5file.setObjectName(u"T5file")

        self.horizontalLayout_12.addWidget(self.T5file)

        self.horizontalSpacer_3 = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.horizontalLayout_12.addItem(self.horizontalSpacer_3)

        self.T5start = QPushButton(self.T5)
        self.T5start.setObjectName(u"T5start")

        self.horizontalLayout_12.addWidget(self.T5start)

        self.horizontalLayout_12.setStretch(0, 1)
        self.horizontalLayout_12.setStretch(1, 1)
        self.horizontalLayout_12.setStretch(2, 5)
        self.horizontalLayout_12.setStretch(3, 1)

        self.verticalLayout_14.addLayout(self.horizontalLayout_12)

        self.verticalSpacer_17 = QSpacerItem(20, 40, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_14.addItem(self.verticalSpacer_17)

        self.horizontalLayout_13 = QHBoxLayout()
        self.horizontalLayout_13.setObjectName(u"horizontalLayout_13")
        self.label_17 = QLabel(self.T5)
        self.label_17.setObjectName(u"label_17")

        self.horizontalLayout_13.addWidget(self.label_17)

        self.T5big = QRadioButton(self.T5)
        self.T5big.setObjectName(u"T5big")

        self.horizontalLayout_13.addWidget(self.T5big)

        self.T5small = QRadioButton(self.T5)
        self.T5small.setObjectName(u"T5small")

        self.horizontalLayout_13.addWidget(self.T5small)

        self.horizontalSpacer_6 = QSpacerItem(40, 20, QSizePolicy.Expanding, QSizePolicy.Minimum)

        self.horizontalLayout_13.addItem(self.horizontalSpacer_6)

        self.T5stop = QPushButton(self.T5)
        self.T5stop.setObjectName(u"T5stop")

        self.horizontalLayout_13.addWidget(self.T5stop)


        self.verticalLayout_14.addLayout(self.horizontalLayout_13)

        self.verticalSpacer_18 = QSpacerItem(20, 40, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_14.addItem(self.verticalSpacer_18)

        self.groupBox_4 = QGroupBox(self.T5)
        self.groupBox_4.setObjectName(u"groupBox_4")
        self.verticalLayout_13 = QVBoxLayout(self.groupBox_4)
        self.verticalLayout_13.setObjectName(u"verticalLayout_13")
        self.T5info = QLabel(self.groupBox_4)
        self.T5info.setObjectName(u"T5info")

        self.verticalLayout_13.addWidget(self.T5info)


        self.verticalLayout_14.addWidget(self.groupBox_4)

        self.tabWidget.addTab(self.T5, "")
        self.T6 = QWidget()
        self.T6.setObjectName(u"T6")
        self.horizontalLayout_18 = QHBoxLayout(self.T6)
        self.horizontalLayout_18.setObjectName(u"horizontalLayout_18")
        self.verticalLayout_23 = QVBoxLayout()
        self.verticalLayout_23.setObjectName(u"verticalLayout_23")
        self.verticalSpacer_20 = QSpacerItem(20, 40, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_23.addItem(self.verticalSpacer_20)

        self.horizontalLayout_16 = QHBoxLayout()
        self.horizontalLayout_16.setObjectName(u"horizontalLayout_16")
        self.label_18 = QLabel(self.T6)
        self.label_18.setObjectName(u"label_18")

        self.horizontalLayout_16.addWidget(self.label_18)

        self.T6add = QPushButton(self.T6)
        self.T6add.setObjectName(u"T6add")

        self.horizontalLayout_16.addWidget(self.T6add)

        self.T6file = QLineEdit(self.T6)
        self.T6file.setObjectName(u"T6file")

        self.horizontalLayout_16.addWidget(self.T6file)

        self.horizontalLayout_16.setStretch(0, 1)
        self.horizontalLayout_16.setStretch(1, 1)
        self.horizontalLayout_16.setStretch(2, 5)

        self.verticalLayout_23.addLayout(self.horizontalLayout_16)

        self.verticalSpacer_19 = QSpacerItem(20, 40, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_23.addItem(self.verticalSpacer_19)

        self.horizontalLayout_17 = QHBoxLayout()
        self.horizontalLayout_17.setObjectName(u"horizontalLayout_17")
        self.label_19 = QLabel(self.T6)
        self.label_19.setObjectName(u"label_19")

        self.horizontalLayout_17.addWidget(self.label_19)

        self.gridLayout = QGridLayout()
        self.gridLayout.setObjectName(u"gridLayout")
        self.groupBox_5 = QGroupBox(self.T6)
        self.groupBox_5.setObjectName(u"groupBox_5")
        self.verticalLayout_16 = QVBoxLayout(self.groupBox_5)
        self.verticalLayout_16.setObjectName(u"verticalLayout_16")
        self.T6quality = QComboBox(self.groupBox_5)
        self.T6quality.setObjectName(u"T6quality")

        self.verticalLayout_16.addWidget(self.T6quality)


        self.gridLayout.addWidget(self.groupBox_5, 0, 0, 1, 1)

        self.groupBox_6 = QGroupBox(self.T6)
        self.groupBox_6.setObjectName(u"groupBox_6")
        self.verticalLayout_17 = QVBoxLayout(self.groupBox_6)
        self.verticalLayout_17.setObjectName(u"verticalLayout_17")
        self.T6speed = QComboBox(self.groupBox_6)
        self.T6speed.setObjectName(u"T6speed")

        self.verticalLayout_17.addWidget(self.T6speed)


        self.gridLayout.addWidget(self.groupBox_6, 0, 1, 1, 1)

        self.groupBox_7 = QGroupBox(self.T6)
        self.groupBox_7.setObjectName(u"groupBox_7")
        self.verticalLayout_18 = QVBoxLayout(self.groupBox_7)
        self.verticalLayout_18.setObjectName(u"verticalLayout_18")
        self.T6grade = QComboBox(self.groupBox_7)
        self.T6grade.setObjectName(u"T6grade")

        self.verticalLayout_18.addWidget(self.T6grade)


        self.gridLayout.addWidget(self.groupBox_7, 0, 2, 1, 1)

        self.groupBox_8 = QGroupBox(self.T6)
        self.groupBox_8.setObjectName(u"groupBox_8")
        self.verticalLayout_19 = QVBoxLayout(self.groupBox_8)
        self.verticalLayout_19.setObjectName(u"verticalLayout_19")
        self.T6type = QComboBox(self.groupBox_8)
        self.T6type.setObjectName(u"T6type")

        self.verticalLayout_19.addWidget(self.T6type)


        self.gridLayout.addWidget(self.groupBox_8, 1, 0, 1, 1)

        self.groupBox_9 = QGroupBox(self.T6)
        self.groupBox_9.setObjectName(u"groupBox_9")
        self.verticalLayout_20 = QVBoxLayout(self.groupBox_9)
        self.verticalLayout_20.setObjectName(u"verticalLayout_20")
        self.T6level = QComboBox(self.groupBox_9)
        self.T6level.setObjectName(u"T6level")

        self.verticalLayout_20.addWidget(self.T6level)


        self.gridLayout.addWidget(self.groupBox_9, 1, 1, 1, 1)

        self.groupBox_10 = QGroupBox(self.T6)
        self.groupBox_10.setObjectName(u"groupBox_10")
        self.verticalLayout_21 = QVBoxLayout(self.groupBox_10)
        self.verticalLayout_21.setObjectName(u"verticalLayout_21")
        self.T6audio = QComboBox(self.groupBox_10)
        self.T6audio.setObjectName(u"T6audio")

        self.verticalLayout_21.addWidget(self.T6audio)


        self.gridLayout.addWidget(self.groupBox_10, 1, 2, 1, 1)


        self.horizontalLayout_17.addLayout(self.gridLayout)

        self.horizontalLayout_17.setStretch(0, 1)
        self.horizontalLayout_17.setStretch(1, 5)

        self.verticalLayout_23.addLayout(self.horizontalLayout_17)

        self.verticalLayout_23.setStretch(1, 1)
        self.verticalLayout_23.setStretch(3, 3)

        self.horizontalLayout_18.addLayout(self.verticalLayout_23)

        self.verticalLayout_22 = QVBoxLayout()
        self.verticalLayout_22.setObjectName(u"verticalLayout_22")
        self.T6start = QPushButton(self.T6)
        self.T6start.setObjectName(u"T6start")

        self.verticalLayout_22.addWidget(self.T6start)

        self.T6stop = QPushButton(self.T6)
        self.T6stop.setObjectName(u"T6stop")

        self.verticalLayout_22.addWidget(self.T6stop)


        self.horizontalLayout_18.addLayout(self.verticalLayout_22)

        self.tabWidget.addTab(self.T6, "")
        self.T7 = QWidget()
        self.T7.setObjectName(u"T7")
        self.horizontalLayout_6 = QHBoxLayout(self.T7)
        self.horizontalLayout_6.setObjectName(u"horizontalLayout_6")
        self.T7about = QTextBrowser(self.T7)
        self.T7about.setObjectName(u"T7about")

        self.horizontalLayout_6.addWidget(self.T7about)

        self.verticalLayout_7 = QVBoxLayout()
        self.verticalLayout_7.setObjectName(u"verticalLayout_7")
        self.verticalSpacer_7 = QSpacerItem(20, 40, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_7.addItem(self.verticalSpacer_7)

        self.verticalSpacer_8 = QSpacerItem(20, 40, QSizePolicy.Minimum, QSizePolicy.Expanding)

        self.verticalLayout_7.addItem(self.verticalSpacer_8)

        self.T7this = QLabel(self.T7)
        self.T7this.setObjectName(u"T7this")

        self.verticalLayout_7.addWidget(self.T7this)

        self.T7new = QLabel(self.T7)
        self.T7new.setObjectName(u"T7new")

        self.verticalLayout_7.addWidget(self.T7new)

        self.T7check = QPushButton(self.T7)
        self.T7check.setObjectName(u"T7check")

        self.verticalLayout_7.addWidget(self.T7check)


        self.horizontalLayout_6.addLayout(self.verticalLayout_7)

        self.horizontalLayout_6.setStretch(0, 4)
        self.horizontalLayout_6.setStretch(1, 1)
        self.tabWidget.addTab(self.T7, "")

        self.verticalLayout.addWidget(self.tabWidget)

        self.groupBox = QGroupBox(Form)
        self.groupBox.setObjectName(u"groupBox")
        self.horizontalLayout_2 = QHBoxLayout(self.groupBox)
        self.horizontalLayout_2.setObjectName(u"horizontalLayout_2")
        self.infobox = QTextBrowser(self.groupBox)
        self.infobox.setObjectName(u"infobox")

        self.horizontalLayout_2.addWidget(self.infobox)


        self.verticalLayout.addWidget(self.groupBox)

        self.verticalLayout.setStretch(0, 2)
        self.verticalLayout.setStretch(1, 1)

        self.retranslateUi(Form)

        self.tabWidget.setCurrentIndex(6)


        QMetaObject.connectSlotsByName(Form)
    # setupUi

    def retranslateUi(self, Form):
        Form.setWindowTitle(QCoreApplication.translate("Form", u"Little Rabbit Convert", None))
        self.label.setText(QCoreApplication.translate("Form", u"\u8bf7\u9009\u62e9\u89c6\u9891\u6587\u4ef6\uff1a", None))
        self.T1add.setText(QCoreApplication.translate("Form", u"\u6d4f\u89c8", None))
        self.T1start.setText(QCoreApplication.translate("Form", u"\u5f00\u59cb\u622a\u53d6", None))
        self.label_2.setText(QCoreApplication.translate("Form", u"\u5f00\u59cb\u65f6\u95f4\uff1a", None))
        self.label_3.setText(QCoreApplication.translate("Form", u"\u7ed3\u675f\u65f6\u95f4\uff1a", None))
        self.T1stop.setText(QCoreApplication.translate("Form", u"\u505c\u6b62\u7f16\u7801", None))
        self.label_4.setText(QCoreApplication.translate("Form", u"\u65f6\u95f4\u683c\u5f0f\u652f\u6301 hh:mm:ss \u65f6\uff1a\u5206\uff1a\u79d2 \u4e0d\u5e26\u6beb\u7c73\u5f62\u5f0f\uff0c\u6216\u8005 00:00:00:000 \u540e\u4e09\u4f4d\u4f4d\u6beb\u7c73\u3002 ", None))
        self.label_9.setText(QCoreApplication.translate("Form", u"\u5feb\u901f\u622a\u53d6\u89c6\u9891\u7247\u6bb5\u529f\u80fd\u4e0d\u8fdb\u884c\u91cd\u65b0\u7f16\u7801\uff0c\u901f\u5ea6\u6781\u5feb\uff0c\u7f3a\u70b9\u662f\u526a\u8f91\u70b9\u4f1a\u4ece\u8d77\u59cb\u70b9\u9644\u4ef6\u7684\u5173\u952e\u5e27\u9009\u53d6\uff0c\u4e0d\u80fd\u7cbe\u51c6\u526a\u8f91\u3002", None))
        self.tabWidget.setTabText(self.tabWidget.indexOf(self.T1), QCoreApplication.translate("Form", u"\u89c6\u9891\u622a\u53d6", None))
        self.groupBox_2.setTitle(QCoreApplication.translate("Form", u"\u6587\u4ef6\u5217\u8868", None))
        self.label_5.setText(QCoreApplication.translate("Form", u"\u63d0\u793a\uff1a\u53ea\u6709\u7f16\u7801\u5b8c\u6210\u76f8\u540c\u7684\u89c6\u9891\u6587\u4ef6\u624d\u53ef\u4ee5\u8fdb\u884c\u5408\u5e76\u3002", None))
        self.T2add.setText(QCoreApplication.translate("Form", u"\u6dfb\u52a0\u89c6\u9891", None))
        self.T2del.setText(QCoreApplication.translate("Form", u"\u5220\u9664\u89c6\u9891", None))
        self.T2start.setText(QCoreApplication.translate("Form", u"\u5f00\u59cb\u5408\u6210", None))
        self.T2stop.setText(QCoreApplication.translate("Form", u"\u505c\u6b62\u7f16\u7801", None))
        self.tabWidget.setTabText(self.tabWidget.indexOf(self.T2), QCoreApplication.translate("Form", u"\u591a\u89c6\u9891\u8fde\u63a5", None))
        self.label_6.setText(QCoreApplication.translate("Form", u"\u9009\u62e9\u89c6\u9891\u6587\u4ef6\uff1a", None))
        self.T3add.setText(QCoreApplication.translate("Form", u"\u6d4f\u89c8", None))
        self.T3start.setText(QCoreApplication.translate("Form", u"\u5f00\u59cb\u7f16\u7801", None))
        self.T3stop.setText(QCoreApplication.translate("Form", u"\u505c\u6b62\u7f16\u7801", None))
        self.tabWidget.setTabText(self.tabWidget.indexOf(self.T3), QCoreApplication.translate("Form", u"\u97f3\u89c6\u9891\u5206\u79bb", None))
        self.label_7.setText(QCoreApplication.translate("Form", u"\u9009\u62e9\u89c6\u9891\u6587\u4ef6\uff1a", None))
        self.T4addvideo.setText(QCoreApplication.translate("Form", u"\u6d4f\u89c8", None))
        self.T4start.setText(QCoreApplication.translate("Form", u"\u5f00\u59cb\u5408\u5e76", None))
        self.label_15.setText(QCoreApplication.translate("Form", u"\u9009\u62e9\u97f3\u9891\u6587\u4ef6\uff1a", None))
        self.T4addaudio.setText(QCoreApplication.translate("Form", u"\u6d4f\u89c8", None))
        self.T4stop.setText(QCoreApplication.translate("Form", u"\u505c\u6b62\u7f16\u7801", None))
        self.tabWidget.setTabText(self.tabWidget.indexOf(self.T4), QCoreApplication.translate("Form", u"\u97f3\u89c6\u9891\u5408\u5e76", None))
        self.label_16.setText(QCoreApplication.translate("Form", u"\u9009\u62e9\u6587\u4ef6\u5939\uff1a", None))
        self.T5add.setText(QCoreApplication.translate("Form", u"\u6d4f\u89c8", None))
        self.T5start.setText(QCoreApplication.translate("Form", u"\u5f00\u59cb\u7f16\u7801", None))
        self.label_17.setText(QCoreApplication.translate("Form", u"\u9009\u62e9\u538b\u7f29\u9009\u9879\uff1a", None))
        self.T5big.setText(QCoreApplication.translate("Form", u"\u66f4\u9ad8\u8d28\u91cf", None))
        self.T5small.setText(QCoreApplication.translate("Form", u"\u66f4\u5c0f\u4f53\u79ef", None))
        self.T5stop.setText(QCoreApplication.translate("Form", u"\u505c\u6b62\u7f16\u7801", None))
        self.groupBox_4.setTitle(QCoreApplication.translate("Form", u"\u5f85\u7f16\u7801\u6587\u4ef6\u6570\u91cf", None))
        self.T5info.setText("")
        self.tabWidget.setTabText(self.tabWidget.indexOf(self.T5), QCoreApplication.translate("Form", u"\u6279\u91cf\u538b\u7f29", None))
        self.label_18.setText(QCoreApplication.translate("Form", u"\u9009\u62e9\u89c6\u9891\u6587\u4ef6\uff1a", None))
        self.T6add.setText(QCoreApplication.translate("Form", u"\u6d4f\u89c8", None))
        self.label_19.setText(QCoreApplication.translate("Form", u"\u9009\u62e9\u538b\u7f29\u9009\u9879\uff1a", None))
        self.groupBox_5.setTitle(QCoreApplication.translate("Form", u"\u753b\u9762\u8d28\u91cf\uff1a", None))
        self.groupBox_6.setTitle(QCoreApplication.translate("Form", u"\u7f16\u7801\u901f\u5ea6\uff1a", None))
        self.groupBox_7.setTitle(QCoreApplication.translate("Form", u"\u7f16\u7801\u7b49\u7ea7\uff1a", None))
        self.groupBox_8.setTitle(QCoreApplication.translate("Form", u"\u89c6\u9891\u7c7b\u522b\uff1a", None))
        self.groupBox_9.setTitle(QCoreApplication.translate("Form", u"\u538b\u7f29\u7b49\u7ea7\uff08\u6570\u5b57\u8d8a\u5927\uff0c\u6587\u4ef6\u8d8a\u5c0f\uff09\uff1a", None))
        self.groupBox_10.setTitle(QCoreApplication.translate("Form", u"\u97f3\u9891\u7801\u7387\uff1a", None))
        self.T6start.setText(QCoreApplication.translate("Form", u"\u5f00\u59cb\u7f16\u7801", None))
        self.T6stop.setText(QCoreApplication.translate("Form", u"\u505c\u6b62\u7f16\u7801", None))
        self.tabWidget.setTabText(self.tabWidget.indexOf(self.T6), QCoreApplication.translate("Form", u"\u81ea\u5b9a\u4e49\u7f16\u7801", None))
        self.T7this.setText(QCoreApplication.translate("Form", u"\u5f53\u524d\u7248\u672c\uff1a", None))
        self.T7new.setText(QCoreApplication.translate("Form", u"\u6700\u65b0\u7248\u672c\uff1a", None))
        self.T7check.setText(QCoreApplication.translate("Form", u"\u67e5\u770b\u7248\u672c\u4fe1\u606f", None))
        self.tabWidget.setTabText(self.tabWidget.indexOf(self.T7), QCoreApplication.translate("Form", u"\u5173    \u4e8e", None))
        self.groupBox.setTitle(QCoreApplication.translate("Form", u"\u8f6c\u7801\u8fdb\u5ea6\u4fe1\u606f", None))
    # retranslateUi

