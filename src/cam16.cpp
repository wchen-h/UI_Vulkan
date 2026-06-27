#include "cam16.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
// CAT16 matrix (from colour-science/colour: colour/adaptation/datasets/cat.py)
// Reference: Li et al. (2017), doi:10.1002/col.22131
// ============================================================
static const float M16[3][3] = {
    { 0.401288f,  0.650173f, -0.051461f},
    {-0.250268f,  1.204414f,  0.045854f},
    {-0.002079f,  0.048952f,  0.953127f}
};

// Inverse of CAT16 (computed with numpy.linalg.inv)
static const float M16_inv[3][3] = {
    { 1.862068f, -1.011255f,  0.149187f},
    { 0.387527f,  0.621447f, -0.008974f},
    {-0.015841f, -0.034123f,  1.049964f}
};

// BT.2020 RGB -> XYZ matrix (D65 white, normalized so Y_white=1 when RGB=(1,1,1))
// Standard ITU-R BT.2020 values
static const float M_BT2020_TO_XYZ[3][3] = {
    {0.63695805f, 0.14461690f, 0.16888098f},
    {0.26269834f, 0.67800984f, 0.05929187f},
    {0.00000000f, 0.02807269f, 1.06092719f}
};

static const float M_XYZ_TO_BT2020[3][3] = {
    { 1.716647f, -0.355663f, -0.253383f},
    {-0.666666f,  1.616449f,  0.015783f},
    { 0.017640f, -0.042772f,  0.942154f}
};

// ============================================================
// Matrix multiply helper
// ============================================================
static void mat3_mul_vec(const float m[3][3], const float v[3], float out[3]) {
    for (int i = 0; i < 3; ++i)
        out[i] = m[i][0]*v[0] + m[i][1]*v[1] + m[i][2]*v[2];
}

// spow (signed power, from colour-science colour.algebra.spow)
static float spow(float base, float exp) {
    return std::copysign(std::pow(std::fabs(base), exp), base);
}

// ============================================================
// PQ encode/decode (ST.2084)
// ============================================================
float pq_encode(float nit) {
    // Source: shaders/pq_convert.frag linearToPQ + hdr_app.cpp
    float y = nit / 10000.0f;
    if (y <= 0.0f) return 0.0f;
    float yPow = std::pow(y, 2610.0f / 16384.0f);
    float num = 3424.0f / 4096.0f + (2413.0f / 128.0f) * yPow;
    float den = 1.0f + (2392.0f / 128.0f) * yPow;
    return std::pow(num / den, 2523.0f / 32.0f);
}

float pq_decode(float pq_code) {
    // Inverse of pq_encode
    if (pq_code <= 0.0f) return 0.0f;
    float vp = std::pow(pq_code, 32.0f / 2523.0f);
    float num = std::max(vp - 3424.0f / 4096.0f, 0.0f);
    float den = (2413.0f / 128.0f) - (2392.0f / 128.0f) * vp;
    if (den <= 0.0f) return 10000.0f;
    return 10000.0f * std::pow(num / den, 16384.0f / 2610.0f);
}

// ============================================================
// BT.2020 linear RGB (nit) <-> CIE XYZ (scale=100)
// ============================================================
// whiteNit = display peak luminance (reference white)
static const float WHITE_NIT = 4000.0f;

void bt2020_nit_to_xyz100(const float rgb_nit[3], float xyz[3]) {
    float norm[3] = {rgb_nit[0] / WHITE_NIT, rgb_nit[1] / WHITE_NIT, rgb_nit[2] / WHITE_NIT};
    float xyz_norm[3];
    mat3_mul_vec(M_BT2020_TO_XYZ, norm, xyz_norm);
    xyz[0] = xyz_norm[0] * 100.0f;
    xyz[1] = xyz_norm[1] * 100.0f;
    xyz[2] = xyz_norm[2] * 100.0f;
}

void xyz100_to_bt2020_nit(const float xyz[3], float rgb_nit[3]) {
    float xyz_norm[3] = {xyz[0] / 100.0f, xyz[1] / 100.0f, xyz[2] / 100.0f};
    float norm[3];
    mat3_mul_vec(M_XYZ_TO_BT2020, xyz_norm, norm);
    rgb_nit[0] = norm[0] * WHITE_NIT;
    rgb_nit[1] = norm[1] * WHITE_NIT;
    rgb_nit[2] = norm[2] * WHITE_NIT;
}

