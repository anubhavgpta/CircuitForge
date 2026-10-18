#include "CircuitModel.h"
#include "CircuitCompiler.h"
#include "NetlistParser.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <functional>
#include <stdexcept>
namespace Desktop {
namespace {
void Fail(const QString &Message) {
    throw std::runtime_error(Message.toStdString());
}
class GraphEdit : public QUndoCommand {
  public:
    GraphEdit(CircuitModel *Model, const Graph &Next, const QString &Label)
        : Model(Model), Before(Model->Nodes), After(Next) {
        setText(Label);
    }
    void undo() override {
        Model->Replace(Before);
    }
    void redo() override {
        Model->Replace(After);
    }

  private:
    CircuitModel *Model;
    Graph Before, After;
};
QString Key(const Node &N) {
    return N.Type + ":" + N.Name;
}
} // namespace
CircuitModel::CircuitModel(QObject *Parent) : QObject(Parent), History(this) {}
int CircuitModel::InputCount(const QString &Type) {
    if (Type == "INPUT")
        return 0;
    if (Type == "OUTPUT" || Type == "NOT" || Type == "BUF")
        return 1;
    return 2;
}
void CircuitModel::Replace(const Graph &NewGraph) {
    Nodes = NewGraph;
    emit Changed();
}
void CircuitModel::Commit(const Graph &NewGraph, const QString &Description) {
    Validate(NewGraph, false);
    History.push(new GraphEdit(this, NewGraph, Description));
}
int CircuitModel::Add(const QString &Type, QPointF Position) {
    const QStringList Types = {"INPUT", "OUTPUT", "AND",  "OR",  "XOR",
                               "NAND",  "NOR",    "XNOR", "NOT", "BUF"};
    if (!Types.contains(Type))
        Fail("Unknown gate type");
    Graph Next = Nodes;
    int Id = Next.isEmpty() ? 1 : Next.lastKey() + 1;
    QString Name;
    int Suffix = Id;
    do {
        Name = (Type == "INPUT"    ? "in"
                : Type == "OUTPUT" ? "out"
                                   : "n") +
               QString::number(Suffix++);
    } while (WireNames().contains(Name));
    Node N{Id, Type, Name, {}, Position};
    for (int I = 0; I < InputCount(Type); ++I)
        N.Inputs << QString();
    Next[Id] = N;
    Commit(Next, "Add " + Type);
    return Id;
}
int CircuitModel::Source(const QString &Wire) const {
    for (const auto &N : Nodes)
        if (N.Type != "OUTPUT" && N.Name == Wire)
            return N.Id;
    return -1;
}
QStringList CircuitModel::WireNames() const {
    QStringList Result;
    for (const auto &N : Nodes)
        if (N.Type != "OUTPUT")
            Result << N.Name;
    return Result;
}
void CircuitModel::Validate(const Graph &Value, bool Complete) {
    QMap<QString, int> Drivers;
    int PrimaryInputs = 0, PrimaryOutputs = 0;
    const QRegularExpression NamePattern("^[A-Za-z0-9_.$]+$");
    for (const auto &N : Value) {
        if (!NamePattern.match(N.Name).hasMatch())
            Fail("Wire names may contain letters, digits, _, . and $ only.");
        if (N.Inputs.size() != InputCount(N.Type))
            Fail("Incorrect pin count on " + N.Name);
        if (N.Type == "INPUT")
            ++PrimaryInputs;
        if (N.Type == "OUTPUT") {
            ++PrimaryOutputs;
            continue;
        }
        if (Drivers.contains(N.Name))
            Fail("Multiple drivers for wire " + N.Name);
        Drivers[N.Name] = N.Id;
    }
    if (Complete && (!PrimaryInputs || !PrimaryOutputs))
        Fail("Add at least one primary INPUT and OUTPUT terminal.");
    QMap<int, int> Mark;
    std::function<void(int)> Visit = [&](int Id) {
        if (Mark.value(Id) == 1)
            Fail("Connection would create a combinational cycle.");
        if (Mark.value(Id) == 2)
            return;
        Mark[Id] = 1;
        for (const auto &Wire : Value[Id].Inputs) {
            if (Wire.isEmpty()) {
                if (Complete)
                    Fail("Connect every input pin on " + Value[Id].Name +
                         " before saving or running.");
                continue;
            }
            if (!Drivers.contains(Wire))
                Fail("Undriven wire " + Wire);
            Visit(Drivers[Wire]);
        }
        Mark[Id] = 2;
    };
    for (auto It = Value.begin(); It != Value.end(); ++It)
        Visit(It.key());
}
void CircuitModel::Connect(int SourceId, int Target, int Pin) {
    if (!Nodes.contains(SourceId) || !Nodes.contains(Target))
        Fail("Select valid pins.");
    if (Nodes[SourceId].Type == "OUTPUT" || Pin < 0 || Pin >= Nodes[Target].Inputs.size())
        Fail("Connect an output pin to an input pin.");
    if (!Nodes[Target].Inputs[Pin].isEmpty())
        Fail("This input already has a driver. Select its wire and press Delete first.");
    Graph Next = Nodes;
    Next[Target].Inputs[Pin] = Next[SourceId].Name;
    if (Next[Target].Type == "OUTPUT")
        Next[Target].Name = Next[SourceId].Name;
    Commit(Next, "Connect wire");
}
void CircuitModel::Disconnect(int Target, int Pin) {
    if (!Nodes.contains(Target) || Pin < 0 || Pin >= Nodes[Target].Inputs.size())
        return;
    Graph Next = Nodes;
    Next[Target].Inputs[Pin].clear();
    Commit(Next, "Disconnect wire");
}
void CircuitModel::Delete(const QList<int> &Ids, const QList<QPair<int, int>> &Edges) {
    Graph Next = Nodes;
    QSet<QString> Removed;
    for (int Id : Ids) {
        if (Next.contains(Id) && Next[Id].Type != "OUTPUT")
            Removed.insert(Next[Id].Name);
        Next.remove(Id);
    }
    for (auto &N : Next)
        for (auto &Wire : N.Inputs)
            if (Removed.contains(Wire))
                Wire.clear();
    for (const auto &Edge : Edges)
        if (Next.contains(Edge.first) && Edge.second >= 0 &&
            Edge.second < Next[Edge.first].Inputs.size())
            Next[Edge.first].Inputs[Edge.second].clear();
    if (!Ids.isEmpty() || !Edges.isEmpty())
        Commit(Next, "Delete selection");
}
void CircuitModel::Move(const QMap<int, QPointF> &Positions) {
    Graph Next = Nodes;
    bool Changed = false;
    for (auto It = Positions.begin(); It != Positions.end(); ++It)
        if (Next.contains(It.key()) && Next[It.key()].Position != It.value()) {
            Next[It.key()].Position = It.value();
            Changed = true;
        }
    if (Changed)
        Commit(Next, "Move gates");
}
void CircuitModel::Rename(int Id, const QString &Name) {
    if (!Nodes.contains(Id))
        return;
    if (Nodes[Id].Type == "OUTPUT")
        Fail("Output terminals inherit the observed wire name. Rename its source gate instead.");
    Graph Next = Nodes;
    QString Old = Next[Id].Name;
    Next[Id].Name = Name;
    if (Next[Id].Type != "OUTPUT")
        for (auto &N : Next)
            for (auto &Wire : N.Inputs)
                if (Wire == Old) {
                    Wire = Name;
                    if (N.Type == "OUTPUT")
                        N.Name = Name;
                }
    Commit(Next, "Rename wire");
}
void CircuitModel::Clear() {
    History.clear();
    Nodes.clear();
    emit Changed();
    History.setClean();
}
QString CircuitModel::Netlist() const {
    Validate(Nodes, true);
    QStringList Inputs, Outputs, Lines;
    QSet<QString> Seen;
    for (const auto &N : Nodes) {
        if (N.Type == "INPUT")
            Inputs << N.Name;
        else if (N.Type == "OUTPUT") {
            if (!Seen.contains(N.Inputs[0]))
                Outputs << N.Inputs[0];
            Seen.insert(N.Inputs[0]);
        } else
            Lines << N.Type + " " + N.Name + " " + N.Inputs.join(' ');
    }
    return "# CircuitForge Desktop\nINPUT " + Inputs.join(' ') + "\nOUTPUT " + Outputs.join(' ') +
           "\n\n" + Lines.join('\n') + "\n";
}
void CircuitModel::Load(const QString &Path) {
    // Reuse the engine parser/compiler; there is no second simulation implementation.
    auto C = CircuitForge::LoadNetlist(Path.toStdString());
    CircuitForge::CompileCircuit(C);
    Graph Next;
    int Id = 1;
    QStringList Types = {"AND", "OR", "XOR", "NAND", "NOR", "XNOR", "NOT", "BUF"};
    QMap<int, int> Rows;
    for (int W : C.Inputs) {
        Node N{
            Id, "INPUT", QString::fromStdString(C.WireNames[W]), {}, QPointF(0, Rows[0]++ * 110)};
        Next[Id++] = N;
    }
    int MaxLevel = 0;
    for (size_t I = 0; I < C.Gates.size(); ++I) {
        const auto &G = C.Gates[I];
        int Level = C.Levels[I];
        MaxLevel = qMax(MaxLevel, Level);
        Node N{Id,
               Types[int(G.Type)],
               QString::fromStdString(C.WireNames[G.Output]),
               {QString::fromStdString(C.WireNames[G.InputA])},
               QPointF(Level * 220, Rows[Level]++ * 110)};
        if (G.InputB >= 0)
            N.Inputs << QString::fromStdString(C.WireNames[G.InputB]);
        Next[Id++] = N;
    }
    int Row = 0;
    for (int W : C.Outputs) {
        QString Name = QString::fromStdString(C.WireNames[W]);
        Next[Id] = {Id, "OUTPUT", Name, {Name}, QPointF((MaxLevel + 1) * 220, Row++ * 110)};
        ++Id;
    }
    QFile Layout(Path + ".layout.json");
    if (Layout.open(QIODevice::ReadOnly)) {
        auto Object = QJsonDocument::fromJson(Layout.readAll()).object();
        for (auto &N : Next) {
            auto Position = Object.value(Key(N)).toArray();
            if (Position.size() == 2 && Position[0].isDouble() && Position[1].isDouble()) {
                double X = Position[0].toDouble(), Y = Position[1].toDouble();
                if (qIsFinite(X) && qIsFinite(Y) && qAbs(X) < 100000 && qAbs(Y) < 100000)
                    N.Position = {X, Y};
            }
        }
    }
    Validate(Next, true);
    History.clear();
    Replace(Next);
    History.setClean();
}
void CircuitModel::Save(const QString &Path) {
    QByteArray Text = Netlist().toUtf8();
    QSaveFile File(Path);
    if (!File.open(QIODevice::WriteOnly) || File.write(Text) != Text.size() || !File.commit())
        Fail("Cannot save netlist: " + File.errorString());
    QJsonObject Layout;
    for (const auto &N : Nodes)
        Layout[Key(N)] = QJsonArray{N.Position.x(), N.Position.y()};
    QSaveFile Sidecar(Path + ".layout.json");
    QByteArray Data = QJsonDocument(Layout).toJson();
    if (!Sidecar.open(QIODevice::WriteOnly) || Sidecar.write(Data) != Data.size() ||
        !Sidecar.commit())
        Fail("Netlist saved, but layout could not be saved: " + Sidecar.errorString());
    History.setClean();
}
} // namespace Desktop
