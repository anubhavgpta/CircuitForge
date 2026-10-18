#pragma once
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>
#include <memory>
namespace Desktop {
struct JobOptions {
    QString Command = "faults";
    bool Gpu = false, Drop = false;
    int Patterns = 1024, Seed = 1, Block = 128, SearchLimit = 100000;
};
class EngineRunner : public QObject {
    Q_OBJECT
  public:
    explicit EngineRunner(QObject *Parent = nullptr);
    ~EngineRunner() override;
    static QStringList Arguments(const JobOptions &Options, const QString &Netlist,
                                 const QString &Report);
    void Start(const QString &Executable, const QString &Netlist, const JobOptions &Options);
    void Stop();
    bool Busy() const {
        return Process.state() != QProcess::NotRunning;
    }
    QProcess Process;
  signals:
    void Output(const QString &Text, bool Error);
    void State(const QString &State, bool Busy);
    void Completed(const QString &Command, const QByteArray &Csv, bool Success);

  private:
    std::unique_ptr<QTemporaryDir> Directory;
    QString Report, Command;
    bool Stopping = false;
};
struct CsvTable {
    QStringList Headers;
    QList<QStringList> Rows;
    int Column(const QString &Name) const {
        return Headers.indexOf(Name);
    }
    QString Value(int Row, const QString &Name) const;
};
CsvTable ReadCsv(const QByteArray &Data);
} // namespace Desktop
