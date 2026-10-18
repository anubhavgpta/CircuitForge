#pragma once
#include <QApplication>
#include <QWidget>
namespace Desktop {
void ApplyTheme(QApplication &App, bool Dark = true);
void ApplyNativeFrame(QWidget* Window, bool Dark);
}
