"""Построение графиков по результатам SmartPointersBenchmark.

Читает results/time.csv и results/memory.csv, сохраняет PNG в results/plots:
  time_per_op.png  - время одной операции от N (6 сценариев)
  time_total.png   - суммарное время от N, log-log (рост линейный)
  speedup.png      - во сколько раз наши указатели быстрее std при наибольшем N
  memory.png       - байт на объект (указатель + выделенная куча)

Запуск:  python scripts/plot_results.py [--results DIR]
"""

import argparse
import csv
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")  # без окна: только запись в файл
import matplotlib.pyplot as plt  # noqa: E402

# Цвет и маркер закреплены за «семейством» варианта и одинаковы на всех графиках
FAMILY_STYLE = {
    "ours": {"color": "#2a78d6", "marker": "o", "label": "наш (UnqPtr / ShrdPtr)"},
    "std": {"color": "#eb6834", "marker": "s", "label": "STL"},
    "raw": {"color": "#1baf7a", "marker": "^", "label": "сырой указатель"},
    "make_shared": {"color": "#eda100", "marker": "D", "label": "std::make_shared"},
}
FAMILY_ORDER = ["ours", "std", "raw", "make_shared"]

SCENARIO_TITLES = {
    "unique_create": "Создание + удаление (unique)",
    "shared_create": "Создание + удаление (shared)",
    "shared_copy": "Копирование ShrdPtr",
    "pass_by_value": "Передача в функцию по значению",
    "unique_array_create": "Массив int[16] (unique)",
    "shared_array_create": "Массив int[16] (shared)",
}

SURFACE = "#fcfcfb"
TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
TEXT_MUTED = "#898781"
GRID = "#e1e0d9"
AXIS = "#c3c2b7"


def get_family(variant):
    """Относит вариант к семейству по имени: наш / STL / make_shared / сырой."""
    if "UnqPtr" in variant or "ShrdPtr" in variant:
        return "ours"
    if "make_shared" in variant:
        return "make_shared"
    if variant.startswith("std::"):
        return "std"
    return "raw"


def read_time(path):
    """scenario -> variant -> [(count, ns_per_op, total_ms)], в порядке файла."""
    data = defaultdict(lambda: defaultdict(list))
    with open(path, newline="", encoding="utf-8") as file:
        for row in csv.DictReader(file):
            data[row["scenario"]][row["variant"]].append(
                (int(row["count"]), float(row["ns_per_op"]), float(row["total_ms"]))
            )
    return data


def read_memory(path):
    """Строки для наибольшего N: [(variant, bytes_per_object, allocations_per_object)]."""
    with open(path, newline="", encoding="utf-8") as file:
        rows = list(csv.DictReader(file))
    max_count = max(int(row["count"]) for row in rows)
    return max_count, [
        (row["variant"], float(row["bytes_per_object"]), float(row["allocations_per_object"]))
        for row in rows
        if int(row["count"]) == max_count
    ]


def setup_style():
    plt.rcParams.update({
        "figure.facecolor": SURFACE,
        "axes.facecolor": SURFACE,
        "axes.edgecolor": AXIS,
        "axes.labelcolor": TEXT_SECONDARY,
        "axes.titlecolor": TEXT_PRIMARY,
        "axes.titlesize": 11,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "axes.grid": True,
        "grid.color": GRID,
        "grid.linewidth": 0.6,
        "xtick.color": TEXT_MUTED,
        "ytick.color": TEXT_MUTED,
        "xtick.labelcolor": TEXT_SECONDARY,
        "ytick.labelcolor": TEXT_SECONDARY,
        "legend.frameon": False,
        "legend.labelcolor": TEXT_SECONDARY,
        "font.size": 9,
    })


def sorted_variants(variants):
    return sorted(variants, key=lambda name: FAMILY_ORDER.index(get_family(name)))


def plot_scenarios(time_data, output, value_index, y_label, log_y, title):
    """Сетка 2x3: по графику на сценарий, линии - варианты."""
    figure, axes = plt.subplots(2, 3, figsize=(15, 8.5))
    for axis, scenario in zip(axes.flat, SCENARIO_TITLES):
        variants = time_data.get(scenario, {})
        for variant in sorted_variants(variants):
            points = variants[variant]
            style = FAMILY_STYLE[get_family(variant)]
            axis.plot(
                [point[0] for point in points],
                [point[value_index] for point in points],
                color=style["color"], marker=style["marker"], markersize=5,
                linewidth=2, label=variant,
            )
        axis.set_xscale("log")
        if log_y:
            axis.set_yscale("log")
        else:
            axis.set_ylim(bottom=0)
        axis.set_title(SCENARIO_TITLES[scenario])
        axis.set_xlabel("N, число операций")
        axis.set_ylabel(y_label)
        axis.legend(fontsize=8)

    figure.suptitle(title, fontsize=14, color=TEXT_PRIMARY)
    figure.tight_layout()
    figure.savefig(output, dpi=150)
    plt.close(figure)


