from pathlib import Path
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

# Configuracao tipografica e dimensional padrao IEEE (2 colunas = ~7.16 in, 1 coluna = ~3.5 in)
plt.rcParams.update({
    "font.family": "serif",
    "font.serif": ["Times New Roman", "DejaVu Serif", "Liberation Serif"],
    "font.size": 8.5,
    "axes.titlesize": 9.5,
    "axes.labelsize": 8.5,
    "xtick.labelsize": 8.0,
    "ytick.labelsize": 8.0,
    "legend.fontsize": 7.8,
    "figure.dpi": 300,
    "savefig.dpi": 300,
    "axes.grid": True,
    "grid.linestyle": "--",
    "grid.alpha": 0.45,
})

COLORS = {
    "Raylib_Pilha_A": "#1f77b4",  # Azul IEEE
    "Godot_Pilha_B":  "#d62728",  # Vermelho IEEE
}

LABELS = {
    "Raylib_Pilha_A": "Pilha A: C++/Raylib (VBO + NEON)",
    "Godot_Pilha_B":  "Pilha B: Godot 3.5 (GLES2 + SceneTree)",
}

MARKERS = {
    "Raylib_Pilha_A": "o",
    "Godot_Pilha_B":  "s",
}

TRIANGLE_LEVELS = [500, 2500, 10000, 40000]
X_POS = np.arange(len(TRIANGLE_LEVELS))
X_LABELS = ["500", "2.500", "10.000", "40.000"]


def save_figure(fig: plt.Figure, out_dir: Path, base_name: str) -> None:
    fig.tight_layout()
    fig.savefig(out_dir / f"{base_name}.pdf", bbox_inches="tight")
    fig.savefig(out_dir / f"{base_name}.png", bbox_inches="tight")
    plt.close(fig)
    print(f"[GRAFICO] Salvo: {base_name}.pdf / .png")


def load_summary_data(csv_dir: Path) -> pd.DataFrame:
    frames = []
    for fname in ["summary_raylib_pilha_a.csv", "summary_godot_pilha_b.csv"]:
        fpath = csv_dir / fname
        if fpath.exists():
            df = pd.read_csv(fpath)
            frames.append(df)
        else:
            print(f"[AVISO] Arquivo nao encontrado: {fpath.name}")

    if not frames:
        raise FileNotFoundError(
            f"Nenhum arquivo summary_*.csv encontrado em {csv_dir}. "
            "Execute a bateria [B] na Pilha A e na Pilha B antes de rodar este script."
        )

    combined = pd.concat(frames, ignore_index=True)
    return combined


def plot_fps_and_frametime(df: pd.DataFrame, out_dir: Path) -> None:
    fig, axes = plt.subplots(2, 2, figsize=(7.16, 4.6), sharex=True)

    modes = [
        ("mesh_density", "Modo 1: Densidade da Malha (1 Planeta)"),
        ("planet_count", "Modo 2: Quantidade de Planetas (1 a 80 Planetas)"),
    ]

    for col_idx, (mode_key, mode_title) in enumerate(modes):
        sub_mode = df[df["mode"] == mode_key]

        # Linha superior: FPS Medio
        ax_fps = axes[0, col_idx]
        # Linha inferior: Tempo de Quadro Medio com barras de desvio padrao
        ax_ft = axes[1, col_idx]

        for engine in ["Raylib_Pilha_A", "Godot_Pilha_B"]:
            eng_df = sub_mode[sub_mode["engine"] == engine].sort_values("triangles")
            if eng_df.empty:
                continue

            ax_fps.plot(
                X_POS[:len(eng_df)],
                eng_df["avg_fps"],
                marker=MARKERS[engine],
                color=COLORS[engine],
                linewidth=1.6,
                markersize=5,
                label=LABELS[engine],
            )

            ax_ft.errorbar(
                X_POS[:len(eng_df)],
                eng_df["mean_frame_ms"],
                yerr=eng_df["stddev_frame_ms"],
                marker=MARKERS[engine],
                color=COLORS[engine],
                linewidth=1.6,
                markersize=5,
                capsize=3,
                label=LABELS[engine],
            )

        ax_fps.set_title(mode_title)
        ax_fps.set_ylabel("FPS Médio (quadros/s)")
        ax_ft.set_ylabel(r"$T_{\mathrm{quadro}}$ Médio $\pm \sigma$ (ms)")
        ax_ft.set_xlabel(r"Carga Geométrica Total ($N_{\mathrm{tri}}$)")
        ax_ft.set_xticks(X_POS)
        ax_ft.set_xticklabels(X_LABELS)

        # Linha de referencia 30 FPS (33.33 ms) e 60 FPS (16.67 ms) para hardware embarcado
        ax_ft.axhline(16.67, color="gray", linestyle=":", linewidth=0.9, label="Alvo 60 FPS (16,67 ms)" if col_idx == 1 else None)
        ax_ft.axhline(33.33, color="black", linestyle="-.", linewidth=0.9, label="Alvo 30 FPS (33,33 ms)" if col_idx == 1 else None)

    handles, labels = axes[0, 0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=2, frameon=True, bbox_to_anchor=(0.5, 1.03))
    save_figure(fig, out_dir, "fig1_fps_frametime_comparativo")


