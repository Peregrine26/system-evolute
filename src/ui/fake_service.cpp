#include "fake_service.h"

#include <algorithm>
#include <QPainter>
#include <QPainterPath>

namespace crossdesk::ui {

namespace {

// ---- 预览图尺寸：宽 480、高 300，接近左栏卡片比例 ----
// 实际卡片大小由 CardStack 按栏宽算出，WindowCard::paintEvent 会把它 scaled 到卡片尺寸。
constexpr int kPreviewW = 480;
constexpr int kPreviewH = 300;

// 12 个"真实项目"：每个都是常见应用的标志性界面，一眼能区分。
// 不再用"第 N 个窗口 / appN.exe"这种凑数标题——空格子感正是来自那种占位。
QVector<WindowItem> makeWindows(int count) {
    // 完整的 12 个真实应用模板。count < 12 时只取前 count 个。
    struct Template {
        WindowId id;
        const char* title;
        const char* process;
        QRect bounds;
    };
    const Template tmpls[] = {
        {2,  "项目资料",                    "explorer.exe",       QRect(0,    0,   1180, 1040)},
        {4,  "CrossDesk 需求讨论 - Chrome", "chrome.exe",         QRect(1180, 0,   740,  1040)},
        {1,  "main.cpp - CrossDesk - VS Code", "code.exe",        QRect(300,  180, 900,  640)},
        {3,  "设置",                        "SystemSettings.exe", QRect(760,  300, 820,  560)},
        {5,  "Windows PowerShell",          "WindowsTerminal.exe",QRect(120,  120, 760,  520)},
        {6,  "网易云音乐",                  "cloudmusic.exe",     QRect(180,  160, 700,  500)},
        {7,  "微信",                        "WeChat.exe",         QRect(240,  200, 640,  480)},
        {8,  "未命名 - 记事本",             "notepad.exe",        QRect(300,  240, 820,  560)},
        {9,  "收件箱 - Outlook",            "OUTLOOK.EXE",        QRect(360,  280, 720,  480)},
        {10, "demo.mp4 - VLC media player", "vlc.exe",            QRect(420,  320, 640,  520)},
        {11, "无标题 - 画图",               "mspaint.exe",        QRect(160,  360, 580,  440)},
        {12, "计算器",                      "calc.exe",           QRect(480,  400, 360,  540)},
    };
    const int total = sizeof(tmpls) / sizeof(tmpls[0]);
    const int n = qBound(1, count, total);

    QVector<WindowItem> windows;
    for (int i = 0; i < n; ++i) {
        const Template& t = tmpls[i];
        WindowItem w;
        w.id = t.id;
        w.title = QString::fromUtf8(t.title);
        w.processName = QString::fromUtf8(t.process);
        w.bounds = t.bounds;
        w.hasBounds = true;
        windows.push_back(w);
    }
    return windows;
}

// ---- 预览图绘制工具 ----
// 每个应用都先画一层"窗口底"：深色背景 + 顶部标题栏（带三个圆点），
// 再在内容区画各自的标志性元素。这样 12 张图风格统一，但内容各不相同。

// 画窗口底：标题栏 + 内容区。返回内容区的 QRect（供后续画细节）。
QRect drawWindowBase(QPainter& p, int w, int h, const QString& titleText,
                     const QColor& titleBarColor = QColor(0x2b, 0x30, 0x3a)) {
    p.setRenderHint(QPainter::Antialiasing, true);
    // 内容区背景
    p.fillRect(0, 0, w, h, QColor(0x1a, 0x1e, 0x24));
    // 标题栏
    const int titleH = 26;
    p.fillRect(0, 0, w, titleH, titleBarColor);
    // 三个圆点（红黄绿，仿 macOS 窗口控件，辨识度高）
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xff, 0x5f, 0x57));
    p.drawEllipse(QPointF(12, titleH / 2.0), 5, 5);
    p.setBrush(QColor(0xfe, 0xbc, 0x2e));
    p.drawEllipse(QPointF(28, titleH / 2.0), 5, 5);
    p.setBrush(QColor(0x28, 0xc8, 0x40));
    p.drawEllipse(QPointF(44, titleH / 2.0), 5, 5);
    // 标题文字
    if (!titleText.isEmpty()) {
        QFont f = p.font();
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(QColor(0xd6, 0xda, 0xe1));
        p.drawText(QRect(60, 0, w - 70, titleH), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(f).elidedText(titleText, Qt::ElideRight, w - 70));
    }
    return QRect(0, titleH, w, h - titleH);
}

void drawTextLine(QPainter& p, int x, int y, int w, int h, const QColor& c) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRect(x, y, w, h), h / 2.0, h / 2.0);
}

