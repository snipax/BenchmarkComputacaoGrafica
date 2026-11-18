#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "math_neon.hpp"
#include "telemetry.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct BenchmarkConfig {
    int width = 800;
    int height = 480;
    bool fullscreen = false;
    bool vsync = false;

    float cam_orbit_radius = 28.0f;
    float cam_orbit_height = 12.0f;
    float cam_angular_vel = 0.45f;
    float cam_fov = 60.0f;

    Vector3 light_dir = { -0.57735f, -0.57735f, -0.57735f };
    float kd_diffuse = 0.85f;
    float ka_ambient = 0.15f;
    Vector3 base_color = { 0.25f, 0.55f, 0.90f };

    float base_radius = 3.5f;
    float radius_step = 0.22f;
    float base_orbital_speed = 1.2f;
    float speed_decay = 0.012f;
    float self_rotation_speed = 0.8f;

    double warmup_seconds = 3.0;
    double sample_duration_seconds = 15.0;
    int rss_sample_interval = 30;
};

struct StageSpec {
    std::string mode_name;
    std::string stage_id;
    int total_triangles;
    int planet_count;
    int mesh_index; // 0: 500, 1: 2500, 2: 10000, 3: 40000
};

// Leitor leve de parametros numericos do benchmark_config.json sem dependencias externas
static float ExtractJsonFloat(const std::string& content, const std::string& key, float default_val) {
    std::regex re("\"" + key + "\"\\s*:\\s*(-?[0-9]+\\.?[0-9]*)");
    std::smatch match;
    if (std::regex_search(content, match, re) && match.size() > 1) {
        return std::stof(match[1].str());
    }
    return default_val;
}

static bool ExtractJsonBool(const std::string& content, const std::string& key, bool default_val) {
    std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (std::regex_search(content, match, re) && match.size() > 1) {
        return match[1].str() == "true";
    }
    return default_val;
}

