#pragma once
#include "Integration/EngineRunner.h"
#include <QWidget>
namespace Desktop {
class BenchmarkChart : public QWidget {
  public:
    explicit BenchmarkChart(QWidget *Parent = nullptr);
    CsvTable Data;
    QString Axis = "gates";
    void SetData(const CsvTable &Value) {
        Data = Value;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;

  private:
    QList<QPair<QRectF, QString>> Hits;
};
} // namespace Desktop
