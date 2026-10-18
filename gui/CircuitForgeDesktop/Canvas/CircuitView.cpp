#include "CircuitView.h"
#include <QApplication>
#include <QDrag>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <cmath>
namespace Desktop {
GateItem::GateItem(const Node &N) : Value(N) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setPos(N.Position);
    setZValue(2);
    setToolTip(N.Type + " · " + N.Name + "\nClick an output pin, then an input pin to connect.");
}
QPointF GateItem::Pin(int Index) const {
    if (Index < 0)
        return {140, 40};
    return {0, Value.Inputs.size() == 1 ? 40.0 : 25.0 + Index * 30};
}
void GateItem::paint(QPainter *P, const QStyleOptionGraphicsItem *, QWidget *) {
    P->setRenderHint(QPainter::Antialiasing);
    bool Dark = qApp->palette().color(QPalette::Window).lightness() < 128;
    QColor Border = Highlighted    ? QColor("#F07178")
                    : isSelected() ? QColor("#7C8CFF")
                                   : QColor(Dark ? "#35405A" : "#BBC5DA");
    P->setPen(QPen(Border, isSelected() || Highlighted ? 2 : 1));
    P->setBrush(QColor(Dark ? "#1C2230" : "#FFFFFF"));
    P->drawRoundedRect(QRectF(0, 0, 140, 80), 13, 13);
    P->setPen(QColor("#7C8CFF"));
    QFont Title = P->font();
    Title.setPixelSize(14);
    Title.setBold(true);
    P->setFont(Title);
    P->drawText(QRectF(17, 14, 106, 24), Qt::AlignCenter, Value.Type);
    Title.setPixelSize(11);
    Title.setBold(false);
    P->setFont(Title);
    P->setPen(QColor(Dark ? "#AEB7CE" : "#566077"));
    P->drawText(QRectF(12, 45, 116, 20), Qt::AlignCenter,
                P->fontMetrics().elidedText(Value.Name, Qt::ElideMiddle, 112));
    P->setBrush(QColor(Dark ? "#0B0D12" : "#EEF0F6"));
    P->setPen(QPen(Border, 2));
    for (int I = 0; I < Value.Inputs.size(); ++I)
        P->drawEllipse(Pin(I), 5, 5);
    if (Value.Type != "OUTPUT")
        P->drawEllipse(Pin(-1), 5, 5);
}
QVariant GateItem::itemChange(GraphicsItemChange Change, const QVariant &Data) {
    if (Change == ItemPositionHasChanged && scene())
        for (auto *View : scene()->views())
            if (auto *Canvas = qobject_cast<CircuitView *>(View))
                Canvas->UpdateWires();
    return QGraphicsObject::itemChange(Change, Data);
}
CircuitView::CircuitView(CircuitModel *Value, QWidget *Parent)
    : QGraphicsView(Parent), Model(Value) {
    setScene(new QGraphicsScene(this));
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setFrameShape(QFrame::NoFrame);
    setDragMode(RubberBandDrag);
    setAcceptDrops(true);
    setMouseTracking(true);
    setTransformationAnchor(AnchorUnderMouse);
    setViewportUpdateMode(BoundingRectViewportUpdate);
    setSceneRect(-2000, -2000, 6000, 6000);
    connect(Model, &CircuitModel::Changed, this, &CircuitView::Rebuild);
    connect(scene(), &QGraphicsScene::selectionChanged, this, [this] {
        int Id = -1;
        for (auto *Item : scene()->selectedItems())
            if (auto *Gate = dynamic_cast<GateItem *>(Item)) {
                Id = Gate->Value.Id;
                break;
            }
        emit Selection(Id);
    });
}
CircuitView::~CircuitView() {
    disconnect(scene(), nullptr, this, nullptr);
    disconnect(Model, nullptr, this, nullptr);
}
void CircuitView::Rebuild() {
    QSet<int> Selected;
    for (auto *Item : scene()->selectedItems())
        if (auto *Gate = dynamic_cast<GateItem *>(Item))
            Selected.insert(Gate->Value.Id);
    Gates.clear();
    scene()->clear();
    Pending = -1;
    for (const auto &N : Model->Nodes) {
        auto *Item = new GateItem(N);
        scene()->addItem(Item);
        Gates[N.Id] = Item;
        Item->setSelected(Selected.contains(N.Id));
    }
    UpdateWires();
    Highlight(HighlightWire);
}
void CircuitView::UpdateWires() {
    for (auto *Item : scene()->items())
        if (Item->data(0).toString() == "wire")
            delete Item;
    for (const auto &N : Model->Nodes)
        for (int Pin = 0; Pin < N.Inputs.size(); ++Pin) {
            int Source = Model->Source(N.Inputs[Pin]);
            if (!Gates.contains(Source) || !Gates.contains(N.Id))
                continue;
            QPointF A = Gates[Source]->mapToScene(Gates[Source]->Pin(-1)),
                    B = Gates[N.Id]->mapToScene(Gates[N.Id]->Pin(Pin));
            qreal Bend = qMax(45.0, qAbs(B.x() - A.x()) / 2);
            QPainterPath Path(A);
            if (B.x() - A.x() > 270) {
                qreal Lane = qMin(A.y(), B.y()) - 65;
                Path.cubicTo(A + QPointF(35, 0), QPointF(A.x() + 35, Lane),
                             QPointF(A.x() + 55, Lane));
                Path.lineTo(B.x() - 55, Lane);
                Path.cubicTo(QPointF(B.x() - 35, Lane), B - QPointF(35, 0), B);
            } else
                Path.cubicTo(A + QPointF(Bend, 0), B - QPointF(Bend, 0), B);
            auto *Wire = scene()->addPath(
                Path,
                QPen(N.Inputs[Pin] == HighlightWire ? QColor("#F07178") : QColor("#6577AF"), 2));
            Wire->setFlag(QGraphicsItem::ItemIsSelectable);
            Wire->setData(0, "wire");
            Wire->setData(1, N.Id);
            Wire->setData(2, Pin);
            Wire->setToolTip("Net " + N.Inputs[Pin] + " · select and Delete to disconnect");
            Wire->setZValue(1);
        }
}
void CircuitView::Highlight(const QString &Wire) {
    HighlightWire = Wire;
    for (auto *Item : Gates) {
        Item->Highlighted = Item->Value.Name == Wire && Item->Value.Type != "OUTPUT";
        Item->update();
    }
    UpdateWires();
}
void CircuitView::Fit() {
    if (Gates.isEmpty()) {
        resetTransform();
        centerOn(300, 200);
        return;
    }
    QRectF Bounds;
    for (auto *Gate : Gates)
        Bounds = Bounds.united(Gate->sceneBoundingRect());
    Bounds.adjust(-20, -20, 20, 20);
    bool Overlay = parentWidget() && parentWidget()->objectName() == "Workspace";
    QRectF Available = Overlay ? QRectF(255, 35, qMax(300, viewport()->width() - 550),
                                        qMax(220, viewport()->height() - 450))
                               : QRectF(viewport()->rect()).adjusted(30, 30, -30, -30);
    qreal Factor =
        qMin(1.2, qMin(Available.width() / Bounds.width(), Available.height() / Bounds.height()));
    resetTransform();
    scale(Factor, Factor);
    QPointF Offset = (Available.center() - QRectF(viewport()->rect()).center()) / Factor;
    centerOn(Bounds.center() - Offset);
}
void CircuitView::drawForeground(QPainter *Painter, const QRectF &) {
    if (Pending < 0 || !Gates.contains(Pending))
        return;
    QPointF A = Gates[Pending]->mapToScene(Gates[Pending]->Pin(-1)),
            B = mapToScene(viewport()->mapFromGlobal(QCursor::pos()));
    QPainterPath Path(A);
    Path.cubicTo(A + QPointF(60, 0), B - QPointF(60, 0), B);
    Painter->setPen(QPen(QColor("#7C8CFF"), 2, Qt::DashLine));
    Painter->drawPath(Path);
}
void CircuitView::drawBackground(QPainter *P, const QRectF &Area) {
    P->fillRect(Area, qApp->palette().color(QPalette::Window));
    P->setPen(
        QColor(qApp->palette().color(QPalette::Window).lightness() < 128 ? "#242B3B" : "#CED5E4"));
    int Step = transform().m11() < 0.35 ? 80 : 20;
    for (qreal X = std::floor(Area.left() / Step) * Step; X < Area.right(); X += Step)
        for (qreal Y = std::floor(Area.top() / Step) * Step; Y < Area.bottom(); Y += Step)
            P->drawPoint(QPointF(X, Y));
}
void CircuitView::wheelEvent(QWheelEvent *E) {
    qreal Factor = E->angleDelta().y() > 0 ? 1.12 : 1 / 1.12;
    if (transform().m11() * Factor >= 0.12 && transform().m11() * Factor <= 3.5)
        scale(Factor, Factor);
    E->accept();
}
QPair<int, int> CircuitView::HitPin(QPoint Position) const {
    QPointF Point = mapToScene(Position);
    for (auto *Gate : Gates) {
        for (int Pin = 0; Pin < Gate->Value.Inputs.size(); ++Pin)
            if (QLineF(Point, Gate->mapToScene(Gate->Pin(Pin))).length() < 11 / transform().m11())
                return {Gate->Value.Id, Pin};
        if (Gate->Value.Type != "OUTPUT" &&
            QLineF(Point, Gate->mapToScene(Gate->Pin(-1))).length() < 11 / transform().m11())
            return {Gate->Value.Id, -1};
    }
    return {-1, -2};
}
void CircuitView::FinishWire(QPair<int, int> Pin) {
    try {
        if (Pin.first < 0 || Pin.second < 0)
            throw std::runtime_error("Choose an input pin to finish this connection.");
        Model->Connect(Pending, Pin.first, Pin.second);
        emit Message("Wire connected", false);
    } catch (const std::exception &Error) {
        emit Message(QString::fromUtf8(Error.what()), true);
    }
    Pending = -1;
    viewport()->update();
}
void CircuitView::mousePressEvent(QMouseEvent *E) {
    if (E->button() == Qt::MiddleButton) {
        Panning = true;
        PanStart = E->pos();
        setCursor(Qt::ClosedHandCursor);
        E->accept();
        return;
    }
    if (E->button() == Qt::LeftButton) {
        auto Pin = HitPin(E->pos());
        if (Pin.first >= 0) {
            if (Pending >= 0) {
                FinishWire(Pin);
            } else if (Pin.second == -1) {
                Pending = Pin.first;
                emit Message("Choose an input pin · Escape cancels", false);
            } else
                emit Message("Start a connection at an output pin on the right.", true);
            E->accept();
            return;
        }
    }
    QGraphicsView::mousePressEvent(E);
}
void CircuitView::mouseMoveEvent(QMouseEvent *E) {
    if (Pending >= 0) {
        viewport()->update();
        return;
    }
    if (Panning) {
        auto Delta = E->pos() - PanStart;
        PanStart = E->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - Delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - Delta.y());
        return;
    }
    QGraphicsView::mouseMoveEvent(E);
}
void CircuitView::mouseReleaseEvent(QMouseEvent *E) {
    if (E->button() == Qt::MiddleButton) {
        Panning = false;
        unsetCursor();
        return;
    }
    if (Pending >= 0) {
        auto Pin = HitPin(E->pos());
        if (Pin.first >= 0 && Pin.second >= 0)
            FinishWire(Pin);
        return;
    }
    QGraphicsView::mouseReleaseEvent(E);
    QMap<int, QPointF> Positions;
    for (auto *Gate : Gates) {
        QPointF P = Gate->pos();
        if (P == Gate->Value.Position)
            continue;
        if (Snap)
            P = {qRound(P.x() / 20) * 20.0, qRound(P.y() / 20) * 20.0};
        Positions[Gate->Value.Id] = P;
    }
    Model->Move(Positions);
}
void CircuitView::dragEnterEvent(QDragEnterEvent *E) {
    if (E->mimeData()->hasFormat("application/x-circuitforge-gate"))
        E->acceptProposedAction();
}
void CircuitView::dragMoveEvent(QDragMoveEvent *E) {
    if (E->mimeData()->hasFormat("application/x-circuitforge-gate"))
        E->acceptProposedAction();
}
void CircuitView::dropEvent(QDropEvent *E) {
    try {
        Model->Add(QString::fromUtf8(E->mimeData()->data("application/x-circuitforge-gate")),
                   mapToScene(E->position().toPoint()));
        E->acceptProposedAction();
    } catch (const std::exception &Error) {
        emit Message(Error.what(), true);
    }
}
void CircuitView::DeleteSelection() {
    QList<int> Ids;
    QList<QPair<int, int>> Edges;
    for (auto *Item : scene()->selectedItems()) {
        if (auto *Gate = dynamic_cast<GateItem *>(Item))
            Ids << Gate->Value.Id;
        else if (Item->data(0).toString() == "wire")
            Edges << qMakePair(Item->data(1).toInt(), Item->data(2).toInt());
    }
    Model->Delete(Ids, Edges);
}
void CircuitView::keyPressEvent(QKeyEvent *E) {
    if (E->key() == Qt::Key_Delete || E->key() == Qt::Key_Backspace) {
        DeleteSelection();
        return;
    }
    if (E->key() == Qt::Key_Escape) {
        Pending = -1;
        scene()->clearSelection();
        emit Message("Connection cancelled", false);
        return;
    }
    if (E->key() == Qt::Key_F) {
        Fit();
        return;
    }
    QGraphicsView::keyPressEvent(E);
}
void CircuitView::contextMenuEvent(QContextMenuEvent *E) {
    QMenu Menu;
    Menu.addAction("Fit circuit", this, &CircuitView::Fit);
    Menu.addAction("Delete selection", this, &CircuitView::DeleteSelection);
    auto *SnapAction = Menu.addAction("Snap to grid");
    SnapAction->setCheckable(true);
    SnapAction->setChecked(Snap);
    connect(SnapAction, &QAction::toggled, this, [this](bool On) { Snap = On; });
    Menu.exec(E->globalPos());
}
GateLibrary::GateLibrary(QWidget *Parent) : QListWidget(Parent) {
    setDragEnabled(true);
    setSpacing(4);
    for (const auto &Type :
         QStringList{"INPUT", "OUTPUT", "AND", "OR", "XOR", "NAND", "NOR", "XNOR", "NOT", "BUF"}) {
        auto *Item = new QListWidgetItem(Type, this);
        Item->setSizeHint({150, 31});
        Item->setToolTip("Drag " + Type + " onto the canvas, or double-click to add");
    }
}
void GateLibrary::startDrag(Qt::DropActions) {
    if (!currentItem())
        return;
    auto *Drag = new QDrag(this);
    auto *Mime = new QMimeData;
    Mime->setData("application/x-circuitforge-gate", currentItem()->text().toUtf8());
    Drag->setMimeData(Mime);
    Drag->exec(Qt::CopyAction);
}
} // namespace Desktop
