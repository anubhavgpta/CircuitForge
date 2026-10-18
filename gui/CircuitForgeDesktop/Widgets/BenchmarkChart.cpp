#include "BenchmarkChart.h"
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <algorithm>
namespace Desktop {
BenchmarkChart::BenchmarkChart(QWidget *Parent) : QWidget(Parent) {
    setMinimumSize(250, 160);
    setMouseTracking(true);
}
void BenchmarkChart::paintEvent(QPaintEvent *) {
    QPainter P(this);
    P.setRenderHint(QPainter::Antialiasing);
    Hits.clear();
    P.setPen(palette().color(QPalette::WindowText));
    if (Data.Rows.isEmpty()) {
        P.drawText(rect().adjusted(15, 15, -15, -15), Qt::AlignCenter | Qt::TextWordWrap,
                   "Run a benchmark or import a benchmark CSV.\nCompare complete workloads, "
                   "including transfers.");
        return;
    }
    const QStringList Keys = {"scalar_ms", "cpu_packed_ms", "end_to_end_ms"};
    const QStringList Labels = {"Scalar CPU", "Packed CPU", "GPU end-to-end"};
    const QList<QColor> Colors = {QColor("#9299AA"), QColor("#4BCBA5"), QColor("#7C8CFF")};
    double Max = 0;
    int Count = qMin(24, int(Data.Rows.size()));
    for (int R = 0; R < Count; ++R)
        for (int K = 0; K < 3; ++K) {
            if (K == 2 && Data.Value(R, "gpu_kernel_ms").isEmpty())
                continue;
            Max = qMax(Max, Data.Value(R, Keys[K]).toDouble());
        }
    if (Max <= 0)
        Max = 1;
    QRectF Plot(50, 35, width() - 65, height() - 70);
    P.setPen(QColor("#626C84"));
    P.drawLine(Plot.bottomLeft(), Plot.bottomRight());
    P.drawText(5, 32, QString::number(Max, 'g', 3) + " ms");
    for (int K = 0; K < 3; ++K) {
        P.setPen(Colors[K]);
        P.drawText(12 + K * (width() / 3), 16, Labels[K]);
    }
    double Group = Plot.width() / Count;
    for (int R = 0; R < Count; ++R) {
        for (int K = 0; K < 3; ++K) {
            if (K == 2 && Data.Value(R, "gpu_kernel_ms").isEmpty())
                continue;
            double Value = Data.Value(R, Keys[K]).toDouble();
            QRectF Bar(Plot.left() + R * Group + K * Group / 3 + 2,
                       Plot.bottom() - Value / Max * Plot.height(), qMax(2.0, Group / 3 - 4),
                       Value / Max * Plot.height());
            P.setPen(Qt::NoPen);
            P.setBrush(Colors[K]);
            P.drawRoundedRect(Bar, 3, 3);
            QString Tip = Labels[K] + ": " + QString::number(Value, 'g', 6) +
                          " ms\nGates: " + Data.Value(R, "gates") +
                          " · patterns: " + Data.Value(R, "patterns") +
                          "\nBlock: " + Data.Value(R, "block_size") +
                          " · dropping: " + Data.Value(R, "dropping") +
                          "\nPacked CPU / GPU end-to-end: " + Data.Value(R, "cpu_over_gpu");
            Hits << qMakePair(Bar.adjusted(0, -4, 0, 4), Tip);
        }
        P.setPen(palette().color(QPalette::WindowText));
        P.drawText(QRectF(Plot.left() + R * Group, Plot.bottom() + 5, Group, 20), Qt::AlignCenter,
                   Data.Value(R, Axis));
    }
}
void BenchmarkChart::mouseMoveEvent(QMouseEvent *E) {
    for (const auto &Hit : Hits)
        if (Hit.first.contains(E->position())) {
            QToolTip::showText(E->globalPosition().toPoint(), Hit.second, this);
            return;
        }
    QToolTip::hideText();
}
} // namespace Desktop
