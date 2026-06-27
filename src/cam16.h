#pragma once

// CAM16 colour appearance model - C++ implementation
// Formulas sourced from colour-science/colour (BSD-3-Clause)
// See cam16/cam16_formulas.md for formula references

// BT.2020 RGB (linear, in nit) <-> CIE XYZ (scale=100)
// Matrix from ITU-R BT.2020 standard (D65 white point)
// Luma coefficients: 0.2627, 0.6780, 0.0593 (verified from HDR Vivid PDF)

struct CAM16ViewingConditions {
    float XYZ_w[3];  // adapting white XYZ (scale=100), D65 = [95.04, 100.0, 108.88]
    float L_A;        // adapting luminance (cd/m^2)
    float Y_b;        // background luminance factor (scale=100)
    float F = 1.0f;  // surround: Average
    float c = 0.69f; // surround: Average
    float N_c = 1.0f; // surround: Average
};

struct CAM16Intermediate {
    float n, F_L, N_bb, N_cb, z, D;
    float D_RGB[3];
    float RGB_aw[3];
    float A_w;
};

struct CAM16Appearance {
    float J, C, h, s, Q, M;
};

// Precompute viewing-condition-dependent parameters
void cam16_precompute(const CAM16ViewingConditions& vc, CAM16Intermediate& im);

// Forward: XYZ (scale=100) -> appearance correlates
CAM16Appearance cam16_forward(const float XYZ[3],
                              const CAM16ViewingConditions& vc,
                              const CAM16Intermediate& im);

// Inverse: (J, C, h) -> XYZ (scale=100)
void cam16_inverse(float XYZ_out[3],
                   float J, float C, float h,
                   const CAM16ViewingConditions& vc,
                   const CAM16Intermediate& im);

// Bridge: (Q', s) -> (J', C')
void cam16_Qs_to_JC(float& J_out, float& C_out,
                    float Q, float s,
                    const CAM16ViewingConditions& vc,
                    const CAM16Intermediate& im);

// PQ encode/decode (ST.2084)
float pq_encode(float nit);
float pq_decode(float pq_code);

// BT.2020 linear RGB (nit) <-> CIE XYZ (scale=100)
// RGB_nit is in cd/m^2; XYZ is in [0,100] scale where Y_white=100
void bt2020_nit_to_xyz100(const float rgb_nit[3], float xyz[3]);
void xyz100_to_bt2020_nit(const float xyz[3], float rgb_nit[3]);

// Full per-pixel pipeline:
// Input:  rgb_pq_in[3]  (PQ code * 1023, 10-bit full range, BT.2020)
// Output: rgb_pq_out[3] (PQ code * 1023, clamped [0,1023])
// Returns: true if pixel was in gamut (no clamp needed)
bool cam16_adjust_pixel(const float rgb_pq_in[3],
                        float rgb_pq_out[3],
                        float q_scale,
                        const CAM16ViewingConditions& vc,
                        const CAM16Intermediate& im);