static BenchmarkConfig LoadBenchmarkConfig(const fs::path& json_path) {
    BenchmarkConfig cfg;
    std::ifstream file(json_path);
    if (!file.is_open()) {
        std::cout << "[AVISO] benchmark_config.json nao encontrado em " << json_path
                  << ". Usando valores padrao.\n";
        return cfg;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();

    cfg.width = static_cast<int>(ExtractJsonFloat(text, "width", 800.0f));
    cfg.height = static_cast<int>(ExtractJsonFloat(text, "height", 480.0f));
    cfg.fullscreen = ExtractJsonBool(text, "fullscreen", false);
    cfg.vsync = ExtractJsonBool(text, "vsync", false);

    cfg.cam_orbit_radius = ExtractJsonFloat(text, "orbit_radius", 28.0f);
    cfg.cam_orbit_height = ExtractJsonFloat(text, "orbit_height", 12.0f);
    cfg.cam_angular_vel = ExtractJsonFloat(text, "angular_velocity_rad_s", 0.45f);
    cfg.cam_fov = ExtractJsonFloat(text, "fov_degrees", 60.0f);

    cfg.kd_diffuse = ExtractJsonFloat(text, "kd_diffuse", 0.85f);
    cfg.ka_ambient = ExtractJsonFloat(text, "ka_ambient", 0.15f);

    cfg.base_radius = ExtractJsonFloat(text, "base_radius", 3.5f);
    cfg.radius_step = ExtractJsonFloat(text, "radius_step", 0.22f);
    cfg.base_orbital_speed = ExtractJsonFloat(text, "base_orbital_speed", 1.2f);
    cfg.speed_decay = ExtractJsonFloat(text, "speed_decay", 0.012f);
    cfg.self_rotation_speed = ExtractJsonFloat(text, "self_rotation_speed", 0.8f);

    cfg.warmup_seconds = static_cast<double>(ExtractJsonFloat(text, "warmup_seconds", 3.0f));
    cfg.sample_duration_seconds = static_cast<double>(ExtractJsonFloat(text, "sample_duration_seconds", 15.0f));
    cfg.rss_sample_interval = static_cast<int>(ExtractJsonFloat(text, "rss_sample_interval_frames", 30.0f));

    return cfg;
}

// Localiza a raiz do repositorio BenchmarkComputacaoGrafica subindo diretorios a partir do executavel
static fs::path FindProjectRoot() {
    fs::path curr = fs::current_path();
    for (int i = 0; i < 5; ++i) {
        if (fs::exists(curr / "shared" / "benchmark_config.json")) {
            return curr;
        }
        if (curr.has_parent_path()) {
            curr = curr.parent_path();
        }
    }
    return fs::current_path();
}

// Constroi os estados orbitais deterministas para N planetas
static std::vector<MathSimd::PlanetOrbitState> BuildPlanetOrbits(int planet_count, const BenchmarkConfig& cfg) {
    std::vector<MathSimd::PlanetOrbitState> planets(planet_count);
    for (int i = 0; i < planet_count; ++i) {
        MathSimd::PlanetOrbitState p;
        if (i == 0) {
            p.orbit_radius = 0.0f;
            p.orbital_speed = 0.0f;
            p.initial_phase = 0.0f;
            p.self_rotation_speed = cfg.self_rotation_speed;
            p.scale = (planet_count == 1) ? 2.2f : 1.4f;
        } else {
            p.orbit_radius = cfg.base_radius + static_cast<float>(i) * cfg.radius_step;
            p.orbital_speed = std::max(0.15f, cfg.base_orbital_speed - static_cast<float>(i) * cfg.speed_decay);
            p.initial_phase = static_cast<float>(i) * 2.3999632f; // Angulo aureo para distribuicao uniforme
            p.self_rotation_speed = cfg.self_rotation_speed + static_cast<float>(i % 5) * 0.15f;
            p.scale = 0.55f;
        }
        planets[i] = p;
    }
    return planets;
}

int main() {
    fs::path root_dir = FindProjectRoot();
    fs::path config_path = root_dir / "shared" / "benchmark_config.json";
    fs::path meshes_dir = root_dir / "shared" / "meshes";
    fs::path shaders_dir = root_dir / "raylib_pilha_a" / "shaders";
    fs::path csv_out_dir = root_dir / "analysis" / "results" / "csv";
    fs::create_directories(csv_out_dir);

    BenchmarkConfig cfg = LoadBenchmarkConfig(config_path);

    // Configura flags de janela garantindo V-Sync desativado para medir o tempo real de quadro
    unsigned int flags = FLAG_MSAA_4X_HINT;
    if (cfg.fullscreen) flags |= FLAG_FULLSCREEN_MODE;
    SetConfigFlags(flags);

    InitWindow(cfg.width, cfg.height, "Pilha A - C++/Raylib (VBO + ARM NEON) - Benchmark CG");
    SetTargetFPS(0); // Sem trava artificial de FPS

    // Seleciona o shader conforme a API grafica compilada na Raylib
#if defined(GRAPHICS_API_OPENGL_ES2)
    fs::path vs_path = shaders_dir / "lambert_es2.vs";
    fs::path fs_path = shaders_dir / "lambert_es2.fs";
#else
    fs::path vs_path = shaders_dir / "lambert_gl33.vs";
    fs::path fs_path = shaders_dir / "lambert_gl33.fs";
#endif

    Shader lambert_shader = LoadShader(vs_path.string().c_str(), fs_path.string().c_str());
    int loc_mvp = GetShaderLocation(lambert_shader, "mvp");
    int loc_model = GetShaderLocation(lambert_shader, "matModel");
    int loc_light_dir = GetShaderLocation(lambert_shader, "uLightDir");
    int loc_base_color = GetShaderLocation(lambert_shader, "uBaseColor");
    int loc_kd = GetShaderLocation(lambert_shader, "uKd");
    int loc_ka = GetShaderLocation(lambert_shader, "uKa");

    SetShaderValue(lambert_shader, loc_light_dir, &cfg.light_dir.x, SHADER_UNIFORM_VEC3);
    SetShaderValue(lambert_shader, loc_base_color, &cfg.base_color.x, SHADER_UNIFORM_VEC3);
    SetShaderValue(lambert_shader, loc_kd, &cfg.kd_diffuse, SHADER_UNIFORM_FLOAT);
    SetShaderValue(lambert_shader, loc_ka, &cfg.ka_ambient, SHADER_UNIFORM_FLOAT);

    // Carrega as 4 malhas esfericas diretamente em VBOs estaticos na VRAM
    const std::vector<std::string> mesh_files = {
        "planet_500.obj",
        "planet_2500.obj",
        "planet_10000.obj",
        "planet_40000.obj"
    };

    std::vector<Model> planet_models(mesh_files.size());
    for (size_t i = 0; i < mesh_files.size(); ++i) {
        fs::path obj_path = meshes_dir / mesh_files[i];
        planet_models[i] = LoadModel(obj_path.string().c_str());
        planet_models[i].materials[0].shader = lambert_shader;
    }

    // Define os 8 estagios experimentais (4 de densidade da malha + 4 de quantidade de planetas)
    const std::vector<StageSpec> all_stages = {
        // Modo 1: Escalonamento por Densidade Poligonal (1 planeta)
        { "mesh_density", "MD_500",   500,   1, 0 },
        { "mesh_density", "MD_2500",  2500,  1, 1 },
        { "mesh_density", "MD_10000", 10000, 1, 2 },
        { "mesh_density", "MD_40000", 40000, 1, 3 },
        // Modo 2: Escalonamento por Quantidade de Planetas (malha base de 500 triangulos)
        { "planet_count", "PC_500",   500,   1,  0 },
        { "planet_count", "PC_2500",  2500,  5,  0 },
        { "planet_count", "PC_10000", 10000, 20, 0 },
        { "planet_count", "PC_40000", 40000, 80, 0 }
    };

    int current_stage_idx = 0;
    bool benchmark_suite_running = false;
    bool show_hud = true;

    // Variaveis de camera para o Modo Interativo e Modo Sintetico
    float interactive_cam_yaw = 0.6f;
    float interactive_cam_pitch = 0.4f;
    float interactive_cam_dist = cfg.cam_orbit_radius;

    std::vector<MathSimd::PlanetOrbitState> active_planets;
    std::vector<Matrix> model_matrices;
    std::vector<Matrix> mvp_matrices;
    std::vector<StageSummary> suite_summaries;

    TelemetryCollector telemetry;
    double stage_timer_start = telemetry.GetMonotonicSeconds();
    bool in_warmup_phase = true;

    auto ActivateStage = [&](int stage_idx) {
        current_stage_idx = stage_idx;
        const auto& st = all_stages[current_stage_idx];
        active_planets = BuildPlanetOrbits(st.planet_count, cfg);
        telemetry.StartStage("Raylib_Pilha_A",
                             st.mode_name,
                             st.stage_id,
                             st.planet_count,
                             st.total_triangles,
                             cfg.rss_sample_interval);
        stage_timer_start = telemetry.GetMonotonicSeconds();
        in_warmup_phase = true;
    };

    ActivateStage(0);

    while (!WindowShouldClose()) {
        double now_sec = telemetry.GetMonotonicSeconds();
        float sim_time = static_cast<float>(now_sec);

        // Controles de Teclado
        if (IsKeyPressed(KEY_H)) show_hud = !show_hud;

        if (!benchmark_suite_running) {
            // Alternar entre Modo 1 (mesh_density: indices 0..3) e Modo 2 (planet_count: indices 4..7)
            if (IsKeyPressed(KEY_M)) {
                int next_idx = (current_stage_idx + 4) % 8;
                ActivateStage(next_idx);
            }
            int mode_offset = (current_stage_idx >= 4) ? 4 : 0;
            if (IsKeyPressed(KEY_ONE))   ActivateStage(mode_offset + 0);
            if (IsKeyPressed(KEY_TWO))   ActivateStage(mode_offset + 1);
            if (IsKeyPressed(KEY_THREE)) ActivateStage(mode_offset + 2);
            if (IsKeyPressed(KEY_FOUR))  ActivateStage(mode_offset + 3);

            // Iniciar bateria completa automatizada (8 estagios)
            if (IsKeyPressed(KEY_B)) {
                benchmark_suite_running = true;
                suite_summaries.clear();
                ActivateStage(0);
            }

            // Controle de camera interativa com mouse + roda do mouse + setas/WASD
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                Vector2 delta = GetMouseDelta();
                interactive_cam_yaw += delta.x * 0.008f;
                interactive_cam_pitch = Clamp(interactive_cam_pitch + delta.y * 0.008f, -1.4f, 1.4f);
            }
            if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))  interactive_cam_yaw -= 0.02f;
            if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) interactive_cam_yaw += 0.02f;
            if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))    interactive_cam_pitch = Clamp(interactive_cam_pitch + 0.02f, -1.4f, 1.4f);
            if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))  interactive_cam_pitch = Clamp(interactive_cam_pitch - 0.02f, -1.4f, 1.4f);

            float wheel = GetMouseWheelMove();
            if (std::abs(wheel) > 0.001f) {
                interactive_cam_dist = Clamp(interactive_cam_dist - wheel * 2.0f, 4.0f, 80.0f);
            }
        } else {
            // Permite cancelar a bateria com BACKSPACE se necessario
            if (IsKeyPressed(KEY_BACKSPACE)) {
                benchmark_suite_running = false;
            }
        }

        // Posicionamento da Camera
        Camera3D camera = { 0 };
        camera.target = { 0.0f, 0.0f, 0.0f };
        camera.up = { 0.0f, 1.0f, 0.0f };
        camera.fovy = cfg.cam_fov;
        camera.projection = CAMERA_PERSPECTIVE;

        if (benchmark_suite_running) {
            // Equacao do artigo: orbita sintetica a velocidade angular constante theta(t) = omega * t
            float theta = cfg.cam_angular_vel * sim_time;
            camera.position.x = cfg.cam_orbit_radius * std::cos(theta);
            camera.position.y = cfg.cam_orbit_height;
            camera.position.z = cfg.cam_orbit_radius * std::sin(theta);
        } else {
            camera.position.x = interactive_cam_dist * std::cos(interactive_cam_pitch) * std::cos(interactive_cam_yaw);
            camera.position.y = interactive_cam_dist * std::sin(interactive_cam_pitch);
            camera.position.z = interactive_cam_dist * std::cos(interactive_cam_pitch) * std::sin(interactive_cam_yaw);
        }

        // Matrizes View e Projection para o lote SIMD
        double aspect = static_cast<double>(GetScreenWidth()) / static_cast<double>(GetScreenHeight());
        Matrix mat_view = MatrixLookAt(camera.position, camera.target, camera.up);
        Matrix mat_proj = MatrixPerspective(camera.fovy * DEG2RAD, aspect, 0.1, 200.0);
        Matrix mat_view_proj = MathSimd::MatrixMultiplySimd(mat_proj, mat_view);

        // Calculo em lote (ARM NEON no RPi3 / Escalar no PC) das matrizes Model e MVP de todos os planetas
        MathSimd::UpdatePlanetsBatchSimd(active_planets, mat_view_proj, sim_time, model_matrices, mvp_matrices);

        // Renderizacao do Quadro
        BeginDrawing();
        ClearBackground({ 10, 12, 22, 255 }); // Fundo espacial escuro

        BeginMode3D(camera);

        const auto& current_stage = all_stages[current_stage_idx];
        Mesh active_mesh = planet_models[current_stage.mesh_index].meshes[0];

        rlEnableShader(lambert_shader.id);
        for (size_t i = 0; i < active_planets.size(); ++i) {
            // Envia as matrizes calculadas pelo nosso nucleo SIMD para os uniforms do shader
            SetShaderValueMatrix(lambert_shader, loc_model, model_matrices[i]);
            SetShaderValueMatrix(lambert_shader, loc_mvp, mvp_matrices[i]);

            // Vincula o VBO da malha e dispara o draw call indexado/direto
            rlEnableVertexArray(active_mesh.vaoId);
            if (active_mesh.vaoId == 0) {
                // Em OpenGL ES 2.0 puro (sem VAO), vincula os VBOs de posicao, texcoord e normal
                rlEnableVertexBuffer(active_mesh.vboId[0]);
                rlSetVertexAttribute(lambert_shader.locs[SHADER_LOC_VERTEX_POSITION], 3, RL_FLOAT, 0, 0, 0);
                rlEnableVertexAttribute(lambert_shader.locs[SHADER_LOC_VERTEX_POSITION]);

                rlEnableVertexBuffer(active_mesh.vboId[1]);
                rlSetVertexAttribute(lambert_shader.locs[SHADER_LOC_VERTEX_TEXCOORD01], 2, RL_FLOAT, 0, 0, 0);
                rlEnableVertexAttribute(lambert_shader.locs[SHADER_LOC_VERTEX_TEXCOORD01]);

                rlEnableVertexBuffer(active_mesh.vboId[2]);
                rlSetVertexAttribute(lambert_shader.locs[SHADER_LOC_VERTEX_NORMAL], 3, RL_FLOAT, 0, 0, 0);
                rlEnableVertexAttribute(lambert_shader.locs[SHADER_LOC_VERTEX_NORMAL]);

                if (active_mesh.indices != nullptr) {
                    rlEnableVertexBufferElement(active_mesh.vboId[6]);
                }
            }

            if (active_mesh.indices != nullptr) {
                rlDrawVertexArrayElements(0, active_mesh.triangleCount * 3, nullptr);
            } else {
                rlDrawVertexArray(0, active_mesh.vertexCount);
            }
            rlDisableVertexArray();
        }
        rlDisableShader();

        EndMode3D();

        // HUD Informativo
        if (show_hud) {
            DrawRectangle(10, 10, 460, 185, Fade(BLACK, 0.72f));
            DrawRectangleLines(10, 10, 460, 185, SKYBLUE);

            DrawText(TextFormat("PILHA A: C++/Raylib | SIMD: %s", MathSimd::GetSimdBackendName()), 20, 18, 16, SKYBLUE);
            DrawText(TextFormat("Estado: %s", benchmark_suite_running
                     ? (in_warmup_phase ? "BENCHMARK (AQUECIMENTO)" : "BENCHMARK (GRAVANDO CSV)")
                     : "MODO INTERATIVO (Mouse/WASD)"),
                     20, 40, 16, benchmark_suite_running ? YELLOW : GREEN);

            DrawText(TextFormat("Modo: %s | Estagio: %s",
                     current_stage.mode_name.c_str(), current_stage.stage_id.c_str()), 20, 62, 16, WHITE);
            DrawText(TextFormat("Planetas Ativos: %d | Triangulos Totais: %d",
                     current_stage.planet_count, current_stage.total_triangles), 20, 84, 16, WHITE);

            double last_ft = telemetry.GetPrevFrameTimeMs();
            double inst_fps = (last_ft > 1e-5) ? (1000.0 / last_ft) : 0.0;
            DrawText(TextFormat("FPS: %.1f | T_quadro: %.3f ms | RSS: %.2f MB",
                     inst_fps, last_ft, telemetry.GetCachedRssMB()), 20, 108, 16, GOLD);

            DrawText("[M] Trocar Modo (Densidade vs Qtd Planetas) | [1-4] Carga", 20, 138, 14, LIGHTGRAY);
            DrawText("[B] Rodar Bateria Completa (CSV) | [H] Ocultar HUD", 20, 158, 14, LIGHTGRAY);
        }

        EndDrawing(); // Dispara o buffer swap (SwapBuffers)

        // Coleta de Telemetria pos-SwapBuffers (Equacoes 3 e 4 do artigo)
        double elapsed_in_stage = telemetry.GetMonotonicSeconds() - stage_timer_start;
        if (benchmark_suite_running) {
            if (in_warmup_phase) {
                telemetry.RecordBufferSwap(false);
                if (elapsed_in_stage >= cfg.warmup_seconds) {
                    in_warmup_phase = false;
                    stage_timer_start = telemetry.GetMonotonicSeconds();
                }
            } else {
                telemetry.RecordBufferSwap(true);
                if (elapsed_in_stage >= cfg.sample_duration_seconds) {
                    // Exporta o CSV individual do estagio
                    fs::path stage_csv = csv_out_dir / ("raylib_" + current_stage.stage_id + ".csv");
                    telemetry.ExportStageCsv(stage_csv.string());
                    StageSummary sum = telemetry.ComputeSummary();
                    suite_summaries.push_back(sum);

                    std::cout << "[CONCLUIDO] " << current_stage.stage_id
                              << " | FPS Medio: " << sum.avg_fps
                              << " | T_quadro: " << sum.mean_frame_ms << " ms"
                              << " | Jitter Medio: " << sum.mean_jitter_ms << " ms"
                              << " | RSS Pico: " << sum.peak_rss_mb << " MB\n";

                    if (current_stage_idx + 1 < static_cast<int>(all_stages.size())) {
                        ActivateStage(current_stage_idx + 1);
                    } else {
                        // Salva o arquivo consolidado com o resumo dos 8 estagios
                        fs::path sum_csv = csv_out_dir / "summary_raylib_pilha_a.csv";
                        std::ofstream sfile(sum_csv);
                        sfile << "engine,mode,stage_id,planets,triangles,frames,avg_fps,mean_frame_ms,stddev_frame_ms,p99_frame_ms,mean_jitter_ms,max_jitter_ms,peak_rss_mb\n";
                        sfile << std::fixed << std::setprecision(4);
                        for (const auto& s : suite_summaries) {
                            sfile << s.engine_name << "," << s.mode_name << "," << s.stage_id << ","
                                  << s.planet_count << "," << s.total_triangles << "," << s.total_frames << ","
                                  << s.avg_fps << "," << s.mean_frame_ms << "," << s.stddev_frame_ms << ","
                                  << s.p99_frame_ms << "," << s.mean_jitter_ms << "," << s.max_jitter_ms << ","
                                  << s.peak_rss_mb << "\n";
                        }
                        std::cout << "=========================================================\n";
                        std::cout << "[SUCESSO] Bateria completa finalizada! CSVs salvos em:\n"
                                  << csv_out_dir.string() << "\n";
                        std::cout << "=========================================================\n";
                        benchmark_suite_running = false;
                        ActivateStage(0);
                    }
                }
            }
        } else {
            telemetry.RecordBufferSwap(false);
        }
    }

    for (auto& model : planet_models) {
        UnloadModel(model);
    }
    UnloadShader(lambert_shader);
    CloseWindow();

    return 0;
}