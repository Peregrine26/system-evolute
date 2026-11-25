#include "desktop/language.h"
#include <QApplication>
#include <QHash>
#include <QTranslator>
#include <QLocale>
namespace crossdesk::desktop {
namespace {
class English final : public QTranslator {
public:
    explicit English(QObject* parent) : QTranslator(parent) {}
    bool isEmpty() const override { return false; }
    QString translate(const char*, const char* source, const char*, int) const override {
        static const QHash<QString, QString> text{
            {"让桌面各归其位", "A place for everything"}, {"桌面", "Desktop"}, {"设置与备份", "Settings and backup"},
            {"文件格子", "Files"}, {"待办", "Tasks"}, {"随记", "Notes"}, {"时光", "Clock"}, {"天气", "Weather"}, {"音乐", "Music"}, {"搜索", "Search"},
            {"显示所选", "Show selected"}, {"合成标签组", "Group in tabs"}, {"拆分标签组", "Ungroup"}, {"删除格子", "Remove widgets"}, {"窗口总览", "Window overview"},
            {"显示全部格子", "Show all widgets"}, {"收起 / 显示所有格子", "Hide / show all widgets"}, {"整理文件夹…", "Organize folder…"}, {"撤销上次文件移动", "Undo last file move"}, {"退出", "Quit"},
            {"偏好设置", "Preferences"}, {"导出备份…", "Export backup…"}, {"恢复备份…", "Restore backup…"}, {"打开数据目录", "Open data folder"}, {"打开总览", "Open dashboard"},
            {"字体、配色与背景", "Fonts, colors and background"}, {"性能与诊断", "Performance and diagnostics"}, {"收起", "Collapse"}, {"展开", "Expand"}, {"置顶", "Always on top"}, {"名称", "Name"}, {"外观", "Appearance"},
            {"格子名称", "Widget name"}, {"格子组", "Widget group"}, {"浅色", "Light"}, {"深色", "Dark"}, {"格子不透明度", "Widget opacity"}, {"悬停展开收起的格子", "Expand collapsed widgets on hover"},
            {"关闭总览时保留托盘", "Keep running in tray when closed"}, {"登录 Windows 时启动", "Start with Windows"}, {"总览快捷键", "Dashboard shortcut"}, {"格子快捷键", "Widgets shortcut"}, {"随记快捷键", "Notes shortcut"}, {"搜索快捷键", "Search shortcut"},
            {"%1 个格子 · %2 条待办 · %3 篇随记", "%1 widgets · %2 tasks · %3 notes"}, {"添加待办", "Add a task"}, {"添加", "Add"}, {"提醒", "Remind"}, {"不重复", "No repeat"}, {"每天", "Daily"}, {"每周", "Weekly"},
            {"全部", "All"}, {"未完成", "Pending"}, {"已完成", "Completed"}, {"已过期", "Overdue"}, {"编辑", "Edit"}, {"附件", "Attachments"}, {"明天", "Tomorrow"}, {"转为随记", "Convert to note"}, {"删除", "Delete"},
            {"编辑待办", "Edit task"}, {"任务", "Task"}, {"截止时间", "Due date"}, {"到期提醒", "Remind when due"}, {"重复", "Repeat"}, {"备注", "Notes"}, {"新随记", "New note"}, {"转为待办", "Convert to task"}, {"预览", "Preview"},
            {"导出 Markdown", "Export Markdown"}, {"导出随记", "Export note"}, {"并发编辑副本", "Conflicting edit copy"}, {"添加随记附件", "Add note attachment"}, {"添加附件", "Add attachment"}, {"打开附件", "Open attachment"},
            {"选择", "Choose"}, {"上级", "Up"}, {"新建", "New"}, {"图标", "Icons"}, {"列表", "List"}, {"大小", "Size"}, {"类型", "Type"}, {"日期", "Date"}, {"新建文件夹", "New folder"}, {"映射文件夹", "Map folder"},
            {"选择要映射的文件夹", "Choose a folder to map"}, {"打开", "Open"}, {"复制", "Copy"}, {"剪切", "Cut"}, {"重命名", "Rename"}, {"移至…", "Move to…"}, {"移动到文件夹", "Move to folder"},
            {"移到回收站", "Move to Recycle Bin"}, {"打开方式", "Open with"}, {"属性", "Properties"}, {"系统右键菜单", "System context menu"}, {"粘贴副本", "Paste copy"}, {"导入文件…", "Import files…"}, {"导入文件", "Import files"},
            {"在资源管理器中打开", "Open in Explorer"}, {"整理当前文件夹…", "Organize current folder…"}, {"文件夹", "Folder"}, {"图片", "Images"}, {"文档", "Documents"}, {"影音", "Media"}, {"压缩包", "Archives"}, {"快捷方式", "Shortcuts"}, {"其他", "Other"},
            {"整理预览", "Organization preview"}, {"原文件", "Source file"}, {"目标目录", "Destination folder"}, {"现有格子 · ", "Widget · "},
            {"收藏查询", "Save search"}, {"收藏夹", "Favorites"}, {"搜索目录", "Search folders"}, {"清除历史", "Clear history"}, {"文件", "File"}, {"应用", "App"}, {"设置", "Settings"},
            {"位置", "Location"}, {"修改时间", "Modified"}, {"移除当前查询", "Remove current search"}, {"输入关键词开始搜索", "Enter a query to search"}, {"正在搜索…", "Searching…"}, {"搜索文件、应用、设置、待办、随记", "Search files, apps, settings, tasks and notes"},
            {"额外搜索目录", "Additional search folders"}, {"添加目录", "Add folder"}, {"移除所选目录", "Remove selected folder"}, {"结果数量上限", "Result limit"}, {"保存", "Save"}, {"打开所选", "Open selected"}, {"复制位置", "Copy location"},
            {"复制文件", "Copy files"}, {"在文件夹中显示", "Show in folder"}, {"文件移到回收站", "Move files to Recycle Bin"}, {"确认回收", "Confirm recycling"},
            {"语法：ext:pdf、type:folder、name:报告、\"完整短语\"、-排除词；Ctrl/Shift 多选", "Syntax: ext:pdf type:folder name:report \"exact phrase\" -exclude; Ctrl/Shift to select multiple"},
            {"已找到 %1 项，继续搜索…", "%1 results, searching…"}, {"%1 项结果 · 检查 %2 个文件项目%3", "%1 results · %2 file entries checked%3"}, {"（达到数量上限）", " (result limit reached)"},
            {"此格子使用独立外观", "Use a separate appearance for this widget"}, {"字体", "Font"}, {"文字大小", "Text size"}, {"文字颜色", "Text color"}, {"背景颜色", "Background color"}, {"跟随主题", "Follow theme"},
            {"恢复主题配色", "Reset theme colors"}, {"背景图片", "Background image"}, {"选择图片", "Choose image"}, {"填充", "Fill"}, {"适应", "Fit"}, {"拉伸", "Stretch"}, {"图片适配", "Image fit"}, {"图片透明度", "Image opacity"}, {"图标文字或图片路径", "Icon text or image path"},
            {"可重建磁盘缓存", "Rebuildable disk cache"}, {"均衡", "Balanced"}, {"节省资源", "Save resources"}, {"自定义", "Custom"}, {"资源策略", "Resource policy"}, {"图片缓存上限", "Image cache limit"}, {"隐藏后释放图片（0 为保留）", "Release hidden images after (0 to keep)"},
            {"减少持续动画", "Reduce continuous animation"}, {"清理可重建缓存", "Clear rebuildable cache"}, {"导出诊断信息", "Export diagnostics"}, {"诊断已导出，不包含文件路径和记录正文", "Diagnostics exported without file paths or record content"},
            {"语言（重启生效）", "Language (restart required)"}, {"待办提醒", "Task reminder"}, {" 秒", " seconds"},
            {"刷新 · Ctrl+R", "Refresh · Ctrl+R"}, {"定位 · Enter", "Activate · Enter"}, {"等宽 50:50", "Equal 50:50"}, {"左侧主窗口 60:40", "Main left 60:40"}, {"确认排列 · Ctrl+Enter", "Arrange · Ctrl+Enter"},
            {"窗口", "Window"}, {"进程", "Process"}, {"边界", "Bounds"}, {"请选择一个窗口", "Select one window"}, {"请选择两个不同窗口", "Select two different windows"}, {"已定位窗口", "Window activated"},
#include "desktop/language_en.inc"
        };
        return text.value(QString::fromUtf8(source));
    }
};
}
void initializeLanguage(const QString& code) {
    if (code == "en") { QLocale::setDefault(QLocale(QLocale::English)); qApp->installTranslator(new English(qApp)); }
}
}
