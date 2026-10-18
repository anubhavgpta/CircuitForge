#pragma once
#include "Models/CircuitModel.h"
#include <QGraphicsObject>
#include <QGraphicsView>
#include <QListWidget>
namespace Desktop {
class GateItem : public QGraphicsObject {
  public:
    explicit GateItem(const Node &Value);
    Node Value;
    bool Highlighted = false;
    QRectF boundingRect() const override {
        return {-10, -10, 160, 102};
    }
    QPointF Pin(int Index) const; // -1 is the output pin.
    void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override;

  protected:
    QVariant itemChange(GraphicsItemChange Change, const QVariant &Value) override;
};
class CircuitView : public QGraphicsView {
    Q_OBJECT
  public:
    explicit CircuitView(CircuitModel *Model, QWidget *Parent = nullptr);
    ~CircuitView() override;
    CircuitModel *Model;
    QMap<int, GateItem *> Gates;
    void Rebuild();
    void UpdateWires();
    void Fit();
    void Highlight(const QString &Wire);
    void DeleteSelection();
    bool Snap = true;
  signals:
    void Selection(int Id);
    void Message(const QString &Text, bool Error);

  protected:
    void drawBackground(QPainter *, const QRectF &) override;
    void drawForeground(QPainter *, const QRectF &) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

  private:
    QPair<int, int> HitPin(QPoint Position) const;
    void FinishWire(QPair<int, int> Pin);
    int Pending = -1;
    bool Panning = false;
    QPoint PanStart;
    QString HighlightWire;
};
class GateLibrary : public QListWidget {
    Q_OBJECT
  public:
    explicit GateLibrary(QWidget *Parent = nullptr);

  protected:
    void startDrag(Qt::DropActions) override;
};
} // namespace Desktop