def plot_jitter_and_p99(df: pd.DataFrame, out_dir: Path) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(7.16, 2.9))
    bar_width = 0.35

    modes = [
        ("mesh_density", "Densidade da Malha (1 Planeta)"),
        ("planet_count", "Quantidade de Planetas (1–80)"),
    ]

    for idx, (mode_key, title) in enumerate(modes):
        ax = axes[idx]
        sub_mode = df[df["mode"] == mode_key]

        ray = sub_mode[sub_mode["engine"] == "Raylib_Pilha_A"].sort_values("triangles")
        god = sub_mode[sub_mode["engine"] == "Godot_Pilha_B"].sort_values("triangles")

        if not ray.empty:
            ax.bar(
                X_POS[:len(ray)] - bar_width / 2,
                ray["mean_jitter_ms"],
                width=bar_width,
                color=COLORS["Raylib_Pilha_A"],
                edgecolor="black",
                linewidth=0.5,
                label="Pilha A: Jitter Médio",
            )
            ax.plot(
                X_POS[:len(ray)] - bar_width / 2,
                ray["p99_frame_ms"],
                marker="^",
                linestyle="--",
                color="#0b3c5d",
                linewidth=1.1,
                markersize=4.5,
                label="Pilha A: P99 Frame Time",
            )

        if not god.empty:
            ax.bar(
                X_POS[:len(god)] + bar_width / 2,
                god["mean_jitter_ms"],
                width=bar_width,
                color=COLORS["Godot_Pilha_B"],
                edgecolor="black",
                linewidth=0.5,
                label="Pilha B: Jitter Médio",
            )
            ax.plot(
                X_POS[:len(god)] + bar_width / 2,
                god["p99_frame_ms"],
                marker="v",
                linestyle="--",
                color="#7f0000",
                linewidth=1.1,
                markersize=4.5,
                label="Pilha B: P99 Frame Time",
            )

        ax.set_title(title)
        ax.set_xlabel(r"Carga Geométrica Total ($N_{\mathrm{tri}}$)")
        ax.set_ylabel("Latência / Jitter (ms)")
        ax.set_xticks(X_POS)
        ax.set_xticklabels(X_LABELS)

    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=2, frameon=True, bbox_to_anchor=(0.5, 1.12))
    save_figure(fig, out_dir, "fig2_jitter_p99_estabilidade")


def plot_memory_rss(df: pd.DataFrame, out_dir: Path) -> None:
    fig, ax = plt.subplots(figsize=(3.55, 2.7))

    stage_order = ["MD_500", "MD_2500", "MD_10000", "MD_40000", "PC_500", "PC_2500", "PC_10000", "PC_40000"]
    x = np.arange(len(stage_order))
    bar_width = 0.38

    for offset, engine in [(-bar_width / 2, "Raylib_Pilha_A"), (bar_width / 2, "Godot_Pilha_B")]:
        eng_df = df[df["engine"] == engine].set_index("stage_id")
        vals = [eng_df.loc[st, "peak_rss_mb"] if st in eng_df.index else 0.0 for st in stage_order]
        ax.bar(
            x + offset,
            vals,
            width=bar_width,
            color=COLORS[engine],
            edgecolor="black",
            linewidth=0.5,
            label=LABELS[engine],
        )

    ax.set_title("Consumo de Memória Física Residente (Pico RSS)")
    ax.set_ylabel("Memória RSS (MB)")
    ax.set_xticks(x)
    ax.set_xticklabels(stage_order, rotation=35, ha="right", fontsize=7.2)
    ax.legend(loc="upper left", frameon=True, fontsize=6.8)

    save_figure(fig, out_dir, "fig3_memoria_rss")