// ---- 每个应用的预览 ----
QPixmap drawExplorer(int w, int h) {
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QRect body = drawWindowBase(p, w, h, "项目资料");
    // 地址栏
    p.fillRect(QRect(0, body.top(), w, 24), QColor(0x24, 0x28, 0x30));
    QFont f = p.font(); f.setPixelSize(10); p.setFont(f);
    p.setPen(QColor(0x9a, 0xa0, 0xaa));
    p.drawText(QRect(10, body.top(), w - 20, 24), Qt::AlignLeft | Qt::AlignVCenter,
               "D:\\Projects\\CrossDesk");
    // 左侧导航栏
    const int navW = w * 0.22;
    p.fillRect(QRect(0, body.top() + 24, navW, body.height() - 24), QColor(0x20, 0x24, 0x2c));
    p.setPen(Qt::NoPen);
    const char* navItems[] = {"收藏夹", "桌面", "下载", "文档", "项目资料"};
    for (int i = 0; i < 5; ++i) {
        if (i == 4) p.setBrush(QColor(0x3a, 0x42, 0x50));
        else p.setBrush(QColor(0x2a, 0x2f, 0x38));
        p.drawRoundedRect(QRect(6, body.top() + 30 + i * 22, navW - 12, 18), 4, 4);
        p.setPen(QColor(0xd6, 0xda, 0xe1));
        p.drawText(QRect(12, body.top() + 30 + i * 22, navW - 18, 18),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(navItems[i]));
        p.setPen(Qt::NoPen);
    }
    // 文件网格
    const int gridX = navW + 8;
    const int gridW = w - gridX - 4;
    const int cols = 4, rows = 3;
    const int cellW = gridW / cols, cellH = (body.height() - 30) / rows;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int x = gridX + c * cellW + 4;
            const int y = body.top() + 28 + r * cellH;
            // 文件夹图标（黄色）
            p.setBrush(QColor(0xfe, 0xbc, 0x2e));
            p.drawRoundedRect(QRect(x + cellW / 2 - 14, y, 28, 22), 3, 3);
            p.setBrush(QColor(0xe0, 0xa5, 0x26));
            p.drawRect(QRect(x + cellW / 2 - 14, y, 10, 6));
            // 文件名
            p.setPen(QColor(0x9a, 0xa0, 0xaa));
            f.setPixelSize(9); p.setFont(f);
            p.drawText(QRect(x, y + 24, cellW - 4, 12), Qt::AlignCenter,
                       QString("file_%1").arg(r * cols + c + 1));
            p.setPen(Qt::NoPen);
        }
    }
    return pm;
}

QPixmap drawChrome(int w, int h) {
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QRect body = drawWindowBase(p, w, h, "CrossDesk 需求讨论");
    // 标签栏
    const int tabH = 26;
    p.fillRect(QRect(0, body.top(), w, tabH), QColor(0x3a, 0x40, 0x4a));
    // 三个标签，第一个选中
    const int tabW = w / 3;
    for (int i = 0; i < 3; ++i) {
        if (i == 0) p.fillRect(QRect(i * tabW, body.top(), tabW - 2, tabH), QColor(0x1a, 0x1e, 0x24));
        QFont f = p.font(); f.setPixelSize(10); p.setFont(f);
        p.setPen(i == 0 ? QColor(0xf2, 0xf5, 0xf9) : QColor(0x9a, 0xa0, 0xaa));
        p.drawText(QRect(i * tabW + 8, body.top(), tabW - 16, tabH),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   i == 0 ? "需求文档" : (i == 1 ? "设计稿" : "测试"));
    }
    // 地址栏
    const int addrY = body.top() + tabH;
    p.fillRect(QRect(0, addrY, w, 24), QColor(0x24, 0x28, 0x30));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x1a, 0x1e, 0x24));
    p.drawRoundedRect(QRect(8, addrY + 3, w - 16, 18), 9, 9);
    QFont f = p.font(); f.setPixelSize(10); p.setFont(f);
    p.setPen(QColor(0x9a, 0xa0, 0xaa));
    p.drawText(QRect(16, addrY + 3, w - 32, 18), Qt::AlignLeft | Qt::AlignVCenter,
               "https://git.example.com/crossdesk/spec");
    // 网页内容
    const int contentY = addrY + 24 + 12;
    const int contentW = w - 24;
    p.setPen(QColor(0xf2, 0xf5, 0xf9));
    f.setPixelSize(13); f.setBold(true); p.setFont(f);
    p.drawText(QRect(12, contentY, contentW, 20), Qt::AlignLeft, "CrossDesk 窗口管理需求");
    f.setBold(false); f.setPixelSize(10); p.setFont(f);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 6; ++i) {
        const int lw = (i % 3 == 2) ? contentW * 0.6 : contentW * 0.9;
        drawTextLine(p, 12, contentY + 26 + i * 14, lw, 8, QColor(255, 255, 255, 28));
    }
    return pm;
}

