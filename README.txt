VC-IL-Utils 合并版（英文版 + 汉化版）
目录
EN\VC-IL-Utils.asi        英文版（165,376 字节）
EN\ILUtilsKeys.ini        英文版按键配置

CN\VC-IL-Utils.asi        汉化版（164,864 字节，GBK 界面）
CN\ILUtilsKeys.ini        汉化版按键配置

src\Main_EN.cpp           英文版源码（UTF-8）
src\Main_CN.cpp           汉化版源码（GBK 编码，编译时不要加 /utf-8）
src\build.bat             编译脚本

用法
二选一，把对应目录里的 VC-IL-Utils.asi 和 ILUtilsKeys.ini 放到 gta-vc.exe 同目录：

  英文版：EN\ 里的两个文件
  汉化版：CN\ 里的两个文件（任务名、界面、按钮、提示均为中文）

按键（默认，均可在 ILUtilsKeys.ini 修改）
F4    生成爆炸桶
F5    快速存档 —— 直接打开游戏自带存档界面
F6    切换 On Mission 变量（0/1）
F7    标记 / 还原任务车辆（同一个键切换）
F8    装 / 拆车炸弹（同一个键切换）
F9    车辆生成器（输入车型名或 ID）
F10   给当前所有车辆装炸弹
F11   当前车辆开启 GodBike
F12   任务选择列表（Start 或双击开始任务）
[     信息面板开关

信息面板（[）
- 实时显示：X/Y/Z 坐标、速度、生命、护甲、模型 ID、是否在车内、通缉度、任务状态；
- 速度单位 km/h：m_vecMoveSpeed 长度 × 180（1 帧 = 1/50 秒，1 单位≈1 米），步行/驾车都显示；
- 坐标输入框：支持 "x, y, z" / "x y z" / "x;y;z"，回车或点“传送”；
  车内用 SET_CAR_COORDINATES (0x00AB)，步行用 SET_PLAYER_COORDINATES (0x0055)；
- “复制坐标”：把当前坐标填进输入框并复制到剪贴板；
- 输入合法坐标后显示“距离目标”；
- 窗口可拖动（标题栏、文字区、空白处都能拖）；
- 鼠标落在任意插件窗口上时强制显示指针（30ms 定时器 + 激活时强制）。

任务选择（F12）
内置 96 个任务
- INTL / 日版分别使用对应 main.scm 的任务偏移表；
- 不建议在任务进行中再启动另一个任务（游戏可能会崩）。

标记车辆（F7）
- 按一次：把当前车辆标记为任务车辆（不会被交通清理掉）；
- 同一辆车再按一次：还原为普通交通车辆；
- 换车后按 F7 会标记新车。

汉化说明
- 任务名来自“无名汉化”版 VC ）；
- 界面文字为 GBK/ANSI，直接用 CreateWindowA 显示，不需要改系统区域设置；
- 想改任务名：编辑 src\Main_CN.cpp 里的 MISSION_NAMES 后重新编译。

兼容性
INTL 1.0（PE 时间戳 0x504D6947）与日版（0x40758BC6），插件自动识别。

编译
英文版：
  cl /nologo /utf-8 /EHsc /O2 /MT /LD Main.cpp /link /MACHINE:X86 user32.lib comctl32.lib gdi32.lib /OUT:VC-IL-Utils.asi

汉化版：
  cl /nologo /EHsc /O2 /MT /LD Main.cpp /link /MACHINE:X86 user32.lib comctl32.lib gdi32.lib /OUT:VC-IL-Utils-CN.asi

（汉化版源码是 GBK，不能加 /utf-8）