def plot_temporal_distribution_40k(csv_dir: Path, out_dir: Path) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(7.16, 2.6), sharey=True)

    scenarios = [
        ("MD_40000", "Série Temporal — 1 Planeta (40.000 tri)"),
        ("PC_40000", "Série Temporal — 80 Planetas (40.000 tri)"),
    ]

    plotted_any = False
    for idx, (stage_id, title) in enumerate(scenarios):
        ax = axes[idx]
        for prefix, engine in [("raylib", "Raylib_Pilha_A"), ("godot", "Godot_Pilha_B")]:
            fpath = csv_dir / f"{prefix}_{stage_id}.csv"
            if fpath.exists():
                sdf = pd.read_csv(fpath)
                ax.plot(
                    sdf["elapsed_s"],
                    sdf["frame_time_ms"],
                    color=COLORS[engine],
                    linewidth=0.8,
                    alpha=0.85,
                    label=LABELS[engine],
                )
                plotted_any = True

        ax.set_title(title)
        ax.set_xlabel("Tempo de Execução (s)")
        if idx == 0:
            ax.set_ylabel(r"Tempo de Quadro $T_{\mathrm{quadro}}(n)$ (ms)")

    if plotted_any:
        handles, labels = axes[0].get_legend_handles_labels()
        fig.legend(handles, labels, loc="upper center", ncol=2, frameon=True, bbox_to_anchor=(0.5, 1.08))
        save_figure(fig, out_dir, "fig4_serie_temporal_40k")
    else:
        plt.close(fig)
        print("[AVISO] CSVs individuais de 40k nao encontrados para gerar a fig4.")


def export_latex_table(df: pd.DataFrame, out_dir: Path) -> None:
    stage_order = ["MD_500", "MD_2500", "MD_10000", "MD_40000", "PC_500", "PC_2500", "PC_10000", "PC_40000"]
    df = df.copy()
    df["stage_rank"] = df["stage_id"].apply(lambda s: stage_order.index(s) if s in stage_order else 99)
    df = df.sort_values(["stage_rank", "engine"])

    tex_path = out_dir.parent / "tabela_resultados_ieee.tex"
    with tex_path.open("w", encoding="utf-8") as f:
        f.write("% Tabela gerada automaticamente por analysis/process_results.py\n")
        f.write("% Requer \\usepackage{booktabs} no preambulo do Overleaf\n")
        f.write("\\begin{table*}[t]\n")
        f.write("\\centering\n")
        f.write("\\caption{Resultados Comparativos de Desempenho: Pilha A (C++/Raylib + VBO + ARM NEON) vs. Pilha B (Godot 3.5 GLES2)}\n")
        f.write("\\label{tab:resultados_benchmark}\n")
        f.write("\\begin{tabular}{@{}llrrrrrr@{}}\n")
        f.write("\\toprule\n")
        f.write("\\textbf{Cenário} & \\textbf{Pilha} & \\textbf{Planetas} & \\textbf{FPS Médio} & \\textbf{$T_{\\text{quadro}}$ ($\\mu \\pm \\sigma$) [ms]} & \\textbf{$P_{99}$ [ms]} & \\textbf{Jitter [ms]} & \\textbf{RSS [MB]} \\\\\n")
        f.write("\\midrule\n")

        for _, row in df.iterrows():
            st_escaped = str(row["stage_id"]).replace("_", "\\_")
            eng_short = "Pilha A (Raylib)" if row["engine"] == "Raylib_Pilha_A" else "Pilha B (Godot 3.5)"
            f.write(
                f"{st_escaped} & {eng_short} & {int(row['planets'])} & "
                f"{row['avg_fps']:.2f} & {row['mean_frame_ms']:.3f} $\\pm$ {row['stddev_frame_ms']:.3f} & "
                f"{row['p99_frame_ms']:.3f} & {row['mean_jitter_ms']:.3f} & {row['peak_rss_mb']:.2f} \\\\\n"
            )

        f.write("\\bottomrule\n")
        f.write("\\end{tabular}\n")
        f.write("\\end{table*}\n")

    print(f"[LATEX] Tabela IEEE exportada em: {tex_path}")


def main() -> None:
    analysis_dir = Path(__file__).resolve().parent
    csv_dir = analysis_dir / "results" / "csv"
    graphs_dir = analysis_dir / "results" / "graphs"
    graphs_dir.mkdir(parents=True, exist_ok=True)

    df_summary = load_summary_data(csv_dir)

    print("\n================ RESUMO CONSOLIDADO ================")
    cols = ["stage_id", "engine", "planets", "triangles", "avg_fps", "mean_frame_ms", "p99_frame_ms", "mean_jitter_ms", "peak_rss_mb"]
    print(df_summary[cols].to_string(index=False))
    print("====================================================\n")

    plot_fps_and_frametime(df_summary, graphs_dir)
    plot_jitter_and_p99(df_summary, graphs_dir)
    plot_memory_rss(df_summary, graphs_dir)
    plot_temporal_distribution_40k(csv_dir, graphs_dir)
    export_latex_table(df_summary, graphs_dir)

    print(f"\n[CONCLUIDO] Todos os artefatos prontos em: {graphs_dir}")


if __name__ == "__main__":
    main()