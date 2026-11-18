#ifndef MATH_NEON_HPP
#define MATH_NEON_HPP

#include "raylib.h"
#include "raymath.h"
#include <cmath>
#include <vector>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    #include <arm_neon.h>
    #define USING_ARM_NEON 1
#else
    #define USING_ARM_NEON 0
#endif

namespace MathSimd {

inline const char* GetSimdBackendName() {
#if USING_ARM_NEON
    return "ARM_NEON_128bit";
#else
    return "Scalar_x86_Fallback";
#endif
}

// Multiplicacao algebrica de matrizes 4x4: C = left * right
inline Matrix MatrixMultiplySimd(const Matrix& left, const Matrix& right) {
    Matrix result = { 0 };

#if USING_ARM_NEON
    // Na struct Matrix da Raylib, cada linha (m0,m4,m8,m12)... e contigua na memoria
    float32x4_t r_row0 = vld1q_f32(&right.m0);
    float32x4_t r_row1 = vld1q_f32(&right.m1);
    float32x4_t r_row2 = vld1q_f32(&right.m2);
    float32x4_t r_row3 = vld1q_f32(&right.m3);

    float32x4_t res0 = vmulq_n_f32(r_row0, left.m0);
    res0 = vmlaq_n_f32(res0, r_row1, left.m4);
    res0 = vmlaq_n_f32(res0, r_row2, left.m8);
    res0 = vmlaq_n_f32(res0, r_row3, left.m12);
    vst1q_f32(&result.m0, res0);

    float32x4_t res1 = vmulq_n_f32(r_row0, left.m1);
    res1 = vmlaq_n_f32(res1, r_row1, left.m5);
    res1 = vmlaq_n_f32(res1, r_row2, left.m9);
    res1 = vmlaq_n_f32(res1, r_row3, left.m13);
    vst1q_f32(&result.m1, res1);

    float32x4_t res2 = vmulq_n_f32(r_row0, left.m2);
    res2 = vmlaq_n_f32(res2, r_row1, left.m6);
    res2 = vmlaq_n_f32(res2, r_row2, left.m10);
    res2 = vmlaq_n_f32(res2, r_row3, left.m14);
    vst1q_f32(&result.m2, res2);

    float32x4_t res3 = vmulq_n_f32(r_row0, left.m3);
    res3 = vmlaq_n_f32(res3, r_row1, left.m7);
    res3 = vmlaq_n_f32(res3, r_row2, left.m11);
    res3 = vmlaq_n_f32(res3, r_row3, left.m15);
    vst1q_f32(&result.m3, res3);
#else
    // Na raymath.h, MatrixMultiply(a, b) computa b * a; invertemos os argumentos
    // para manter a mesma convencao algebrica C = left * right do bloco NEON.
    result = MatrixMultiply(right, left);
#endif

    return result;
}

struct PlanetOrbitState {
    float orbit_radius;
    float orbital_speed;
    float initial_phase;
    float self_rotation_speed;
    float scale;
};

// Atualiza em lote (batch) todas as matrizes Model e MVP via SIMD/Escalar
inline void UpdatePlanetsBatchSimd(const std::vector<PlanetOrbitState>& planets,
                                   const Matrix& view_proj,
                                   float time_sec,
                                   std::vector<Matrix>& out_model_matrices,
                                   std::vector<Matrix>& out_mvp_matrices) {
    const size_t count = planets.size();
    out_model_matrices.resize(count);
    out_mvp_matrices.resize(count);

    for (size_t i = 0; i < count; ++i) {
        const auto& p = planets[i];

        float x = 0.0f;
        float z = 0.0f;
        if (i > 0) {
            float angle = p.initial_phase + p.orbital_speed * time_sec;
            x = p.orbit_radius * std::cos(angle);
            z = p.orbit_radius * std::sin(angle);
        }

        Matrix mat_scale = MatrixScale(p.scale, p.scale, p.scale);
        Matrix mat_rot_y = MatrixRotateY(p.self_rotation_speed * time_sec);
        Matrix mat_trans = MatrixTranslate(x, 0.0f, z);

        // Composicao algebrica: Model = Translate * RotateY * Scale
        Matrix rot_scale = MatrixMultiplySimd(mat_rot_y, mat_scale);
        Matrix model = MatrixMultiplySimd(mat_trans, rot_scale);

        out_model_matrices[i] = model;
        // MVP = ViewProj * Model
        out_mvp_matrices[i] = MatrixMultiplySimd(view_proj, model);
    }
}

} // namespace MathSimd

#endif // MATH_NEON_HPP