// ============================================================
// CAM16 intermediate parameters
// Source: ciecam02.py viewing_conditions_dependent_parameters + degree_of_adaptation
//         + cam16.py XYZ_to_CAM16 Step 0 (whitepoint preprocessing)
// ============================================================
void cam16_precompute(const CAM16ViewingConditions& vc, CAM16Intermediate& im) {
    // Source: ciecam02.py viewing_conditions_dependent_parameters
    im.n = vc.Y_b / vc.XYZ_w[1];  // n = Y_b / Y_w
    float Y_w = vc.XYZ_w[1];

    // Source: hunt.py luminance_level_adaptation_factor
    float k = 1.0f / (5.0f * vc.L_A + 1.0f);
    float k4 = k * k * k * k;
    im.F_L = 0.2f * k4 * (5.0f * vc.L_A) +
             0.1f * (1.0f - k4) * (1.0f - k4) * std::pow(5.0f * vc.L_A, 1.0f / 3.0f);

    // Source: ciecam02.py chromatic_induction_factors
    im.N_bb = im.N_cb = 0.725f * std::pow(1.0f / im.n, 0.2f);

    // Source: ciecam02.py base_exponential_non_linearity
    im.z = 1.48f + std::sqrt(im.n);

    // Source: ciecam02.py degree_of_adaptation
    im.D = vc.F * (1.0f - (1.0f / 3.6f) * std::exp((-vc.L_A - 42.0f) / 92.0f));
    im.D = std::clamp(im.D, 0.0f, 1.0f);

    // Source: cam16.py XYZ_to_CAM16 Step 0
    float RGB_w[3];
    mat3_mul_vec(M16, vc.XYZ_w, RGB_w);
    for (int i = 0; i < 3; ++i)
        im.D_RGB[i] = im.D * Y_w / RGB_w[i] + 1.0f - im.D;

    float RGB_wc[3];
    for (int i = 0; i < 3; ++i)
        RGB_wc[i] = im.D_RGB[i] * RGB_w[i];

    // Source: ciecam02.py post_adaptation_non_linear_response_compression_forward
    for (int i = 0; i < 3; ++i) {
        float val = RGB_wc[i];
        float fl_rgb = std::pow(im.F_L * std::fabs(val) / 100.0f, 0.42f);
        im.RGB_aw[i] = 400.0f * std::copysign(fl_rgb, val) / (27.13f + fl_rgb) + 0.1f;
    }

    // Source: ciecam02.py achromatic_response_forward
    im.A_w = (2.0f * im.RGB_aw[0] + im.RGB_aw[1] + (1.0f / 20.0f) * im.RGB_aw[2] - 0.305f) * im.N_bb;
}

