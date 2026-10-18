#include "MainWindow.h"
#include "Canvas/CircuitView.h"
#include "Theme.h"
#include "Widgets/BenchmarkChart.h"
#include "Widgets/FloatingPanel.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableView>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
namespace Desktop {
namespace {
QLabel *Muted(const QString &Text) {
    auto *Label = new QLabel(Text);
    Label->setObjectName("Muted");
    Label->setWordWrap(true);
    return Label;
}
QTableView *Table() {
    auto *View = new QTableView;
    View->setSelectionBehavior(QAbstractItemView::SelectRows);
    View->setSelectionMode(QAbstractItemView::SingleSelection);
    View->setEditTriggers(QAbstractItemView::NoEditTriggers);
    View->setAlternatingRowColors(true);
    View->verticalHeader()->hide();
    View->horizontalHeader()->setStretchLastSection(true);
    return View;
}
void WriteFile(const QString &Path, const QByteArray &Data) {
    QSaveFile File(Path);
    if (!File.open(QIODevice::WriteOnly) || File.write(Data) != Data.size() || !File.commit())
        throw std::runtime_error(
            ("Cannot write " + Path + ": " + File.errorString()).toStdString());
}
} // namespace
MainWindow::MainWindow(bool RestoreSettings) : Model(this), Runner(this), Persist(RestoreSettings) {
    setWindowTitle("CircuitForge Desktop");
    setMinimumSize(1000, 720);
    resize(1500, 980);
    Workspace = new QWidget;
    Workspace->setObjectName("Workspace");
    setCentralWidget(Workspace);
    Canvas = new CircuitView(&Model, Workspace);
    Canvas->setObjectName("CircuitCanvas");
    BuildPanels();
    BuildMenus();
    StateLabel = new QLabel("Ready");
    StateLabel->setObjectName("Chip");
    statusBar()->addWidget(StateLabel);
    StatusLabel = new QLabel;
    statusBar()->addPermanentWidget(StatusLabel, 1);
    connect(&Model, &CircuitModel::Changed, this, &MainWindow::UpdateModel);
    connect(&Model.History, &QUndoStack::cleanChanged, this, [this] {
        setWindowTitle("CircuitForge Desktop — " +
                       (CurrentPath.isEmpty() ? "Untitled" : QFileInfo(CurrentPath).fileName()) +
                       (Model.History.isClean() ? "" : " *"));
    });
    connect(Canvas, &CircuitView::Selection, this, &MainWindow::UpdateSelection);
    connect(Canvas, &CircuitView::Message, this, &MainWindow::Log);
    connect(&Runner, &EngineRunner::Output, this, [this](const QString &Text, bool Error) {
        Log(Text, Error && !Text.startsWith("Coverage:"));
    });
    connect(&Runner, &EngineRunner::State, this, [this](const QString &Text, bool Busy) {
        StateLabel->setText(Text);
        Canvas->setEnabled(!Busy);
        Panels["Library"]->Body()->setEnabled(!Busy);
        Panels["Properties"]->Body()->setEnabled(!Busy);
        for (auto *Action : EditActions)
            Action->setEnabled(!Busy);
        for (auto *Button : JobButtons)
            Button->setEnabled(!Busy);
        EngineEdit->setEnabled(!Busy);
        Log(Text, false);
    });
    connect(&Runner, &EngineRunner::Completed, this, &MainWindow::Finish);
    QSettings Settings;
    Dark = Persist ? Settings.value("Dark", true).toBool() : true;
    ApplyTheme(*qApp, Dark);
    ApplyNativeFrame(this,Dark);
    QString Engine = Persist ? Settings.value("Engine").toString() : QString();
    if (Engine.isEmpty()) {
        QDir Here(QApplication::applicationDirPath());
        for (const auto &Candidate : QStringList{Here.filePath("CircuitForge.exe"),
                                                 Here.filePath("../build-gpu/CircuitForge.exe"),
                                                 Here.filePath("../build-cpu/CircuitForge.exe")})
            if (QFileInfo::exists(Candidate)) {
                Engine = QFileInfo(Candidate).absoluteFilePath();
                break;
            }
    }
    EngineEdit->setText(Engine);
    QTimer::singleShot(0, this, [this] {
        if (Persist)
            RestoreWorkspace();
        else
            ResetPanels();
        UpdateModel();
        Canvas->Fit();
        QueryGpu();
    });
    Log("Ready. Open a netlist, or drag INPUT, gates and OUTPUT onto the canvas.");
}
MainWindow::~MainWindow() {
    disconnect(&Runner, nullptr, this, nullptr);
    disconnect(&Model, nullptr, this, nullptr);
    disconnect(&Model.History, nullptr, this, nullptr);
    disconnect(Canvas, nullptr, this, nullptr);
    for (auto *Process : findChildren<QProcess *>()) {
        Process->disconnect(this);
        if (Process->state() != QProcess::NotRunning) {
            Process->kill();
            Process->waitForFinished(1000);
        }
    }
    delete takeCentralWidget();
}
FloatingPanel *MainWindow::Panel(const QString &Key, const QString &Title) {
    auto *Value = new FloatingPanel(Title, Workspace);
    Value->setObjectName("FloatingPanel");
    Value->setProperty("panelKey", Key);
    Panels[Key] = Value;
    return Value;
}
QPushButton *MainWindow::JobButton(const QString &Text, const QString &Command,
                                   const QString &Variant) {
    auto *Button = new QPushButton(Text);
    if (!Variant.isEmpty())
        Button->setProperty("variant", Variant);
    JobButtons << Button;
    connect(Button, &QPushButton::clicked, this, [this, Command] { Run(Command); });
    return Button;
}
void MainWindow::BuildPanels() {
    auto *Library = Panel("Library", "Gate Library");
    auto *L = new QVBoxLayout(Library->Body());
    L->setContentsMargins(0, 0, 0, 0);
    L->addWidget(Muted("COMPONENTS · drag onto canvas"));
    auto *Gates = new GateLibrary;
    Gates->setObjectName("GateLibrary");
    L->addWidget(Gates);
    L->addWidget(Muted(
        "Connect: output pin → input pin\nPan: middle drag · zoom: wheel\nFit: F · delete: Del"));
    connect(Gates, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *Item) {
        Model.Add(Item->text(), Canvas->mapToScene(Canvas->viewport()->rect().center()));
    });
    auto *Properties = Panel("Properties", "Properties");
    auto *P = new QVBoxLayout(Properties->Body());
    P->setContentsMargins(0, 0, 0, 0);
    SelectionLabel = new QLabel("No selection");
    SelectionLabel->setObjectName("PanelTitle");
    P->addWidget(SelectionLabel);
    NameEdit = new QLineEdit;
    NameEdit->setPlaceholderText("Wire name");
    NameEdit->setObjectName("WireName");
    P->addWidget(NameEdit);
    auto *Rename = new QPushButton("Apply name");
    P->addWidget(Rename);
    connect(Rename, &QPushButton::clicked, this, [this] {
        try {
            Model.Rename(SelectedId, NameEdit->text().trimmed());
        } catch (const std::exception &E) {
            Log(E.what(), true);
        }
    });
    P->addWidget(Muted("Primary INPUT nodes drive nets. OUTPUT nodes observe an existing net."));
    auto *Preview = new QPushButton("Inspect netlist");
    P->addWidget(Preview);
    connect(Preview, &QPushButton::clicked, this, [this] { ShowPanel("Netlist"); });
    P->addStretch();
    auto *Simulation = Panel("Simulation", "Simulation");
    auto *S = new QVBoxLayout(Simulation->Body());
    S->setContentsMargins(0, 0, 0, 0);
    auto *Options = new QHBoxLayout;
    Backend = new QComboBox;
    Backend->addItems({"CPU", "GPU"});
    Backend->setToolTip(
        "Backend for fault analysis and benchmarks. Logic truth tables and ATPG use CPU.");
    Backend->setObjectName("Backend");
    PatternCount = new QSpinBox;
    PatternCount->setRange(1, 16777216);
    PatternCount->setValue(256);
    PatternCount->setPrefix("Patterns  ");
    PatternCount->setToolTip("Exact 2^inputs is exhaustive; other counts are sampled.");
    PatternCount->setObjectName("Patterns");
    Seed = new QSpinBox;
    Seed->setRange(0, 2147483647);
    Seed->setValue(1);
    Seed->setPrefix("Seed  ");
    BlockSize = new QComboBox;
    BlockSize->addItems({"64", "128", "256", "512", "1024"});
    BlockSize->setCurrentText("128");
    BlockSize->setToolTip("CUDA threads per block");
    Dropping = new QCheckBox("Drop detected");
    Options->addWidget(Backend);
    Options->addWidget(PatternCount);
    Options->addWidget(Seed);
    Options->addWidget(BlockSize);
    Options->addWidget(Dropping);
    S->addLayout(Options);
    auto *Runs = new QHBoxLayout;
    Runs->addWidget(JobButton("Logic · CPU", "simulate"));
    Runs->addWidget(JobButton("Run faults", "faults", "primary"));
    Runs->addWidget(JobButton("Benchmark", "benchmark"));
    auto *Stop = new QPushButton("Stop");
    Stop->setProperty("variant", "danger");
    Stop->setObjectName("StopJob");
    Runs->addWidget(Stop);
    S->addLayout(Runs);
    connect(Stop, &QPushButton::clicked, &Runner, &EngineRunner::Stop);
    connect(Backend, &QComboBox::currentTextChanged, this, [this] { UpdateStatus(); });
    auto *ConsolePanel = Panel("Console", "Output Console");
    auto *C = new QVBoxLayout(ConsolePanel->Body());
    C->setContentsMargins(0, 0, 0, 0);
    Console = new QTextEdit;
    Console->setReadOnly(true);
    Console->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    Console->document()->setMaximumBlockCount(3000);
    Console->setObjectName("Console");
    C->addWidget(Console);
    auto *Tools = new QHBoxLayout;
    Tools->addStretch();
    auto *Copy = new QPushButton("Copy");
    auto *Clear = new QPushButton("Clear");
    auto *SaveLog = new QPushButton("Save log…");
    Tools->addWidget(Copy);
    Tools->addWidget(Clear);
    Tools->addWidget(SaveLog);
    C->addLayout(Tools);
    connect(Copy, &QPushButton::clicked, this,
            [this] { qApp->clipboard()->setText(Console->toPlainText()); });
    connect(Clear, &QPushButton::clicked, Console, &QTextEdit::clear);
    connect(SaveLog, &QPushButton::clicked, this, [this] {
        QString Path = QFileDialog::getSaveFileName(this, "Save console", {}, "Text (*.txt)");
        if (!Path.isEmpty())
            try {
                WriteFile(Path, Console->toPlainText().toUtf8());
            } catch (const std::exception &E) {
                Log(E.what(), true);
            }
    });
    auto *Faults = Panel("Faults", "Fault Analysis");
    auto *F = new QVBoxLayout(Faults->Body());
    F->setContentsMargins(0, 0, 0, 0);
    auto *Target = new QHBoxLayout;
    FaultWire = new QComboBox;
    FaultWire->setMinimumWidth(140);
    FaultValue = new QComboBox;
    FaultValue->addItems({"SA0", "SA1"});
    Target->addWidget(FaultWire, 1);
    Target->addWidget(FaultValue);
    Target->addWidget(JobButton("Analyze all", "faults", "primary"));
    F->addLayout(Target);
    CoverageLabel = new QLabel("Coverage: not measured");
    F->addWidget(CoverageLabel);
    ShowClasses = new QCheckBox("Show equivalence representatives");
    F->addWidget(ShowClasses);
    FaultTable = Table();
    FaultTable->setObjectName("FaultTable");
    F->addWidget(FaultTable);
    auto *FaultTools = new QHBoxLayout;
    FaultTools->addWidget(JobButton("Compute classes", "collapse"));
    auto *TargetAtpg = new QPushButton("ATPG for selected fault");
    FaultTools->addWidget(TargetAtpg);
    F->addLayout(FaultTools);
    F->addWidget(
        Muted("SA0/SA1 highlight the selected stem. Analyze all simulates both stuck values on "
              "every wire. Counts with dropping include only processed batches."));
    connect(FaultWire, &QComboBox::currentTextChanged, this, [this] { SelectFault(); });
    connect(FaultValue, &QComboBox::currentIndexChanged, this, [this] { SelectFault(); });
    connect(TargetAtpg, &QPushButton::clicked, this, [this] {
        ShowPanel("ATPG");
        Run("atpg");
    });
    JobButtons << TargetAtpg;
    connect(ShowClasses, &QCheckBox::toggled, this,
            [this](bool Show) { Display(FaultTable, Show ? ClassData : FaultData); });
    connect(FaultTable, &QTableView::clicked, this, [this](const QModelIndex &Index) {
        const auto &Data = ShowClasses->isChecked() ? ClassData : FaultData;
        FaultWire->setCurrentText(Data.Value(Index.row(), "wire"));
        FaultValue->setCurrentIndex(Data.Value(Index.row(), "stuck").toInt());
    });
    auto *Atpg = Panel("ATPG", "Automatic Test Patterns");
    auto *A = new QVBoxLayout(Atpg->Body());
    A->setContentsMargins(0, 0, 0, 0);
    A->addWidget(Muted("Selected target follows the Fault Analysis inspector. The current CLI "
                       "searches all faults; this view selects the requested result."));
    SearchLimit = new QSpinBox;
    SearchLimit->setRange(0, 1000000000);
    SearchLimit->setValue(100000);
    SearchLimit->setPrefix("Node limit  ");
    auto *AtpgTools = new QHBoxLayout;
    AtpgTools->addWidget(SearchLimit);
    AtpgTools->addWidget(JobButton("Generate tests", "atpg", "primary"));
    A->addLayout(AtpgTools);
    AtpgResult = new QLabel("No target result yet");
    AtpgResult->setWordWrap(true);
    A->addWidget(AtpgResult);
    AtpgTable = Table();
    AtpgTable->setObjectName("AtpgTable");
    A->addWidget(AtpgTable);
    connect(AtpgTable, &QTableView::clicked, this, [this](const QModelIndex &Index) {
        FaultWire->setCurrentText(AtpgData.Value(Index.row(), "wire"));
        FaultValue->setCurrentIndex(AtpgData.Value(Index.row(), "stuck").toInt());
    });
    auto *Bench = Panel("Benchmark", "Benchmark Dashboard");
    auto *B = new QVBoxLayout(Bench->Body());
    B->setContentsMargins(0, 0, 0, 0);
    auto *BenchTools = new QHBoxLayout;
    auto *Import = new QPushButton("Import CSV…");
    BenchTools->addWidget(Import);
    ChartAxis = new QComboBox;
    ChartAxis->addItems({"gates", "patterns", "block_size", "dropping"});
    BenchTools->addWidget(ChartAxis);
    BenchTools->addWidget(JobButton("Run benchmark", "benchmark", "primary"));
    B->addLayout(BenchTools);
    BenchmarkSummary = new QLabel("No measurements yet");
    BenchmarkSummary->setWordWrap(true);
    B->addWidget(BenchmarkSummary);
    Chart = new BenchmarkChart;
    B->addWidget(Chart, 1);
    BenchmarkTable = Table();
    BenchmarkTable->setMinimumHeight(100);
    B->addWidget(BenchmarkTable, 1);
    B->addWidget(Muted("Hover bars for metrics. GPU bars include transfers and host overhead. "
                       "Chart shows up to 24 rows; the table retains all rows."));
    connect(ChartAxis, &QComboBox::currentTextChanged, this, [this] { UpdateChart(); });
    connect(Import, &QPushButton::clicked, this, [this] {
        QString Path =
            QFileDialog::getOpenFileName(this, "Import benchmark CSV", {}, "CSV (*.csv)");
        if (Path.isEmpty())
            return;
        QFile File(Path);
        if (!File.open(QIODevice::ReadOnly) || File.size() > 64 * 1024 * 1024) {
            Log("Cannot read CSV, or file exceeds 64 MiB.", true);
            return;
        }
        try {
            auto Data = ReadCsv(File.readAll());
            for (const auto &Header :
                 QStringList{"gates", "patterns", "scalar_ms", "cpu_packed_ms", "end_to_end_ms"})
                if (Data.Column(Header) < 0)
                    throw std::runtime_error("This is not a CircuitForge benchmark CSV.");
            BenchmarkData = Data;
            UpdateChart();
            Log("Imported " + QString::number(Data.Rows.size()) + " benchmark rows");
        } catch (const std::exception &E) {
            Log(E.what(), true);
        }
    });
    auto *Settings = Panel("Engine", "Engine Settings");
    auto *E = new QVBoxLayout(Settings->Body());
    E->setContentsMargins(0, 0, 0, 0);
    E->addWidget(Muted("Select the existing CircuitForge CLI executable. GUI rendering and CUDA "
                       "execution use separate processes."));
    EngineEdit = new QLineEdit;
    EngineEdit->setPlaceholderText("Path to CircuitForge.exe");
    EngineEdit->setObjectName("EnginePath");
    E->addWidget(EngineEdit);
    auto *Browse = new QPushButton("Choose executable…");
    E->addWidget(Browse);
    connect(Browse, &QPushButton::clicked, this, [this] {
        QString Path = QFileDialog::getOpenFileName(this, "Choose CircuitForge engine", {},
                                                    "Executable (*.exe)");
        if (!Path.isEmpty())
            EngineEdit->setText(Path);
    });
    JobButtons << Browse;
    E->addStretch();
    auto *Netlist = Panel("Netlist", "Netlist Preview");
    auto *N = new QVBoxLayout(Netlist->Body());
    N->setContentsMargins(0, 0, 0, 0);
    NetlistPreview = new QPlainTextEdit;
    NetlistPreview->setReadOnly(true);
    NetlistPreview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    N->addWidget(NetlistPreview);
    auto *Results = Panel("Logic", "Logic Results");
    auto *R = new QVBoxLayout(Results->Body());
    R->setContentsMargins(0, 0, 0, 0);
    R->addWidget(Muted("Packed CPU truth table · output columns follow the netlist declaration."));
    SimulationTable = Table();
    R->addWidget(SimulationTable);
    auto *Export = new QPushButton("Export latest report…");
    R->addWidget(Export);
    connect(Export, &QPushButton::clicked, this, &MainWindow::ExportCsv);
}
void MainWindow::BuildMenus() {
    auto *File = menuBar()->addMenu("Project");
    auto *New = File->addAction("New circuit", QKeySequence::New, this, &MainWindow::NewCircuit);
    auto *Open =
        File->addAction("Open netlist…", QKeySequence::Open, this, &MainWindow::OpenDialog);
    auto *SaveAction = File->addAction("Save", QKeySequence::Save, this, [this] { Save(); });
    auto *SaveAs = File->addAction("Save as…", QKeySequence::SaveAs, this, [this] { Save(true); });
    EditActions << New << Open << SaveAction << SaveAs;
    File->addSeparator();
    File->addAction("Export latest CSV…", this, &MainWindow::ExportCsv);
    File->addAction("Engine settings…", this, [this] { ShowPanel("Engine"); });
    File->addSeparator();
    File->addAction("Exit", QKeySequence::Quit, this, &QWidget::close);
    auto *Edit = menuBar()->addMenu("Edit");
    auto *Undo = Model.History.createUndoAction(this, "Undo");
    Undo->setShortcut(QKeySequence::Undo);
    auto *Redo = Model.History.createRedoAction(this, "Redo");
    Redo->setShortcut(QKeySequence::Redo);
    Edit->addAction(Undo);
    Edit->addAction(Redo);
    EditActions << Undo << Redo;
    auto *Delete = Edit->addAction("Delete selection", Canvas, &CircuitView::DeleteSelection);
    EditActions << Delete;
    auto *View = menuBar()->addMenu("View");
    View->addAction("Fit circuit", QKeySequence("F"), Canvas, &CircuitView::Fit);
    for (auto It = Panels.begin(); It != Panels.end(); ++It) {
        QString Key = It.key();
        View->addAction(Key + " panel", this, [this, Key] { ShowPanel(Key); });
    }
    View->addSeparator();
    View->addAction("Reset workspace layout", this, &MainWindow::ResetPanels);
    auto *Light = View->addAction("Toggle light / dark", this, [this] {
        Dark = !Dark;
        ApplyTheme(*qApp, Dark);
    ApplyNativeFrame(this,Dark);
        Canvas->viewport()->update();
    });
    Light->setShortcut(QKeySequence("Ctrl+Shift+L"));
    auto *RunMenu = menuBar()->addMenu("Run");
    for (const auto &Command : QStringList{"simulate", "faults", "collapse", "atpg", "benchmark"}) {
        auto *Action = RunMenu->addAction(Command, this, [this, Command] { Run(Command); });
        EditActions << Action;
    }
    RunMenu->addAction("Stop", QKeySequence("Shift+F5"), &Runner, &EngineRunner::Stop);
    auto *Toolbar = addToolBar("Workspace");
    Toolbar->setMovable(false);
    auto *Brand = new QLabel("  CircuitForge  ");
    Brand->setObjectName("Brand");
    Toolbar->addWidget(Brand);
    Toolbar->addAction(Open);
    Toolbar->addAction(SaveAction);
    Toolbar->addSeparator();
    Toolbar->addAction("Fit canvas", Canvas, &CircuitView::Fit);
    Toolbar->addAction("Faults", this, [this] { ShowPanel("Faults"); });
    Toolbar->addAction("ATPG", this, [this] { ShowPanel("ATPG"); });
    Toolbar->addAction("Benchmarks", this, [this] { ShowPanel("Benchmark"); });
}
void MainWindow::Log(const QString &Text, bool Error) {
    if (!Console)
        return;
    for (const QString &Line : Text.split('\n', Qt::SkipEmptyParts)) {
        QString Color = Error ? "#F07178" : Line.startsWith("Coverage:") ? "#4BCBA5" : "#9299AA";
        Console->append("<span style='color:" + Color + "'>" +
                        QTime::currentTime().toString("HH:mm:ss") + "  " + Line.toHtmlEscaped() +
                        "</span>");
    }
    Console->moveCursor(QTextCursor::End);
}
void MainWindow::UpdateModel() {
    QString SelectedWire = FaultWire->currentText();
    FaultWire->blockSignals(true);
    FaultWire->clear();
    FaultWire->addItems(Model.WireNames());
    if (FaultWire->findText(SelectedWire) >= 0)
        FaultWire->setCurrentText(SelectedWire);
    FaultWire->blockSignals(false);
    FaultData = {};
    AtpgData = {};
    ClassData = {};
    LastCoverage = "Not measured";
    CoverageLabel->setText("Coverage: not measured · run analysis for this circuit");
    Display(FaultTable, {});
    Display(AtpgTable, {});
    AtpgResult->setText("No target result for this revision");
    try {
        NetlistPreview->setPlainText(Model.Netlist());
    } catch (const std::exception &E) {
        NetlistPreview->setPlainText("Incomplete circuit\n\n" + QString::fromUtf8(E.what()));
    }
    UpdateSelection(SelectedId);
    UpdateStatus();
}
void MainWindow::UpdateSelection(int Id) {
    SelectedId = Id;
    bool Valid = Model.Nodes.contains(Id);
    NameEdit->setEnabled(Valid);
    SelectionLabel->setText(Valid ? Model.Nodes[Id].Type + " · node " + QString::number(Id)
                                  : "No selection");
    NameEdit->setText(Valid ? Model.Nodes[Id].Name : QString());
}
void MainWindow::UpdateStatus() {
    if (!StatusLabel)
        return;
    int Gates = 0;
    for (const auto &N : Model.Nodes)
        if (N.Type != "INPUT" && N.Type != "OUTPUT")
            ++Gates;
    StatusLabel->setText((CurrentPath.isEmpty() ? "Untitled" : QFileInfo(CurrentPath).fileName()) +
                         "   ·   " + QString::number(Gates) + " gates / " +
                         QString::number(Model.WireNames().size()) + " wires   ·   " + GpuName +
                         "   ·   " + Backend->currentText());
}
void MainWindow::Display(QTableView *View, const CsvTable &Data) {
    auto *Previous = View->model();
    auto *Items = new QStandardItemModel(View);
    const QMap<QString, QString> Labels = {{"fault_id", "Fault ID"},
                                           {"wire", "Wire"},
                                           {"stuck", "Fault type"},
                                           {"detected", "Status"},
                                           {"first_pattern", "First pattern"},
                                           {"detecting_patterns", "Detecting patterns"},
                                           {"observed_detecting_patterns", "Observed patterns"},
                                           {"representative", "Representative ID"},
                                           {"nodes", "Search nodes"},
                                           {"pattern", "Pattern"},
                                           {"status", "Outcome"}};
    QStringList Headers;
    for (const auto &Header : Data.Headers)
        Headers << Labels.value(Header, Header);
    Items->setHorizontalHeaderLabels(Headers);
    int Displayed=0;
    for (const auto &Row : Data.Rows) {
        if(Displayed++>=10000)break;
        QList<QStandardItem *> Cells;
        for (int Column = 0; Column < Row.size(); ++Column) {
            QString Value = Row[Column];
            if (Data.Headers[Column] == "detected")
                Value = Value == "1" ? "Detected" : "Undetected";
            if (Data.Headers[Column] == "stuck")
                Value = "SA" + Value;
            auto *Item = new QStandardItem(Value);
            if (Data.Headers[Column] == "detected")
                Item->setForeground(QColor(Row[Column] == "1" ? "#4BCBA5" : "#F0B35B"));
            Cells << Item;
        }
        Items->appendRow(Cells);
    }
    if(Data.Rows.size()>10000)Log("Table shows the first 10,000 rows. Export the CSV for the complete report.");
    View->setModel(Items);
    if (Previous)
        Previous->deleteLater();
    View->resizeColumnsToContents();
}
QString MainWindow::EnginePath() const {
    return EngineEdit->text().trimmed();
}
void MainWindow::SetEnginePath(const QString &Path) {
    EngineEdit->setText(Path);
}
void MainWindow::Run(const QString &Command) {
    try {
        if (Runner.Busy())
            throw std::runtime_error("Stop the current job before starting another.");
        JobNetlist = Model.Netlist();
        JobOptions O;
        O.Command = Command;
        O.Gpu = Backend->currentText() == "GPU";
        O.Drop = Dropping->isChecked();
        O.Patterns = PatternCount->value();
        O.Seed = Seed->value();
        O.Block = BlockSize->currentText().toInt();
        O.SearchLimit = SearchLimit->value();
        Runner.Start(EnginePath(), JobNetlist, O);
    } catch (const std::exception &E) {
        Log(E.what(), true);
        ShowPanel("Console");
        if (!QFileInfo::exists(EnginePath()))
            ShowPanel("Engine");
    }
}
void MainWindow::Finish(const QString &Command, const QByteArray &Csv, bool Success) {
    if (!Success)
        return;
    try {
        auto Data = ReadCsv(Csv);
        LatestCsv = Csv;
        if (Command == "faults") {
            FaultData = Data;
            int Detected = 0;
            for (int R = 0; R < Data.Rows.size(); ++R)
                Detected += Data.Value(R, "detected") == "1";
            LastCoverage = Data.Rows.isEmpty()
                               ? "N/A"
                               : QString::number(100.0 * Detected / Data.Rows.size(), 'f', 1) + "%";
            CoverageLabel->setText("Coverage  " + LastCoverage + "   ·   " +
                                   QString::number(Detected) + " / " +
                                   QString::number(Data.Rows.size()) + " detected");
            ShowClasses->setChecked(false);
            Display(FaultTable, FaultData);
            ShowPanel("Faults");
            SelectFault();
        } else if (Command == "collapse") {
            ClassData = Data;
            ShowClasses->setChecked(true);
            Display(FaultTable, ClassData);
            ShowPanel("Faults");
        } else if (Command == "atpg") {
            AtpgData = Data;
            Display(AtpgTable, Data);
            ShowPanel("ATPG");
            UpdateAtpg();
        } else if (Command == "benchmark") {
            if (BenchmarkData.Headers != Data.Headers)
                BenchmarkData = Data;
            else
                BenchmarkData.Rows.append(Data.Rows);
            UpdateChart();
            ShowPanel("Benchmark");
        } else {
            Display(SimulationTable, Data);
            ShowPanel("Logic");
        }
        Log("Loaded " + QString::number(Data.Rows.size()) + " " + Command + " result rows");
    } catch (const std::exception &E) {
        Log(E.what(), true);
    }
}
void MainWindow::SelectFault() {
    Canvas->Highlight(FaultWire->currentText());
    const auto &Data = ShowClasses->isChecked() ? ClassData : FaultData;
    for (int R = 0; R < Data.Rows.size(); ++R)
        if (Data.Value(R, "wire") == FaultWire->currentText() &&
            Data.Value(R, "stuck").toInt() == FaultValue->currentIndex()) {
            FaultTable->selectRow(R);
            break;
        }
    UpdateAtpg();
}
void MainWindow::UpdateAtpg() {
    for (int R = 0; R < AtpgData.Rows.size(); ++R)
        if (AtpgData.Value(R, "wire") == FaultWire->currentText() &&
            AtpgData.Value(R, "stuck").toInt() == FaultValue->currentIndex()) {
            QString Status = AtpgData.Value(R, "status");
            AtpgResult->setText(
                FaultWire->currentText() + " " + FaultValue->currentText() + " · " + Status +
                "\nTest vector: " +
                (AtpgData.Value(R, "pattern").isEmpty() ? "—" : AtpgData.Value(R, "pattern")) +
                " · search nodes: " + AtpgData.Value(R, "nodes") + "\nInput order: " + [this] {
                    QStringList Names;
                    for (const auto &N : Model.Nodes)
                        if (N.Type == "INPUT")
                            Names << N.Name;
                    return Names.join(" ");
                }());
            AtpgTable->selectRow(R);
            return;
        }
    AtpgResult->setText("No result for " + FaultWire->currentText() + " " +
                        FaultValue->currentText() + ". Aborted means unresolved, never redundant.");
}
void MainWindow::UpdateChart() {
    Chart->Axis = ChartAxis->currentText();
    Chart->SetData(BenchmarkData);
    Display(BenchmarkTable, BenchmarkData);
    int R = BenchmarkData.Rows.size() - 1;
    if (R >= 0)
        BenchmarkSummary->setText("Latest: " + BenchmarkData.Value(R, "gates") + " gates · " +
                                  BenchmarkData.Value(R, "patterns") + " patterns · kernel " +
                                  BenchmarkData.Value(R, "gpu_kernel_ms") +
                                  " ms\nPacked CPU / GPU end-to-end: " +
                                  (BenchmarkData.Value(R, "cpu_over_gpu").isEmpty()
                                       ? "N/A"
                                       : BenchmarkData.Value(R, "cpu_over_gpu")) +
                                  " · last circuit fault coverage: " + LastCoverage);
}
void MainWindow::ShowPanel(const QString &Key) {
    auto *Value = Panels.value(Key);
    if (!Value)
        return;
    Value->show();
    Value->SetCollapsed(false);
    Value->KeepInWorkspace();
    Value->raise();
}
void MainWindow::ResetPanels() {
    int W = Workspace->width(), H = Workspace->height();
    Panels["Library"]->setGeometry(18, 18, 224, qMin(485, H - 260));
    Panels["Properties"]->setGeometry(W - 280, 18, 260, 280);
    Panels["Simulation"]->setGeometry(qMax(250, (W - 700) / 2), qMax(330, H - 370), 700, 160);
    Panels["Console"]->setGeometry(18, H - 200, W - 36, 184);
    Panels["Faults"]->setGeometry(qMax(260, W - 720), 40, 690, 460);
    Panels["ATPG"]->setGeometry(qMax(250, (W - 700) / 2), 70, 700, 440);
    Panels["Benchmark"]->setGeometry(qMax(250, (W - 850) / 2), 35, 850, 530);
    Panels["Engine"]->setGeometry((W - 520) / 2, 100, 520, 230);
    Panels["Netlist"]->setGeometry((W - 640) / 2, 60, 640, 470);
    Panels["Logic"]->setGeometry((W - 650) / 2, 60, 650, 430);
    for (auto It = Panels.begin(); It != Panels.end(); ++It) {
        It.value()->SetCollapsed(false);
        It.value()->setVisible(
            QStringList{"Library", "Properties", "Simulation", "Console"}.contains(It.key()));
        It.value()->KeepInWorkspace();
    }
    Restored = true;
}
void MainWindow::resizeEvent(QResizeEvent *E) {
    QMainWindow::resizeEvent(E);
    Canvas->setGeometry(Workspace->rect());
    if (Restored)
        for (auto *Value : Panels)
            Value->KeepInWorkspace();
}
void MainWindow::SaveWorkspace() {
    if (!Persist)
        return;
    QSettings Settings;
    Settings.setValue("Geometry", saveGeometry());
    Settings.setValue("Engine", EnginePath());
    Settings.setValue("Dark", Dark);
    for (auto It = Panels.begin(); It != Panels.end(); ++It) {
        Settings.setValue("Panels/" + It.key() + "/geometry", It.value()->WorkspaceGeometry());
        Settings.setValue("Panels/" + It.key() + "/visible", It.value()->isVisible());
        Settings.setValue("Panels/" + It.key() + "/collapsed", It.value()->IsCollapsed());
    }
}
void MainWindow::RestoreWorkspace() {
    QSettings Settings;
    if (Settings.contains("Geometry"))
        restoreGeometry(Settings.value("Geometry").toByteArray());
    ResetPanels();
    for (auto It = Panels.begin(); It != Panels.end(); ++It) {
        QString Key = "Panels/" + It.key();
        if (Settings.contains(Key + "/geometry"))
            It.value()->setGeometry(Settings.value(Key + "/geometry").toRect());
        It.value()->setVisible(Settings.value(Key + "/visible", It.value()->isVisible()).toBool());
        It.value()->SetCollapsed(Settings.value(Key + "/collapsed", false).toBool());
        It.value()->KeepInWorkspace();
    }
}
bool MainWindow::CanDiscard() {
    if (Model.History.isClean())
        return true;
    auto Choice = QMessageBox::question(
        this, "Unsaved circuit", "Save the current circuit before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (Choice == QMessageBox::Cancel)
        return false;
    if (Choice == QMessageBox::Save)
        return Save();
    return true;
}
void MainWindow::NewCircuit() {
    if (Runner.Busy() || !CanDiscard())
        return;
    CurrentPath.clear();
    Model.Clear();
    Canvas->Fit();
    Log("New circuit");
}
void MainWindow::OpenDialog() {
    if (Runner.Busy() || !CanDiscard())
        return;
    QString Path = QFileDialog::getOpenFileName(this, "Open netlist", CurrentPath,
                                                "Circuit netlists (*.txt *.bench);;All files (*)");
    if (!Path.isEmpty())
        OpenPath(Path);
}
void MainWindow::OpenPath(const QString &Path) {
    try {
        if (Runner.Busy())
            throw std::runtime_error("Stop the current job before opening a circuit.");
        Model.Load(Path);
        CurrentPath = Path;
        UpdateStatus();
        setWindowTitle("CircuitForge Desktop — " + QFileInfo(Path).fileName());
        Canvas->Fit();
        Log("Loaded " + Path);
    } catch (const std::exception &E) {
        Log(E.what(), true);
    }
}
bool MainWindow::Save(bool SaveAs) {
    QString Path = CurrentPath;
    if (SaveAs || Path.isEmpty() || Path.endsWith(".bench", Qt::CaseInsensitive))
        Path = QFileDialog::getSaveFileName(this, "Save CircuitForge netlist", Path,
                                            "CircuitForge (*.txt)");
    if (Path.isEmpty())
        return false;
    try {
        Model.Save(Path);
        CurrentPath = Path;
        UpdateStatus();
        setWindowTitle("CircuitForge Desktop — " + QFileInfo(Path).fileName());
        Log("Saved netlist and layout sidecar");
        return true;
    } catch (const std::exception &E) {
        Log(E.what(), true);
        QMessageBox::warning(this, "Cannot save circuit", E.what());
        return false;
    }
}
void MainWindow::ExportCsv() {
    if (LatestCsv.isEmpty()) {
        Log("Run a job before exporting a report.", true);
        return;
    }
    QString Path =
        QFileDialog::getSaveFileName(this, "Export latest engine report", {}, "CSV (*.csv)");
    if (!Path.isEmpty())
        try {
            WriteFile(Path, LatestCsv);
        } catch (const std::exception &E) {
            Log(E.what(), true);
        }
}
void MainWindow::closeEvent(QCloseEvent *E) {
    if (Runner.Busy()) {
        auto Choice = QMessageBox::question(this, "Job running", "Stop the engine job and close?",
                                            QMessageBox::Yes | QMessageBox::No);
        if (Choice != QMessageBox::Yes) {
            E->ignore();
            return;
        }
        Runner.Stop();
    }
    if (!CanDiscard()) {
        E->ignore();
        return;
    }
    SaveWorkspace();
    E->accept();
}
void MainWindow::QueryGpu() {
    auto *Process = new QProcess(this);
    QString Program = QStandardPaths::findExecutable("nvidia-smi");
    if (Program.isEmpty()) {
        GpuName = "GPU: unavailable (nvidia-smi not found)";
        UpdateStatus();
        return;
    }
    connect(Process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, Process](int Code, QProcess::ExitStatus) {
                QString Info = QString::fromLocal8Bit(Process->readAllStandardOutput()).trimmed();
                GpuName = Code == 0 && !Info.isEmpty() ? Info.split('\n').first()
                                                       : "GPU information unavailable";
                UpdateStatus();
                Process->deleteLater();
            });
    connect(Process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        GpuName = "GPU query unavailable";
        UpdateStatus();
    });
    Process->start(Program, {"--query-gpu=name,memory.total", "--format=csv,noheader"});
    QTimer::singleShot(5000, Process, [Process] {
        if (Process->state() != QProcess::NotRunning)
            Process->kill();
    });
}
} // namespace Desktop
