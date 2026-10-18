#pragma once
#include <QMap>
#include <QObject>
#include <QPointF>
#include <QStringList>
#include <QUndoStack>
namespace Desktop {
struct Node {
    int Id = 0;
    QString Type, Name;
    QStringList Inputs;
    QPointF Position;
};
using Graph = QMap<int, Node>;
class CircuitModel : public QObject {
    Q_OBJECT
  public:
    explicit CircuitModel(QObject *Parent = nullptr);
    Graph Nodes;
    QUndoStack History;
    static int InputCount(const QString &Type);
    int Add(const QString &Type, QPointF Position);
    void Connect(int Source, int Target, int Pin);
    void Disconnect(int Target, int Pin);
    void Delete(const QList<int> &Ids, const QList<QPair<int, int>> &Edges = {});
    void Move(const QMap<int, QPointF> &Positions);
    void Rename(int Id, const QString &Name);
    void Clear();
    void Load(const QString &Path);
    void Save(const QString &Path);
    QString Netlist() const;
    QStringList WireNames() const;
    int Source(const QString &Wire) const;
    void Replace(const Graph &NewGraph);
    void Commit(const Graph &NewGraph, const QString &Description);
    static void Validate(const Graph &Value, bool Complete);
  signals:
    void Changed();
};
} // namespace Desktop
