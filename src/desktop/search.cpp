#include <QCoreApplication>
#include "desktop/search.h"
#include <QRegularExpression>
namespace crossdesk::desktop {
bool matchesSearch(const SearchHit& hit, const QString& query) {
    static const QRegularExpression tokens(QStringLiteral("(?:[^\\s\"]+|\"[^\"]*\")+"));
    auto parts = tokens.globalMatch(query);
    while (parts.hasNext()) {
        QString token = parts.next().captured(); bool negative = token.startsWith('-');
        if (negative) token.remove(0, 1);
        token.remove('"'); if (token.isEmpty()) continue;
        bool match;
        if (token.startsWith("type:", Qt::CaseInsensitive)) match = hit.kind.compare(token.mid(5), Qt::CaseInsensitive) == 0;
        else if (token.startsWith("ext:", Qt::CaseInsensitive)) match = hit.name.endsWith('.' + token.mid(4), Qt::CaseInsensitive);
        else if (token.startsWith("name:", Qt::CaseInsensitive)) match = hit.name.contains(token.mid(5), Qt::CaseInsensitive);
        else match = (hit.name + '\n' + hit.location).contains(token, Qt::CaseInsensitive);
        if (negative ? match : !match) return false;
    }
    return true;
}
QList<SearchHit> systemSearchEntries() {
    QList<SearchHit> result;
#ifdef Q_OS_WIN
    const QList<QPair<QString, QString>> items{
        {QCoreApplication::translate("CrossDesk", "显示 Display"), "display"}, {QCoreApplication::translate("CrossDesk", "声音 Sound"), "sound"},
        {QCoreApplication::translate("CrossDesk", "通知 Notifications"), "notifications"}, {QCoreApplication::translate("CrossDesk", "电源 Power"), "powersleep"},
        {QCoreApplication::translate("CrossDesk", "存储 Storage"), "storagesense"}, {QCoreApplication::translate("CrossDesk", "关于 About"), "about"},
        {QCoreApplication::translate("CrossDesk", "蓝牙 Bluetooth"), "bluetooth"}, {QCoreApplication::translate("CrossDesk", "打印机 Printers"), "printers"},
        {QCoreApplication::translate("CrossDesk", "鼠标 Mouse"), "mousetouchpad"}, {QCoreApplication::translate("CrossDesk", "网络 Network"), "network-status"},
        {QStringLiteral("Wi-Fi"), "network-wifi"}, {QCoreApplication::translate("CrossDesk", "代理 Proxy"), "network-proxy"},
        {QCoreApplication::translate("CrossDesk", "背景 Background"), "personalization-background"}, {QCoreApplication::translate("CrossDesk", "颜色 Colors"), "colors"},
        {QCoreApplication::translate("CrossDesk", "主题 Themes"), "themes"}, {QCoreApplication::translate("CrossDesk", "任务栏 Taskbar"), "taskbar"},
        {QCoreApplication::translate("CrossDesk", "应用 Apps"), "appsfeatures"}, {QCoreApplication::translate("CrossDesk", "默认应用 Default apps"), "defaultapps"},
        {QCoreApplication::translate("CrossDesk", "启动应用 Startup"), "startupapps"}, {QCoreApplication::translate("CrossDesk", "账户 Accounts"), "yourinfo"},
        {QCoreApplication::translate("CrossDesk", "日期时间 Date time"), "dateandtime"}, {QCoreApplication::translate("CrossDesk", "语言 Language"), "regionlanguage"},
        {QCoreApplication::translate("CrossDesk", "辅助功能 Accessibility"), "easeofaccess"}, {QCoreApplication::translate("CrossDesk", "隐私 Privacy"), "privacy"},
        {QCoreApplication::translate("CrossDesk", "Windows 更新 Update"), "windowsupdate"}, {QCoreApplication::translate("CrossDesk", "恢复 Recovery"), "recovery"},
        {QCoreApplication::translate("CrossDesk", "安全中心 Security"), "windowsdefender"}, {QCoreApplication::translate("CrossDesk", "游戏 Game mode"), "gaming-gamemode"}
    };
    for (const auto& item : items) result.append({item.first, "settings", "ms-settings:" + item.second, {}, {}, 0});
#endif
    return result;
}
}