QPixmap drawVsCode(int w, int h) {
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QRect body = drawWindowBase(p, w, h, "main.cpp - CrossDesk");
    // 活动栏（最左竖条）
    const int actW = 36;
    p.fillRect(QRect(0, body.top(), actW, body.height()), QColor(0x2d, 0x2d, 0x2d));
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 5; ++i) {
        p.setBrush(i == 0 ? QColor(0x0a, 0x84, 0xff) : QColor(0x4a, 0x4a, 0x4a));
        p.drawRect(QRect(actW / 2 - 5, body.top() + 16 + i * 28, 10, 14));
    }
    // 文件树
    const int treeW = w * 0.18;
    p.fillRect(QRect(actW, body.top(), treeW, body.height()), QColor(0x25, 0x25, 0x25));
    QFont f = p.font(); f.setPixelSize(9); p.setFont(f);
    const char* files[] = {"src/", "  main.cpp", "  cards.cpp", "  service.h", "tests/", "CMakeLists.txt"};
    for (int i = 0; i < 6; ++i) {
        if (i == 1) {
            p.setBrush(QColor(0x3a, 0x3a, 0x3a));
            p.drawRect(QRect(actW, body.top() + 12 + i * 16, treeW, 16));
        }
        p.setPen(i == 1 ? QColor(0xf2, 0xf5, 0xf9) : QColor(0x9a, 0xa0, 0xaa));
        p.drawText(QRect(actW + 6, body.top() + 12 + i * 16, treeW - 8, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(files[i]));
        p.setPen(Qt::NoPen);
    }
    // 代码区：行号 + 语法高亮
    const int codeX = actW + treeW;
    const int codeW = w - codeX;
    const int lineNoW = 30;
    p.fillRect(QRect(codeX, body.top(), lineNoW, body.height()), QColor(0x1e, 0x1e, 0x1e));
    p.fillRect(QRect(codeX + lineNoW, body.top(), codeW - lineNoW, body.height()), QColor(0x1e, 0x1e, 0x1e));
    // 代码行
    struct Line { const char* text; QColor color; };
    const Line lines[] = {
        {"#include <QPainter>", QColor(0xc5, 0x86, 0xc0)},
        {"", QColor(0,0,0)},
        {"void CardStack::relayout() {", QColor(0x56, 0x9c, 0xd6)},
        {"    const int n = m_windows.size();", QColor(0xd4, 0xd4, 0xd4)},
        {"    if (n == 0) return;", QColor(0xd4, 0xd4, 0xd4)},
        {"    const int slots = qMin(n, 4);", QColor(0xd4, 0xd4, 0xd4)},
        {"    for (int i = 0; i < n; ++i) {", QColor(0x56, 0x9c, 0xd6)},
        {"        placeCard(i, geo(i));", QColor(0xd4, 0xd4, 0xd4)},
        {"    }", QColor(0x56, 0x9c, 0xd6)},
        {"}", QColor(0x56, 0x9c, 0xd6)},
    };
    f.setPixelSize(11); p.setFont(f);
    for (int i = 0; i < 10; ++i) {
        p.setPen(QColor(0x5a, 0x5a, 0x5a));
        p.drawText(QRect(codeX + 2, body.top() + 8 + i * 16, lineNoW - 4, 16),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(i + 1));
        p.setPen(lines[i].color);
        p.drawText(QRect(codeX + lineNoW + 6, body.top() + 8 + i * 16, codeW - lineNoW - 8, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(lines[i].text));
    }
    return pm;
}

QPixmap drawTerminal(int w, int h) {
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QRect body = drawWindowBase(p, w, h, "Windows PowerShell", QColor(0x12, 0x12, 0x12));
    p.fillRect(QRect(0, body.top(), w, body.height()), QColor(0x0c, 0x0c, 0x0c));
    QFont f = p.font(); f.setPixelSize(11); f.setFamily("Consolas"); p.setFont(f);
    // 命令行
    struct Line { const char* text; QColor color; };
    const Line lines[] = {
        {"PS C:\\Projects\\CrossDesk>", QColor(0x4e, 0xc9, 0xb0)},
        {"  cmake --build build", QColor(0xd4, 0xd4, 0xd4)},
        {"  [1/1] Linking CXX executable crossdesk.exe", QColor(0x9a, 0xa0, 0xaa)},
        {"PS C:\\Projects\\CrossDesk>", QColor(0x4e, 0xc9, 0xb0)},
        {"  ./build/crossdesk.exe --fake-count 12", QColor(0xd4, 0xd4, 0xd4)},
        {"  [init] 12 windows loaded", QColor(0x6a, 0x99, 0x55)},
        {"  [ui] card stack: 4 slots, 3 layers", QColor(0x6a, 0x99, 0x55)},
        {"  [ready]", QColor(0x6a, 0x99, 0x55)},
        {"PS C:\\Projects\\CrossDesk>", QColor(0x4e, 0xc9, 0xb0)},
        {"  _", QColor(0xd4, 0xd4, 0xd4)},
    };
    for (int i = 0; i < 10; ++i) {
        p.setPen(lines[i].color);
        p.drawText(QRect(8, body.top() + 6 + i * 16, w - 16, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(lines[i].text));
    }
    return pm;
}

QPixmap drawMusic(int w, int h) {
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QRect body = drawWindowBase(p, w, h, "网易云音乐", QColor(0x2a, 0x0e, 0x1a));
    p.fillRect(QRect(0, body.top(), w, body.height()), QColor(0x1a, 0x08, 0x12));
    // 左侧封面
    const int coverW = w * 0.35;
    p.fillRect(QRect(0, body.top(), coverW, body.height()), QColor(0x22, 0x0e, 0x1a));
    // 封面圆角矩形（专辑图）
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x8e, 0x44, 0xad));
    p.drawRoundedRect(QRect(20, body.top() + 20, coverW - 40, coverW - 40), 8, 8);
    // 唱片中心圆
    p.setBrush(QColor(0x1a, 0x08, 0x12));
    p.drawEllipse(QPointF(coverW / 2.0, body.top() + coverW / 2.0), 14, 14);
    p.setBrush(QColor(0x4a, 0x2a, 0x3a));
    p.drawEllipse(QPointF(coverW / 2.0, body.top() + coverW / 2.0), 5, 5);
    // 歌名
    QFont f = p.font(); f.setPixelSize(12); f.setBold(true); p.setFont(f);
    p.setPen(QColor(0xf2, 0xf5, 0xf9));
    p.drawText(QRect(12, body.top() + coverW, coverW - 24, 18),
               Qt::AlignCenter, "夜空中最亮的星");
    f.setBold(false); f.setPixelSize(10); p.setFont(f);
    p.setPen(QColor(0x9a, 0x70, 0x8a));
    p.drawText(QRect(12, body.top() + coverW + 18, coverW - 24, 14),
               Qt::AlignCenter, "逃跑计划");
    // 播放进度条
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x4a, 0x2a, 0x3a));
    p.drawRoundedRect(QRect(20, body.bottom() - 24, coverW - 40, 4), 2, 2);
    p.setBrush(QColor(0xec, 0x40, 0x7a));
    p.drawRoundedRect(QRect(20, body.bottom() - 24, (coverW - 40) * 0.4, 4), 2, 2);
    // 右侧播放列表
    const int listX = coverW + 8;
    const int listW = w - listX;
    f.setPixelSize(11); p.setFont(f);
    const char* songs[] = {"1. 夜空中最亮的星 - 逃跑计划",
                           "2. 平凡之路 - 朴树",
                           "3. 蓝莲花 - 许巍",
                           "4. 像风一样自由 - 许巍",
                           "5. 光年之外 - 邓紫棋",
                           "6. 稻香 - 周杰伦",
                           "7. 起风了 - 买辣椒也用券"};
    for (int i = 0; i < 7; ++i) {
        if (i == 0) {
            p.setBrush(QColor(0x4a, 0x1e, 0x2e));
            p.drawRect(QRect(listX, body.top() + 10 + i * 22, listW - 4, 20));
        }
        p.setPen(i == 0 ? QColor(0xec, 0x40, 0x7a) : QColor(0xaa, 0x88, 0x9a));
        p.drawText(QRect(listX + 6, body.top() + 10 + i * 22, listW - 12, 20),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(songs[i]));
        p.setPen(Qt::NoPen);
    }
    return pm;
}

