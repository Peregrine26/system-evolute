#pragma once
#include "desktop/store.h"
#include <atomic>
#include <functional>
#include <QStringList>
class QWidget;
namespace crossdesk::desktop {
// The cancellation callback is polled between 1 MiB chunks. Targets are
// committed only after checksum verification; pre-existing targets are refused.
using TransferProgress = std::function<void(qint64, qint64, const QString&)>;
FileResult copyPathControlled(const QString& source, const QString& destination,
                             const std::atomic_bool& cancelled,
                             const TransferProgress& progress = {});
bool createFileShortcut(const QString& source, const QString& destination, QString* error = nullptr);
struct TransferSummary { int completed = 0; QStringList errors; bool cancelled = false; };
TransferSummary transferFiles(Store& store, const QStringList& sources, const QString& directory,
                              bool move, QWidget* parent);
}
