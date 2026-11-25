#include "desktop/search.h"
#include "desktop/page_factories.h"
#include "desktop/appearance.h"
#include "desktop/search_apps.h"
#include <QApplication>
#include <QLineEdit>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <iostream>
using namespace crossdesk::desktop;
int main(int argc, char** argv) {
    QApplication app(argc, argv); bool ok = true;
    if (app.arguments().contains("--apps-readonly")) {
        const auto apps = installedApplications();
        std::cout << "Native application catalog entries=" << apps.size() << '\n';
        return apps.isEmpty() ? 1 : 0;
    }
    auto check = [&](bool value, const char* name) { if (!value) { std::cerr << name << '\n'; ok = false; } };
    SearchHit hit{QStringLiteral("项目 报告.pdf"), "file", "C:/fixture/reports", {}, {}, 42};
    check(matchesSearch(hit, "ext:pdf type:file \"项目 报告\" -draft"), "combined query failed");
    check(!matchesSearch(hit, "ext:docx"), "extension filter failed");
    check(!matchesSearch(hit, "-reports"), "exclusion failed");
    check(!matchesSearch(hit, "type:folder"), "type filter failed");
    check(matchesSearch(hit, "name:报告"), "name filter failed");
    QTemporaryDir dir; Store store(dir.path()); store.load();
    store.setRecords("todos", {QJsonObject{{"id", "b70f982e-81bc-4f78-9d59-fb48ba99b354"}, {"title", "SearchFixtureUnique"}, {"notes", "body"}}});
    std::unique_ptr<QWidget> page(createSearchPage(store, [](const QString&) {}));
    auto* query = page->findChild<QLineEdit*>("searchQuery");
    auto* results = page->findChild<QTreeWidget*>("searchResults");
    check(query && results, "search controls missing"); if (!query || !results) return 1;
    query->setText("SearchFixtureUnique type:todo");
    QMetaObject::invokeMethod(query, "returnPressed", Qt::DirectConnection);
    check(results->topLevelItemCount() == 1 && results->topLevelItem(0)->text(1) == "todo", "record query missing");
    check(!store.settings()["searchHistory"].toArray().isEmpty(), "history not persisted");
    query->clear(); QMetaObject::invokeMethod(query, "returnPressed", Qt::DirectConnection);
    check(results->topLevelItemCount() == 0, "stale results remained after clear");
    const auto snapshot = QJsonDocument(diagnosticSnapshot(store)).toJson();
    check(!snapshot.contains("SearchFixtureUnique") && !snapshot.contains(dir.path().toUtf8()), "diagnostics exposed private data");
    QDir().mkpath(dir.path() + "/cache/weather");
    { QFile file(dir.path() + "/cache/weather/data"); file.open(QIODevice::WriteOnly); file.write("cache"); }
    { QFile file(dir.path() + "/keep.txt"); file.open(QIODevice::WriteOnly); file.write("keep"); }
    check(cacheSize(dir.path()) == 5 && clearManagedCache(dir.path()) && cacheSize(dir.path()) == 0 && QFile::exists(dir.path() + "/keep.txt"), "cache clearing crossed boundary");
    QJsonObject appearance{{"background", "/nonexistent"}, {"fontSize", 1000}};
    check(appearanceImage(appearance).isNull() && appearanceStyle(appearance, "QWidget").contains("32px"), "appearance bounds invalid");
    return ok ? 0 : 1;
}