QPixmap drawWeChat(int w, int h) {
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QRect body = drawWindowBase(p, w, h, "微信", QColor(0x2a, 0x2a, 0x2a));
    // 左侧联系人列表
    const int listW = w * 0.32;
    p.fillRect(QRect(0, body.top(), listW, body.height()), QColor(0x24, 0x24, 0x24));
    p.setPen(Qt::NoPen);
    const char* names[] = {"CrossDesk 群", "张三", "产品经理", "设计师-小李", "文件传输助手"};
    const QColor avatars[] = {QColor(0x07, 0xc1, 0x60), QColor(0x4a, 0x90, 0xe2),
                              QColor(0xe2, 0x8a, 0x4a), QColor(0x9c, 0x27, 0xb0), QColor(0x60, 0x7d, 0x8b)};
    QFont f = p.font(); f.setPixelSize(10); p.setFont(f);
    for (int i = 0; i < 5; ++i) {
        if (i == 0) {
            p.setBrush(QColor(0x2e, 0x2e, 0x2e));
            p.drawRect(QRect(0, body.top() + i * 40, listW, 40));
        }
        p.setBrush(avatars[i]);
        p.drawRoundedRect(QRect(8, body.top() + i * 40 + 8, 24, 24), 6, 6);
        p.setPen(i == 0 ? QColor(0xf2, 0xf5, 0xf9) : QColor(0xaa, 0xaa, 0xaa));
        p.drawText(QRect(38, body.top() + i * 40 + 8, listW - 44, 24),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(names[i]));
        p.setPen(Qt::NoPen);
    }
    // 右侧聊天区
    const int chatX = listW;
    p.fillRect(QRect(chatX, body.top(), w - chatX, body.height()), QColor(0x1a, 0x1a, 0x1a));
    // 聊天标题
    p.fillRect(QRect(chatX, body.top(), w - chatX, 30), QColor(0x24, 0x24, 0x24));
    f.setPixelSize(12); f.setBold(true); p.setFont(f);
    p.setPen(QColor(0xf2, 0xf5, 0xf9));
    p.drawText(QRect(chatX + 12, body.top(), w - chatX - 24, 30),
               Qt::AlignLeft | Qt::AlignVCenter, "CrossDesk 群 (4)");
    // 气泡
    f.setBold(false); f.setPixelSize(10); p.setFont(f);
    // 对方消息（左，灰）
    p.setBrush(QColor(0x2e, 0x2e, 0x2e));
    p.drawRoundedRect(QRect(chatX + 30, body.top() + 42, 140, 28), 6, 6);
    p.setPen(QColor(0xd4, 0xd4, 0xd4));
    p.drawText(QRect(chatX + 38, body.top() + 42, 124, 28),
               Qt::AlignLeft | Qt::AlignVCenter, "窗口堆叠做完了没？");
    // 自己消息（右，绿）
    p.setBrush(QColor(0x07, 0xc1, 0x60));
    p.drawRoundedRect(QRect(w - 180, body.top() + 80, 160, 28), 6, 6);
    p.setPen(Qt::white);
    p.drawText(QRect(w - 172, body.top() + 80, 144, 28),
               Qt::AlignLeft | Qt::AlignVCenter, "12 个窗口都改好了");
    // 对方再回
    p.setBrush(QColor(0x2e, 0x2e, 0x2e));
    p.drawRoundedRect(QRect(chatX + 30, body.top() + 118, 180, 28), 6, 6);
    p.setPen(QColor(0xd4, 0xd4, 0xd4));
    p.drawText(QRect(chatX + 38, body.top() + 118, 164, 28),
               Qt::AlignLeft | Qt::AlignVCenter, "右键展开 Task View 呢？");
    // 输入框
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x24, 0x24, 0x24));
    p.drawRect(QRect(chatX + 4, body.bottom() - 50, w - chatX - 8, 46));
    return pm;
}

