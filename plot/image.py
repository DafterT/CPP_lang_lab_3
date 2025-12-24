import argparse
import json
import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns
import os

# Параметры запуска
parser = argparse.ArgumentParser(description='Отрисовка результатов бенчмарка изображений.')
parser.add_argument('--t', dest='use_time', action='store_true',
                    help='Показывать время одной итерации вместо скорости.')
args = parser.parse_args()

USE_TIME = args.use_time

# Настройки стиля
sns.set_theme(style="whitegrid")

# Путь к файлу с результатами (предпочитаем файл с данными по потокам)
CANDIDATE_FILES = ['../results_image.json', '../build/results_image.json']

def has_thread_counts(payload):
    for bench in payload.get('benchmarks', []):
        name = bench.get('name', '')
        if 'ThreadPool' not in name:
            continue
        numeric_parts = [part for part in name.split('/')[2:] if part.isdigit()]
        if len(numeric_parts) >= 3:
            return True
    return False

data = None
FILENAME = None
for path in CANDIDATE_FILES:
    if not os.path.exists(path):
        continue
    with open(path, 'r') as f:
        candidate_data = json.load(f)
    if data is None:
        data = candidate_data
        FILENAME = path
    if has_thread_counts(candidate_data):
        data = candidate_data
        FILENAME = path
        break

if not FILENAME or data is None:
    print("Ошибка: Файл с результатами не найден. Запустите бенчмарк сначала.")
    exit()

print(f"Читаю файл {FILENAME}...")

SYSTEM_THREADS = 8
THREAD_COUNTS = [1, 4, 8, 16]
THREAD_LABELS = [str(t) for t in THREAD_COUNTS]

TIME_UNIT_FACTORS = {
    'ns': 1e-3,
    'us': 1.0,
    'ms': 1e3,
    's': 1e6
}

def convert_time(value, unit_from, unit_to):
    from_factor = TIME_UNIT_FACTORS.get(unit_from)
    to_factor = TIME_UNIT_FACTORS.get(unit_to)
    if from_factor is None or to_factor is None:
        return value
    return value * (from_factor / to_factor)

TIME_UNIT = None
if USE_TIME:
    for bench in data.get('benchmarks', []):
        bench_unit = bench.get('time_unit')
        if bench_unit:
            TIME_UNIT = bench_unit
            break
    if TIME_UNIT is None:
        TIME_UNIT = 'us'

if USE_TIME:
    METRIC_FIELD = f'Time ({TIME_UNIT})'
    METRIC_LABEL = f'Время ({TIME_UNIT})'
    MAIN_TITLE_TEMPLATE = 'Время на итерацию (Kernel {k}x{k})'
    TP_TITLE_TEMPLATE = 'ThreadPool: время на итерацию (Kernel {k}x{k})'
    TPF_TITLE_TEMPLATE = 'ThreadPool Full: время на итерацию (Kernel {k}x{k})'
    VALUE_FORMAT = '%.3f'
else:
    METRIC_FIELD = 'Speed (GB/s)'
    METRIC_LABEL = 'Скорость (GB/s)'
    MAIN_TITLE_TEMPLATE = 'Производительность (Kernel {k}x{k})'
    TP_TITLE_TEMPLATE = 'ThreadPool (Kernel {k}x{k})'
    TPF_TITLE_TEMPLATE = 'ThreadPool Full (Kernel {k}x{k})'
    VALUE_FORMAT = '%.3f'

# 1. Парсинг данных
records = []
for bench in data['benchmarks']:
    # Разбиваем строку вида: "BlurFixture/BM_ProcessDefault/32/3"
    # или "BlurFixture/BM_ProcessThreadPool/32/3/8"
    name_parts = bench['name'].split('/')
    
    # Индексы:
    # 0: BlurFixture
    # 1: Имя метода (BM_ProcessDefault / BM_ProcessSIMD)
    # далее: параметры (размер, ядро, потоки и т.д.)
    if len(name_parts) < 3:
        continue
    
    method_raw = name_parts[1]
    numeric_parts = [int(part) for part in name_parts[2:] if part.isdigit()]
    if len(numeric_parts) < 2:
        continue
    img_size = numeric_parts[0]
    kernel_size = numeric_parts[1]
    
    threads = None
    for part in name_parts:
        if part.startswith('threads:') and part[8:].isdigit():
            threads = int(part[8:])
            break
        if part.startswith('thread:') and part[7:].isdigit():
            threads = int(part[7:])
            break
    if threads is None and len(numeric_parts) > 2:
        threads = numeric_parts[2]
    if threads is None:
        bench_threads = bench.get('threads')
        if isinstance(bench_threads, int) and bench_threads > 1:
            threads = bench_threads
    
    if threads is None and 'ThreadPool' in method_raw:
        threads = SYSTEM_THREADS
    
    if USE_TIME:
        time_value = bench.get('real_time')
        if time_value is None:
            time_value = bench.get('cpu_time')
        if time_value is None:
            continue
        time_unit = bench.get('time_unit', TIME_UNIT)
        metric_value = convert_time(time_value, time_unit, TIME_UNIT)
    else:
        # Переводим байты/сек в ГБ/сек (10^9)
        metric_value = bench['bytes_per_second'] / 1e9
    
    if 'SIMD' in method_raw:
        method_group = 'SIMD'
        method = 'SIMD (AVX-512)'
    elif 'ThreadPoolFull' in method_raw:
        method_group = 'ThreadPool Full'
        method = f'ThreadPool Full (T={threads})' if threads is not None else 'ThreadPool Full'
    elif 'ThreadPool' in method_raw:
        method_group = 'ThreadPool'
        method = f'ThreadPool (T={threads})' if threads is not None else 'ThreadPool'
    else:
        method_group = 'Default'
        method = 'Default (C++)'

    records.append({
        'Method': method,
        'Method Group': method_group,
        'Image Size': img_size,
        'Kernel Size': kernel_size,
        'Threads': threads,
        METRIC_FIELD: metric_value
    })

