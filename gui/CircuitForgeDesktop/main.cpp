#include "App/MainWindow.h"
#include "App/Theme.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
int main(int Argc, char **Argv) {
    QApplication App(Argc, Argv);
    App.setOrganizationName("CircuitForge");
    App.setApplicationName("CircuitForge Desktop");
    QCommandLineParser Parser;
    Parser.setApplicationDescription(
        "Native CircuitForge circuit editor and CUDA engine workspace");
    Parser.addHelpOption();
    Parser.addOption({"open", "Open a netlist", "path"});
    Parser.process(App);
    Desktop::ApplyTheme(App);
    Desktop::MainWindow Window;
    Window.show();
    if (Parser.isSet("open"))
        QTimer::singleShot(0, &Window, [&] { Window.OpenPath(Parser.value("open")); });
    return App.exec();
}