QPixmap drawNotepad(int w, int h) {
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QRect body = drawWindowBase(p, w, h, "未命名 - 记事本", QColor(0xf0, 0xf0, 0xf0));
    p.fillRect(QRect(0, body.top(), w, body.height()), Qt::white);
    // 菜单栏
    p.fillRect(QRect(0, body.top(), w, 20), QColor(0xf0, 0xf0, 0xf0));
    QFont f = p.font(); f.setPixelSize(10); p.setFont(f);
    p.setPen(QColor(0x33, 0x33, 0x33));
    const char* menus[] = {"文件(F)", "编辑(E)", "格式(O)", "查看(V)", "帮助(H)"};
    for (int i = 0; i < 5; ++i) {
        p.drawText(QRect(8 + i * 60, body.top(), 56, 20),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(menus[i]));
    }
    // 文本内容
    f.setPixelSize(12); f.setFamily("Consolas"); p.setFont(f);
    p.setPen(QColor(0x1a, 0x1a, 0x1a));
    const char* text[] = {
        "CrossDesk 开发日志",
        "",
        "1. 左栏 4 格分组：窗口 1-4 各占一格",
        "2. 第 5-8 叠在 1-4 后面，9-12 再叠一层",
        "3. 悬停任意卡/影子层 → 单独上提 10px",
        "4. 右键 → Task View（全屏、模糊背景）",
        "5. Ctrl+点第二张卡 → 两窗左右排列",
        "",
        "TODO:",
        "  - M6: DWM 真实缩略图接入",
    };
    for (int i = 0; i < 10; ++i) {
        p.drawText(QRect(12, body.top() + 28 + i * 18, w - 24, 18),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(text[i]));
    }
    return pm;
}

QPixmap drawOutlook(int w, int h) {
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QRect body = drawWindowBase(p, w, h, "收件箱 - Outlook");
    // 左侧文件夹
    const int folderW = w * 0.22;
    p.fillRect(QRect(0, body.top(), folderW, body.height()), QColor(0x20, 0x24, 0x2c));
    QFont f = p.font(); f.setPixelSize(10); p.setFont(f);
    const char* folders[] = {"收件箱 (3)", "已发送", "草稿", "已删除", "垃圾邮件"};
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 5; ++i) {
        if (i == 0) {
            p.setBrush(QColor(0x0a, 0x84, 0xff));
            p.drawRoundedRect(QRect(4, body.top() + 12 + i * 24, folderW - 8, 20), 4, 4);
            p.setPen(Qt::white);
        } else {
            p.setPen(QColor(0x9a, 0xa0, 0xaa));
        }
        p.drawText(QRect(12, body.top() + 12 + i * 24, folderW - 18, 20),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(folders[i]));
        p.setPen(Qt::NoPen);
    }
    // 中间邮件列表
    const int listX = folderW;
    const int listW = w * 0.35;
    p.fillRect(QRect(listX, body.top(), listW, body.height()), QColor(0x1b, 0x1e, 0x25));
    const char* subjects[] = {"CrossDesk 周报", "设计评审会议", "明天能看演示吗？"};
    const char* senders[] = {"产品经理", "设计师-小李", "张三"};
    for (int i = 0; i < 3; ++i) {
        if (i == 0) {
            p.setBrush(QColor(0x24, 0x2a, 0x35));
            p.drawRect(QRect(listX, body.top() + i * 60, listW, 60));
        }
        f.setPixelSize(10); f.setBold(true); p.setFont(f);
        p.setPen(i == 0 ? QColor(0xf2, 0xf5, 0xf9) : QColor(0xd6, 0xda, 0xe1));
        p.drawText(QRect(listX + 8, body.top() + i * 60 + 6, listW - 16, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(senders[i]));
        f.setBold(false); p.setFont(f);
        p.setPen(QColor(0x9a, 0xa0, 0xaa));
        p.drawText(QRect(listX + 8, body.top() + i * 60 + 22, listW - 16, 14),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(subjects[i]));
        drawTextLine(p, listX + 8, body.top() + i * 60 + 42, listW - 20, 6, QColor(255, 255, 255, 18));
        p.setPen(Qt::NoPen);
    }
    // 右侧正文
    const int detailX = listX + listW;
    p.fillRect(QRect(detailX, body.top(), w - detailX, body.height()), QColor(0x1a, 0x1e, 0x24));
    f.setPixelSize(13); f.setBold(true); p.setFont(f);
    p.setPen(QColor(0xf2, 0xf5, 0xf9));
    p.drawText(QRect(detailX + 12, body.top() + 12, w - detailX - 24, 20),
               Qt::AlignLeft, "CrossDesk 周报");
    f.setBold(false); f.setPixelSize(10); p.setFont(f);
    p.setPen(QColor(0x9a, 0xa0, 0xaa));
    p.drawText(QRect(detailX + 12, body.top() + 34, w - detailX - 24, 16),
               Qt::AlignLeft, "来自：产品经理 | 今天 10:30");
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 5; ++i) {
        const int lw = (i % 3 == 2) ? (w - detailX - 24) * 0.6 : (w - detailX - 24) * 0.9;
        drawTextLine(p, detailX + 12, body.top() + 60 + i * 16, lw, 8, QColor(255, 255, 255, 24));
    }
    return pm;
}

