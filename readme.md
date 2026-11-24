# Benchmark Comparativo de Motores Gráficos em Hardware Embarcado Restrito

Repositório oficial do projeto científico desenvolvido para a disciplina de **Computação Gráfica e Visualização Científica** (Engenharia de Computação — Fundação Hermínio Ometto, FHO).

**Autor:** Edilson Martins Alves

---

## 1. Visão Geral

Este projeto investiga o impacto quantitativo da camada de abstração de motores gráficos sobre a taxa de quadros (FPS), o tempo de quadro ($T_{\text{quadro}}$), o *jitter* de renderização e o consumo de memória física residente (RSS) em hardware embarcado de recursos restritos (**Raspberry Pi 3** sob **OpenGL ES 2.0**).

São comparadas duas pilhas tecnológicas executando exatamente os mesmos ativos geométricos, trajetórias orbitais, parâmetros de câmera e equação de iluminação difusa de Lambert ($I_d = k_d \cdot \max(0, \mathbf{N} \cdot \mathbf{L})$):

* **Pilha A (Pipeline Direto Minimalista):** Implementação nativa em **C++17 + Raylib**, com alocação estática de geometria em *Vertex Buffer Objects* (VBOs) e cálculo em lote de matrizes $4 \times 4$ (Model e MVP) acelerado por intrínsecos vetoriais **ARM NEON** (`arm_neon.h`), com *fallback* escalar automático para desenvolvimento em PC (`x86_64`).
* **Pilha B (Motor de Propósito Geral):** Implementação orientada a objetos na **Godot Engine 3.5** (backend **GLES2**), estruturada em grafo hierárquico de nós espaciais (*Spatial Nodes* / *Scene Tree*).

---

## 2. Desenho Experimental

As malhas esféricas paramétricas são geradas matematicamente com topologia exata dada por $N_{\text{tri}} = 2S(R - 1)$, avaliando quatro patamares de carga poligonal total:

$$N_{\text{tri}} \in \{500, 2500, 10000, 40000\}$$

O benchmark isola gargalos de GPU e de CPU por meio de **dois modos complementares**:

| Modo | Identificador | Configuração da Cena | Gargalo Principal Avaliado |
| :--- | :--- | :--- | :--- |
| **Modo 1 — Densidade da Malha** | `mesh_density` (`MD_*`) | 1 planeta central com malha de $500$ a $40.000$ triângulos | Taxa de transferência de VBOs e processamento de vértices na GPU |
| **Modo 2 — Quantidade de Planetas** | `planet_count` (`PC_*`) | $1, 5, 20 \text{ e } 80$ planetas em órbita ($500$ triângulos cada) | Cálculo de matrizes na CPU (ARM NEON), *Scene Tree* e *draw dispatch* |

### Métricas Coletadas
* **Tempo de Quadro ($T_{\text{quadro}}$):** Intervalo entre trocas de buffer via relógio monotônico de hardware (`CLOCK_MONOTONIC_RAW` no Linux / `QueryPerformanceCounter` no Windows):
  $$T_{\text{quadro}}(n) = t_{\text{swap}}(n) - t_{\text{swap}}(n-1)$$
* **Jitter de Renderização ($J$):** Variação absoluta de latência entre quadros consecutivos:
  $$J(n) = |T_{\text{quadro}}(n) - T_{\text{quadro}}(n-1)|$$
* **Percentil de Cauda ($P_{99}$):** Latência máxima atendida em 99% dos quadros.
* **Memória Física Residente (RSS):** Amostrada diretamente via `/proc/self/statm` no Linux (Raspberry Pi 3).

---

## 3. Estrutura do Repositório

