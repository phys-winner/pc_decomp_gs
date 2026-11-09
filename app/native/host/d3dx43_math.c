#include "host.h"
#include <math.h>

#define EXPORT(name, nargs) WINAPI_EXPORT("d3dx9_43.dll", name, nargs)

typedef struct { float x, y; } v2;
typedef struct { float x, y, z; } v3;
typedef struct { float x, y, z, w; } v4;
typedef struct { float x, y, z, w; } quat;

static float farg(int index)
{
    uint32_t value = ARG(index);
    float result;
    memcpy(&result, &value, 4);
    return result;
}

static v3 v3_read(uint32_t address)
{
    v3 value;
    memcpy(&value, GPTR(address), sizeof value);
    return value;
}

static v4 v4_read(uint32_t address)
{
    v4 value;
    memcpy(&value, GPTR(address), sizeof value);
    return value;
}

static quat quat_read(uint32_t address)
{
    quat value;
    memcpy(&value, GPTR(address), sizeof value);
    return value;
}

static void v3_write(uint32_t address, v3 value) { memcpy(GPTR(address), &value, sizeof value); }
static void v4_write(uint32_t address, v4 value) { memcpy(GPTR(address), &value, sizeof value); }
static void quat_write(uint32_t address, quat value) { memcpy(GPTR(address), &value, sizeof value); }
static v3 v3_add(v3 a, v3 b) { return (v3){a.x + b.x, a.y + b.y, a.z + b.z}; }
static v3 v3_sub(v3 a, v3 b) { return (v3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static v3 v3_scale(v3 a, float scale) { return (v3){a.x * scale, a.y * scale, a.z * scale}; }
static float v3_dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static v3 v3_cross(v3 a, v3 b) { return (v3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static float v3_length(v3 a) { return sqrtf(v3_dot(a, a)); }
static v3 v3_normalize(v3 a) { float length = v3_length(a); return length > 0 ? v3_scale(a, 1.0f / length) : a; }

static float *matrix(uint32_t address) { return (float *)GPTR(address); }

static void matrix_identity(float *m)
{
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void matrix_multiply(float *out, const float *a, const float *b)
{
    float result[16];
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            result[row * 4 + col] = a[row * 4] * b[col] + a[row * 4 + 1] * b[4 + col] +
                a[row * 4 + 2] * b[8 + col] + a[row * 4 + 3] * b[12 + col];
    memcpy(out, result, sizeof result);
}

static void matrix_translation(float *m, float x, float y, float z)
{
    matrix_identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

static void matrix_rotation_x(float *m, float angle)
{
    matrix_identity(m);
    m[5] = m[10] = cosf(angle);
    m[6] = sinf(angle);
    m[9] = -m[6];
}

static void matrix_rotation_y(float *m, float angle)
{
    matrix_identity(m);
    m[0] = m[10] = cosf(angle);
    m[2] = -sinf(angle);
    m[8] = -m[2];
}

static void matrix_rotation_z(float *m, float angle)
{
    matrix_identity(m);
    m[0] = m[5] = cosf(angle);
    m[1] = sinf(angle);
    m[4] = -m[1];
}

static void matrix_rotation_quaternion(float *m, quat q)
{
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    matrix_identity(m);
    m[0] = 1 - 2 * (yy + zz);
    m[1] = 2 * (q.x * q.y + q.z * q.w);
    m[2] = 2 * (q.x * q.z - q.y * q.w);
    m[4] = 2 * (q.x * q.y - q.z * q.w);
    m[5] = 1 - 2 * (xx + zz);
    m[6] = 2 * (q.y * q.z + q.x * q.w);
    m[8] = 2 * (q.x * q.z + q.y * q.w);
    m[9] = 2 * (q.y * q.z - q.x * q.w);
    m[10] = 1 - 2 * (xx + yy);
}

static quat quaternion_multiply(quat a, quat b)
{
    return (quat){
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

static quat quaternion_axis(v3 axis, float angle)
{
    axis = v3_normalize(axis);
    float half = angle * 0.5f, sine = sinf(half);
    return (quat){axis.x * sine, axis.y * sine, axis.z * sine, cosf(half)};
}

static v4 transform4(v4 value, const float *m)
{
    return (v4){
        value.x * m[0] + value.y * m[4] + value.z * m[8] + value.w * m[12],
        value.x * m[1] + value.y * m[5] + value.z * m[9] + value.w * m[13],
        value.x * m[2] + value.y * m[6] + value.z * m[10] + value.w * m[14],
        value.x * m[3] + value.y * m[7] + value.z * m[11] + value.w * m[15],
    };
}

EXPORT(D3DXMatrixRotationX, 2) { matrix_rotation_x(matrix(ARG(0)), farg(1)); return ARG(0); }
EXPORT(D3DXMatrixRotationY, 2) { matrix_rotation_y(matrix(ARG(0)), farg(1)); return ARG(0); }
EXPORT(D3DXMatrixRotationZ, 2) { matrix_rotation_z(matrix(ARG(0)), farg(1)); return ARG(0); }

EXPORT(D3DXMatrixScaling, 4)
{
    float *m = matrix(ARG(0));
    matrix_identity(m);
    m[0] = farg(1);
    m[5] = farg(2);
    m[10] = farg(3);
    return ARG(0);
}

EXPORT(D3DXMatrixMultiply, 3) { matrix_multiply(matrix(ARG(0)), matrix(ARG(1)), matrix(ARG(2))); return ARG(0); }

EXPORT(D3DXMatrixRotationYawPitchRoll, 4)
{
    float roll[16], pitch[16], yaw[16], temp[16];
    matrix_rotation_z(roll, farg(3));
    matrix_rotation_x(pitch, farg(2));
    matrix_rotation_y(yaw, farg(1));
    matrix_multiply(temp, roll, pitch);
    matrix_multiply(matrix(ARG(0)), temp, yaw);
    return ARG(0);
}

EXPORT(D3DXMatrixRotationQuaternion, 2) { matrix_rotation_quaternion(matrix(ARG(0)), quat_read(ARG(1))); return ARG(0); }

EXPORT(D3DXQuaternionRotationAxis, 3) { quat_write(ARG(0), quaternion_axis(v3_read(ARG(1)), farg(2))); return ARG(0); }
EXPORT(D3DXQuaternionMultiply, 3) { quat_write(ARG(0), quaternion_multiply(quat_read(ARG(1)), quat_read(ARG(2)))); return ARG(0); }

EXPORT(D3DXQuaternionRotationYawPitchRoll, 4)
{
    quat value = quaternion_multiply(
        quaternion_multiply(quaternion_axis((v3){0, 0, 1}, farg(3)), quaternion_axis((v3){1, 0, 0}, farg(2))),
        quaternion_axis((v3){0, 1, 0}, farg(1)));
    quat_write(ARG(0), value);
    return ARG(0);
}

EXPORT(D3DXQuaternionNormalize, 2)
{
    quat q = quat_read(ARG(1));
    float length = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (length > 0) { q.x /= length; q.y /= length; q.z /= length; q.w /= length; }
    quat_write(ARG(0), q);
    return ARG(0);
}

EXPORT(D3DXQuaternionInverse, 2)
{
    quat q = quat_read(ARG(1));
    float norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    quat result = {0, 0, 0, 0};
    if (norm > 0) result = (quat){-q.x / norm, -q.y / norm, -q.z / norm, q.w / norm};
    quat_write(ARG(0), result);
    return ARG(0);
}

EXPORT(D3DXQuaternionSlerp, 4)
{
    quat a = quat_read(ARG(1)), b = quat_read(ARG(2));
    float t = farg(3), cosine = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (cosine < 0) { cosine = -cosine; b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; }
    float left = 1 - t, right = t;
    if (cosine < 0.9995f) {
        float angle = acosf(cosine < -1 ? -1 : cosine > 1 ? 1 : cosine), sine = sinf(angle);
        left = sinf((1 - t) * angle) / sine;
        right = sinf(t * angle) / sine;
    }
    quat_write(ARG(0), (quat){left * a.x + right * b.x, left * a.y + right * b.y,
        left * a.z + right * b.z, left * a.w + right * b.w});
    return ARG(0);
}

EXPORT(D3DXQuaternionRotationMatrix, 2)
{
    const float *m = matrix(ARG(1));
    quat q;
    float trace = m[0] + m[5] + m[10];
    if (trace > 0) {
        float root = sqrtf(trace + 1.0f) * 2.0f;
        q = (quat){(m[6] - m[9]) / root, (m[8] - m[2]) / root, (m[1] - m[4]) / root, root * 0.25f};
    } else if (m[0] > m[5] && m[0] > m[10]) {
        float root = sqrtf(1.0f + m[0] - m[5] - m[10]) * 2.0f;
        q = (quat){root * 0.25f, (m[4] + m[1]) / root, (m[8] + m[2]) / root, (m[6] - m[9]) / root};
    } else if (m[5] > m[10]) {
        float root = sqrtf(1.0f + m[5] - m[0] - m[10]) * 2.0f;
        q = (quat){(m[4] + m[1]) / root, root * 0.25f, (m[9] + m[6]) / root, (m[8] - m[2]) / root};
    } else {
        float root = sqrtf(1.0f + m[10] - m[0] - m[5]) * 2.0f;
        q = (quat){(m[8] + m[2]) / root, (m[9] + m[6]) / root, root * 0.25f, (m[1] - m[4]) / root};
    }
    quat_write(ARG(0), q);
    return ARG(0);
}

EXPORT(D3DXVec2Normalize, 2)
{
    v2 value;
    memcpy(&value, GPTR(ARG(1)), sizeof value);
    float length = sqrtf(value.x * value.x + value.y * value.y);
    if (length > 0) { value.x /= length; value.y /= length; }
    memcpy(GPTR(ARG(0)), &value, sizeof value);
    return ARG(0);
}

EXPORT(D3DXVec3Normalize, 2) { v3_write(ARG(0), v3_normalize(v3_read(ARG(1)))); return ARG(0); }
EXPORT(D3DXVec3Transform, 3) { v4_write(ARG(0), transform4(v4_read(ARG(1)), matrix(ARG(2)))); return ARG(0); }

EXPORT(D3DXVec3TransformCoord, 3)
{
    v3 input = v3_read(ARG(1));
    v4 value = transform4((v4){input.x, input.y, input.z, 1.0f}, matrix(ARG(2)));
    if (value.w != 0) { value.x /= value.w; value.y /= value.w; value.z /= value.w; }
    v3_write(ARG(0), (v3){value.x, value.y, value.z});
    return ARG(0);
}

EXPORT(D3DXVec3TransformNormal, 3)
{
    v3 input = v3_read(ARG(1));
    v4 value = transform4((v4){input.x, input.y, input.z, 0.0f}, matrix(ARG(2)));
    v3_write(ARG(0), (v3){value.x, value.y, value.z});
    return ARG(0);
}

EXPORT(D3DXVec2TransformNormal, 3)
{
    v2 input;
    memcpy(&input, GPTR(ARG(1)), sizeof input);
    const float *m = matrix(ARG(2));
    v2 result = {input.x * m[0] + input.y * m[4], input.x * m[1] + input.y * m[5]};
    memcpy(GPTR(ARG(0)), &result, sizeof result);
    return ARG(0);
}

EXPORT(D3DXVec3TransformArray, 6)
{
    for (uint32_t i = 0; i < ARG(5); ++i) {
        v3 input = v3_read(ARG(2) + i * ARG(3));
        v4_write(ARG(0) + i * ARG(1), transform4((v4){input.x, input.y, input.z, 1.0f}, matrix(ARG(4))));
    }
    return ARG(0);
}

EXPORT(D3DXVec3TransformCoordArray, 6)
{
    for (uint32_t i = 0; i < ARG(5); ++i) {
        v3 input = v3_read(ARG(2) + i * ARG(3));
        v4 value = transform4((v4){input.x, input.y, input.z, 1.0f}, matrix(ARG(4)));
        if (value.w != 0) { value.x /= value.w; value.y /= value.w; value.z /= value.w; }
        v3_write(ARG(0) + i * ARG(1), (v3){value.x, value.y, value.z});
    }
    return ARG(0);
}

EXPORT(D3DXVec3TransformNormalArray, 6)
{
    for (uint32_t i = 0; i < ARG(5); ++i) {
        v3 input = v3_read(ARG(2) + i * ARG(3));
        v4 value = transform4((v4){input.x, input.y, input.z, 0.0f}, matrix(ARG(4)));
        v3_write(ARG(0) + i * ARG(1), (v3){value.x, value.y, value.z});
    }
    return ARG(0);
}

EXPORT(D3DXMatrixPerspectiveFovRH, 5)
{
    float *m = matrix(ARG(0));
    memset(m, 0, 64);
    float fov = farg(1), aspect = farg(2), near_plane = farg(3), far_plane = farg(4);
    float scale = 1.0f / tanf(fov * 0.5f);
    m[0] = scale / aspect;
    m[5] = scale;
    m[10] = far_plane / (near_plane - far_plane);
    m[11] = -1.0f;
    m[14] = near_plane * far_plane / (near_plane - far_plane);
    return ARG(0);
}

EXPORT(D3DXMatrixLookAtRH, 4)
{
    v3 eye = v3_read(ARG(1)), at = v3_read(ARG(2)), up = v3_read(ARG(3));
    v3 z = v3_normalize(v3_sub(eye, at));
    v3 x = v3_normalize(v3_cross(up, z));
    v3 y = v3_cross(z, x);
    float *m = matrix(ARG(0));
    matrix_identity(m);
    m[0] = x.x; m[4] = x.y; m[8] = x.z; m[12] = -v3_dot(x, eye);
    m[1] = y.x; m[5] = y.y; m[9] = y.z; m[13] = -v3_dot(y, eye);
    m[2] = z.x; m[6] = z.y; m[10] = z.z; m[14] = -v3_dot(z, eye);
    return ARG(0);
}

EXPORT(D3DXMatrixInverse, 3)
{
    const float *source = matrix(ARG(2));
    double work[4][8], determinant = 1.0;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) work[row][col] = source[row * 4 + col];
        for (int col = 0; col < 4; ++col) work[row][col + 4] = row == col;
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row)
            if (fabs(work[row][col]) > fabs(work[pivot][col])) pivot = row;
        if (fabs(work[pivot][col]) < 1e-20) {
            if (ARG(1)) *(float *)GPTR(ARG(1)) = 0.0f;
            return 0;
        }
        if (pivot != col) {
            determinant = -determinant;
            for (int k = 0; k < 8; ++k) {
                double temporary = work[col][k]; work[col][k] = work[pivot][k]; work[pivot][k] = temporary;
            }
        }
        double divisor = work[col][col];
        determinant *= divisor;
        for (int k = 0; k < 8; ++k) work[col][k] /= divisor;
        for (int row = 0; row < 4; ++row) if (row != col) {
            double factor = work[row][col];
            for (int k = 0; k < 8; ++k) work[row][k] -= factor * work[col][k];
        }
    }
    float *out = matrix(ARG(0));
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) out[row * 4 + col] = (float)work[row][col + 4];
    if (ARG(1)) *(float *)GPTR(ARG(1)) = (float)determinant;
    return ARG(0);
}

EXPORT(D3DXPlaneNormalize, 2)
{
    v4 plane = v4_read(ARG(1));
    float length = sqrtf(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
    if (length > 0) { plane.x /= length; plane.y /= length; plane.z /= length; plane.w /= length; }
    v4_write(ARG(0), plane);
    return ARG(0);
}

EXPORT(D3DXPlaneFromPoints, 4)
{
    v3 a = v3_read(ARG(1)), b = v3_read(ARG(2)), c = v3_read(ARG(3));
    v3 normal = v3_cross(v3_sub(b, a), v3_sub(c, a));
    float length = v3_length(normal);
    if (length > 0) normal = v3_scale(normal, 1.0f / length);
    v4_write(ARG(0), (v4){normal.x, normal.y, normal.z, -v3_dot(normal, a)});
    return ARG(0);
}

EXPORT(D3DXMatrixTransformation, 7)
{
    float out[16], temporary[16], operation[16];
    matrix_identity(out);
    v3 scaling_center = ARG(1) ? v3_read(ARG(1)) : (v3){0, 0, 0};
    v3 rotation_center = ARG(4) ? v3_read(ARG(4)) : (v3){0, 0, 0};
    matrix_translation(operation, -scaling_center.x, -scaling_center.y, -scaling_center.z);
    matrix_multiply(temporary, out, operation); memcpy(out, temporary, 64);
    if (ARG(2)) { matrix_rotation_quaternion(operation, quat_read(ARG(2))); matrix_multiply(temporary, out, operation); memcpy(out, temporary, 64); }
    matrix_identity(operation);
    if (ARG(3)) { v3 scale = v3_read(ARG(3)); operation[0] = scale.x; operation[5] = scale.y; operation[10] = scale.z; }
    matrix_multiply(temporary, out, operation); memcpy(out, temporary, 64);
    matrix_translation(operation, scaling_center.x - rotation_center.x, scaling_center.y - rotation_center.y, scaling_center.z - rotation_center.z);
    matrix_multiply(temporary, out, operation); memcpy(out, temporary, 64);
    if (ARG(5)) { matrix_rotation_quaternion(operation, quat_read(ARG(5))); matrix_multiply(temporary, out, operation); memcpy(out, temporary, 64); }
    matrix_translation(operation, rotation_center.x, rotation_center.y, rotation_center.z);
    matrix_multiply(temporary, out, operation); memcpy(out, temporary, 64);
    if (ARG(6)) { v3 translation = v3_read(ARG(6)); matrix_translation(operation, translation.x, translation.y, translation.z); matrix_multiply(temporary, out, operation); memcpy(out, temporary, 64); }
    memcpy(matrix(ARG(0)), out, 64);
    return ARG(0);
}