df = pd.DataFrame(records)

df['Threads'] = pd.to_numeric(df['Threads'], errors='coerce')
df['Threads Label'] = df['Threads'].apply(lambda x: str(int(x)) if pd.notna(x) else None)
df['Threads Label'] = pd.Categorical(df['Threads Label'], categories=THREAD_LABELS, ordered=True)

# 2. Фильтрация по числу потоков для общего графика
threadpool_groups = ['ThreadPool', 'ThreadPool Full']
df_main = df.copy()
for group in threadpool_groups:
    group_mask = df_main['Method Group'] == group
    if not group_mask.any():
        continue
    has_thread_counts = df_main.loc[group_mask, 'Threads'].notna().any()
    if has_thread_counts:
        df_main = df_main[~group_mask | (df_main['Threads'] == SYSTEM_THREADS)]
    else:
        print(f"Предупреждение: число потоков не найдено в данных {group}, фильтрация пропущена.")

# 3. Генерация отдельных графиков
kernel_sizes = sorted(df['Kernel Size'].unique())

saved_files = []

def save_plot(subset, title, filename, hue, legend_title, hue_order=None):
    if subset.empty:
        return
    plt.figure(figsize=(12, 8))
    ax = sns.barplot(
        data=subset,
        x='Image Size',
        y=METRIC_FIELD,
        hue=hue,
        hue_order=hue_order,
        palette="viridis"
    )
    plt.title(title, fontsize=16, pad=20)
    plt.ylabel(METRIC_LABEL, fontsize=14)
    plt.xlabel('Размер изображения (NxN)', fontsize=14)
    plt.legend(title=legend_title, fontsize=12, title_fontsize=12)
    for container in ax.containers:
        ax.bar_label(container, fmt=VALUE_FORMAT, padding=3, fontsize=10)
    plt.tight_layout()
    plt.savefig(filename, dpi=150)
    print(f"Сохранено: {filename}")
    plt.close()
    saved_files.append(filename)

for k_size in kernel_sizes:
    subset = df_main[df_main['Kernel Size'] == k_size]
    save_plot(
        subset,
        MAIN_TITLE_TEMPLATE.format(k=k_size),
        f'benchmark_image_kernel_{k_size}.png',
        hue='Method',
        legend_title='Метод'
    )
    
    tp_subset = df[
        (df['Kernel Size'] == k_size)
        & (df['Method Group'] == 'ThreadPool')
        & (df['Threads'].isin(THREAD_COUNTS))
    ]
    if not tp_subset.empty:
        save_plot(
            tp_subset,
            TP_TITLE_TEMPLATE.format(k=k_size),
            f'benchmark_image_kernel_{k_size}_threadpool.png',
            hue='Threads Label',
            legend_title='Потоки',
            hue_order=THREAD_LABELS
        )
    
    tpf_subset = df[
        (df['Kernel Size'] == k_size)
        & (df['Method Group'] == 'ThreadPool Full')
        & (df['Threads'].isin(THREAD_COUNTS))
    ]
    if not tpf_subset.empty:
        save_plot(
            tpf_subset,
            TPF_TITLE_TEMPLATE.format(k=k_size),
            f'benchmark_image_kernel_{k_size}_threadpool_full.png',
            hue='Threads Label',
            legend_title='Потоки',
            hue_order=THREAD_LABELS
        )

if saved_files:
    print(f"\nГотово! Создано изображений: {len(saved_files)}.")
else:
    print("\nГотово! Нет данных для построения графиков.")