QPixmap drawVlc(int w, int h) {
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QRect body = drawWindowBase(p, w, h, "demo.mp4 - VLC");
    // 视频区（深色 + 一个"画面"）
    const int videoH = body.height() - 40;
    p.fillRect(QRect(0, body.top(), w, videoH), QColor(0x08, 0x08, 0x08));
    // 假画面：渐变
    QLinearGradient g(0, body.top(), w, body.top() + videoH);
    g.setColorAt(0, QColor(0x1a, 0x3a, 0x5a));
    g.setColorAt(0.5, QColor(0x2a, 0x5a, 0x8a));
    g.setColorAt(1, QColor(0x0a, 0x1a, 0x2a));
    p.fillRect(QRect(0, body.top(), w, videoH), g);
    // 中心播放按钮（VLC 橙色三角）
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xff, 0x80, 0x00));
    p.drawEllipse(QPointF(w / 2.0, body.top() + videoH / 2.0), 28, 28);
    p.setBrush(Qt::white);
    QPainterPath tri;
    tri.moveTo(w / 2.0 - 8, body.top() + videoH / 2.0 - 10);
    tri.lineTo(w / 2.0 - 8, body.top() + videoH / 2.0 + 10);
    tri.lineTo(w / 2.0 + 12, body.top() + videoH / 2.0);
    tri.closeSubpath();
    p.drawPath(tri);
    // 控制条
    const int ctrlY = body.top() + videoH;
    p.fillRect(QRect(0, ctrlY, w, 40), QColor(0x14, 0x14, 0x14));
    // 进度条
    p.setBrush(QColor(0x2a, 0x2a, 0x2a));
    p.drawRoundedRect(QRect(12, ctrlY + 10, w - 24, 6), 3, 3);
    p.setBrush(QColor(0xff, 0x80, 0x00));
    p.drawRoundedRect(QRect(12, ctrlY + 10, (w - 24) * 0.35, 6), 3, 3);
    // 时间
    QFont f = p.font(); f.setPixelSize(10); p.setFont(f);
    p.setPen(QColor(0xaa, 0xaa, 0xaa));
    p.drawText(QRect(12, ctrlY + 22, 80, 14), Qt::AlignLeft, "01:23");
    p.drawText(QRect(w - 92, ctrlY + 22, 80, 14), Qt::AlignRight, "03:58");
    // 音量图标
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xaa, 0xaa, 0xaa));
    p.drawRoundedRect(QRect(w - 140, ctrlY + 24, 4, 10), 1, 1);
    p.drawRoundedRect(QRect(w - 134, ctrlY + 20, 4, 18), 1, 1);
    p.drawRoundedRect(QRect(w - 128, ctrlY + 16, 4, 26), 1, 1);
    return pm;
}

