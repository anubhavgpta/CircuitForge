"""Generate deterministic DAG fixtures and run matched benchmarks. No dependencies."""
import argparse, csv, io, random, subprocess
from pathlib import Path
Parser = argparse.ArgumentParser()
Parser.add_argument("--exe", default="build-gpu/CircuitForge.exe")
Parser.add_argument("--output", default="benchmarks/results.csv")
Parser.add_argument("--cpu", action="store_true")
Args = Parser.parse_args()
Root = Path(__file__).resolve().parents[1]
Rows = []
for Size in (16, 128, 512):
    Rng = random.Random(20261018 + Size)
    Names = [f"i{I}" for I in range(16)]
    Lines = ["INPUT " + " ".join(Names)]
    for I in range(Size):
        A, B = Rng.sample(Names, 2)
        Name = f"n{I}"
        Lines.append(f"{Rng.choice(['AND','OR','XOR','NAND','NOR','XNOR'])} {Name} {A} {B}")
        Names.append(Name)
    Lines.insert(1, "OUTPUT " + " ".join(Names[-4:]))
    Netlist = Root / f"circuits/benchmarks/Generated{Size}.txt"
    Netlist.write_text("\n".join(Lines) + "\n")
    for Count in (33, 257):
        for Block in ((128,) if Args.cpu else (64, 128, 256)):
            for Drop in (False, True):
                Cmd = [str((Root/Args.exe).resolve()), "benchmark", str(Netlist), "--patterns", str(Count), "--seed", "20261018", "--block-size", str(Block), "--repetitions", "3", "--cpu" if Args.cpu else "--gpu"]
                if Drop: Cmd += ["--fault-dropping"]
                Result = subprocess.run(Cmd, cwd=Root, text=True, capture_output=True, check=True)
                Rows.extend(csv.DictReader(io.StringIO(Result.stdout)))
                print(f"Finished gates={Size} patterns={Count} block={Block} dropping={Drop}", flush=True)
with (Root/Args.output).open("w", newline="") as File:
    Writer = csv.DictWriter(File, fieldnames=list(Rows[0]))
    Writer.writeheader(); Writer.writerows(Rows)