// ============================================================
// CAM16 forward: XYZ (scale=100) -> (J, C, h, Q, M, s)
// Source: cam16.py XYZ_to_CAM16
// ============================================================
CAM16Appearance cam16_forward(const float XYZ[3],
                              const CAM16ViewingConditions& vc,
                              const CAM16Intermediate& im) {
    // Step 1: XYZ -> RGB (CAT16 sharpened)
    float RGB[3];
    mat3_mul_vec(M16, XYZ, RGB);

    // Step 2: chromatic adaptation
    float RGB_c[3];
    for (int i = 0; i < 3; ++i)
        RGB_c[i] = im.D_RGB[i] * RGB[i];

    // Step 3: forward non-linear compression
    float RGB_a[3];
    for (int i = 0; i < 3; ++i) {
        float val = RGB_c[i];
        float fl_rgb = std::pow(im.F_L * std::fabs(val) / 100.0f, 0.42f);
        RGB_a[i] = 400.0f * std::copysign(fl_rgb, val) / (27.13f + fl_rgb) + 0.1f;
    }

    // Step 4: opponent colour dimensions
    // Source: ciecam02.py opponent_colour_dimensions_forward
    float a = RGB_a[0] - 12.0f * RGB_a[1] / 11.0f + RGB_a[2] / 11.0f;
    float b = (RGB_a[0] + RGB_a[1] - 2.0f * RGB_a[2]) / 9.0f;

    // Step 5: hue angle
    // Source: ciecam02.py hue_angle
    float h = std::fmod(std::atan2(b, a) * 180.0f / M_PI + 360.0f, 360.0f);

    // Step 6: eccentricity factor
    // Source: ciecam02.py eccentricity_factor
    float e_t = 0.25f * (std::cos(2.0f + h * M_PI / 180.0f) + 3.8f);

    // Step 7: achromatic response + lightness
    // Source: ciecam02.py achromatic_response_forward + lightness_correlate
    float A = (2.0f * RGB_a[0] + RGB_a[1] + (1.0f / 20.0f) * RGB_a[2] - 0.305f) * im.N_bb;
    float J = 100.0f * spow(A / im.A_w, vc.c * im.z);

    // Step 8: brightness
    // Source: ciecam02.py brightness_correlate
    float Q = (4.0f / vc.c) * std::sqrt(J / 100.0f) * (im.A_w + 4.0f) * std::pow(im.F_L, 0.25f);

    // Step 9: chroma
    // Source: ciecam02.py temporary_magnitude_quantity_forward + chroma_correlate
    float t = (50000.0f / 13.0f * vc.N_c * im.N_cb) * e_t *
              std::sqrt(a * a + b * b) / (RGB_a[0] + RGB_a[1] + 21.0f * RGB_a[2] / 20.0f);
    float C = std::pow(t, 0.9f) * std::sqrt(J / 100.0f) * std::pow(1.64f - std::pow(0.29f, im.n), 0.73f);

    // Step 10: colourfulness
    float M = C * std::pow(im.F_L, 0.25f);

    // Step 11: saturation
    // Source: ciecam02.py saturation_correlate
    float s = 100.0f * std::sqrt(M / Q);

    return {J, C, h, s, Q, M};
}

// ============================================================
// CAM16 inverse: (J, C, h) -> XYZ (scale=100)
// Source: cam16.py CAM16_to_XYZ
// ============================================================
void cam16_inverse(float XYZ_out[3],
                   float J, float C, float h,
                   const CAM16ViewingConditions& vc,
                   const CAM16Intermediate& im) {
    // Step 2: temporary magnitude quantity t
    // Source: ciecam02.py temporary_magnitude_quantity_inverse
    float J_safe = std::max(J, 0.0001f);
    float t = std::pow(C / (std::sqrt(J_safe / 100.0f) * std::pow(1.64f - std::pow(0.29f, im.n), 0.73f)),
                       1.0f / 0.9f);

    // Step 3: e_t, A, P_1~P_3
    // Source: ciecam02.py eccentricity_factor + achromatic_response_inverse + P
    float e_t = 0.25f * (std::cos(2.0f + h * M_PI / 180.0f) + 3.8f);
    float A = im.A_w * std::pow(J_safe / 100.0f, 1.0f / (vc.c * im.z));

    float P_1 = (50000.0f / 13.0f * vc.N_c * im.N_cb * e_t) / t;
    float P_2 = A / im.N_bb + 0.305f;
    float P_3 = 21.0f / 20.0f;

    // Step 4: opponent colour dimensions (inverse)
    // Source: ciecam02.py opponent_colour_dimensions_inverse
    float hr = h * M_PI / 180.0f;
    float sin_hr = std::sin(hr);
    float cos_hr = std::cos(hr);
    float n_ab = P_2 * (2.0f + P_3) * (460.0f / 1403.0f);

    float a = 0.0f, b = 0.0f;
    if (std::fabs(sin_hr) >= std::fabs(cos_hr)) {
        float P_4 = P_1 / sin_hr;
        b = n_ab / (P_4 + (2.0f + P_3) * (220.0f / 1403.0f) * cos_hr / sin_hr
                     - 27.0f / 1403.0f + P_3 * (6300.0f / 1403.0f));
        a = b * cos_hr / sin_hr;
    } else {
        float P_5 = P_1 / cos_hr;
        a = n_ab / (P_5 + (2.0f + P_3) * (220.0f / 1403.0f)
                     - ((27.0f / 1403.0f) - P_3 * (6300.0f / 1403.0f)) * sin_hr / cos_hr);
        b = a * sin_hr / cos_hr;
    }

    if (t == 0.0f) { a = 0.0f; b = 0.0f; }

    // Step 5: matrix post-adaptation non-linear response compression
    // Source: ciecam02.py matrix_post_adaptation_non_linear_response_compression
    float RGB_a[3];
    float input[3] = {P_2, a, b};
    float matrix[3][3] = {{460, 451, 288}, {460, -891, -261}, {460, -220, -6300}};
    mat3_mul_vec(matrix, input, RGB_a);
    for (int i = 0; i < 3; ++i) RGB_a[i] /= 1403.0f;

    // Step 6: inverse non-linear compression
    // Source: ciecam02.py post_adaptation_non_linear_response_compression_inverse
    float RGB_c[3];
    for (int i = 0; i < 3; ++i) {
        float val = RGB_a[i] - 0.1f;
        RGB_c[i] = std::copysign(1.0f, val) * 100.0f / im.F_L *
                   std::pow(27.13f * std::fabs(val) / (400.0f - std::fabs(val)), 1.0f / 0.42f);
    }

    // Step 7: inverse chromatic adaptation + inverse CAT16
    // Source: cam16.py CAM16_to_XYZ Steps 6-7
    float RGB[3];
    for (int i = 0; i < 3; ++i)
        RGB[i] = RGB_c[i] / im.D_RGB[i];

    mat3_mul_vec(M16_inv, RGB, XYZ_out);
}

