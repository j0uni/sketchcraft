// SketchCraft: a 3D modeller that opens, draws, edits and saves SketchUp 2017 (.skp) models.
//   sketchcraft [model.skp] [--screenshot out.png]

#include "ui/MainWindow.h"
#include "ui/PluginPath.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QSurfaceFormat>

int main(int argc, char** argv) {
    ui::addBuildTimePluginPath();
    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    fmt.setVersion(2, 1);
    fmt.setProfile(QSurfaceFormat::NoProfile);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SketchCraft"));
    QApplication::setOrganizationName(QStringLiteral("SketchCraft"));

    QCommandLineParser args;
    args.setApplicationDescription(QStringLiteral("3D modeller for .skp files"));
    args.addHelpOption();
    const QCommandLineOption shot(QStringLiteral("screenshot"),
                                  QStringLiteral("Draw about 120 frames, save the window as a PNG and quit."),
                                  QStringLiteral("file.png"));
    args.addOption(shot);
    args.addPositionalArgument(QStringLiteral("model"), QStringLiteral("A .skp file to open."));
    args.process(app);

    ui::MainWindow w;
    w.show();
    const QStringList files = args.positionalArguments();
    w.startWith(files.isEmpty() ? QString() : files.front());
    if (args.isSet(shot)) w.screenshotAndQuit(args.value(shot), 120);
    return QApplication::exec();
}
