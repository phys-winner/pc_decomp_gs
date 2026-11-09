#include <windows.h>
#include <cstdio>

struct Vector3 { float x, y, z; };
struct Matrix { float values[16]; };
static FILE *log_file;
static Vector3 *(WINAPI *original_transform)(Vector3 *, DWORD, const Vector3 *, DWORD, const Matrix *, DWORD);

extern "C" Vector3 *WINAPI D3DXVec3TransformCoordArray(Vector3 *out, DWORD out_stride,
    const Vector3 *input, DWORD input_stride, const Matrix *matrix, DWORD count)
{
    if (!log_file) {
        fopen_s(&log_file, "d3dxtrace.txt", "w");
        if (log_file) setvbuf(log_file, nullptr, _IOLBF, 4096);
    }
    if (log_file && matrix && input && count == 4 && out_stride == 28 && input_stride == 12) {
        const float *m = matrix->values;
        fprintf(log_file, "matrix %p input %p: %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g\n",
            matrix, input, input->x, input->y, input->z,
            m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7],
            m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
    }
    return original_transform(out, out_stride, input, input_stride, matrix, count);
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *)
{
    if (reason == DLL_PROCESS_ATTACH) {
        HMODULE library = LoadLibraryA("C:\\Windows\\System32\\d3dx9_43.dll");
        if (library) original_transform = reinterpret_cast<decltype(original_transform)>(
            GetProcAddress(library, "D3DXVec3TransformCoordArray"));
        DisableThreadLibraryCalls(instance);
    }
    return original_transform != nullptr;
}