// ============================================================
// Bridge: (Q', s) -> (J', C')
// Source: cam16_formulas.md §7
// ============================================================
void cam16_Qs_to_JC(float& J_out, float& C_out,
                    float Q, float s,
                    const CAM16ViewingConditions& vc,
                    const CAM16Intermediate& im) {
    // Q = (4/c) * sqrt(J/100) * (A_w + 4) * F_L^0.25
    // => J = 100 * (Q * c / (4 * (A_w + 4) * F_L^0.25))^2
    float denom = 4.0f * (im.A_w + 4.0f) * std::pow(im.F_L, 0.25f);
    if (denom <= 0.0f) {
        J_out = 0.0f; C_out = 0.0f; return;
    }
    J_out = 100.0f * std::pow(Q * vc.c / denom, 2.0f);

    // s = 100 * sqrt(M/Q) => M = (s/100)^2 * Q
    // M = C * F_L^0.25 => C = M / F_L^0.25
    float M = (s / 100.0f) * (s / 100.0f) * Q;
    C_out = M / std::pow(im.F_L, 0.25f);
}

// ============================================================
// Full per-pixel pipeline
// Input:  rgb_pq_in[3]  (10-bit full range [0,1023], PQ encoded, BT.2020)
// Output: rgb_pq_out[3] (10-bit full range [0,1023], clamped)
// Returns: true if in gamut (no clamp)
// ============================================================
bool cam16_adjust_pixel(const float rgb_pq_in[3],
                        float rgb_pq_out[3],
                        float q_scale,
                        const CAM16ViewingConditions& vc,
                        const CAM16Intermediate& im) {
    // 1. PQ decode -> nit
    float nit[3];
    for (int i = 0; i < 3; ++i)
        nit[i] = pq_decode(rgb_pq_in[i] / 1023.0f);

    // 2. BT.2020 nit -> XYZ (scale=100)
    float XYZ[3];
    bt2020_nit_to_xyz100(nit, XYZ);

    // 3. CAM16 forward
    CAM16Appearance app = cam16_forward(XYZ, vc, im);

    // 4. Adjust: Q' = Q * k, h' = h, s' = s
    float Q_new = app.Q * q_scale;

    // 5. Bridge: (Q', s) -> (J', C')
    float J_new, C_new;
    cam16_Qs_to_JC(J_new, C_new, Q_new, app.s, vc, im);

    // 6. CAM16 inverse
    float XYZ_out[3];
    cam16_inverse(XYZ_out, J_new, C_new, app.h, vc, im);

    // 7. XYZ -> BT.2020 nit
    float nit_out[3];
    xyz100_to_bt2020_nit(XYZ_out, nit_out);

    // 8. PQ encode -> 10-bit
    bool inGamut = true;
    for (int i = 0; i < 3; ++i) {
        float pq = pq_encode(nit_out[i]);
        float val = pq * 1023.0f;
        if (val < 0.0f || val > 1023.0f) inGamut = false;
        rgb_pq_out[i] = std::clamp(val, 0.0f, 1023.0f);
    }

    return inGamut;
}
