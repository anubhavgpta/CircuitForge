#pragma once
#include "Integration/EngineRunner.h"
#include "Models/CircuitModel.h"
#include <QMainWindow>
#include <QMap>
class QCloseEvent;
class QResizeEvent;
class QTableView;
class QStandardItemModel;
class QPlainTextEdit;
class QTextEdit;
class QLineEdit;
class QLabel;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QPushButton;
namespace Desktop {
class FloatingPanel;
class CircuitView;
class BenchmarkChart;
class GateLibrary;
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(bool RestoreSettings = true);
    ~MainWindow() override;
    CircuitModel Model;
    EngineRunner Runner;
    CircuitView *Canvas;
    void OpenPath(const QString &Path);
    void Run(const QString &Command);
    void Log(const QString &Text, bool Error = false);
    QString EnginePath() const;
    void SetEnginePath(const QString &Path);
    void ResetPanels();

  protected:
    void resizeEvent(QResizeEvent *) override;
    void closeEvent(QCloseEvent *) override;

  private:
    QWidget *Workspace;
    QMap<QString, FloatingPanel *> Panels;
    QString CurrentPath, GpuName = "GPU: checking…", LastCoverage = "Not measured";
    QString JobNetlist; // Result provenance: edits are locked for the duration of the process.
    bool Persist = true, Restored = false, Dark = true;
    int SelectedId = -1;
    QLineEdit *EngineEdit, *NameEdit;
    QLabel *SelectionLabel, *StateLabel, *StatusLabel, *CoverageLabel, *AtpgResult,
        *BenchmarkSummary;
    QSpinBox *PatternCount, *Seed, *SearchLimit;
    QComboBox *Backend, *BlockSize, *FaultWire, *FaultValue, *ChartAxis;
    QCheckBox *Dropping, *ShowClasses;
    QTextEdit *Console;
    QPlainTextEdit *NetlistPreview;
    QTableView *FaultTable, *AtpgTable, *BenchmarkTable, *SimulationTable;
    BenchmarkChart *Chart;
    QList<QPushButton *> JobButtons;
    QList<QAction *> EditActions;
    CsvTable FaultData, AtpgData, ClassData, BenchmarkData;
    QByteArray LatestCsv;
    FloatingPanel *Panel(const QString &Key, const QString &Title);
    QPushButton *JobButton(const QString &Text, const QString &Command,
                           const QString &Variant = {});
    void BuildMenus();
    void BuildPanels();
    void UpdateModel();
    void UpdateStatus();
    void UpdateSelection(int Id);
    void Display(QTableView *Table, const CsvTable &Data);
    void Finish(const QString &Command, const QByteArray &Csv, bool Success);
    void SelectFault();
    void UpdateAtpg();
    void UpdateChart();
    void ShowPanel(const QString &Key);
    void NewCircuit();
    void OpenDialog();
    bool Save(bool SaveAs = false);
    bool CanDiscard();
    void ExportCsv();
    void SaveWorkspace();
    void RestoreWorkspace();
    void QueryGpu();
};
} // namespace Desktop