def plot_speedup(time_data, output):
    """Горизонтальные столбцы: время STL / время нашего указателя при наибольшем N."""
    labels, speedups = [], []
    max_count = 0
    for scenario, title in SCENARIO_TITLES.items():
        variants = time_data.get(scenario, {})
        ours = [points for name, points in variants.items() if get_family(name) == "ours"]
        standard = [points for name, points in variants.items() if get_family(name) == "std"]
        if not ours or not standard:
            continue
        max_count = ours[0][-1][0]
        labels.append(title)
        speedups.append(standard[0][-1][1] / ours[0][-1][1])

    figure, axis = plt.subplots(figsize=(9, 4.5))
    positions = range(len(labels))
    axis.barh(positions, speedups, height=0.6, color=FAMILY_STYLE["ours"]["color"])
    axis.axvline(1.0, color=TEXT_SECONDARY, linewidth=1, linestyle="--")
    for position, value in zip(positions, speedups):
        axis.text(value + 0.05, position, f"{value:.2f}×", va="center", color=TEXT_PRIMARY)
    axis.set_yticks(list(positions), labels)
    axis.invert_yaxis()
    axis.grid(axis="y", visible=False)
    axis.set_xlabel("время STL / время нашего указателя  (> 1 - наш быстрее)")
    axis.set_title(f"Ускорение относительно STL, N = {max_count:,}".replace(",", " "))
    figure.tight_layout()
    figure.savefig(output, dpi=150)
    plt.close(figure)


def plot_memory(memory_count, memory_rows, output):
    """Столбцы: байт на объект; подпись - число выделений памяти на объект."""
    figure, axis = plt.subplots(figsize=(9, 4.5))
    positions = range(len(memory_rows))
    colors = [FAMILY_STYLE[get_family(variant)]["color"] for variant, _, _ in memory_rows]
    values = [bytes_per_object for _, bytes_per_object, _ in memory_rows]
    axis.bar(positions, values, width=0.6, color=colors)
    for position, (_, bytes_per_object, allocations) in zip(positions, memory_rows):
        axis.text(position, bytes_per_object + 0.8, f"{bytes_per_object:.0f} Б\n{allocations:.0f} аллок.",
                  ha="center", va="bottom", color=TEXT_PRIMARY)
    axis.set_xticks(list(positions), [variant for variant, _, _ in memory_rows])
    axis.set_ylim(0, max(values) * 1.3)
    axis.grid(axis="x", visible=False)
    axis.set_ylabel("байт на объект")
    axis.set_title(f"Память на один объект (указатель + куча), N = {memory_count:,}".replace(",", " "))
    figure.tight_layout()
    figure.savefig(output, dpi=150)
    plt.close(figure)


def main():
    default_results = Path(__file__).resolve().parent.parent / "results"
    parser = argparse.ArgumentParser(description="Графики по результатам SmartPointersBenchmark")
    parser.add_argument("--results", type=Path, default=default_results,
                        help="папка с time.csv и memory.csv (по умолчанию results/ проекта)")
    arguments = parser.parse_args()

    time_file = arguments.results / "time.csv"
    memory_file = arguments.results / "memory.csv"
    for file in (time_file, memory_file):
        if not file.exists():
            parser.error(f"нет файла {file}: сначала запустите SmartPointersBenchmark")

    plots_directory = arguments.results / "plots"
    plots_directory.mkdir(parents=True, exist_ok=True)

    setup_style()
    time_data = read_time(time_file)
    plot_scenarios(time_data, plots_directory / "time_per_op.png", 1, "нс на операцию", False,
                   "Время одной операции")
    plot_scenarios(time_data, plots_directory / "time_total.png", 2, "суммарное время, мс", True,
                   "Суммарное время N операций (log-log)")
    plot_speedup(time_data, plots_directory / "speedup.png")
    memory_count, memory_rows = read_memory(memory_file)
    plot_memory(memory_count, memory_rows, plots_directory / "memory.png")

    print(f"Графики сохранены в {plots_directory}")


if __name__ == "__main__":
    main()