QPixmap drawSettings(int w, int h) {
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QRect body = drawWindowBase(p, w, h, "设置");
    // 左侧分类
    const int catW = w * 0.28;
    p.fillRect(QRect(0, body.top(), catW, body.height()), QColor(0x20, 0x24, 0x2c));
    QFont f = p.font(); f.setPixelSize(11); p.setFont(f);
    const char* cats[] = {"系统", "设备", "网络和 Internet", "个性化",
                          "应用", "账户", "时间和语言", "更新和安全"};
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 8; ++i) {
        if (i == 0) {
            p.setBrush(QColor(0x0a, 0x84, 0xff));
            p.drawRoundedRect(QRect(4, body.top() + 8 + i * 26, catW - 8, 22), 4, 4);
            p.setPen(Qt::white);
        } else {
            p.setPen(QColor(0xd6, 0xda, 0xe1));
        }
        p.drawText(QRect(12, body.top() + 8 + i * 26, catW - 18, 22),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(cats[i]));
        p.setPen(Qt::NoPen);
    }
    // 右侧设置项
    const int detX = catW;
    p.fillRect(QRect(detX, body.top(), w - catW, body.height()), QColor(0x1a, 0x1e, 0x24));
    f.setPixelSize(14); f.setBold(true); p.setFont(f);
    p.setPen(QColor(0xf2, 0xf5, 0xf9));
    p.drawText(QRect(detX + 16, body.top() + 10, w - detX - 32, 22), Qt::AlignLeft, "系统");
    f.setBold(false); f.setPixelSize(10); p.setFont(f);
    // 几个设置行：每行左边文字、右边开关
    const char* labels[] = {"显示", "声音", "通知", "电源和睡眠", "存储"};
    for (int i = 0; i < 5; ++i) {
        const int y = body.top() + 44 + i * 36;
        p.setPen(QColor(0xd6, 0xda, 0xe1));
        p.drawText(QRect(detX + 16, y, 120, 28), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromUtf8(labels[i]));
        // 开关
        p.setPen(Qt::NoPen);
        const bool on = (i % 2 == 0);
        p.setBrush(on ? QColor(0x0a, 0x84, 0xff) : QColor(0x4a, 0x4a, 0x4a));
        p.drawRoundedRect(QRect(w - 60, y + 4, 36, 20), 10, 10);
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(on ? w - 28 : w - 52, y + 14), 8, 8);
        // 分隔线
        p.setBrush(QColor(0x2a, 0x2e, 0x36));
        p.drawRect(QRect(detX + 8, y + 30, w - detX - 16, 1));
    }
    return pm;
}

QPixmap drawPaint(int w, int h) {
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QRect body = drawWindowBase(p, w, h, "无标题 - 画图", QColor(0xe8, 0xe8, 0xe8));
    p.fillRect(QRect(0, body.top(), w, body.height()), QColor(0xf5, 0xf5, 0xf5));
    // 工具栏（顶部）
    p.fillRect(QRect(0, body.top(), w, 28), QColor(0xe0, 0xe0, 0xe0));
    // 颜色块
    p.setPen(Qt::NoPen);
    const QColor colors[] = {Qt::black, Qt::red, QColor(0x07,0xc1,0x60), Qt::blue,
                             QColor(0xfe,0xbc,0x2e), Qt::magenta, Qt::cyan, Qt::darkGray};
    for (int i = 0; i < 8; ++i) {
        p.setBrush(colors[i]);
        p.drawRect(QRect(8 + i * 22, body.top() + 6, 16, 16));
    }
    // 画布
    p.setBrush(Qt::white);
    p.drawRect(QRect(12, body.top() + 34, w - 24, body.height() - 46));
    // 画几个图形（看起来像在画东西）
    QPen pen(Qt::black, 2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    // 一个矩形
    p.drawRect(QRect(w / 2 - 60, body.top() + 50, 120, 80));
    // 一个椭圆
    p.setBrush(QColor(0xfe, 0xbc, 0x2e));
    p.drawEllipse(QRect(w / 2 - 40, body.top() + 70, 60, 40));
    p.setBrush(Qt::NoBrush);
    // 一条线
    p.drawLine(w / 2 - 80, body.top() + 150, w / 2 + 80, body.top() + 150);
    // 文字
    QFont f = p.font(); f.setPixelSize(11); p.setFont(f);
    p.setPen(QColor(0x33, 0x33, 0x33));
    p.drawText(QRect(w / 2 - 40, body.top() + 160, 80, 20), Qt::AlignCenter, "CrossDesk");
    return pm;
}

QPixmap drawCalc(int w, int h) {
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QRect body = drawWindowBase(p, w, h, "计算器", QColor(0x1a, 0x1a, 0x1a));
    p.fillRect(QRect(0, body.top(), w, body.height()), QColor(0x0c, 0x0c, 0x0c));
    // 显示屏
    p.fillRect(QRect(8, body.top() + 8, w - 16, 48), QColor(0x1a, 0x1a, 0x1a));
    QFont f = p.font(); f.setPixelSize(20); f.setBold(true); p.setFont(f);
    p.setPen(QColor(0xf2, 0xf5, 0xf9));
    p.drawText(QRect(16, body.top() + 8, w - 32, 48), Qt::AlignRight | Qt::AlignVCenter, "1,024");
    // 按钮网格
    f.setBold(false); f.setPixelSize(12); p.setFont(f);
    const char* btns[] = {"%", "CE", "C", "\u232B",
                          "1/x", "x\u00B2", "\u221Ax", "\u00F7",
                          "7", "8", "9", "\u00D7",
                          "4", "5", "6", "\u2212",
                          "1", "2", "3", "+",
                          "\u00B1", "0", ".", "="};
    const int cols = 4;
    const int btnW = (w - 24) / cols;
    const int btnH = (body.height() - 72) / 6;
    for (int i = 0; i < 24; ++i) {
        const int r = i / cols, c = i % cols;
        const int x = 8 + c * (btnW + 4);
        const int y = body.top() + 64 + r * (btnH + 4);
        if (i == 23) {
            // = 号：橙色、跨一行高
            p.setBrush(QColor(0x0a, 0x84, 0xff));
        } else if (i < 8) {
            p.setBrush(QColor(0x2a, 0x2a, 0x2a));
        } else if (i == 7 || i == 11 || i == 15 || i == 19) {
            p.setBrush(QColor(0x3a, 0x3a, 0x3a));
        } else {
            p.setBrush(QColor(0x1a, 0x1a, 0x1a));
        }
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRect(x, y, btnW, btnH), 4, 4);
        p.setPen(QColor(0xf2, 0xf5, 0xf9));
        p.drawText(QRect(x, y, btnW, btnH), Qt::AlignCenter,
                   QString::fromUtf8(btns[i]));
    }
    return pm;
}

