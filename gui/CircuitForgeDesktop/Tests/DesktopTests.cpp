#include "App/MainWindow.h"
#include "App/Theme.h"
#include "Canvas/CircuitView.h"
#include "Integration/EngineRunner.h"
#include "Widgets/FloatingPanel.h"
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QGraphicsScene>
#include <QMimeData>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
using namespace Desktop;
class DesktopTests : public QObject {
    Q_OBJECT
  private:
    QString Root = QString::fromUtf8(CIRCUITFORGE_ROOT);
    QString Engine = Root + "/build-gpu/CircuitForge.exe";
    QTemporaryDir SettingsDirectory;
    QString HalfAdder() {
        return "INPUT A B\nOUTPUT SUM CARRY\nXOR SUM A B\nAND CARRY A B\n";
    }
  private slots:
    void initTestCase() {
        QVERIFY(SettingsDirectory.isValid());
        QCoreApplication::setOrganizationName("CircuitForgeTests");
        QCoreApplication::setApplicationName("DesktopTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, SettingsDirectory.path());
        ApplyTheme(*qApp);
    }
    void ModelEditing() {
        CircuitModel M;
        int A = M.Add("INPUT", {0, 0}), B = M.Add("INPUT", {0, 120}), Gate = M.Add("AND", {220, 0}),
            Out = M.Add("OUTPUT", {440, 0});
        M.Rename(A, "A");
        M.Rename(B, "B");
        M.Rename(Gate, "Z");
        M.Connect(A, Gate, 0);
        M.Connect(B, Gate, 1);
        M.Connect(Gate, Out, 0);
        QVERIFY(M.Netlist().contains("AND Z A B"));
        QVERIFY_EXCEPTION_THROWN(M.Connect(A, Gate, 0), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(M.Connect(Out, Gate, 0), std::runtime_error);
        M.Disconnect(Gate, 0);
        QVERIFY_EXCEPTION_THROWN(M.Netlist(), std::runtime_error);
        M.History.undo();
        QVERIFY(M.Netlist().contains("AND Z A B"));
        M.History.redo();
        QVERIFY(M.Nodes[Gate].Inputs[0].isEmpty());
        M.History.undo();
        M.Rename(A, "Renamed");
        QCOMPARE(M.Nodes[Gate].Inputs[0], QString("Renamed"));
        QVERIFY_EXCEPTION_THROWN(M.Rename(B, "Renamed"), std::runtime_error);
        QTemporaryDir Directory;
        QString Path = Directory.filePath("circuit with spaces.txt");
        M.Move({{Gate, {360, 180}}});
        M.Save(Path);
        QVERIFY(M.History.isClean());
        CircuitModel Copy;
        Copy.Load(Path);
        QCOMPARE(Copy.Netlist(), M.Netlist());
        bool Found = false;
        for (const auto &N : Copy.Nodes)
            if (N.Name == "Z" && N.Type == "AND") {
                QCOMPARE(N.Position, QPointF(360, 180));
                Found = true;
            }
        QVERIFY(Found);
        M.Delete({Gate});
        QVERIFY(M.Nodes[Out].Inputs[0].isEmpty());
        M.History.undo();
        QVERIFY(!M.Nodes[Out].Inputs[0].isEmpty());
    }
    void CycleAndParser() {
        CircuitModel M;
        int A = M.Add("NOT", {0, 0}), B = M.Add("NOT", {200, 0});
        M.Connect(A, B, 0);
        QVERIFY_EXCEPTION_THROWN(M.Connect(B, A, 0), std::runtime_error);
        QVERIFY(M.Nodes[A].Inputs[0].isEmpty());
        M.Load(Root + "/circuits/FullAdder.txt");
        QCOMPARE(M.WireNames().size(), 8);
        QVERIFY(M.Netlist().contains("OUTPUT SUM COUT"));
        QTemporaryDir D;
        QFile F(D.filePath("bad.txt"));
        QVERIFY(F.open(QIODevice::WriteOnly));
        F.write("INPUT A\nOUTPUT Z\nDFF Z A\n");
        F.close();
        auto Previous = M.Netlist();
        QVERIFY_EXCEPTION_THROWN(M.Load(F.fileName()), std::runtime_error);
        QCOMPARE(M.Netlist(), Previous);
    }
    void CommandsAndCsv() {
        JobOptions O;
        O.Gpu = true;
        O.Drop = true;
        O.Command = "simulate";
        auto Args =
            EngineRunner::Arguments(O, "C:/space path/input.txt", "C:/space path/report.csv");
        QCOMPARE(Args[1], QString("C:/space path/input.txt"));
        QVERIFY(!Args.contains("--gpu"));
        QVERIFY(!Args.contains("--fault-dropping"));
        O.Command = "faults";
        Args = EngineRunner::Arguments(O, "input", "output");
        QVERIFY(Args.contains("--gpu"));
        QVERIFY(Args.contains("--fault-dropping"));
        O.Command = "atpg";
        Args = EngineRunner::Arguments(O, "input", "output");
        QVERIFY(Args.contains("--search-limit"));
        QVERIFY(!Args.contains("--patterns"));
        O.Block = 17;
        QVERIFY_EXCEPTION_THROWN(EngineRunner::Arguments(O, "a", "b"), std::runtime_error);
        auto Csv = ReadCsv("name,value\r\n\"a,b\",\"a\"\"b\"\r\n");
        QCOMPARE(Csv.Value(0, "name"), QString("a,b"));
        QCOMPARE(Csv.Value(0, "value"), QString("a\"b"));
        QVERIFY_EXCEPTION_THROWN(ReadCsv("a,b\n1\n"), std::runtime_error);
    }
    void CanvasInteraction() {
        CircuitModel M;
        CircuitView View(&M);
        View.resize(900, 600);
        View.show();
        int A = M.Add("INPUT", {0, 0}), Gate = M.Add("NOT", {220, 0});
        View.Fit();
        QTest::qWait(100);
        QPoint Output = View.mapFromScene(View.Gates[A]->mapToScene(View.Gates[A]->Pin(-1)));
        QPoint Input = View.mapFromScene(View.Gates[Gate]->mapToScene(View.Gates[Gate]->Pin(0)));
        QTest::mouseClick(View.viewport(), Qt::LeftButton, Qt::NoModifier, Output);
        QTest::mouseClick(View.viewport(), Qt::LeftButton, Qt::NoModifier, Input);
        QCOMPARE(M.Nodes[Gate].Inputs[0], M.Nodes[A].Name);
        QMimeData Mime;
        Mime.setData("application/x-circuitforge-gate", "XOR");
        QDragEnterEvent Enter(QPoint(400, 350), Qt::CopyAction, &Mime, Qt::LeftButton,
                              Qt::NoModifier);
        QApplication::sendEvent(View.viewport(), &Enter);
        QDropEvent Drop(QPointF(400, 350), Qt::CopyAction, &Mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(View.viewport(), &Drop);
        QCOMPARE(M.Nodes.size(), 3);
        View.Gates[Gate]->setSelected(true);
        QTest::keyClick(&View, Qt::Key_Delete);
        QVERIFY(!M.Nodes.contains(Gate));
        M.History.undo();
        QVERIFY(M.Nodes.contains(Gate));
    }
    void AsyncEngine() {
        if (!QFileInfo::exists(Engine))
            QSKIP("Build build-gpu/CircuitForge.exe for engine integration tests.");
        EngineRunner R;
        QSignalSpy Done(&R, &EngineRunner::Completed);
        JobOptions O;
        O.Patterns = 33;
        for (const auto &Command :
             QStringList{"simulate", "faults", "collapse", "atpg", "benchmark"}) {
            O.Command = Command;
            O.Gpu = true;
            R.Start(Engine, HalfAdder(), O);
            QTRY_VERIFY_WITH_TIMEOUT(Done.count() > 0, 30000);
            auto Args = Done.takeFirst();
            QVERIFY2(Args[2].toBool(), qPrintable(Command));
            QVERIFY(!ReadCsv(Args[1].toByteArray()).Rows.isEmpty());
        }
        O.Command = "benchmark";
        O.Patterns = 16777216;
        O.Gpu = true;
        int Ticks = 0;
        QTimer Timer;
        Timer.setInterval(5);
        connect(&Timer, &QTimer::timeout, this, [&] { ++Ticks; });
        Timer.start();
        R.Start(Engine, HalfAdder(), O);
        QTRY_VERIFY_WITH_TIMEOUT(Ticks >= 4, 5000);
        QVERIFY(R.Busy());
        R.Stop();
        QTRY_VERIFY_WITH_TIMEOUT(Done.count() > 0, 5000);
        QVERIFY(!Done.takeFirst()[2].toBool());
        QVERIFY(!R.Busy());
        QVERIFY_EXCEPTION_THROWN(R.Start("missing.exe", HalfAdder(), O), std::runtime_error);
        O.Command = "simulate";
        O.Patterns = 4;
        R.Start(Engine, "INPUT A\nOUTPUT X\n", O);
        QTRY_VERIFY_WITH_TIMEOUT(Done.count() > 0, 5000);
        QVERIFY(!Done.takeFirst()[2].toBool());
    }
    void WindowWorkflow() {
        MainWindow W(false);
        W.show();
        QTest::qWait(150);
        W.OpenPath(Root + "/circuits/FullAdder.txt");
        W.Canvas->Fit();
        QCOMPARE(W.Model.WireNames().size(), 8);
        QVERIFY(W.grab().save("Desktop.png"));
        auto *Library = W.findChild<GateLibrary *>("GateLibrary");
        QVERIFY(Library);
        int Before = W.Model.Nodes.size();
        QTest::mouseClick(Library->viewport(), Qt::LeftButton, Qt::NoModifier,
                          Library->visualItemRect(Library->item(2)).center());
        QTest::mouseDClick(Library->viewport(), Qt::LeftButton, Qt::NoModifier,
                           Library->visualItemRect(Library->item(2)).center());
        QCOMPARE(W.Model.Nodes.size(), Before + 1);
        W.Model.History.undo();
        if (QFileInfo::exists(Engine)) {
            W.SetEnginePath(Engine);
            auto *Backend = W.findChild<QComboBox *>("Backend");
            Backend->setCurrentText("GPU");
            QSignalSpy Done(&W.Runner, &EngineRunner::Completed);
            W.Run("faults");
            QVERIFY(!W.Canvas->isEnabled());
            QTRY_VERIFY_WITH_TIMEOUT(Done.count() > 0, 10000);
            QVERIFY(W.Canvas->isEnabled());
            auto *Table = W.findChild<QTableView *>("FaultTable");
            QCOMPARE(Table->model()->rowCount(), 16);
            QVERIFY(W.grab().save("Desktop-faults.png"));
            W.Run("atpg");
            QTRY_VERIFY_WITH_TIMEOUT(Done.count() > 1, 10000);
            QCOMPARE(W.findChild<QTableView *>("AtpgTable")->model()->rowCount(), 16);
        }
        if(QFileInfo::exists(Engine)){
            QSignalSpy BenchDone(&W.Runner,&EngineRunner::Completed);
            W.Run("benchmark");QTRY_VERIFY_WITH_TIMEOUT(BenchDone.count()>0,15000);
            for(auto* Panel:W.findChildren<FloatingPanel*>())if(Panel->property("panelKey").toString()=="Faults"||Panel->property("panelKey").toString()=="ATPG")Panel->hide();
            QVERIFY(W.grab().save("Desktop-benchmark.png"));
        }
        ApplyTheme(*qApp,false);QTest::qWait(30);QVERIFY(W.grab().save("Desktop-light.png"));ApplyTheme(*qApp,true);
        W.Model.History.setClean();
    }
    void PanelMouseInteraction(){
        QWidget Workspace;Workspace.resize(1000,700);FloatingPanel Panel("Test",&Workspace);Panel.setGeometry(50,50,300,300);Workspace.show();QTest::qWait(50);
        auto* Header=Panel.findChild<QWidget*>("PanelHeader");QVERIFY(Header);QPoint Start=Header->rect().center();QPoint Initial=Panel.pos();
        QTest::mousePress(Header,Qt::LeftButton,Qt::NoModifier,Start);QTest::mouseMove(Header,Start+QPoint(60,40));QTest::mouseRelease(Header,Qt::LeftButton,Qt::NoModifier,Start+QPoint(60,40));QVERIFY(Panel.pos()!=Initial);
        auto* Grip=Panel.findChild<QWidget*>("PanelResizeGrip");QVERIFY(Grip);QSize Before=Panel.size();QSize WorkspaceBefore=Workspace.size();QTest::mousePress(Grip,Qt::LeftButton,Qt::NoModifier,Grip->rect().center());QTest::mouseMove(Grip,Grip->rect().center()+QPoint(40,30));QTest::mouseRelease(Grip,Qt::LeftButton);QVERIFY(Panel.width()>Before.width());QCOMPARE(Workspace.size(),WorkspaceBefore);
        Panel.hide();QVERIFY(!Panel.isVisible());Panel.show();QVERIFY(Panel.isVisible());
    }
    void PanelPersistence() {
        QRect Saved;
        {
            MainWindow W(true);
            W.show();
            QTest::qWait(150);
            auto Panels = W.findChildren<FloatingPanel *>();
            QVERIFY(!Panels.isEmpty());
            auto *Panel = Panels.first();
            Panel->move(85, 120);
            Panel->resize(380, 350);
            Saved = Panel->geometry();
            Panel->SetCollapsed(true);
            QVERIFY(Panel->IsCollapsed());
            Panel->SetCollapsed(false);
            QVERIFY(!Panel->IsCollapsed());
            Saved = Panel->geometry();
            Panel->SetCollapsed(true);
            W.close();
        }
        MainWindow Restored(true);
        Restored.show();
        QTest::qWait(150);
        auto *Panel = Restored.findChildren<FloatingPanel *>().first();
        QVERIFY(Panel->IsCollapsed());
        Panel->SetCollapsed(false);
        QCOMPARE(Panel->geometry(), Saved);
        Restored.close();
    }
};
QTEST_MAIN(DesktopTests)
#include "DesktopTests.moc"
