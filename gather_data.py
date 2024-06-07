#!/usr/bin/env python3
import csv
import itertools
import os
import random
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path


def dir(path: str | Path) -> Path:
    result = Path(path)
    result.mkdir(parents=True, exist_ok=True)
    assert result.is_dir()
    return result


BENCHMARK_LENGTH_SECONDS: int = 10
TMP_DIR = dir("/tmp")
CONFIG_PATH = TMP_DIR / "raid.conf"
DISKS_DIR = dir(TMP_DIR / "disks")
DISK_SUFFIX = ".bin"
N_BLOCKS = 10000
RAID_TYPE = "RTP"
MAX_CONCURRENT_THREADS = 1


def disk_path(disk_num: int) -> Path:
    return (DISKS_DIR / str(disk_num)).with_suffix(DISK_SUFFIX)


def is_prime(n: int) -> bool:
    match n:
        case 0, 1:
            return False
        case 2, 3:
            return True
        case _:
            if n % 2 == 0:
                return False
            for i in range(3, int(n**0.5) + 1, 2):
                if n % i == 0:
                    return False
            return True


@dataclass
class Throughput:
    user: str
    proc: str
    wall: str


THROUGHPUT_REGEXP = re.compile(r"(\S+) throughput \(bytes/s\): (\S+)\t(\S+)\t(\S+)")


def parse_throughput(s: str) -> dict[str, Throughput]:
    return {
        name: Throughput(user, proc, wall)
        for (name, user, proc, wall) in THROUGHPUT_REGEXP.findall(s)
    }


@dataclass
class BenchResult:
    read: Throughput
    write: Throughput


def parse_bench_result(s: str | bytes) -> BenchResult:
    return BenchResult(**{k.lower(): v for k, v in parse_throughput(s).items()})


@dataclass
class Config:
    dimension: int
    block_size: int
    failures: int
    mode: str
    rw_ratio: float

    def __post_init__(self):
        if not is_prime(self.dimension + 1):
            raise ValueError("Dimension+1 should be prime")
        if self.mode not in ["l", "r"]:
            raise ValueError("Mode should be either l or r")

    def write(self, config_path: Path | str):
        L_BRACE = "{"
        R_BRACE = "}"

        failed = random.sample(range(self.code_length), self.failures)
        with open(config_path, "w") as f:
            f.write(
                f"""\
DiskCapacity = {self.block_size * N_BLOCKS}
MaxConcurrentThreads = {MAX_CONCURRENT_THREADS}
RAIDType = {RAID_TYPE}

{RAID_TYPE} {L_BRACE}
    Dimension = {self.dimension}
    StripeUnitSize = {self.block_size}
{R_BRACE}
"""
            )
            for i in range(self.code_length):
                f.write(
                    f"""
disk {L_BRACE}
    file = "{disk_path(i)}"
    online = {str(i not in failed).lower()}
{R_BRACE}
"""
                )

    @property
    def code_length(self) -> int:
        return self.dimension + 3

    def run(
        self, executable: Path, config_path: Path | str = CONFIG_PATH
    ) -> BenchResult:
        assert executable.is_file()
        remove_disks()
        self.write(config_path=config_path)
        subprocess.run([executable, config_path, "i"]).check_returncode()
        res = subprocess.run(
            [
                executable,
                config_path,
                "b",
                self.mode,
                "a",
                str(self.rw_ratio),
                str(self.block_size),
                str(MAX_CONCURRENT_THREADS),
                str(BENCHMARK_LENGTH_SECONDS),
            ],
            capture_output=True,
            text=True,
        )
        print(res.stdout)
        res.check_returncode()
        return parse_bench_result(res.stdout)


def remove_disks():
    for name in os.listdir(DISKS_DIR):
        file = DISKS_DIR / name
        if file.suffix == DISK_SUFFIX:
            file.unlink()


def main():
    random.seed(4)
    OUTPUT_FILE = f"{RAID_TYPE}.csv"
    EXECUTABLE = Path(".") / "cmake-build-release-llvm" / "testbed"
    DIMENSIONS = [4, 6, 12, 22, 100]
    BLOCK_SIZES = [32, 512, 1024, 2048, 4096]
    FAILURES = [0, 1, 2, 3]
    MODE = ["l", "r"]
    RW_RATIO = [0.1, 0.5, 0.9]

    with open(OUTPUT_FILE, "w") as f:
        csv.writer(f).writerow(
            [
                "dimension",
                "block_size",
                "failures",
                "mode",
                "rw_ratio",
                "read_user",
                "read_proc",
                "read_wall",
                "write_user",
                "write_proc",
                "write_wall",
            ]
        )

    for dimension, block_size, failures, mode, rw_ratio in itertools.product(
        DIMENSIONS,
        BLOCK_SIZES,
        FAILURES,
        MODE,
        RW_RATIO,
    ):
        config = Config(
            dimension=dimension,
            block_size=block_size,
            failures=failures,
            mode=mode,
            rw_ratio=rw_ratio,
        )
        result = config.run(executable=EXECUTABLE)
        with open(OUTPUT_FILE, "a") as f:
            csv.writer(f).writerow(
                [
                    config.dimension,
                    config.block_size,
                    config.failures,
                    config.mode,
                    config.rw_ratio,
                    result.read.user,
                    result.read.proc,
                    result.read.wall,
                    result.write.user,
                    result.write.proc,
                    result.write.wall,
                ]
            )

    remove_disks()


if __name__ == "__main__":
    main()