// 按 processName 派发到对应的绘制函数。
QPixmap makePreviewFor(const WindowItem& w) {
    const int W = kPreviewW, H = kPreviewH;
    const QString& proc = w.processName.toLower();
    if (proc == "explorer.exe")        return drawExplorer(W, H);
    if (proc == "chrome.exe")          return drawChrome(W, H);
    if (proc == "code.exe")            return drawVsCode(W, H);
    if (proc == "windowsterminal.exe") return drawTerminal(W, H);
    if (proc == "cloudmusic.exe")      return drawMusic(W, H);
    if (proc == "wechat.exe")          return drawWeChat(W, H);
    if (proc == "notepad.exe")         return drawNotepad(W, H);
    if (proc == "outlook.exe")         return drawOutlook(W, H);
    if (proc == "vlc.exe")             return drawVlc(W, H);
    if (proc == "systemsettings.exe")  return drawSettings(W, H);
    if (proc == "mspaint.exe")         return drawPaint(W, H);
    if (proc == "calc.exe")            return drawCalc(W, H);
    // 兜底：通用窗口占位（不应该走到，12 个都覆盖了）。
    QPixmap pm(W, H);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    drawWindowBase(p, W, H, w.title);
    return pm;
}

QHash<WindowId, QPixmap> makePreviews(const QVector<WindowItem>& windows) {
    QHash<WindowId, QPixmap> out;
    for (const auto& w : windows) {
        out.insert(w.id, makePreviewFor(w));
    }
    return out;
}

}  // namespace

FakeService::FakeService(QObject* parent, int windowCount)
    : CrossDeskService(parent),
      m_windows(makeWindows(windowCount)),
      m_windowCount(windowCount),
      m_previews(makePreviews(m_windows)),
      m_workArea(0, 0, 1920, 1040) {}

QVector<WindowItem> FakeService::windows() const {
    return m_windows;
}

QRect FakeService::workArea() const {
    return m_workArea;
}

QPixmap FakeService::windowPreview(WindowId window) const {
    return m_previews.value(window);
}

void FakeService::refresh() {
    emit windowsChanged();
}

void FakeService::activateWindow(WindowId window) {
    const auto it = std::find_if(m_windows.begin(), m_windows.end(),
                                 [window](const WindowItem& item) { return item.id == window; });

    if (it == m_windows.end()) {
        emit operationFinished({window, false, QStringLiteral("这个窗口已经关闭，无法定位。")});
        return;
    }

    // M2 的失败路径：系统设置这类窗口拒绝被第三方置前。
    if (it->processName == QStringLiteral("SystemSettings.exe")) {
        emit operationFinished(
            {window, false,
             QStringLiteral("Windows 拒绝把「%1」置于前台（系统窗口限制）。").arg(it->title)});
        return;
    }

    emit operationFinished({window, true, QStringLiteral("已切到「%1」。").arg(it->title)});
}

void FakeService::placeWindow(WindowId window, const QRect& target) {
    const auto it = std::find_if(m_windows.begin(), m_windows.end(),
                                 [window](const WindowItem& item) { return item.id == window; });

    if (it == m_windows.end()) {
        emit operationFinished({window, false, QStringLiteral("这个窗口已经关闭，无法移动。")});
        return;
    }

    it->bounds = target;
    it->hasBounds = true;
    emit operationFinished(
        {window, true,
         QStringLiteral("已把「%1」放到 (%2, %3)，%4×%5。")
             .arg(it->title)
             .arg(target.left())
             .arg(target.top())
             .arg(target.width())
             .arg(target.height())});
}

void FakeService::simulateClose(WindowId window) {
    const auto it = std::find_if(m_windows.begin(), m_windows.end(),
                                 [window](const WindowItem& item) { return item.id == window; });
    if (it == m_windows.end()) {
        return;
    }
    m_windows.erase(it);
    m_previews.remove(window);
}

void FakeService::reset() {
    m_windows = makeWindows(m_windowCount);
    m_previews = makePreviews(m_windows);
}

}  // namespace crossdesk::ui