```text
BenchmarkComputacaoGrafica/
├── shared/
│   ├── benchmark_config.json      # Parâmetros compartilhados (câmera, luz, protocolo)
│   ├── generate_meshes.py         # Gerador das esferas UV paramétricas (.obj, CCW)
│   └── meshes/                    # Malhas exatas (500, 2500, 10000, 40000 triângulos)
├── raylib_pilha_a/
│   ├── CMakeLists.txt             # Build multiplataforma (Windows MSVC / RPi 3 ARM NEON)
│   ├── shaders/                   # Shaders Lambert (GLSL 100 ES2 e GLSL 330)
│   └── src/
│       ├── main.cpp               # Pipeline de renderização, VBOs, câmera e modos
│       ├── math_neon.hpp          # Multiplicação de matrizes 4x4 (ARM NEON + fallback)
│       └── telemetry.hpp          # Coletor monotônico, Jitter, RSS e exportação CSV
├── godot_pilha_b/
│   ├── project.godot              # Projeto Godot 3.5 configurado em GLES2 (V-Sync Off)
│   ├── MainBenchmark.tscn         # Cena raiz
│   ├── BenchmarkController.gd     # Instanciação de Spatial Nodes, shader e telemetria
│   └── meshes/                    # Malhas importadas para a Godot
└── analysis/
    ├── process_results.py         # Gera gráficos vetoriais padrão IEEE e tabela LaTeX
    ├── results/                   # Dados brutos, gráficos e tabela coletados no PC
    └── results_rpi3/              # Dados oficiais, gráficos, tabela e hardware_info do RPi 3
```

---

## 4. Como Compilar e Executar

### Pré-requisito: Geração das Malhas (Opcional — já incluídas)
```bash
python shared/generate_meshes.py
```

### No Windows (Desenvolvimento e Demonstração em PC)
* **Pilha A (C++/Raylib):**
  1. Abra a pasta `raylib_pilha_a/` no **Visual Studio** (*Abrir uma pasta local*).
  2. Aguarde o CMake baixar e configurar a Raylib 5.0 automaticamente via `FetchContent`.
  3. Selecione a configuração **`x64-Release`** e execute (`F5`).
* **Pilha B (Godot 3.5):**
  1. Abra a **Godot 3.5 Standard**, importe `godot_pilha_b/project.godot` e execute (`F5`).

### No Raspberry Pi 3 (Hardware Alvo — OpenGL ES 2.0 + ARM NEON)
1. Fixe a memória unificada da GPU em `/boot/firmware/config.txt` (ou `/boot/config.txt`):
   ```ini
   gpu_mem=128
   ```
2. Instale as dependências e compile a Pilha A:
   ```bash
   sudo apt update && sudo apt install -y \
     build-essential cmake git \
     libgles2-mesa-dev libegl1-mesa-dev libgl1-mesa-dev \
     libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libasound2-dev \
     godot3 python3-pandas python3-matplotlib python3-numpy

   cd raylib_pilha_a
   cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-DGRAPHICS_API_OPENGL_ES2"
   cmake --build build -j2
   ```
3. Trave o *governor* da CPU em `performance` antes das coletas e execute ambas as pilhas:
   ```bash
   echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
   ./raylib_pilha_a/build/benchmark_pilha_a
   godot3 --path ./godot_pilha_b --video-driver GLES2
   ```

---

## 5. Controles (Modo Interativo e Benchmark)

Ambas as pilhas compartilham exatamente os mesmos comandos:

| Tecla / Entrada | Ação |
| :--- | :--- |
| **Mouse (Arrastar) / `WASD` / Setas** | Rotaciona a câmera orbital no modo interativo |
| **Roda do Mouse** | Aproxima ou afasta a câmera |
| **`M`** | Alterna entre Modo 1 (`mesh_density`) e Modo 2 (`planet_count`) |
| **`1`, `2`, `3`, `4`** | Seleciona a carga ($500$, $2.500$, $10.000$ ou $40.000$ triângulos) |
| **`H`** | Exibe ou oculta o HUD (ocultar antes de iniciar a coleta oficial) |
| **`B`** | Inicia a bateria sintética automatizada (8 estágios + exportação CSV) |
| **`Backspace`** | Cancela a bateria em andamento |

---

## 6. Análise Estatística e Gráficos IEEE

Para reprocessar os CSVs e gerar os gráficos (`.pdf` e `.png`) e a tabela LaTeX (`tabela_resultados_ieee.tex`):

```bash
pip install pandas matplotlib numpy
python analysis/process_results.py
```

Os arquivos gerados incluem:
1. `fig1_fps_frametime_comparativo.pdf` — FPS médio e $T_{\text{quadro}} \pm \sigma$ nos modos `mesh_density` e `planet_count`.
2. `fig2_jitter_p99_estabilidade.pdf` — Jitter médio e latência de cauda $P_{99}$.
3. `fig3_memoria_rss.pdf` — Pico de memória física residente (RSS) por estágio.
4. `fig4_serie_temporal_40k.pdf` — Série temporal quadro a quadro em $40.000$ triângulos.
5. `tabela_resultados_ieee.tex` — Tabela consolidada pronta para o template IEEE Conference (Overleaf).