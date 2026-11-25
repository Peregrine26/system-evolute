#pragma once
#include "desktop/search.h"
namespace crossdesk::desktop { QList<SearchHit> installedApplications(); bool openApplication(const QString& location); }
