#include "EngineRunner.h"
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <stdexcept>
namespace Desktop {
namespace {
void Fail(const QString &Message) {
    throw std::runtime_error(Message.toStdString());
}
} // namespace
EngineRunner::EngineRunner(QObject *Parent) : QObject(Parent) {
    connect(&Process, &QProcess::readyReadStandardOutput, this, [this] {
        emit Output(QString::fromLocal8Bit(Process.readAllStandardOutput()), false);
    });
    connect(&Process, &QProcess::readyReadStandardError, this,
            [this] { emit Output(QString::fromLocal8Bit(Process.readAllStandardError()), true); });
    connect(&Process, &QProcess::started, this,
            [this] { emit State("Running · " + Command, true); });
    connect(&Process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError Error) {
        emit Output("Engine error: " + Process.errorString(), true);
        if (Error == QProcess::FailedToStart) {
            emit State("Failed to start", false);
            emit Completed(Command, {}, false);
        }
    });
    connect(
        &Process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int Code, QProcess::ExitStatus Status) {
            bool Success = !Stopping && Code == 0 && Status == QProcess::NormalExit;
            QByteArray Csv;
            if (Success) {
                QFile File(Report);
                if (File.open(QIODevice::ReadOnly)) {
                    if (File.size() > 64 * 1024 * 1024) {
                        Success = false;
                        emit Output(
                            "Report exceeds 64 MiB. Reduce the pattern count for GUI inspection.",
                            true);
                    } else
                        Csv = File.readAll();
                } else {
                    Success = false;
                    emit Output("The engine did not produce a readable report.", true);
                }
            }
            emit State(Stopping  ? "Stopped"
                       : Success ? "Completed"
                                 : "Failed (exit " + QString::number(Code) + ")",
                       false);
            emit Completed(Command, Csv, Success);
        });
}
EngineRunner::~EngineRunner() {
    if (Busy()) {
        Process.kill();
        Process.waitForFinished(1500);
    }
}
QStringList EngineRunner::Arguments(const JobOptions &O, const QString &Netlist,
                                    const QString &Report) {
    if (!QStringList{"simulate", "faults", "collapse", "atpg", "benchmark"}.contains(O.Command))
        Fail("Unsupported engine command.");
    if (O.Patterns < 1 || O.Patterns > 16777216 || O.Seed < 0 || O.Block < 32 || O.Block > 1024 ||
        O.Block % 32 || O.SearchLimit < 0)
        Fail("Invalid simulation settings.");
    QStringList Args{O.Command, Netlist, "--output", Report};
    if (O.Command == "atpg")
        Args << "--search-limit" << QString::number(O.SearchLimit);
    if (O.Command == "simulate" || O.Command == "faults" || O.Command == "benchmark")
        Args << "--patterns" << QString::number(O.Patterns) << "--seed" << QString::number(O.Seed);
    if (O.Command == "faults" || O.Command == "benchmark") {
        Args << (O.Gpu ? "--gpu" : "--cpu") << "--block-size" << QString::number(O.Block);
        if (O.Drop)
            Args << "--fault-dropping";
    }
    return Args;
}
void EngineRunner::Start(const QString &Executable, const QString &Netlist, const JobOptions &O) {
    if (Busy())
        Fail("An engine job is already running.");
    if (!QFileInfo::exists(Executable))
        Fail("Engine executable not found. Choose build-gpu/CircuitForge.exe in Engine settings.");
    Directory = std::make_unique<QTemporaryDir>();
    if (!Directory->isValid())
        Fail("Cannot create job directory.");
    QString Input = Directory->filePath("circuit.txt");
    QFile File(Input);
    QByteArray Bytes = Netlist.toUtf8();
    if (!File.open(QIODevice::WriteOnly) || File.write(Bytes) != Bytes.size())
        Fail("Cannot write engine input.");
    File.close();
    Report = Directory->filePath("report.csv");
    Command = O.Command;
    Stopping = false;
    Process.setProgram(QFileInfo(Executable).absoluteFilePath());
    Process.setArguments(Arguments(O, Input, Report));
    Process.setWorkingDirectory(Directory->path());
    emit State("Starting · " + Command, true);
    emit Output("Starting " + Command + " using " + QFileInfo(Executable).fileName(), false);
    Process.start();
}
void EngineRunner::Stop() {
    if (!Busy())
        return;
    Stopping = true;
    emit State("Stopping…", true);
    Process.terminate();
    QTimer::singleShot(1000, this, [this] {
        if (Stopping && Busy())
            Process.kill();
    });
}
QString CsvTable::Value(int Row, const QString &Name) const {
    int Col = Column(Name);
    return Row >= 0 && Row < Rows.size() && Col >= 0 && Col < Rows[Row].size() ? Rows[Row][Col]
                                                                               : QString();
}
CsvTable ReadCsv(const QByteArray &Data) {
    CsvTable Result;
    QString Text = QString::fromUtf8(Data);
    if (Text.startsWith(QChar(0xfeff)))
        Text.remove(0, 1);
    QString Cell;
    QStringList Row;
    bool Quoted = false, Closed = false;
    auto FinishRow = [&] {
        Row << Cell;
        Cell.clear();
        if (!(Row.size() == 1 && Row[0].isEmpty())) {
            if (Result.Headers.isEmpty())
                Result.Headers = Row;
            else {
                if (Row.size() != Result.Headers.size())
                    Fail("CSV row has an inconsistent number of columns.");
                Result.Rows << Row;
            }
        }
        Row.clear();
        Closed = false;
    };
    for (int I = 0; I < Text.size(); ++I) {
        QChar C = Text[I];
        if (Quoted) {
            if (C == '"') {
                if (I + 1 < Text.size() && Text[I + 1] == '"') {
                    Cell += '"';
                    ++I;
                } else {
                    Quoted = false;
                    Closed = true;
                }
            } else
                Cell += C;
        } else if (C == '"') {
            if (!Cell.isEmpty() || Closed)
                Fail("Malformed CSV quoting.");
            Quoted = true;
        } else if (C == ',') {
            Row << Cell;
            Cell.clear();
            Closed = false;
        } else if (C == '\n') {
            FinishRow();
        } else if (C == '\r') {
            if (I + 1 < Text.size() && Text[I + 1] == '\n')
                ++I;
            FinishRow();
        } else {
            if (Closed)
                Fail("Unexpected text after CSV quote.");
            Cell += C;
        }
    }
    if (Quoted)
        Fail("Unterminated CSV quote.");
    if (!Cell.isEmpty() || !Row.isEmpty() || Closed)
        FinishRow();
    if (Result.Headers.isEmpty())
        Fail("Empty CSV report.");
    return Result;
}
} // namespace Desktop
