#pragma once
// Call before creating the application object: adds the Qt plugin folder found at build time, for Qt
// installs whose own reported plugin folder is missing.

#include <QCoreApplication>
#include <QDir>
#include <QString>

namespace ui {

inline void addBuildTimePluginPath() {
#ifdef CRAFT_QT_PLUGIN_DIR
    const QString dir = QStringLiteral(CRAFT_QT_PLUGIN_DIR);
    if (QDir(dir).exists()) QCoreApplication::addLibraryPath(dir);
#endif
}

}  // namespace ui
